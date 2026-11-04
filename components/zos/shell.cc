#include "zos/shell.h"

#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/task.h"

namespace zos {

namespace {

constexpr char kTag[] = "zos_shell";
constexpr int kRowHeight = 44;
constexpr int kRowInset = 4;

bool SameText(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return a == b;
    return std::strcmp(a, b) == 0;
}

bool SameHints(const KeyHints& a, const KeyHints& b) {
    return SameText(a.up, b.up) && SameText(a.down, b.down) &&
           SameText(a.select, b.select);
}

int WifiLevel(int rssi) {
    if (rssi > -55) return 4;
    if (rssi > -65) return 3;
    if (rssi > -75) return 2;
    return 1;
}

}  // namespace

/** Home: the app selector. */
class Shell::Launcher : public App {
public:
    explicit Launcher(Shell& shell) : shell_(shell) {}

    const char* id() const override { return "home"; }
    const char* title() const override { return "应用"; }
    KeyHints hints() const override {
        KeyHints hints;
        hints.select = "打开";
        return hints;
    }

    bool OnKey(Context& ctx, Key key) override {
        const size_t count = shell_.apps_.size();
        if (count == 0) return false;
        if (key == Key::kSelect) {
            shell_.Open(static_cast<int>(selected_));
            return true;
        }
        const size_t before = selected_;
        const size_t first_before = first_;
        selected_ = key == Key::kUp ? (selected_ + count - 1) % count
                                    : (selected_ + 1) % count;
        if (selected_ < first_) first_ = selected_;
        if (selected_ >= first_ + VisibleRows()) {
            first_ = selected_ + 1 - VisibleRows();
        }
        if (first_ != first_before) {
            ctx.Invalidate();
        } else {
            // Only the two affected rows change: a small partial refresh.
            ctx.Invalidate(Union(RowRect(before), RowRect(selected_)));
        }
        return true;
    }

    void Render(Context&, ZectrixCanvas& canvas, const Rect&) override {
        const size_t count = shell_.apps_.size();
        if (count == 0) {
            canvas.TextCentered(130, "没有应用");
            return;
        }
        const size_t last = std::min(count, first_ + VisibleRows());
        for (size_t i = first_; i < last; ++i) {
            const Rect row = RowRect(i);
            const App& app = *shell_.apps_[i].app;
            const bool active = i == selected_;
            const int text_y = row.y + (row.h - 16) / 2;
            if (active) {
                canvas.FillRect(row.x, row.y, row.w, row.h, true);
            } else {
                canvas.Rect(row.x, row.y, row.w, row.h);
            }
            canvas.Text(row.x + 12, text_y, app.title(), 1, active);
            const char* summary = app.summary();
            canvas.Text(row.x + row.w - 12 - canvas.TextWidth(summary), text_y,
                        summary, 1, active);
        }
    }

private:
    static size_t VisibleRows() {
        return static_cast<size_t>(Chrome::Content().h / kRowHeight);
    }
    Rect RowRect(size_t index) const {
        const Rect content = Chrome::Content();
        return {12, content.y + kRowInset +
                        static_cast<int>(index - first_) * kRowHeight,
                content.w - 24, kRowHeight - 6};
    }

