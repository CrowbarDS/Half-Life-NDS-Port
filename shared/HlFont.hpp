#pragma once

// Drawing on a screen that is a grid of 4-bit tiles (the touch screen):
// rectangles, frames, and text in a cooked font (HlUi.hpp). The screen is
// whatever memory the caller points this at -- video memory on the console,
// a plain array in the host tests.
//
// A DS tile is eight rows of 32 bits, four bits a pixel, the leftmost in the
// lowest bits; the tiles of a canvas are in reading order, one for each cell.
//
// std-only and inline.

#include <cstring>

#include "HlUi.hpp"

namespace hldata
{
    // The sixteen colours a canvas has.
    inline constexpr int kUiBack = 0;      ///< the screen itself
    inline constexpr int kUiPanel = 1;     ///< a button
    inline constexpr int kUiPanelDown = 2; ///< ... being pressed
    inline constexpr int kUiEdge = 3;      ///< round a button
    inline constexpr int kUiShadow = 4;    ///< the dark edge of an outlined letter
    inline constexpr int kUiWhite = 5;     ///< three of each: faint, half, full
    inline constexpr int kUiOrange = 8;
    inline constexpr int kUiGrey = 11;
    inline constexpr int kUiRed = 14;
    inline constexpr int kUiTrack = 15;    ///< what a slider slides in

    /// Each as the DS has colours: five bits of red, green and blue.
    inline constexpr std::uint16_t uiColour(int index)
    {
        constexpr std::uint16_t kColours[16] = {
            // (r | g << 5 | b << 10)
            2 | 2 << 5 | 3 << 10,    5 | 5 << 5 | 7 << 10,    12 | 8 << 5 | 2 << 10,   14 | 13 << 5 | 12 << 10,
            0,                       10 | 10 << 5 | 10 << 10, 20 | 20 << 5 | 20 << 10, 31 | 31 << 5 | 31 << 10,
            10 | 5 << 5 | 0 << 10,   20 | 11 << 5 | 0 << 10,  31 | 20 << 5 | 2 << 10,  5 | 5 << 5 | 6 << 10,
            9 | 9 << 5 | 10 << 10,   15 | 15 << 5 | 16 << 10, 31 | 4 << 5 | 3 << 10,   3 | 3 << 5 | 4 << 10};
        return kColours[index & 15];
    }

    /// What each of a glyph's three levels is drawn in.
    struct Ink
    {
        std::uint8_t level[4];
    };
    /// `ramp`: kUiWhite, kUiOrange or kUiGrey.
    inline Ink inkFor(const FontView &font, int ramp)
    {
        const auto full = static_cast<std::uint8_t>(ramp + 2);
        if (font.ok() && font.header->outlined)
            return Ink{{0, static_cast<std::uint8_t>(kUiShadow), full, full}};
        return Ink{{0, static_cast<std::uint8_t>(ramp), static_cast<std::uint8_t>(ramp + 1), full}};
    }
    inline Ink inkPlain(int colour)
    {
        const auto c = static_cast<std::uint8_t>(colour);
        return Ink{{0, c, c, c}};
    }

    struct Canvas
    {
        std::uint32_t *tiles = nullptr; ///< (width / 8) * (height / 8) tiles of eight words
        int width = 0, height = 0;      ///< in pixels: multiples of eight

