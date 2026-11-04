#include "zos/chrome.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace zos {

namespace {

constexpr int kWidth = ZectrixCanvas::kWidth;
constexpr int kHeight = ZectrixCanvas::kHeight;

// Header icons are drawn white on the black bar.
void DrawBattery(ZectrixCanvas& canvas, const StatusInfo& status) {
    constexpr int x = 356, y = 9, w = 30, h = 14;
    canvas.Rect(x, y, w, h, false);
    canvas.FillRect(x + w, y + 4, 3, h - 8, false);
    char text[8];
    if (status.battery_valid) {
        const int fill = (w - 4) * status.battery_percent / 100;
        canvas.FillRect(x + 2, y + 2, fill, h - 4, false);
        std::snprintf(text, sizeof(text), "%d%%%s", status.battery_percent,
                      status.charging ? "+" : "");
    } else {
        std::snprintf(text, sizeof(text), "--%%");
    }
    canvas.Text(x - 6 - canvas.TextWidth(text), 8, text, 1, true);
}

void DrawWifi(ZectrixCanvas& canvas, const StatusInfo& status) {
    constexpr int x = 270, bottom = 23, bar_w = 4, gap = 2;
    for (int i = 0; i < 4; ++i) {
        const int bar_h = 4 + i * 3;
        const int bx = x + i * (bar_w + gap);
        if (status.wifi_connected && i < status.wifi_level) {
            canvas.FillRect(bx, bottom - bar_h, bar_w, bar_h, false);
        } else {
            canvas.Rect(bx, bottom - bar_h, bar_w, bar_h, false);
        }
    }
    if (status.wifi_config_mode) {
        canvas.Text(x + 26, 8, "AP", 1, true);
    } else if (!status.wifi_connected) {
        canvas.Line(x - 2, bottom + 1, x + 22, bottom - 15, false);
    }
}

}  // namespace

Rect Chrome::Header() { return {0, 0, kWidth, kHeaderHeight}; }
Rect Chrome::Footer() {
    return {0, kHeight - kFooterHeight, kWidth, kFooterHeight};
}
Rect Chrome::Content() {
    return {0, kHeaderHeight, kWidth,
            kHeight - kHeaderHeight - kFooterHeight};
}

void Chrome::DrawHeader(ZectrixCanvas& canvas, const char* title,
                        const StatusInfo& status) {
    canvas.FillRect(0, 0, kWidth, kHeaderHeight, true);
    canvas.Text(10, 8, title, 1, true);
    DrawWifi(canvas, status);
    DrawBattery(canvas, status);
}

void Chrome::DrawFooter(ZectrixCanvas& canvas, const KeyHints& hints,
                        bool home_rule) {
    const Rect footer = Footer();
    canvas.FillRect(footer.x, footer.y, footer.w, footer.h, false);
    canvas.Line(0, footer.y, kWidth - 1, footer.y);

    char left[96] = {};
    size_t used = 0;
    const auto append = [&](const char* key, const char* note) {
        if (note == nullptr) return;
        used += std::snprintf(left + used, sizeof(left) - used, "%s%s %s",
                              used ? "  " : "", key, note);
        used = std::min(used, sizeof(left) - 1);
    };
    append("UP", hints.up);
    append("DOWN", hints.down);
    append("OK", hints.select);

    const char* rule = home_rule ? "长按 OK：主页" : "长按 DOWN：关机";
    const int text_y = footer.y + 8;
    canvas.Text(8, text_y, left);
    const int rule_x = kWidth - 8 - canvas.TextWidth(rule);
    if (8 + canvas.TextWidth(left) + 12 < rule_x) {
        canvas.Text(rule_x, text_y, rule);
    }
}

}  // namespace zos
