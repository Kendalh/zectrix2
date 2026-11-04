#include "apps/photos_app.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <ctime>

#include "esp_log.h"
#include "zos/chrome.h"

namespace {

constexpr char kTag[] = "photos";
constexpr int kMargin = 8;
constexpr int kListW = 208;
constexpr int kRowH = 26;
constexpr int kPreviewX = 228;

// Cut `text` to at most max_w pixels, ending in "…" when shortened.
std::string FitOneLine(ZectrixCanvas& canvas, const std::string& text, int max_w) {
    if (canvas.TextWidth(text.c_str()) <= max_w) return text;
    const int ellipsis_w = canvas.TextWidth("…");
    std::string out;
    int w = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        const size_t len = std::min<size_t>(lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4,
                                            text.size() - i);
        const std::string ch = text.substr(i, len);
        w += canvas.TextWidth(ch.c_str());
        if (w + ellipsis_w > max_w) break;
        out += ch;
        i += len;
    }
    return out + "…";
}

}  // namespace

namespace {
// "off" = never switch by itself (-1); "5m", "10m", "15m" = that many minutes in milliseconds. An older or
// unknown stored value counts as 5 minutes.
int64_t IntervalMs(const std::string& text) {
    if (text == "off") return -1;
    if (text == "10m") return 10 * 60 * 1000;
    if (text == "15m") return 15 * 60 * 1000;
    return 5 * 60 * 1000;
}
}  // namespace

zos::KeyHints PhotosApp::hints() const {
    zos::KeyHints hints;
    if (!list_.empty() && !showing_) {
        hints.select = "播放";
    }
    return hints;
}

void PhotosApp::Reload() {
    const int current = (!list_.empty() && sel_ < list_.size()) ? list_[sel_].id : -1;
    list_ = photos::Store::Get().List();
    sel_ = 0;
    for (size_t i = 0; i < list_.size(); ++i)
        if (list_[i].id == current) sel_ = i;  // keep the selection when the list changed under it
    LoadPreview();
}

void PhotosApp::LoadPreview() {
    if (list_.empty()) {
        preview_id_ = -1;
        preview_.clear();
        return;
    }
    const int id = list_[sel_].id;
    if (id == preview_id_ && !preview_.empty()) return;
    std::vector<uint8_t> image;
    if (!photos::Store::Get().Load(id, &image)) {
        preview_id_ = -1;
        preview_.clear();
        return;
    }
    preview_.assign(static_cast<size_t>(kPreviewW) * kPreviewH, 15);
    auto level = [&](int x, int y) {
        const uint8_t byte = image[(static_cast<size_t>(y) * 400 + x) / 2];
        return (x & 1) ? (byte & 0x0F) : (byte >> 4);
    };
    for (int y = 0; y < kPreviewH; ++y) {
        for (int x = 0; x < kPreviewW; ++x) {
            const int sx = x * 5 / 2, sy = y * 5 / 2;  // 400x300 -> 160x120: average the 2x2 block
            preview_[static_cast<size_t>(y) * kPreviewW + x] =
                static_cast<uint8_t>((level(sx, sy) + level(sx + 1, sy) + level(sx, sy + 1) + level(sx + 1, sy + 1) + 2) / 4);
        }
    }
    preview_id_ = id;
}

std::string PhotosApp::Signature() const {
    std::string sig = std::to_string(list_.size());
    for (const photos::Info& info : photos::Store::Get().List()) sig += "|" + std::to_string(info.seq);
    return sig;
}

void PhotosApp::OnEnter(zos::Context&) {
    showing_ = false;
    preview_id_ = -1;
    sel_ = 0;
    Reload();
    shown_sig_ = Signature();
}

void PhotosApp::OnLeave(zos::Context&) { showing_ = false; }

void PhotosApp::ShowCurrent(zos::Context& ctx) {
    if (list_.empty()) return;
    std::vector<uint8_t> image;
    if (!photos::Store::Get().Load(list_[sel_].id, &image)) return;
    showing_ = true;
    const esp_err_t err = ctx.display().Show4bpp(image.data(), image.size());  // the whole panel: no header, no footer
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "4bpp refresh failed: %s", esp_err_to_name(err));
        showing_ = false;
        ctx.Invalidate();
    }
}

