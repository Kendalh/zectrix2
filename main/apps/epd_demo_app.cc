#include "apps/epd_demo_app.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "zos/chrome.h"

extern "C" {
extern const uint8_t kLighthouse1bppStart[]
    asm("_binary_lighthouse_400x300_1bpp_bin_start");
extern const uint8_t kLighthouse1bppEnd[]
    asm("_binary_lighthouse_400x300_1bpp_bin_end");
extern const uint8_t kSnowPath1bppStart[]
    asm("_binary_snow_path_400x300_1bpp_bin_start");
extern const uint8_t kSnowPath1bppEnd[]
    asm("_binary_snow_path_400x300_1bpp_bin_end");
extern const uint8_t kFootprintAnimationStart[]
    asm("_binary_footprint_animation_bin_start");
extern const uint8_t kFootprintAnimationEnd[]
    asm("_binary_footprint_animation_bin_end");
extern const uint8_t kMountain4bppStart[]
    asm("_binary_mountain_400x300_4bpp_bin_start");
extern const uint8_t kMountain4bppEnd[]
    asm("_binary_mountain_400x300_4bpp_bin_end");
}

namespace {

constexpr char kTag[] = "epd_demo";
constexpr uint8_t kFootprintMagic[] = {'Z', 'F', 'P', '1'};
constexpr size_t kAnimationHeaderSize = 8;
constexpr size_t kStepHeaderSize = 12;
constexpr int kRowHeight = 44;

constexpr const char* kItems[] = {"全刷新（1bpp）",
                                  "局部刷新（脚印动画）",
                                  "16 级灰度（4bpp）", "运行全部场景"};
constexpr const char* kSceneNames[] = {"灯塔", "脚印", "山景"};
constexpr size_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

uint16_t ReadLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(data[1] << 8);
}

// Runs `call` and adds its duration to `*total_ms`.
template <typename Fn>
esp_err_t Timed(int64_t* total_ms, Fn call) {
    const int64_t start = esp_timer_get_time();
    const esp_err_t err = call();
    *total_ms += (esp_timer_get_time() - start) / 1000;
    return err;
}

}  // namespace

zos::KeyHints EpdDemoApp::hints() const {
    zos::KeyHints hints;
    hints.select = "运行（按任意键停止）";
    return hints;
}

zos::Rect EpdDemoApp::RowRect(size_t index) const {
    const zos::Rect content = zos::Chrome::Content();
    return {12, content.y + 4 + static_cast<int>(index) * kRowHeight,
            content.w - 24, kRowHeight - 6};
}

void EpdDemoApp::OnLeave(zos::Context&) {
    playing_ = false;
    run_all_ = false;
}

bool EpdDemoApp::OnKey(zos::Context& ctx, zos::Key key) {
    if (playing_) {
        Stop(ctx);
        return true;
    }
    if (key == zos::Key::kSelect) {
        run_all_ = selected_ == kItemCount - 1;
        Begin(run_all_ ? kLighthouse : static_cast<Scene>(selected_));
        return true;
    }
    const size_t before = selected_;
    selected_ = key == zos::Key::kUp ? (selected_ + kItemCount - 1) % kItemCount
                                     : (selected_ + 1) % kItemCount;
    ctx.Invalidate(zos::Union(RowRect(before), RowRect(selected_)));
    return true;
}

void EpdDemoApp::Begin(Scene scene) {
    scene_ = scene;
    step_ = 0;
    wait_ms_ = 0;
    refresh_ms_ = 0;
    playing_ = true;
    ESP_LOGI(kTag, "scene %d", static_cast<int>(scene));
}

void EpdDemoApp::Stop(zos::Context& ctx) {
    playing_ = false;
    run_all_ = false;
    ctx.Invalidate();
}

void EpdDemoApp::Finish(zos::Context& ctx, esp_err_t error) {
    has_result_ = true;
    last_scene_ = scene_;
    last_ms_ = refresh_ms_;
    last_error_ = error;
    if (error == ESP_OK && run_all_ && scene_ != kMountain) {
        Begin(static_cast<Scene>(scene_ + 1));
        return;
    }
    Stop(ctx);
}

int64_t EpdDemoApp::NextTickMs(zos::Context&) { return playing_ ? wait_ms_ : -1; }

void EpdDemoApp::OnTick(zos::Context& ctx) {
    if (!playing_) return;
    switch (scene_) {
        case kLighthouse: StepLighthouse(ctx); break;
        case kFootprints: StepFootprints(ctx); break;
        case kMountain: StepMountain(ctx); break;
    }
}