        void pixel(int x, int y, int colour)
        {
            if (x < 0 || y < 0 || x >= width || y >= height)
                return;
            std::uint32_t &row = tiles[((y >> 3) * (width >> 3) + (x >> 3)) * 8 + (y & 7)];
            const int shift = (x & 7) * 4;
            row = (row & ~(0xFu << shift)) | (static_cast<std::uint32_t>(colour & 15) << shift);
        }
        int at(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= width || y >= height)
                return 0;
            return static_cast<int>((tiles[((y >> 3) * (width >> 3) + (x >> 3)) * 8 + (y & 7)] >> ((x & 7) * 4)) & 15);
        }
        void fill(int x, int y, int w, int h, int colour)
        {
            int x1 = x + w, y1 = y + h;
            x = x < 0 ? 0 : x;
            y = y < 0 ? 0 : y;
            x1 = x1 > width ? width : x1;
            y1 = y1 > height ? height : y1;
            if (x >= x1 || y >= y1)
                return;
            const std::uint32_t whole = 0x11111111u * static_cast<std::uint32_t>(colour & 15);
            for (int ty = y; ty < y1; ++ty)
            {
                for (int tx = x; tx < x1;)
                {
                    // A tile's row at a time: all of it, or the part inside.
                    const int from = tx & 7;
                    const int to = (tx | 7) + 1 > x1 ? x1 - (tx & ~7) : 8;
                    std::uint32_t &row = tiles[((ty >> 3) * (width >> 3) + (tx >> 3)) * 8 + (ty & 7)];
                    if (from == 0 && to == 8)
                    {
                        row = whole;
                    }
                    else
                    {
                        const std::uint32_t high = to == 8 ? 0xFFFFFFFFu : ((1u << (to * 4)) - 1u);
                        const std::uint32_t mask = high & ~((1u << (from * 4)) - 1u);
                        row = (row & ~mask) | (whole & mask);
                    }
                    tx = (tx & ~7) + to;
                }
            }
        }
        void clear(int colour = kUiBack) { fill(0, 0, width, height, colour); }
        /// The edge of a rectangle, a pixel thick.
        void frame(int x, int y, int w, int h, int colour)
        {
            fill(x, y, w, 1, colour);
            fill(x, y + h - 1, w, 1, colour);
            fill(x, y, 1, h, colour);
            fill(x + w - 1, y, 1, h, colour);
        }
        /// A straight line from one point to another, both drawn.
        void line(int x0, int y0, int x1, int y1, int colour)
        {
            const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
            const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
            for (int err = dx - dy;;)
            {
                pixel(x0, y0, colour);
                if (x0 == x1 && y0 == y1)
                    return;
                const int twice = err * 2;
                if (twice > -dy)
                {
                    err -= dy;
                    x0 += sx;
                }
                if (twice < dx)
                {
                    err += dx;
                    y0 += sy;
                }
            }
        }
        /// A plate: a rectangle with its four corners cut off at a slant,
        /// `cut` pixels each way; filled with `inside`, edged with `edge`,
        /// and what the corners lose made `outside`.
        void plate(int x, int y, int w, int h, int cut, int inside, int edge, int outside = kUiBack)
        {
            fill(x, y, w, h, inside);
            for (int r = 0; r < cut; ++r)
            {
                const int n = cut - r;
                for (int row : {y + r, y + h - 1 - r})
                {
                    fill(x, row, n, 1, outside);
                    fill(x + w - n, row, n, 1, outside);
                    pixel(x + n, row, edge);
                    pixel(x + w - 1 - n, row, edge);
                }
            }
            fill(x + cut, y, w - 2 * cut, 1, edge);
            fill(x + cut, y + h - 1, w - 2 * cut, 1, edge);
            fill(x, y + cut, 1, h - 2 * cut, edge);
            fill(x + w - 1, y + cut, 1, h - 2 * cut, edge);
        }
        /// A button: filled, edged, its corners taken off.
        void button(int x, int y, int w, int h, bool down)
        {
            fill(x, y, w, h, down ? kUiPanelDown : kUiPanel);
            frame(x, y, w, h, kUiEdge);
            for (int cx : {x, x + w - 1})
            {
                for (int cy : {y, y + h - 1})
                    pixel(cx, cy, kUiBack);
            }
        }
    };

    /// How wide `text` is in `font` (its first `count` characters; all of it
    /// if negative).
    inline int textWidth(const FontView &font, const char *text, int count = -1)
    {
        int width = 0;
        for (int i = 0; text[i] != '\0' && (count < 0 || i < count); ++i)
            width += font.width(static_cast<unsigned char>(text[i]));
        return width;
    }

    /// How many characters of `text` go on a line `most` pixels wide: up to
    /// the last space that fits, if the whole does not (or as many letters
    /// as fit, of a word too long for the line). At least one, if there is
    /// one.
    inline int textFit(const FontView &font, const char *text, int most)
    {
        int width = 0, space = -1, i = 0;
        for (; text[i] != '\0' && text[i] != '\n'; ++i)
        {
            // (A space that would not fit ends the line where it is.)
            if (text[i] == ' ' && i > 0 && width <= most && width + font.width(' ') > most)
                return i;
            width += font.width(static_cast<unsigned char>(text[i]));
            if (width > most)
                return space > 0 ? space : (i > 0 ? i : 1);
            if (text[i] == ' ')
                space = i;
        }
        return i;
    }

    /// Draw `text` (its first `count` characters; all if negative) with its
    /// top left at (x, y). Returns where the pen ends up.
    inline int drawText(Canvas &canvas, const FontView &font, int x, int y, const char *text, const Ink &ink,
                        int count = -1)
    {
        if (!font.ok())
            return x;
        for (int i = 0; text[i] != '\0' && (count < 0 || i < count); ++i)
        {
            const auto c = static_cast<unsigned char>(text[i]);
            const int wide = font.width(c);
            if (const std::uint32_t *glyph = font.glyph(c))
            {
                for (int row = 0; row < font.height(); ++row)
                {
                    std::uint32_t bits = glyph[row];
                    for (int col = 0; bits != 0 && col < kFontMaxWidth; ++col, bits >>= 2)
                    {
                        if (bits & 3)
                            canvas.pixel(x + col, y + row, ink.level[bits & 3]);
                    }
                }
            }
            x += wide;
        }
        return x;
    }

    /// ... with its middle at `cx`.
    inline void drawTextCentred(Canvas &canvas, const FontView &font, int cx, int y, const char *text, const Ink &ink)
    {
        drawText(canvas, font, cx - textWidth(font, text) / 2, y, text, ink);
    }

    /// A font made of 8x8 letters a bit a pixel (the console's own, which is
    /// in the ROM), in the form above: for a card that has no fonts on it.
    /// `out` needs kPlainFontWords words. `msbLeft`: the leftmost pixel is
    /// each byte's highest bit.
    inline constexpr int kPlainFontWords = (12 + 96 + 96 * 8 * 4) / 4;
    inline void plainFont(const std::uint8_t *tiles, bool msbLeft, std::uint32_t *out)
    {
        auto *bytes = reinterpret_cast<std::uint8_t *>(out);
        FontHeader header{};
        std::memcpy(header.magic, kFontMagic, 4);
        header.version = kFontVersion;
        header.height = 8;
        header.first = 32;
        header.count = 96;
        header.outlined = 0;
        std::memcpy(bytes, &header, sizeof(header));
        for (int i = 0; i < 96; ++i)
            bytes[sizeof(header) + i] = 8;
        std::uint32_t *rows = out + (sizeof(header) + 96) / 4;
        for (int i = 0; i < 96 * 8; ++i)
        {
            std::uint32_t row = 0;
            for (int x = 0; x < 8; ++x)
            {
                const bool set = (tiles[i] >> (msbLeft ? 7 - x : x)) & 1;
                row |= (set ? 3u : 0u) << (x * 2);
            }
            rows[i] = row;
        }
    }
} // namespace hldata