bool PhotosApp::OnKey(zos::Context& ctx, zos::Key key) {
    if (showing_) {
        if (key == zos::Key::kSelect) {  // back to the list
            showing_ = false;
            LoadPreview();
            ctx.Invalidate();
            return true;
        }
        if (list_.size() > 1 && (key == zos::Key::kUp || key == zos::Key::kDown)) {
            sel_ = key == zos::Key::kDown ? (sel_ + 1) % list_.size() : (sel_ + list_.size() - 1) % list_.size();
            ShowCurrent(ctx);
        }
        return true;
    }
    if (list_.empty()) return false;
    switch (key) {
        case zos::Key::kUp:
        case zos::Key::kDown:
            sel_ = key == zos::Key::kDown ? (sel_ + 1) % list_.size() : (sel_ + list_.size() - 1) % list_.size();
            LoadPreview();
            ctx.Invalidate();
            return true;
        case zos::Key::kSelect:
            ShowCurrent(ctx);
            return true;
        default:
            return false;
    }
}

// Photos can arrive from the web page while this page is open.
int64_t PhotosApp::NextTickMs(zos::Context& ctx) {
    return showing_ ? IntervalMs(ctx.settings().GetString("interval", "5m")) : 4000;
}

// Slideshow: while a photo is up the next one follows after the interval (the tick restarts with every key press).
void PhotosApp::OnTick(zos::Context& ctx) {
    if (showing_) {
        if (list_.size() > 1) {
            sel_ = (sel_ + 1) % list_.size();
            ShowCurrent(ctx);
        }
        return;
    }
    const std::string sig = Signature();
    if (sig == shown_sig_) return;
    shown_sig_ = sig;
    Reload();
    ctx.Invalidate();
}

void PhotosApp::Render(zos::Context& ctx, ZectrixCanvas& canvas, const zos::Rect& content) {
    shown_sig_ = Signature();
    if (list_.empty()) {
        const auto center = [&](int y, const char* text, int scale = 1) {
            canvas.Text(std::max(4, (ZectrixCanvas::kWidth - canvas.TextWidth(text, scale)) / 2), y, text, scale);
        };
        center(content.y + 36, "还没有照片", 2);
        center(content.y + 92, "用手机浏览器打开：");
        const std::string url = "http://" + (ctx.net().ip().empty() ? std::string("设备地址") : ctx.net().ip()) + "/photos";
        center(content.y + 118, url.c_str());
        center(content.y + 156, "选一张照片上传，自动转为 16 级灰度");
        return;
    }

    // List with the selection inverted, as many rows as fit.
    const int visible = std::max(1, (content.h - 8) / kRowH);
    const int first = std::max(0, std::min(static_cast<int>(sel_) - visible + 1,
                                           static_cast<int>(list_.size()) - visible));
    for (int row = 0; row < std::min<int>(visible, list_.size() - first); ++row) {
        const size_t index = first + row;
        const int y = content.y + 4 + row * kRowH;
        const bool selected = index == sel_;
        char label[16];
        std::snprintf(label, sizeof(label), "%d ", static_cast<int>(index) + 1);
        if (selected) canvas.FillRect(kMargin, y, kListW, kRowH - 2, true);
        canvas.Text(kMargin + 6, y + 5, label, 1, selected);
        canvas.Text(kMargin + 6 + canvas.TextWidth(label), y + 5,
                    FitOneLine(canvas, list_[index].title, kListW - 12 - canvas.TextWidth(label)).c_str(), 1, selected);
    }

    // Preview of the selected photo, dithered to the panel's black and white.
    static constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const int py = content.y + 8;
    canvas.Rect(kPreviewX - 1, py - 1, kPreviewW + 2, kPreviewH + 2);
    if (!preview_.empty()) {
        for (int y = 0; y < kPreviewH; ++y) {
            for (int x = 0; x < kPreviewW; ++x) {
                const int dark = (15 - preview_[static_cast<size_t>(y) * kPreviewW + x]) * 16 / 15;
                canvas.Pixel(kPreviewX + x, py + y, dark > kBayer[y & 3][x & 3]);
            }
        }
    }
    char info[48];
    std::snprintf(info, sizeof(info), "第 %d / %d 张", static_cast<int>(sel_) + 1, static_cast<int>(list_.size()));
    canvas.Text(kPreviewX, py + kPreviewH + 10, info);
    if (list_[sel_].timestamp != 0) {
        const time_t t = list_[sel_].timestamp;
        tm local = {};
        localtime_r(&t, &local);
        std::snprintf(info, sizeof(info), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
        canvas.Text(kPreviewX, py + kPreviewH + 32, info);
    }
}
