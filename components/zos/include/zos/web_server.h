#ifndef ZOS_WEB_SERVER_H_
#define ZOS_WEB_SERVER_H_

#include "esp_http_server.h"

namespace zos {

class Shell;

/**
 * Device web page on port 80 (reachable once Wi-Fi is connected).
 *   GET  /                          single page, built from /api/apps
 *   GET  /api/status                Wi-Fi and battery
 *   GET  /api/apps                  apps with their settings and values
 *   POST /api/apps/<id>/config      JSON {key: value}; saves and notifies the app
 * Apps only declare ConfigField entries; no per-app web code is needed.
 */
class WebServer {
public:
    /** Idempotent. */
    void Start(Shell& shell);

    /** Free port 80 (the Wi-Fi setup portal needs it). Start() brings it back. */
    void Stop();

    Shell& shell() { return *shell_; }

private:
    Shell* shell_ = nullptr;
    httpd_handle_t server_ = nullptr;
};

}  // namespace zos

#endif  // ZOS_WEB_SERVER_H_
