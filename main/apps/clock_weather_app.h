#ifndef APPS_CLOCK_WEATHER_APP_H_
#define APPS_CLOCK_WEATHER_APP_H_

#include <mutex>
#include <string>

#include "apps/weather/weather.h"
#include "zos/app.h"

/**
 * Clock and weather. The big HH:MM wakes once per minute and partially
 * refreshes only the top area (a full refresh on the hour clears ghosting).
 * Weather (Open-Meteo) is fetched on a worker task, only while Wi-Fi is up
 * and the data is older than the configured interval; the shell is never
 * blocked. The city is a name: it is geocoded once and the result is kept.
 */
class ClockWeatherApp : public zos::App {
public:
    ClockWeatherApp();

    const char* id() const override { return "clock"; }
    const char* title() const override { return "时钟天气"; }
    const char* summary() const override { return "时间和天气"; }
    zos::KeyHints hints() const override;
    const zos::ConfigField* config_fields() const override;
    size_t config_field_count() const override;

    void OnEnter(zos::Context& ctx) override;
    void OnLeave(zos::Context& ctx) override;
    bool OnKey(zos::Context& ctx, zos::Key key) override;
    int64_t NextTickMs(zos::Context& ctx) override;
    void OnTick(zos::Context& ctx) override;
    void OnWake(zos::Context& ctx) override;
    void OnConfigChanged(zos::Context& ctx) override;
    void Render(zos::Context& ctx, ZectrixCanvas& canvas,
                const zos::Rect& content) override;

private:
    struct Job;
    enum class Status { kIdle, kFetching, kFailed, kCityNotFound };

    static void ApplyTimezone(const zos::Settings& settings);
    static zos::Rect TimeArea(const zos::Rect& content);
    static void WorkerEntry(void* arg);
    static void RunWorker(Job* job);

    /** Start a fetch if Wi-Fi is up, none runs and the data is stale (or `force`). */
    void MaybeFetch(zos::Context& ctx, bool force);
    void RenderWeather(ZectrixCanvas& canvas, const zos::Rect& content,
                       const std::string& city, bool connected);

    std::mutex mutex_;  // everything below is shared with the worker task
    WeatherData data_;
    bool has_data_ = false;
    int64_t fetched_ms_ = 0;
    int64_t next_try_ms_ = 0;
    int generation_ = 0;  // bumped when the city changes: in-flight results are dropped
    Status status_ = Status::kIdle;
    bool geo_ready_ = false;  // a worker resolved the city; OnWake() saves it
    std::string geo_query_;
    WeatherCity geo_city_;
};

#endif  // APPS_CLOCK_WEATHER_APP_H_
