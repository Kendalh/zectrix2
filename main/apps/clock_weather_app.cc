#include "apps/clock_weather_app.h"

#include "apps/weather/weather_icons.h"

#include <sys/time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "zos/chrome.h"
#include "zos/network.h"

namespace {

constexpr char kTag[] = "clock_weather";

struct Zone {
    const char* name;
    const char* posix;
};

constexpr Zone kZones[] = {
    {"北京", "CST-8"},
    {"东京", "JST-9"},
    {"UTC", "UTC0"},
    {"伦敦", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"柏林", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"纽约", "EST5EDT,M3.2.0,M11.1.0"},
    {"洛杉矶", "PST8PDT,M3.2.0,M11.1.0"},
};

constexpr zos::ConfigField kFields[] = {
    {"zone", "时区", zos::ConfigField::Type::kChoice, "北京",
     "北京|东京|UTC|伦敦|柏林|纽约|洛杉矶",
     "时间通过 Wi-Fi 校时（NTP），由 RTC 保持。"},
    {"hour24", "24 小时制", zos::ConfigField::Type::kBool, "1", nullptr,
     nullptr},
    {"show_date", "显示日期", zos::ConfigField::Type::kBool, "1", nullptr,
     nullptr},
    {"city", "天气城市", zos::ConfigField::Type::kString, "北京", nullptr,
     "城市名称，中文或英文均可（如 杭州、Tokyo）。保存后自动定位并更新天气。"},
    {"weather_min", "天气更新间隔（分钟）", zos::ConfigField::Type::kChoice,
     "30", "15|30|60", "仅在 Wi-Fi 连接且停留在本页面时更新。"},
};

constexpr int kTimeScale = 5;
constexpr int kRetryMs = 5 * 60 * 1000;
constexpr int kForecastColumns = 4;
constexpr int kTimeAreaWidth = 208;  // left of the weather card

int64_t NowMs() { return esp_timer_get_time() / 1000; }

// ---- drawing helpers --------------------------------------------------

void Blit(ZectrixCanvas& canvas, int x, int y, const uint8_t* bits, int size) {
    const int stride = size / 8;
    for (int row = 0; row < size; ++row) {
        for (int bx = 0; bx < stride; ++bx) {
            const uint8_t byte = bits[row * stride + bx];
            for (int bit = 0; byte != 0 && bit < 8; ++bit) {
                if (byte & (0x80 >> bit)) canvas.Pixel(x + bx * 8 + bit, y + row, true);
            }
        }
    }
}

void DrawIcon(ZectrixCanvas& canvas, int x, int y, WeatherIcon icon, int size) {
    const int index = static_cast<int>(icon);
    Blit(canvas, x, y, size == 48 ? kWeatherIcons48[index] : kWeatherIcons32[index], size);
}

// "25°C" (or "25°" without the C). The font has no degree sign, so the ring is
// drawn. Returns the width; with `draw` false only measures.
int DrawTemp(ZectrixCanvas& canvas, int x, int y, int value, int scale,
             bool with_c, bool draw = true) {
    char digits[12];
    std::snprintf(digits, sizeof(digits), "%d", value);
    const int ring = 2 * scale + 3;
    int width = canvas.TextWidth(digits, scale) + 2 + ring;
    if (with_c) width += 2 + canvas.TextWidth("C", scale);
    if (!draw) return width;
    canvas.Text(x, y, digits, scale);
    const int ring_x = x + canvas.TextWidth(digits, scale) + 2;
    const int ring_y = y + 2 * scale;
    canvas.Rect(ring_x, ring_y, ring, ring);
    if (scale >= 3) canvas.Rect(ring_x + 1, ring_y + 1, ring - 2, ring - 2);
    if (with_c) canvas.Text(ring_x + ring + 2, y, "C", scale);
    return width;
}

void TextCenteredAt(ZectrixCanvas& canvas, int center_x, int y, const char* text) {
    canvas.Text(center_x - canvas.TextWidth(text) / 2, y, text);
}

}  // namespace

