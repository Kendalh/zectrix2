#include "apps/weather/weather.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>

#include "cJSON.h"

const char* WmoText(int code) {
    switch (code) {
        case 0: return "晴";
        case 1: return "晴间多云";
        case 2: return "多云";
        case 3: return "阴";
        case 45: case 48: return "雾";
        case 51: case 53: case 55: return "毛毛雨";
        case 56: case 57: case 66: case 67: return "冻雨";
        case 61: return "小雨";
        case 63: return "中雨";
        case 65: return "大雨";
        case 71: return "小雪";
        case 73: return "中雪";
        case 75: return "大雪";
        case 77: return "雪粒";
        case 80: return "阵雨";
        case 81: return "中阵雨";
        case 82: return "暴雨";
        case 85: return "阵雪";
        case 86: return "大阵雪";
        case 95: return "雷雨";
        case 96: case 99: return "雷雨冰雹";
        default: return "未知";
    }
}

WeatherIcon WmoIcon(int code) {
    switch (code) {
        case 0: return WeatherIcon::kSunny;
        case 1: case 2: return WeatherIcon::kPartlyCloudy;
        case 3: return WeatherIcon::kOvercast;
        case 45: case 48: return WeatherIcon::kFog;
        case 71: case 73: case 75: case 77: case 85: case 86: return WeatherIcon::kSnow;
        case 95: case 96: case 99: return WeatherIcon::kThunder;
        case 63: case 65: case 81: case 82: return WeatherIcon::kHeavyRain;
        default: return (code >= 51 && code <= 82) ? WeatherIcon::kRain : WeatherIcon::kSunny;
    }
}

WeatherIcon NightIcon(WeatherIcon icon) {
    if (icon == WeatherIcon::kSunny) return WeatherIcon::kClearNight;
    if (icon == WeatherIcon::kPartlyCloudy) return WeatherIcon::kPartlyCloudyNight;
    return icon;
}

std::string WindDirectionText(double deg) {
    static const char* const kNames[] = {"北风", "东北风", "东风", "东南风",
                                         "南风", "西南风", "西风", "西北风"};
    deg = std::fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;
    return kNames[static_cast<int>((deg + 22.5) / 45.0) % 8];
}