    Shell& shell_;
    size_t selected_ = 0;
    size_t first_ = 0;
};

Shell::Shell(ZectrixBoard& board, Display& display, Network& network,
             WebServer& web)
    : board_(board),
      display_(display),
      network_(network),
      web_(web),
      launcher_(std::make_unique<Launcher>(*this)) {
    events_ = xQueueCreate(16, sizeof(Event));
}

Shell::~Shell() = default;

void Shell::Register(std::unique_ptr<App> app) {
    apps_.emplace_back(std::move(app));
}

App& Shell::current() {
    return current_ < 0 ? static_cast<App&>(*launcher_)
                        : *apps_[current_].app;
}

void Shell::Invalidate() { Invalidate(Chrome::Content()); }

void Shell::Invalidate(const Rect& rect) {
    if (callback_app_ != current_) return;  // a background app must not draw
    dirty_rect_ = Union(dirty_rect_, rect);
    dirty_ = true;
}

void Shell::GoHome() { Open(-1); }

void Shell::Wake() { Post({Event::kWake, Key::kSelect, -1}); }

void Shell::PostConfigChanged(size_t app_index) {
    Post({Event::kConfig, Key::kSelect, static_cast<int>(app_index)});
}

void Shell::Post(const Event& event) {
    if (events_ == nullptr) return;
    if (event.type == Event::kPowerOff) {
        xQueueSendToFront(events_, &event, 0);
    } else {
        xQueueSend(events_, &event, 0);
    }
}

void Shell::Open(int index) {
    if (index == current_) return;
    callback_app_ = current_;
    current().OnLeave(*this);
    current_ = index;
    callback_app_ = current_;
    // Partial refresh of the whole screen: header and footer are redrawn too.
    // The Display still inserts a full refresh every few partials (ghosting).
    full_ = false;
    dirty_ = true;
    dirty_rect_ = Chrome::Content();
    status_drawn_ = hints_drawn_ = false;
    current().OnEnter(*this);
}

StatusInfo Shell::ReadStatus() {
    StatusInfo status;
    const ZectrixPowerSnapshot power = board_.ReadPowerSnapshot();
    status.battery_valid = power.battery_valid;
    status.battery_percent =
        std::min(100, (static_cast<int>(power.battery_percent) + 2) / 5 * 5);
    status.charging = power.charge.charging;
    status.wifi_connected = network_.connected();
    status.wifi_config_mode = network_.config_mode();
    status.wifi_level =
        status.wifi_connected ? WifiLevel(network_.rssi()) : 0;
    return status;
}

void Shell::Flush() {
    callback_app_ = current_;
    App& app = current();
    if (app.fullscreen()) {
        // The app drives the panel itself.
        was_fullscreen_ = true;
        dirty_ = false;
        return;
    }
    if (was_fullscreen_) {
        full_ = true;
        was_fullscreen_ = false;
    }
    if (!full_ && !dirty_) return;

    ZectrixCanvas& canvas = display_.canvas();
    const StatusInfo status = ReadStatus();
    const KeyHints hints = app.hints();
    const bool header_changed = !status_drawn_ || status != drawn_status_;
    const bool footer_changed = !hints_drawn_ || !SameHints(hints, drawn_hints_);

    if (full_) canvas.Clear();
    const Rect content = Chrome::Content();
    canvas.FillRect(content.x, content.y, content.w, content.h, false);
    app.Render(*this, canvas, content);
    Chrome::DrawHeader(canvas, app.title(), status);
    Chrome::DrawFooter(canvas, hints, current_ >= 0);

    esp_err_t err;
    if (full_) {
        err = display_.Full();
    } else {
        Rect area = dirty_rect_;
        if (header_changed) area = Union(area, Chrome::Header());
        if (footer_changed) area = Union(area, Chrome::Footer());
        err = display_.Partial(area);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "refresh failed: %s", esp_err_to_name(err));
    }
    drawn_status_ = status;
    drawn_hints_ = hints;
    status_drawn_ = hints_drawn_ = true;
    full_ = dirty_ = false;
    dirty_rect_ = {};
}

void Shell::Dispatch(const Event& event) {
    callback_app_ = current_;
    switch (event.type) {
        case Event::kKey:
            if (event.key == Key::kSelectHold) {
                if (current_ >= 0) GoHome();
            } else {
                current().OnKey(*this, event.key);
            }
            break;
        case Event::kPowerOff:
            PowerOff();
        case Event::kWake:
            network_.PollTimeSync();
            if (network_.connected()) web_.Start(*this);
            current().OnWake(*this);
            break;
        case Event::kConfig:
            if (event.app >= 0 && static_cast<size_t>(event.app) < apps_.size()) {
                callback_app_ = event.app;
                apps_[event.app].app->OnConfigChanged(*this);
                callback_app_ = current_;
            }
            break;
    }
}

void Shell::InputTaskEntry(void* arg) {
    Shell* shell = static_cast<Shell*>(arg);
    ZectrixButtonEvent raw;
    while (true) {
        if (!shell->board_.WaitButton(&raw, portMAX_DELAY)) continue;
        Event event = {Event::kKey, Key::kSelect, -1};
        if (raw.action == ZectrixButtonAction::kLongPress) {
            if (raw.button == ZectrixButton::kDown) {
                event.type = Event::kPowerOff;
            } else if (raw.button == ZectrixButton::kOk) {
                event.key = Key::kSelectHold;
            } else {
                continue;
            }
        } else {
            event.key = raw.button == ZectrixButton::kUp     ? Key::kUp
                        : raw.button == ZectrixButton::kDown ? Key::kDown
                                                             : Key::kSelect;
        }
        shell->Post(event);
    }
}

void Shell::Splash() {
    ZectrixCanvas& canvas = display_.canvas();
    canvas.Clear();
    canvas.FillRect(0, 0, 400, 8, true);
    canvas.FillRect(0, 292, 400, 8, true);
    canvas.TextCentered(70, "ZECTRIX", 4);
    canvas.TextCentered(150, "OS", 3);
    canvas.Line(72, 200, 327, 200);
    canvas.TextCentered(214, "电子纸应用平台", 1);
    display_.Full();
}

void Shell::PowerOff() {
    ESP_LOGI(kTag, "powering off");
    display_.Clear();
    board_.SetPowerLed(false);
    board_.SetAudioPower(false);
    vTaskDelay(pdMS_TO_TICKS(100));
    board_.CutBatteryPower();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
    while (true) {
    }
}

void Shell::Run() {
    xTaskCreate(InputTaskEntry, "zos_input", 3072, this, 5, nullptr);

    Splash();
    vTaskDelay(pdMS_TO_TICKS(1200));
    full_ = true;

    while (true) {
        Flush();
        callback_app_ = current_;
        const int64_t wait_ms = current().NextTickMs(*this);
        TickType_t timeout = portMAX_DELAY;
        if (wait_ms >= 0) {
            timeout = wait_ms == 0 ? 0 : std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms));
        }
        Event event;
        if (xQueueReceive(events_, &event, timeout) == pdTRUE) {
            Dispatch(event);
        } else {
            callback_app_ = current_;
            current().OnTick(*this);
        }
    }
}

}  // namespace zos