struct ClockWeatherApp::Job {
    ClockWeatherApp* app;
    zos::Context* ctx;
    int generation;
    std::string query;   // city text from the settings
    WeatherCity city;    // already resolved when `resolve` is false
    bool resolve;
};

ClockWeatherApp::ClockWeatherApp() { ApplyTimezone(zos::Settings("clock")); }

const zos::ConfigField* ClockWeatherApp::config_fields() const { return kFields; }
size_t ClockWeatherApp::config_field_count() const {
    return sizeof(kFields) / sizeof(kFields[0]);
}

zos::KeyHints ClockWeatherApp::hints() const {
    zos::KeyHints hints;
    hints.select = "刷新天气";
    return hints;
}

void ClockWeatherApp::ApplyTimezone(const zos::Settings& settings) {
    const std::string zone = settings.GetString("zone", "北京");
    const char* posix = kZones[0].posix;
    for (const Zone& candidate : kZones) {
        if (zone == candidate.name) posix = candidate.posix;
    }
    setenv("TZ", posix, 1);
    tzset();
}

zos::Rect ClockWeatherApp::TimeArea(const zos::Rect& content) {
    return {0, content.y, kTimeAreaWidth, 114};
}

void ClockWeatherApp::OnEnter(zos::Context& ctx) {
    // Minute updates are all partial; the hour change forces a full refresh.
    ctx.display().set_partial_limit(255);
    MaybeFetch(ctx, false);
}

void ClockWeatherApp::OnLeave(zos::Context& ctx) {
    ctx.display().set_partial_limit(8);
}

bool ClockWeatherApp::OnKey(zos::Context& ctx, zos::Key key) {
    if (key != zos::Key::kSelect) return false;
    MaybeFetch(ctx, true);
    ctx.Invalidate();
    return true;
}

int64_t ClockWeatherApp::NextTickMs(zos::Context&) {
    if (!zos::Network::time_valid()) return -1;  // OnWake() fires on sync
    timeval tv;
    gettimeofday(&tv, nullptr);
    const int64_t into_minute = (tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000;
    return 60000 - into_minute + 20;
}

void ClockWeatherApp::OnTick(zos::Context& ctx) {
    const time_t now = time(nullptr);
    tm local = {};
    localtime_r(&now, &local);
    if (local.tm_min == 0) ctx.RequestFullRefresh();
    ctx.Invalidate(TimeArea(zos::Chrome::Content()));
    MaybeFetch(ctx, false);
}

void ClockWeatherApp::OnWake(zos::Context& ctx) {
    // Save a city the worker resolved, so the next fetch skips geocoding.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (geo_ready_) {
            geo_ready_ = false;
            zos::Settings& settings = ctx.settings();
            settings.SetString("geo_q", geo_query_.c_str());
            settings.SetString("lat", geo_city_.lat.c_str());
            settings.SetString("lon", geo_city_.lon.c_str());
        }
    }
    MaybeFetch(ctx, false);
    ctx.Invalidate();
}

void ClockWeatherApp::OnConfigChanged(zos::Context& ctx) {
    ApplyTimezone(ctx.settings());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;  // drop anything in flight for the old city
        has_data_ = false;
        next_try_ms_ = 0;
        status_ = Status::kIdle;
    }
    MaybeFetch(ctx, true);
    ctx.Invalidate();
}

