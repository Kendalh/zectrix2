#ifndef APPS_PHOTOS_APP_H_
#define APPS_PHOTOS_APP_H_

#include <cstdint>
#include <vector>

#include "apps/photo/photo_store.h"
#include "zos/app.h"

/**
 * Photo album. Photos are uploaded from a phone on the device web page (/photos), which turns them into 16-gray
 * 400x300 pictures in the browser; AI ink paintings can be saved here too. The page lists the titles with a preview of
 * the selected one; OK plays it full screen in 16 grays (no header or footer), the next photo follows by itself after the
 * interval set on the web page /photos (1/5/10 min, setting "interval" of this app); UP/DOWN go to the previous/next photo, OK returns.
 */
class PhotosApp : public zos::App {
public:
    const char* id() const override { return "photos"; }
    const char* title() const override { return "相册"; }
    const char* summary() const override { return "照片播放"; }
    const char* web_page() const override { return "/photos"; }
    zos::KeyHints hints() const override;
    bool fullscreen() const override { return showing_; }

    void OnEnter(zos::Context& ctx) override;
    void OnLeave(zos::Context& ctx) override;
    bool OnKey(zos::Context& ctx, zos::Key key) override;
    int64_t NextTickMs(zos::Context& ctx) override;
    void OnTick(zos::Context& ctx) override;
    void Render(zos::Context& ctx, ZectrixCanvas& canvas, const zos::Rect& content) override;

private:
    static constexpr int kPreviewW = 160;
    static constexpr int kPreviewH = 120;

    void Reload();
    void LoadPreview();
    void ShowCurrent(zos::Context& ctx);
    std::string Signature() const;

    std::vector<photos::Info> list_;
    size_t sel_ = 0;
    bool showing_ = false;
    int preview_id_ = -1;
    std::vector<uint8_t> preview_;  // kPreviewW x kPreviewH, one gray level 0..15 per byte
    std::string shown_sig_;
};

#endif  // APPS_PHOTOS_APP_H_
