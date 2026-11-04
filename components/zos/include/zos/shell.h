#ifndef ZOS_SHELL_H_
#define ZOS_SHELL_H_

#include <cstddef>
#include <memory>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "zos/app.h"
#include "zos/chrome.h"
#include "zos/display.h"
#include "zos/network.h"
#include "zos/settings.h"
#include "zos/web_server.h"

namespace zos {

/**
 * The OS core: Home (app selector), unified key routing, header/footer
 * chrome, refresh scheduling and power-off.
 *
 * Key rules (the same on every page):
 *   Up / Down click   app-defined (Home: move selection)
 *   Select click      app-defined (Home: open app)
 *   Hold Select 1.5s  back to Home
 *   Hold Down 3s      power off
 */
class Shell : public Context {
public:
    Shell(ZectrixBoard& board, Display& display, Network& network,
          WebServer& web);
    ~Shell() override;

    /** Add an app before Run(); Home lists them in this order. */
    void Register(std::unique_ptr<App> app);

    [[noreturn]] void Run();

    // Context
    Display& display() override { return display_; }
    ZectrixBoard& board() override { return board_; }
    Network& net() override { return network_; }
    Settings& settings() override {
        return callback_app_ >= 0 ? apps_[callback_app_].settings
                                  : home_settings_;
    }
    void Invalidate() override;
    void Invalidate(const Rect& rect) override;
    void RequestFullRefresh() override { full_ = true; }
    void GoHome() override;
    esp_err_t ShowImageUnderChrome(const uint8_t* image, size_t size) override;
    esp_err_t UpdateContentFooter() override;
    void Wake() override;

    // Used by the web server (thread-safe).
    size_t app_count() const { return apps_.size(); }
    App& app(size_t index) { return *apps_[index].app; }
    Settings& app_settings(size_t index) { return apps_[index].settings; }
    void PostConfigChanged(size_t app_index);
    /** Header state as last drawn (may be stale; for the web page). */
    StatusInfo status() const { return drawn_status_; }

private:
    struct Entry {
        std::unique_ptr<App> app;
        Settings settings;
        explicit Entry(std::unique_ptr<App> a)
            : app(std::move(a)), settings(app->id()) {}
    };
    struct Event {
        enum Type : uint8_t { kKey, kPowerOff, kWake, kConfig } type;
        Key key;
        int app;
    };

    class Launcher;
    std::vector<uint8_t> footer_bits_;        // the footer as last shown under a picture (1 bpp rows)
    std::unique_ptr<ZectrixCanvas> scratch_;  // header/footer of ShowImageUnderChrome

    static void InputTaskEntry(void* arg);
    void Post(const Event& event);
    void Open(int index);
    App& current();
    void Dispatch(const Event& event);
    void Flush();
    void Splash();
    StatusInfo ReadStatus();
    [[noreturn]] void PowerOff();

    ZectrixBoard& board_;
    Display& display_;
    Network& network_;
    WebServer& web_;
    std::vector<Entry> apps_;
    std::unique_ptr<Launcher> launcher_;
    Settings home_settings_{"home"};
    QueueHandle_t events_ = nullptr;
    int callback_app_ = -1;  // app whose callback is running (settings, Invalidate)

    int current_ = -1;  // -1 = Home
    bool full_ = true;
    bool dirty_ = false;
    Rect dirty_rect_;
    bool was_fullscreen_ = false;
    StatusInfo drawn_status_;
    bool status_drawn_ = false;
    KeyHints drawn_hints_;
    bool hints_drawn_ = false;
};

}  // namespace zos

#endif  // ZOS_SHELL_H_
