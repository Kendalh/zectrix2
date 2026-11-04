#ifndef APPS_WIFI_SETUP_APP_H_
#define APPS_WIFI_SETUP_APP_H_

#include "zos/app.h"

/**
 * Wi-Fi provisioning ("peiwang"): shows the connection, and with OK starts a
 * hotspot with a captive portal where the phone picks the network and
 * password. Leaving the app ends provisioning.
 */
class WifiSetupApp : public zos::App {
public:
    const char* id() const override { return "wifi"; }
    const char* title() const override { return "配网"; }
    const char* summary() const override { return "连接 Wi-Fi"; }
    zos::KeyHints hints() const override;

    void OnEnter(zos::Context& ctx) override;
    void OnLeave(zos::Context& ctx) override;
    bool OnKey(zos::Context& ctx, zos::Key key) override;
    void OnWake(zos::Context& ctx) override;
    void Render(zos::Context& ctx, ZectrixCanvas& canvas,
                const zos::Rect& content) override;

private:
    zos::Network* net_ = nullptr;
};

#endif  // APPS_WIFI_SETUP_APP_H_