void EpdDemoApp::StepLighthouse(zos::Context& ctx) {
    if (step_ > 0) {
        Finish(ctx, ESP_OK);
        return;
    }
    const size_t size = kLighthouse1bppEnd - kLighthouse1bppStart;
    const esp_err_t err = size != ZECTRIX_EPD_1BPP_FRAME_BYTES
        ? ESP_ERR_INVALID_SIZE
        : Timed(&refresh_ms_, [&] {
              return ctx.display().ShowFull1bpp(kLighthouse1bppStart, size);
          });
    if (err != ESP_OK) {
        Finish(ctx, err);
        return;
    }
    step_ = 1;
    wait_ms_ = 2500;
}

void EpdDemoApp::StepFootprints(zos::Context& ctx) {
    const uint8_t* end = kFootprintAnimationEnd;
    if (step_ == 0) {
        const size_t background = kSnowPath1bppEnd - kSnowPath1bppStart;
        const size_t animation = end - kFootprintAnimationStart;
        if (background != ZECTRIX_EPD_1BPP_FRAME_BYTES ||
            animation < kAnimationHeaderSize ||
            std::memcmp(kFootprintAnimationStart, kFootprintMagic,
                        sizeof(kFootprintMagic)) != 0) {
            Finish(ctx, ESP_ERR_INVALID_SIZE);
            return;
        }
        const esp_err_t err = Timed(&refresh_ms_, [&] {
            return ctx.display().ShowFull1bpp(kSnowPath1bppStart, background);
        });
        if (err != ESP_OK) {
            Finish(ctx, err);
            return;
        }
        steps_total_ = ReadLe16(kFootprintAnimationStart + 4);
        cursor_ = kFootprintAnimationStart + kAnimationHeaderSize;
        step_ = 1;
        wait_ms_ = 900;
        return;
    }
    if (step_ > steps_total_) {
        Finish(ctx, ESP_OK);
        return;
    }
    if (static_cast<size_t>(end - cursor_) < kStepHeaderSize) {
        Finish(ctx, ESP_ERR_INVALID_SIZE);
        return;
    }
    const zos::Rect rect = {ReadLe16(cursor_), ReadLe16(cursor_ + 2),
                            ReadLe16(cursor_ + 4), ReadLe16(cursor_ + 6)};
    const uint16_t data_size = ReadLe16(cursor_ + 10);
    cursor_ += kStepHeaderSize;
    const size_t expected = static_cast<size_t>((rect.w + 7) / 8) * rect.h;
    if (data_size != expected || data_size > static_cast<size_t>(end - cursor_)) {
        Finish(ctx, ESP_ERR_INVALID_SIZE);
        return;
    }
    const esp_err_t err = Timed(&refresh_ms_, [&] {
        return ctx.display().ShowPartialRaw(rect, cursor_, data_size);
    });
    cursor_ += data_size;
    if (err != ESP_OK) {
        Finish(ctx, err);
        return;
    }
    ++step_;
    wait_ms_ = step_ > steps_total_ ? 2200 : 400;
}

void EpdDemoApp::StepMountain(zos::Context& ctx) {
    if (step_ > 0) {
        Finish(ctx, ESP_OK);
        return;
    }
    const size_t size = kMountain4bppEnd - kMountain4bppStart;
    const esp_err_t err = size != ZECTRIX_EPD_4BPP_FRAME_BYTES
        ? ESP_ERR_INVALID_SIZE
        : Timed(&refresh_ms_, [&] {
              return ctx.display().Show4bpp(kMountain4bppStart, size);
          });
    if (err != ESP_OK) {
        Finish(ctx, err);
        return;
    }
    step_ = 1;
    wait_ms_ = 5000;
}

void EpdDemoApp::Render(zos::Context&, ZectrixCanvas& canvas,
                        const zos::Rect& content) {
    for (size_t i = 0; i < kItemCount; ++i) {
        const zos::Rect row = RowRect(i);
        const bool active = i == selected_;
        if (active) {
            canvas.FillRect(row.x, row.y, row.w, row.h, true);
        } else {
            canvas.Rect(row.x, row.y, row.w, row.h);
        }
        canvas.Text(row.x + 12, row.y + (row.h - 16) / 2, kItems[i], 1, active);
    }
    if (has_result_) {
        char line[80];
        std::snprintf(line, sizeof(line), "上次：%s  %lld ms  %s",
                      kSceneNames[last_scene_],
                      static_cast<long long>(last_ms_),
                      esp_err_to_name(last_error_));
        canvas.Text(16, content.y + content.h - 22, line);
    }
}
