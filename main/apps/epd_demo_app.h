#ifndef APPS_EPD_DEMO_APP_H_
#define APPS_EPD_DEMO_APP_H_

#include <cstdint>

#include "esp_err.h"
#include "zos/app.h"

/**
 * The original e-paper display showcase as an app: a short menu of scenes
 * (full 1bpp, partial 1bpp animation, 16-gray 4bpp). While a scene plays the
 * app owns the whole panel; any key stops it.
 */
class EpdDemoApp : public zos::App {
public:
    const char* id() const override { return "epddemo"; }
    const char* title() const override { return "墨水屏演示"; }
    const char* summary() const override { return "刷新模式"; }
    zos::KeyHints hints() const override;
    bool fullscreen() const override { return playing_; }

    void OnLeave(zos::Context& ctx) override;
    bool OnKey(zos::Context& ctx, zos::Key key) override;
    int64_t NextTickMs(zos::Context& ctx) override;
    void OnTick(zos::Context& ctx) override;
    void Render(zos::Context& ctx, ZectrixCanvas& canvas,
                const zos::Rect& content) override;

private:
    enum Scene { kLighthouse = 0, kFootprints = 1, kMountain = 2 };

    void Begin(Scene scene);
    void Finish(zos::Context& ctx, esp_err_t error);
    void Stop(zos::Context& ctx);
    zos::Rect RowRect(size_t index) const;
    void StepLighthouse(zos::Context& ctx);
    void StepFootprints(zos::Context& ctx);
    void StepMountain(zos::Context& ctx);

    size_t selected_ = 0;
    bool playing_ = false;
    bool run_all_ = false;
    Scene scene_ = kLighthouse;
    int step_ = 0;
    int64_t wait_ms_ = 0;
    int64_t refresh_ms_ = 0;
    const uint8_t* cursor_ = nullptr;
    uint16_t steps_total_ = 0;

    bool has_result_ = false;
    Scene last_scene_ = kLighthouse;
    int64_t last_ms_ = 0;
    esp_err_t last_error_ = ESP_OK;
};

#endif  // APPS_EPD_DEMO_APP_H_