int WindBeaufort(double kmh) {
    static const double kLimits[] = {2, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
    for (int i = 0; i < 12; ++i) {
        if (kmh < kLimits[i]) return i;
    }
    return 12;
}

const char* AqiText(int aqi) {
    if (aqi <= 50) return "优";
    if (aqi <= 100) return "良";
    if (aqi <= 150) return "轻度污染";
    if (aqi <= 200) return "中度污染";
    if (aqi <= 300) return "重度污染";
    return "严重污染";
}

const char* UvText(double uv) {
    if (uv < 3) return "弱";
    if (uv < 6) return "中等";
    if (uv < 8) return "强";
    if (uv < 11) return "很强";
    return "极强";
}

int WeekdayFromDate(const char* s) {
    int y = 0, m = 0, d = 0;
    if (!s || std::sscanf(s, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) {
        return -1;
    }
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

namespace {

double NumberAt(cJSON* arr, int index, double fallback = 0) {
    cJSON* item = cJSON_GetArrayItem(arr, index);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

const char* StringAt(cJSON* arr, int index) {
    cJSON* item = cJSON_GetArrayItem(arr, index);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

int RoundInt(double v) { return static_cast<int>(std::lround(v)); }

bool ParseDaily(cJSON* daily, WeatherData* out) {
    cJSON* times = cJSON_GetObjectItem(daily, "time");
    cJSON* codes = cJSON_GetObjectItem(daily, "weather_code");
    cJSON* tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
    cJSON* tmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
    if (!cJSON_IsArray(times) || !cJSON_IsArray(codes) || !cJSON_IsArray(tmax) ||
        !cJSON_IsArray(tmin)) {
        return false;
    }
    static const char* const kWeekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    const int n = std::min(7, cJSON_GetArraySize(times));
    out->days.clear();
    for (int i = 0; i < n; ++i) {
        WeatherDay day;
        const int wd = WeekdayFromDate(StringAt(times, i));
        day.label = i == 0 ? "今天" : i == 1 ? "明天" : (wd >= 0 ? kWeekdays[wd] : "--");
        const int code = RoundInt(NumberAt(codes, i, -1));
        day.text = WmoText(code);
        day.icon = WmoIcon(code);
        day.temp_max = RoundInt(NumberAt(tmax, i));
        day.temp_min = RoundInt(NumberAt(tmin, i));
        out->days.push_back(day);
    }
    cJSON* uv = cJSON_GetObjectItem(daily, "uv_index_max");
    if (cJSON_IsArray(uv) && cJSON_GetArraySize(uv) > 0) out->uv_text = UvText(NumberAt(uv, 0));
    return !out->days.empty();
}

}  // namespace

bool ParseForecast(const char* json, WeatherData* out) {
    if (!json || !out) return false;
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;
    cJSON* current = cJSON_GetObjectItem(root, "current");
    cJSON* daily = cJSON_GetObjectItem(root, "daily");
    if (!cJSON_IsObject(current) || !cJSON_IsObject(daily)) {
        cJSON_Delete(root);
        return false;
    }
    auto number = [current](const char* key, double* v) {
        cJSON* item = cJSON_GetObjectItem(current, key);
        if (!cJSON_IsNumber(item)) return false;
        *v = item->valuedouble;
        return true;
    };
    double v = 0;
    bool has_temp = false;
    if (number("temperature_2m", &v)) {
        out->temp = RoundInt(v);
        has_temp = true;
    }
    if (number("apparent_temperature", &v)) out->feels_like = RoundInt(v);
    if (number("relative_humidity_2m", &v)) out->humidity = RoundInt(v);
    const bool night = number("is_day", &v) && v < 0.5;
    if (number("weather_code", &v)) {
        out->text = WmoText(RoundInt(v));
        out->icon = WmoIcon(RoundInt(v));
        if (night) out->icon = NightIcon(out->icon);
    }
    if (number("wind_direction_10m", &v)) out->wind_dir = WindDirectionText(v);
    if (number("wind_speed_10m", &v)) out->wind_scale = WindBeaufort(v);
    cJSON* time_item = cJSON_GetObjectItem(current, "time");  // "2026-10-16T14:30"
    if (cJSON_IsString(time_item) && time_item->valuestring) {
        const char* tpos = std::strchr(time_item->valuestring, 'T');
        if (tpos && std::strlen(tpos + 1) >= 5) out->update_time.assign(tpos + 1, 5);
    }
    const bool ok = ParseDaily(daily, out);
    cJSON_Delete(root);
    return ok && has_temp;
}

bool ParseAir(const char* json, WeatherData* out) {
    if (!json || !out) return false;
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;
    cJSON* current = cJSON_GetObjectItem(root, "current");
    cJSON* aqi = cJSON_IsObject(current) ? cJSON_GetObjectItem(current, "us_aqi") : nullptr;
    const bool ok = cJSON_IsNumber(aqi);
    if (ok) {
        out->aqi = RoundInt(aqi->valuedouble);
        out->aqi_text = AqiText(out->aqi);
    }
    cJSON_Delete(root);
    return ok;
}

bool ParseGeocode(const char* json, std::vector<WeatherCity>* out) {
    if (!json || !out) return false;
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;
    out->clear();
    cJSON* results = cJSON_GetObjectItem(root, "results");  // absent when nothing matched
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, results) {
        cJSON* name = cJSON_GetObjectItem(item, "name");
        cJSON* lat = cJSON_GetObjectItem(item, "latitude");
        cJSON* lon = cJSON_GetObjectItem(item, "longitude");
        if (!cJSON_IsString(name) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) continue;
        WeatherCity city;
        city.name = name->valuestring;
        cJSON* admin = cJSON_GetObjectItem(item, "admin1");
        if (cJSON_IsString(admin)) city.admin = admin->valuestring;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.4f", lat->valuedouble);
        city.lat = buf;
        std::snprintf(buf, sizeof(buf), "%.4f", lon->valuedouble);
        city.lon = buf;
        out->push_back(std::move(city));
    }
    cJSON_Delete(root);
    return true;
}

std::string UrlEncode(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// timezone=auto: times (and the daily split) follow the city, not a fixed zone.
std::string ForecastUrl(const WeatherCity& city) {
    return "https://api.open-meteo.com/v1/forecast?latitude=" + city.lat + "&longitude=" + city.lon +
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,"
           "wind_speed_10m,wind_direction_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,uv_index_max"
           "&timezone=auto&forecast_days=7";
}

std::string AirUrl(const WeatherCity& city) {
    return "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + city.lat +
           "&longitude=" + city.lon + "&current=us_aqi&timezone=auto";
}

std::string GeocodeUrl(const std::string& query) {
    return "https://geocoding-api.open-meteo.com/v1/search?count=1&language=zh&format=json&name=" +
           UrlEncode(query);
}
