#include "zos/display.h"

#include <algorithm>
#include <cstring>

#include "esp_log.h"

namespace zos {

namespace {
constexpr char kTag[] = "zos_display";
}

Rect Union(const Rect& a, const Rect& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.w, b.x + b.w);
    const int y1 = std::max(a.y + a.h, b.y + b.h);
    return {x0, y0, x1 - x0, y1 - y0};
}

esp_err_t Display::Init() {
    zectrix_epd_config_t config = {};
    zectrix_epd_get_default_config(&config);
    return zectrix_epd_new(&config, &epd_);
}

esp_err_t Display::FullFrom(const uint8_t* data, size_t size) {
    esp_err_t err = zectrix_epd_power_on(epd_);
    if (err == ESP_OK) {
        err = zectrix_epd_refresh_full_1bpp(epd_, data, size);
    }
    const esp_err_t off = zectrix_epd_power_off(epd_);
    if (err == ESP_OK) {
        base_valid_ = true;
        partial_count_ = 0;
        err = off;
    }
    return err;
}

esp_err_t Display::Full() {
    return FullFrom(canvas_.data(), canvas_.size());
}

esp_err_t Display::Clear() {
    canvas_.Clear();
    return Full();
}

esp_err_t Display::Partial(const Rect& rect) {
    const int x0 = std::max(0, rect.x) & ~7;
    const int x1 = std::min(ZectrixCanvas::kWidth, (rect.x + rect.w + 7) & ~7);
    const int y0 = std::max(0, rect.y);
    const int y1 = std::min(ZectrixCanvas::kHeight, rect.y + rect.h);
    if (x1 <= x0 || y1 <= y0) {
        return ESP_OK;
    }
    if (!base_valid_ || partial_count_ >= partial_limit_) {
        return Full();
    }
    const size_t row_bytes = static_cast<size_t>(x1 - x0) / 8;
    const int rows = y1 - y0;
    for (int row = 0; row < rows; ++row) {
        std::memcpy(patch_ + static_cast<size_t>(row) * row_bytes,
                    canvas_.data() +
                        static_cast<size_t>(y0 + row) * ZectrixCanvas::kStride +
                        x0 / 8,
                    row_bytes);
    }
    const zectrix_epd_rect_t area = {x0, y0, x1 - x0, rows};
    return ShowPartialRaw({area.x, area.y, area.width, area.height}, patch_,
                          row_bytes * rows);
}

esp_err_t Display::ShowFull1bpp(const uint8_t* data, size_t size) {
    return FullFrom(data, size);
}

esp_err_t Display::ShowPartialRaw(const Rect& rect, const uint8_t* data,
                                  size_t size) {
    if (!base_valid_) {
        return ESP_ERR_INVALID_STATE;
    }
    const zectrix_epd_rect_t area = {rect.x, rect.y, rect.w, rect.h};
    esp_err_t err = zectrix_epd_power_on(epd_);
    if (err == ESP_OK) {
        err = zectrix_epd_refresh_partial_1bpp(epd_, &area, data, size);
    }
    const esp_err_t off = zectrix_epd_power_off(epd_);
    if (err == ESP_OK) {
        ++partial_count_;
        err = off;
    } else {
        ESP_LOGW(kTag, "partial refresh failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t Display::Show4bpp(const uint8_t* data, size_t size) {
    // A white 1bpp full refresh first keeps the gray image free of history.
    std::memset(patch_, 0xFF, sizeof(patch_));
    esp_err_t err = FullFrom(patch_, sizeof(patch_));
    if (err != ESP_OK) {
        return err;
    }
    err = zectrix_epd_power_on(epd_);
    if (err == ESP_OK) {
        err = zectrix_epd_refresh_full_4bpp(epd_, data, size);
    }
    const esp_err_t off = zectrix_epd_power_off(epd_);
    // Partial refresh is unsupported until another 1bpp full refresh.
    base_valid_ = false;
    return err == ESP_OK ? off : err;
}

}  // namespace zos