void ClockWeatherApp::MaybeFetch(zos::Context& ctx, bool force) {
    if (!ctx.net().connected()) return;
    zos::Settings& settings = ctx.settings();
    const int interval_ms = std::atoi(settings.GetString("weather_min", "30").c_str()) * 60 * 1000;
    const int64_t now = NowMs();
    auto* job = new Job();
    job->app = this;
    job->ctx = &ctx;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_ == Status::kFetching) {
            delete job;
            return;
        }
        if (!force) {
            const bool fresh = has_data_ && now - fetched_ms_ < interval_ms;
            if (fresh || now < next_try_ms_) {
                delete job;
                return;
            }
        }
        job->generation = generation_;
        status_ = Status::kFetching;
    }
    job->query = settings.GetString("city", "北京");
    job->city.lat = settings.GetString("lat", "");
    job->city.lon = settings.GetString("lon", "");
    job->resolve = job->city.lat.empty() ||
                   settings.GetString("geo_q", "") != job->query;
    if (xTaskCreate(WorkerEntry, "weather", 8192, job, 3, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "cannot start the weather task");
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = Status::kFailed;
        next_try_ms_ = now + kRetryMs;
        delete job;
    }
}

// Runs on its own task: blocking HTTPS must never stall the shell.
void ClockWeatherApp::WorkerEntry(void* arg) {
    std::unique_ptr<Job> job(static_cast<Job*>(arg));
    ClockWeatherApp* app = job->app;
    Status result = Status::kFailed;
    WeatherData data;
    WeatherCity city = job->city;
    bool resolved = false;
    std::string body;

    if (job->resolve) {
        std::vector<WeatherCity> found;
        if (WeatherHttpGet(GeocodeUrl(job->query), &body) && ParseGeocode(body.c_str(), &found)) {
            if (found.empty()) {
                result = Status::kCityNotFound;
            } else {
                city = found.front();
                resolved = true;
            }
        }
    }
    if (result != Status::kCityNotFound && !city.lat.empty()) {
        if (WeatherHttpGet(ForecastUrl(city), &body) && ParseForecast(body.c_str(), &data)) {
            result = Status::kIdle;
            if (WeatherHttpGet(AirUrl(city), &body)) ParseAir(body.c_str(), &data);
        }
    }
    ESP_LOGI(kTag, "weather %s: %s", city.name.c_str(),
             result == Status::kIdle ? data.text.c_str() : "failed");
    {
        std::lock_guard<std::mutex> lock(app->mutex_);
        if (job->generation == app->generation_) {
            app->status_ = result;
            if (result == Status::kIdle) {
                app->data_ = data;
                app->has_data_ = true;
                app->fetched_ms_ = NowMs();
            } else {
                app->next_try_ms_ = NowMs() + kRetryMs;
            }
            if (resolved) {
                app->geo_ready_ = true;
                app->geo_query_ = job->query;
                app->geo_city_ = city;
            }
        } else {
            app->status_ = Status::kIdle;  // stale: OnWake() fetches the new city
        }
    }
    job->ctx->Wake();
}

