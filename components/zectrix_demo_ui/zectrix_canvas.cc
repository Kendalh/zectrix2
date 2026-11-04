#include "zectrix_canvas.h"

#include <algorithm>
#include <cstdlib>

#include "zectrix_ascii_font_8x16.h"
#include "zectrix_cjk_font_16.h"

void ZectrixCanvas::Clear(bool white) {
    pixels_.fill(white ? 0xff : 0x00);
}

void ZectrixCanvas::Pixel(int x, int y, bool black) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
        return;
    }
    uint8_t& byte = pixels_[static_cast<size_t>(y) * kStride + x / 8];
    const uint8_t mask = static_cast<uint8_t>(1U << (7 - (x & 7)));
    if (black) {
        byte &= static_cast<uint8_t>(~mask);
    } else {
        byte |= mask;
    }
}

void ZectrixCanvas::FillRect(int x, int y, int width, int height,
                             bool black) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(kWidth, x + width);
    const int bottom = std::min(kHeight, y + height);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            Pixel(px, py, black);
        }
    }
}

void ZectrixCanvas::FillGray(int x, int y, int width, int height, int level) {
    // 4x4 Bayer thresholds: a pixel is black when level exceeds its threshold.
    static constexpr uint8_t kBayer[4][4] = {
        {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    for (int row = y; row < y + height; ++row) {
        for (int col = x; col < x + width; ++col) {
            Pixel(col, row, level > kBayer[row & 3][col & 3]);
        }
    }
}

void ZectrixCanvas::Rect(int x, int y, int width, int height, bool black) {
    Line(x, y, x + width - 1, y, black);
    Line(x, y + height - 1, x + width - 1, y + height - 1, black);
    Line(x, y, x, y + height - 1, black);
    Line(x + width - 1, y, x + width - 1, y + height - 1, black);
}

void ZectrixCanvas::Line(int x0, int y0, int x1, int y1, bool black) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        Pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

namespace {

struct Glyph {
    const uint16_t* rows;
    int width;
};

// Decodes one UTF-8 character and returns the pointer after it.
const char* NextCodePoint(const char* text, uint32_t* code_point) {
    const unsigned char lead = static_cast<unsigned char>(*text);
    int extra = 0;
    uint32_t value = lead;
    if (lead >= 0xF0) {
        extra = 3;
        value = lead & 0x07;
    } else if (lead >= 0xE0) {
        extra = 2;
        value = lead & 0x0F;
    } else if (lead >= 0xC0) {
        extra = 1;
        value = lead & 0x1F;
    }
    ++text;
    for (; extra > 0 && (static_cast<unsigned char>(*text) & 0xC0) == 0x80;
         --extra, ++text) {
        value = (value << 6) | (static_cast<unsigned char>(*text) & 0x3F);
    }
    *code_point = extra == 0 ? value : '?';
    return text;
}

// ASCII from the proportional 8x16 font, everything else from the generated
// 16x16 CJK table; unknown characters draw as '?'.
Glyph FindGlyph(uint32_t code_point) {
    if (code_point > 0x7F) {
        size_t low = 0;
        size_t high = sizeof(kZectrixCjkFont16) / sizeof(kZectrixCjkFont16[0]);
        while (low < high) {
            const size_t mid = (low + high) / 2;
            if (kZectrixCjkFont16[mid].code_point < code_point) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        if (low < sizeof(kZectrixCjkFont16) / sizeof(kZectrixCjkFont16[0]) &&
            kZectrixCjkFont16[low].code_point == code_point) {
            return {kZectrixCjkFont16[low].rows, 16};
        }
        code_point = '?';
    }
    if (code_point < 0x20 || code_point > 0x7e) {
        code_point = '?';
    }
    const size_t index = code_point - 0x20;
    return {kZectrixAsciiFont8x16[index], kZectrixAsciiFontWidths[index]};
}

}  // namespace

void ZectrixCanvas::Text(int x, int y, const char* text, int scale,
                         bool inverted) {
    if (text == nullptr || scale <= 0) {
        return;
    }
    int cursor_x = x;
    while (*text != '\0') {
        uint32_t code_point;
        text = NextCodePoint(text, &code_point);
        const Glyph glyph = FindGlyph(code_point);
        if (inverted) {
            FillRect(cursor_x, y, glyph.width * scale, 16 * scale, true);
        }
        for (int row = 0; row < 16; ++row) {
            for (int column = 0; column < glyph.width; ++column) {
                const bool set =
                    (glyph.rows[row] & (1U << (15 - column))) != 0;
                if (!set && !inverted) {
                    continue;
                }
                FillRect(cursor_x + column * scale, y + row * scale,
                         scale, scale, inverted ? !set : true);
            }
        }
        cursor_x += glyph.width * scale;
    }
}

void ZectrixCanvas::TextCentered(int y, const char* text, int scale,
                                 bool inverted) {
    Text((kWidth - TextWidth(text, scale)) / 2, y, text, scale, inverted);
}

int ZectrixCanvas::TextWidth(const char* text, int scale) const {
    if (text == nullptr || scale <= 0) {
        return 0;
    }
    int width = 0;
    while (*text != '\0') {
        uint32_t code_point;
        text = NextCodePoint(text, &code_point);
        width += FindGlyph(code_point).width * scale;
    }
    return width;
}
