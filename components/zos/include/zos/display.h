#ifndef ZOS_DISPLAY_H_
#define ZOS_DISPLAY_H_

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "zectrix_canvas.h"
#include "zectrix_epd.h"
#include "zos/types.h"

namespace zos {

/**
 * Owns the panel and a 1bpp canvas, and decides between partial and full
 * refresh. Every refresh powers the panel on and off again to save energy.
 *
 * Partial refresh rules enforced here:
 *   - x and width are widened to multiples of 8 pixels;
 *   - a full 1bpp refresh must have established the base image first;
 *   - after `partial_limit` partial refreshes the next one becomes a full
 *     refresh to clear ghosting.
 */
class Display {
public:
    esp_err_t Init();

    ZectrixCanvas& canvas() { return canvas_; }

    /** Full refresh of the whole canvas. */
    esp_err_t Full();
    /** Partial refresh of `rect` from the canvas (may be promoted to full). */
    esp_err_t Partial(const Rect& rect);
    /** White full refresh (also the shutdown screen). */
    esp_err_t Clear();

    // Raw access for apps that bring their own frames. The canvas is not
    // touched, so the shell redraws and does a full refresh when the app
    // returns the panel.
    esp_err_t ShowFull1bpp(const uint8_t* data, size_t size);
    esp_err_t ShowPartialRaw(const Rect& rect, const uint8_t* data,
                             size_t size);
    /** White full 1bpp refresh followed by the 16-gray image. */
    esp_err_t Show4bpp(const uint8_t* data, size_t size);

    /** Full refresh after this many partial refreshes (default 8). */
    void set_partial_limit(int limit) { partial_limit_ = limit; }
    int partial_count() const { return partial_count_; }

private:
    esp_err_t FullFrom(const uint8_t* data, size_t size);

    zectrix_epd_handle_t epd_ = nullptr;
    ZectrixCanvas canvas_;
    uint8_t patch_[ZECTRIX_EPD_1BPP_FRAME_BYTES] = {};  // partial rows / white frame
    bool base_valid_ = false;
    int partial_count_ = 0;
    int partial_limit_ = 8;
};

}  // namespace zos

#endif  // ZOS_DISPLAY_H_