void ClockWeatherApp::RenderWeather(ZectrixCanvas& canvas, const zos::Rect& content,
                                    const std::string& city, bool connected) {
    constexpr int kCardX = kTimeAreaWidth, kCardW = 184, kCardH = 106;
    constexpr int kPad = 8;
    WeatherData data;
    Status status;
    bool has_data;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data = data_;
        has_data = has_data_;
        status = status_;
    }
    const int top = content.y + 6;
    canvas.Rect(kCardX, top, kCardW, kCardH);
    char line[80];
    if (!has_data) {
        canvas.Text(kCardX + kPad, top + kPad, city.c_str());
        const char* message = !connected ? "连接 Wi-Fi 后获取天气"
                              : status == Status::kFetching ? "天气获取中…"
                              : status == Status::kCityNotFound ? "找不到该城市"
                              : status == Status::kFailed ? "天气获取失败"
                                                          : "等待更新";
        canvas.Text(kCardX + kPad, top + 44, message);
        if (status == Status::kFailed || status == Status::kCityNotFound) {
            canvas.Text(kCardX + kPad, top + 66,
                        status == Status::kFailed ? "稍后自动重试" : "请在网页中修改");
        }
        return;
    }
    // Current conditions: icon and temperature, then place and feels-like.
    DrawIcon(canvas, kCardX + kPad, top + 8, data.icon, 48);
    DrawTemp(canvas, kCardX + kPad + 48 + 10, top + 16, data.temp, 3, true);
    std::snprintf(line, sizeof(line), "%s %s", city.c_str(), data.text.c_str());
    canvas.Text(kCardX + kPad, top + 64, line);
    canvas.Text(kCardX + kPad, top + 84, "体感");
    DrawTemp(canvas, kCardX + kPad + 36, top + 84, data.feels_like, 1, true);

    // Forecast cards: today and the next three days; today has a heavier frame.
    const int card_top = content.y + 120;
    const int columns = std::min<int>(kForecastColumns, static_cast<int>(data.days.size()));
    for (int i = 0; i < columns; ++i) {
        const WeatherDay& day = data.days[i];
        const int x = 8 + i * 96;
        const int center = x + 46;
        canvas.Rect(x, card_top, 92, 78);
        if (i == 0) canvas.Rect(x + 1, card_top + 1, 90, 76);
        TextCenteredAt(canvas, center, card_top + 6, day.label.c_str());
        DrawIcon(canvas, center - 16, card_top + 24, day.icon, 32);
        const int low = DrawTemp(canvas, 0, 0, day.temp_min, 1, false, false);
        std::snprintf(line, sizeof(line), "~%d", day.temp_max);
        const int tail = canvas.TextWidth(line) + 2 + 5;  // "~max" and a ring
        const int start = center - (low + tail) / 2;
        const int after_low = start + DrawTemp(canvas, start, card_top + 58, day.temp_min, 1, false);
        canvas.Text(after_low, card_top + 58, line);
        const int ring_x = after_low + canvas.TextWidth(line) + 2;
        canvas.Rect(ring_x, card_top + 60, 5, 5);
    }

    // One line with the rest: humidity, wind, air quality, UV.
    int used = 0;
    line[0] = '\0';
    if (data.humidity >= 0) {
        used += std::snprintf(line + used, sizeof(line) - used, "湿度%d%%  ", data.humidity);
    }
    if (!data.wind_dir.empty()) {
        used += std::snprintf(line + used, sizeof(line) - used, "%s%d级  ", data.wind_dir.c_str(), data.wind_scale);
    }
    if (data.aqi >= 0) {
        used += std::snprintf(line + used, sizeof(line) - used, "空气%s %d  ", data.aqi_text.c_str(), data.aqi);
    }
    if (!data.uv_text.empty()) {
        std::snprintf(line + used, sizeof(line) - used, "紫外线%s", data.uv_text.c_str());
    }
    canvas.Text(10, content.y + 206, line);
}

void ClockWeatherApp::Render(zos::Context& ctx, ZectrixCanvas& canvas,
                             const zos::Rect& content) {
    char text[64];
    // The name as typed: the geocoded one may use characters the font lacks (東京).
    const std::string city = ctx.settings().GetString("city", "北京");
    if (!zos::Network::time_valid()) {
        canvas.Text(12, content.y + 8, "--:--", kTimeScale);
        canvas.Text(14, content.y + 96, "时间未设置，连接 Wi-Fi 后自动校时");
    } else {
        const time_t now = time(nullptr);
        tm local = {};
        localtime_r(&now, &local);

        const bool hour24 = ctx.settings().GetBool("hour24", true);
        std::strftime(text, sizeof(text), hour24 ? "%H:%M" : "%I:%M", &local);
        canvas.Text(12, content.y + 8, text, kTimeScale);
        if (!hour24) {
            canvas.Text(12 + canvas.TextWidth(text, kTimeScale) + 6, content.y + 56,
                        local.tm_hour < 12 ? "上午" : "下午");
        }
        if (ctx.settings().GetBool("show_date", true)) {
            static const char* const kWeekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
            std::snprintf(text, sizeof(text), "%04d年%02d月%02d日 星期%s",
                          local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                          kWeekdays[local.tm_wday]);
            canvas.Text(14, content.y + 96, text);
        }
    }
    RenderWeather(canvas, content, city, ctx.net().connected());
}
