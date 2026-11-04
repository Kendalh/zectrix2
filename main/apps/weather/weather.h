#ifndef APPS_WEATHER_WEATHER_H_
#define APPS_WEATHER_WEATHER_H_

#include <cstdint>
#include <string>
#include <vector>

// Weather model and Open-Meteo (https://open-meteo.com, no API key) helpers.
// Parsing is adapted from the youn-ink-fourcolor-firmware project (MIT).
// Pure code: no network, no ESP-IDF, so the PC simulator builds it unchanged.

// Order matches the tables in weather_icons.h (tools/generate_weather_icons.py).
enum class WeatherIcon {
    kSunny, kClearNight, kPartlyCloudy, kPartlyCloudyNight, kOvercast,
    kRain, kHeavyRain, kThunder, kSnow, kFog,
};

struct WeatherDay {
    std::string label;  // 今天 / 明天 / 周X
    std::string text;   // 晴 / 多云 / 小雨
    WeatherIcon icon = WeatherIcon::kSunny;
    int temp_min = 0;
    int temp_max = 0;
};


struct WeatherData {
    std::string text;        // current condition, e.g. 多云
    WeatherIcon icon = WeatherIcon::kSunny;
    int temp = 0;
    int feels_like = 0;
    int humidity = -1;       // percent, -1 unknown
    std::string wind_dir;    // 东南风
    int wind_scale = 0;      // Beaufort
    std::string uv_text;     // 弱 / 中等 / ...
    std::string update_time; // observation time "14:30" (the city's local time)
    int aqi = -1;            // US AQI, -1 unknown
    std::string aqi_text;    // 优 / 良 / ...
    std::vector<WeatherDay> days;  // up to 7, first is today
};

struct WeatherCity {
    std::string name;  // 杭州
    std::string admin; // 浙江
    std::string lat;   // decimal degrees as text
    std::string lon;
};

/** WMO weather interpretation code -> 晴 / 多云 / 小雨 ... */
const char* WmoText(int code);
/** Daytime icon for a WMO code; see NightIcon(). */
WeatherIcon WmoIcon(int code);
/** Night variant of a clear / partly cloudy icon; others are unchanged. */
WeatherIcon NightIcon(WeatherIcon icon);
std::string WindDirectionText(double degrees);
/** km/h -> Beaufort 0..12 */
int WindBeaufort(double kmh);
const char* AqiText(int us_aqi);
const char* UvText(double uv_index);
/** "2026-10-16" -> 0 (Sunday) .. 6, or -1 */
int WeekdayFromDate(const char* yyyy_mm_dd);

/** Parse /v1/forecast (current + daily). */
bool ParseForecast(const char* json, WeatherData* out);
/** Parse the air-quality API (current.us_aqi) into aqi / aqi_text. */
bool ParseAir(const char* json, WeatherData* out);
/** Parse the geocoding search API. An empty result list is a success. */
bool ParseGeocode(const char* json, std::vector<WeatherCity>* out);

std::string UrlEncode(const std::string& text);
std::string ForecastUrl(const WeatherCity& city);
std::string AirUrl(const WeatherCity& city);
std::string GeocodeUrl(const std::string& query);

/** Blocking HTTPS GET (firmware: esp_http_client, simulator: curl). */
bool WeatherHttpGet(const std::string& url, std::string* body);

#endif  // APPS_WEATHER_WEATHER_H_
