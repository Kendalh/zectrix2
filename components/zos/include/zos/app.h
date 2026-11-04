#ifndef ZOS_APP_H_
#define ZOS_APP_H_

#include <cstddef>
#include <cstdint>

#include "zectrix_board.h"
#include "zectrix_canvas.h"
#include "zos/display.h"
#include "zos/network.h"
#include "zos/settings.h"
#include "zos/types.h"

namespace zos {


/** Services the shell offers to the running app. Shell thread only, except Wake(). */
class Context {
public:
    virtual ~Context() = default;

    virtual Display& display() = 0;
    virtual ZectrixBoard& board() = 0;
    virtual Network& net() = 0;
    /** The running app's settings. */
    virtual Settings& settings() = 0;

    /** The whole content area changed: partial refresh of it. */
    virtual void Invalidate() = 0;
    /** Only `rect` (panel coordinates, inside the content area) changed. */
    virtual void Invalidate(const Rect& rect) = 0;
    /** Use a full refresh for the next update (e.g. hourly, to clear ghosting). */
    virtual void RequestFullRefresh() = 0;
    virtual void GoHome() = 0;
    /**
     * Show a whole-panel 16-gray picture (packed 4 bpp, 400x300, ZECTRIX_EPD_4BPP_FRAME_BYTES, high nibble = left
     * pixel, 0 black .. 15 white) with the header and the footer (the running app's title and hints()) laid over its
     * top and bottom rows. Full refresh. The app should report fullscreen() while the picture is up so the shell does
     * not repaint.
     */
    virtual esp_err_t ShowImageUnderChrome(const uint8_t* image, size_t size) = 0;
    /**
     * While a picture of ShowImageUnderChrome is up: redraw the footer from the app's current hints() with a partial
     * refresh of the footer only (the picture is not touched, which is much faster than a full refresh).
     */
    virtual esp_err_t UpdateContentFooter() = 0;
    /** Thread-safe: ask the shell to call the running app's OnWake(). */
    virtual void Wake() = 0;
};

/**
 * An app. The shell draws the header and footer; the app owns the content
 * area and draws it completely in Render(). To change the screen call
 * ctx.Invalidate(...) (partial refresh by default).
 */
class App {
public:
    virtual ~App() = default;

    /** Short stable id ([a-z0-9_], at most 11 chars): NVS namespace and web path. */
    virtual const char* id() const = 0;
    /** Header title and Home entry; ASCII only (the built-in font). */
    virtual const char* title() const = 0;
    /** One-line description shown next to the Home entry. */
    virtual const char* summary() const { return ""; }

    /** Notes for the footer. Only non-obvious keys. */
    virtual KeyHints hints() const { return {}; }

    /** While true the app owns the whole panel: no header/footer, no Render(). */
    virtual bool fullscreen() const { return false; }

    /** Name on the web page when it differs from the app's (e.g. one key page for two apps). */
    virtual const char* web_title() const { return title(); }
    /** An app with its own web page (e.g. "/photos"): listed on the web home next to the apps with settings. */
    virtual const char* web_page() const { return nullptr; }

    /** Settings editable on the device web page. */
    virtual const ConfigField* config_fields() const { return nullptr; }
    virtual size_t config_field_count() const { return 0; }

    virtual void OnEnter(Context& ctx) { (void)ctx; }
    virtual void OnLeave(Context& ctx) { (void)ctx; }
    /** Up, Down or Select click. Return true if it was used. */
    virtual bool OnKey(Context& ctx, Key key) {
        (void)ctx;
        (void)key;
        return false;
    }
    /** Milliseconds until OnTick() is wanted; negative means never. */
    virtual int64_t NextTickMs(Context& ctx) {
        (void)ctx;
        return -1;
    }
    virtual void OnTick(Context& ctx) { (void)ctx; }
    /** Wi-Fi/time/other background change; see Context::Wake(). */
    virtual void OnWake(Context& ctx) { (void)ctx; }
    /** A setting was saved on the web page. */
    virtual void OnConfigChanged(Context& ctx) { (void)ctx; }

    /** Draw the content area (already cleared to white). */
    virtual void Render(Context& ctx, ZectrixCanvas& canvas,
                        const Rect& content) = 0;
};

}  // namespace zos

#endif  // ZOS_APP_H_
