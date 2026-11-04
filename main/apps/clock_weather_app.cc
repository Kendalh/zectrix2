#include "apps/clock_weather_app.h"

#include <sys/time.h>

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

int64_t NowMs() { return esp_timer_get_time() / 1000; }

// ---- icons: 48x48 boxes drawn from primitives -------------------------

void Disc(ZectrixCanvas& canvas, int cx, int cy, int r, bool black) {
    for (int dy = -r; dy <= r; ++dy) {
        const int half = static_cast<int>(std::sqrt(static_cast<double>(r * r - dy * dy)) + 0.5);
        canvas.FillRect(cx - half, cy + dy, half * 2 + 1, 1, black);
    }
}

void Sun(ZectrixCanvas& canvas, int cx, int cy, int r) {
    Disc(canvas, cx, cy, r, true);
    static const double kDirs[8][2] = {{1, 0},      {0.707, 0.707},   {0, 1},     {-0.707, 0.707},
                                       {-1, 0},     {-0.707, -0.707}, {0, -1},    {0.707, -0.707}};
    for (const auto& d : kDirs) {
        canvas.Line(cx + static_cast<int>(d[0] * (r + 4)), cy + static_cast<int>(d[1] * (r + 4)),
                    cx + static_cast<int>(d[0] * (r + 9)), cy + static_cast<int>(d[1] * (r + 9)));
    }
}

// Cloud, 48 wide and 30 tall, with a white halo so it can overlap a sun.
void Cloud(ZectrixCanvas& canvas, int x, int y) {
    for (const bool black : {false, true}) {
        const int g = black ? 0 : 2;
        Disc(canvas, x + 14, y + 20, 8 + g, black);
        Disc(canvas, x + 25, y + 14, 11 + g, black);
        Disc(canvas, x + 37, y + 21, 7 + g, black);
        canvas.FillRect(x + 14 - g, y + 20 - g, 24 + 2 * g, 9 + 2 * g, black);
    }
}

void DrawIcon(ZectrixCanvas& canvas, int x, int y, WeatherIcon icon) {
    switch (icon) {
        case WeatherIcon::kSunny:
            Sun(canvas, x + 24, y + 24, 9);
            break;
        case WeatherIcon::kPartlyCloudy:
            Sun(canvas, x + 15, y + 15, 7);
            Cloud(canvas, x + 2, y + 14);
            break;
        case WeatherIcon::kOvercast:
            Cloud(canvas, x, y + 10);
            break;
        case WeatherIcon::kRain:
            Cloud(canvas, x, y + 2);
            for (int i = 0; i < 3; ++i) {
                canvas.Line(x + 15 + i * 10, y + 36, x + 12 + i * 10, y + 44);
                canvas.Line(x + 16 + i * 10, y + 36, x + 13 + i * 10, y + 44);
            }
            break;
        case WeatherIcon::kThunder:
            Cloud(canvas, x, y + 2);
            canvas.Line(x + 27, y + 32, x + 20, y + 40);
            canvas.Line(x + 28, y + 32, x + 21, y + 40);
            canvas.Line(x + 20, y + 40, x + 28, y + 40);
            canvas.Line(x + 28, y + 40, x + 22, y + 47);
            break;
        case WeatherIcon::kSnow:
            Cloud(canvas, x, y + 2);
            for (int i = 0; i < 3; ++i) {
                for (int row = 0; row < 2; ++row) {
                    const int px = x + 14 + i * 10 + row * 5;
                    const int py = y + 37 + row * 6;
                    canvas.FillRect(px - 1, py, 4, 2, true);
                    canvas.FillRect(px, py - 1, 2, 4, true);
                }
            }
            break;
        case WeatherIcon::kFog:
            for (int row = 0; row < 4; ++row) {
                canvas.FillRect(x + 4 + (row % 2) * 6, y + 10 + row * 8, 36, 3, true);
            }
            break;
    }
}

void TextAt(ZectrixCanvas& canvas, int center_x, int y, const char* text) {
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
    return {0, content.y, content.w, 120};
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
    constexpr int kX = 244;
    WeatherData data;
    Status status;
    bool has_data;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data = data_;
        has_data = has_data_;
        status = status_;
    }
    char line[80];
    canvas.Text(kX, content.y + 8, city.c_str());
    if (!has_data) {
        const char* message = !connected ? "连接 Wi-Fi 后获取天气"
                              : status == Status::kFetching ? "天气获取中…"
                              : status == Status::kCityNotFound ? "找不到该城市"
                              : status == Status::kFailed ? "天气获取失败"
                                                          : "等待更新";
        canvas.Text(kX, content.y + 40, message);
        if (status == Status::kFailed || status == Status::kCityNotFound) {
            canvas.Text(kX, content.y + 62, status == Status::kFailed ? "稍后自动重试" : "请在网页中修改");
        }
        return;
    }
    canvas.Text(kX + canvas.TextWidth(city.c_str()) + 10, content.y + 8, data.text.c_str());
    DrawIcon(canvas, kX, content.y + 30, data.icon);
    std::snprintf(line, sizeof(line), "%d℃", data.temp);
    canvas.Text(kX + 54, content.y + 38, line, 3);
    if (data.humidity >= 0) {
        std::snprintf(line, sizeof(line), "体感%d℃ 湿度%d%%", data.feels_like, data.humidity);
    } else {
        std::snprintf(line, sizeof(line), "体感%d℃", data.feels_like);
    }
    canvas.Text(kX, content.y + 82, line);
    std::snprintf(line, sizeof(line), "%s %d级", data.wind_dir.c_str(), data.wind_scale);
    canvas.Text(kX, content.y + 100, line);

    // Forecast: today and the next three days.
    const int top = content.y + 122;
    canvas.Line(8, top, content.w - 9, top);
    const int columns = std::min<int>(kForecastColumns, static_cast<int>(data.days.size()));
    for (int i = 0; i < columns; ++i) {
        const int center = 4 + i * 98 + 49;
        const WeatherDay& day = data.days[i];
        TextAt(canvas, center, top + 8, day.label.c_str());
        TextAt(canvas, center, top + 28, day.text.c_str());
        std::snprintf(line, sizeof(line), "%d~%d℃", day.temp_min, day.temp_max);
        TextAt(canvas, center, top + 48, line);
    }
    int used = 0;
    line[0] = '\0';
    if (data.aqi >= 0) {
        used += std::snprintf(line + used, sizeof(line) - used, "空气%s %d  ", data.aqi_text.c_str(), data.aqi);
    }
    if (!data.uv_text.empty()) {
        used += std::snprintf(line + used, sizeof(line) - used, "紫外线%s  ", data.uv_text.c_str());
    }
    if (!data.update_time.empty()) {
        std::snprintf(line + used, sizeof(line) - used, "更新%s", data.update_time.c_str());
    }
    canvas.Text(14, top + 76, line);
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
