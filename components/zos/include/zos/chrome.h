#ifndef ZOS_CHROME_H_
#define ZOS_CHROME_H_

#include "zectrix_canvas.h"
#include "zos/types.h"

namespace zos {

/** What the header shows; compared between frames to know when to redraw it. */
struct StatusInfo {
    bool battery_valid = false;
    int battery_percent = 0;
    bool charging = false;
    bool wifi_connected = false;
    bool wifi_config_mode = false;
    int wifi_level = 0;  // 0..4 signal bars

    bool operator==(const StatusInfo& o) const {
        return battery_valid == o.battery_valid &&
               battery_percent == o.battery_percent &&
               charging == o.charging && wifi_connected == o.wifi_connected &&
               wifi_config_mode == o.wifi_config_mode &&
               wifi_level == o.wifi_level;
    }
    bool operator!=(const StatusInfo& o) const { return !(*this == o); }
};

/**
 * The common app frame:
 *   header  title on the left, Wi-Fi and battery on the right
 *   content the app's area (Chrome::Content())
 *   footer  key notes on the left, global key rule on the right
 */
class Chrome {
public:
    static constexpr int kHeaderHeight = 32;
    static constexpr int kFooterHeight = 30;

    static Rect Header();
    static Rect Footer();
    static Rect Content();

    static void DrawHeader(ZectrixCanvas& canvas, const char* title,
                           const StatusInfo& status);
    /** `home_rule`: true on app pages ("Hold OK Home"), false on Home. */
    static void DrawFooter(ZectrixCanvas& canvas, const KeyHints& hints,
                           bool home_rule);
};

}  // namespace zos

#endif  // ZOS_CHROME_H_
