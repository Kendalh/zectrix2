#include "apps/clock_app.h"

#include <sys/time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "zos/chrome.h"
#include "zos/network.h"

namespace {

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
};

constexpr int kTimeScale = 8;
constexpr int kDateScale = 2;

}  // namespace

ClockApp::ClockApp() { ApplyTimezone(zos::Settings("clock")); }

const zos::ConfigField* ClockApp::config_fields() const { return kFields; }
size_t ClockApp::config_field_count() const {
    return sizeof(kFields) / sizeof(kFields[0]);
}

void ClockApp::ApplyTimezone(const zos::Settings& settings) {
    const std::string zone = settings.GetString("zone", "北京");
    const char* posix = kZones[0].posix;
    for (const Zone& candidate : kZones) {
        if (zone == candidate.name) posix = candidate.posix;
    }
    setenv("TZ", posix, 1);
    tzset();
}

zos::Rect ClockApp::TimeArea(const zos::Rect& content) {
    return {0, content.y + 16, content.w, 196};
}

void ClockApp::OnEnter(zos::Context& ctx) {
    // Minute updates are all partial; the hour change forces a full refresh.
    ctx.display().set_partial_limit(255);
}

void ClockApp::OnLeave(zos::Context& ctx) {
    ctx.display().set_partial_limit(8);
}

int64_t ClockApp::NextTickMs(zos::Context&) {
    if (!zos::Network::time_valid()) return -1;  // OnWake() fires on sync
    timeval tv;
    gettimeofday(&tv, nullptr);
    const int64_t into_minute = (tv.tv_sec % 60) * 1000 + tv.tv_usec / 1000;
    return 60000 - into_minute + 20;
}

void ClockApp::OnTick(zos::Context& ctx) {
    const time_t now = time(nullptr);
    tm local = {};
    localtime_r(&now, &local);
    if (local.tm_min == 0) ctx.RequestFullRefresh();
    ctx.Invalidate(TimeArea(zos::Chrome::Content()));
}

void ClockApp::OnWake(zos::Context& ctx) { ctx.Invalidate(); }

void ClockApp::OnConfigChanged(zos::Context& ctx) {
    ApplyTimezone(ctx.settings());
    ctx.Invalidate();
}

void ClockApp::Render(zos::Context& ctx, ZectrixCanvas& canvas,
                      const zos::Rect& content) {
    char text[64];
    if (!zos::Network::time_valid()) {
        canvas.TextCentered(content.y + 40, "--:--", kTimeScale);
        canvas.TextCentered(content.y + 190, "时间未设置");
        canvas.TextCentered(content.y + 212, "连接 Wi-Fi 后自动校时");
        return;
    }
    const time_t now = time(nullptr);
    tm local = {};
    localtime_r(&now, &local);

    const bool hour24 = ctx.settings().GetBool("hour24", true);
    if (hour24) {
        std::strftime(text, sizeof(text), "%H:%M", &local);
    } else {
        std::strftime(text, sizeof(text), "%I:%M", &local);
    }
    canvas.TextCentered(content.y + 24, text, kTimeScale);
    if (!hour24) {
        canvas.TextCentered(content.y + 24 + 16 * kTimeScale + 4,
                            local.tm_hour < 12 ? "上午" : "下午", 1);
    }
    if (ctx.settings().GetBool("show_date", true)) {
        static const char* const kWeekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
        std::snprintf(text, sizeof(text), "%04d年%02d月%02d日 星期%s",
                      local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                      kWeekdays[local.tm_wday]);
        canvas.TextCentered(content.y + 24 + 16 * kTimeScale + 28, text,
                            kDateScale);
    }
}
