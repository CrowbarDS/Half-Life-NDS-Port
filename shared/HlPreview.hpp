#pragma once

// A multiplayer map's picture, for its card in the menus:
// <card>/_nds/hl_nds/data/saves/previews/<map>.hpv
//
// Taken by the console that hosts a match on the map the first time it does
// (NetGame.cpp): the map seen from where the game shows it between matches
// (its info_intermission; else where a player first comes in), the screen's
// 3D shrunk by half. A header, then kPreviewWidth by kPreviewHeight pixels of
// the console's 15-bit colour, row by row.
//
// std-only: the host tests make one and read it back.

#include <cstdint>
#include <cstring>

namespace hldata
{
    inline constexpr char kPreviewMagic[4] = {'H', 'L', 'P', 'V'};
    inline constexpr std::uint16_t kPreviewVersion = 1;
    inline constexpr int kPreviewWidth = 128;
    inline constexpr int kPreviewHeight = 96;
    inline constexpr const char *kPreviewFolder = "previews/";

    struct PreviewHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t width;
        std::uint16_t height;
        std::uint16_t mapVersion; ///< previewMapVersion of the map it was taken of (0: not said)
    };
    static_assert(sizeof(PreviewHeader) == 12);
    inline constexpr std::size_t kPreviewBytes =
        sizeof(PreviewHeader) + static_cast<std::size_t>(kPreviewWidth) * kPreviewHeight * 2;

    inline void previewHeader(PreviewHeader &header)
    {
        std::memcpy(header.magic, kPreviewMagic, 4);
        header.version = kPreviewVersion;
        header.width = kPreviewWidth;
        header.height = kPreviewHeight;
        header.mapVersion = 0;
    }

    /// What says which cooking of a map a picture is of: from its level's
    /// checksum, and never 0, which is what a picture from before this says.
    inline std::uint16_t previewMapVersion(std::uint32_t levelCrc)
    {
        const std::uint16_t version = static_cast<std::uint16_t>(levelCrc ^ (levelCrc >> 16));
        return version != 0 ? version : 1;
    }
    /// Whether a picture's header is of a map with this checksum.
    inline bool previewOfMap(const PreviewHeader &header, std::uint32_t levelCrc)
    {
        return header.mapVersion == previewMapVersion(levelCrc);
    }

    /// A small one (half each way), for a console short of memory: sent it,
    /// in place of the whole.
    inline constexpr int kPreviewSmallWidth = kPreviewWidth / 2;
    inline constexpr int kPreviewSmallHeight = kPreviewHeight / 2;
    inline constexpr std::size_t kPreviewSmallBytes =
        sizeof(PreviewHeader) + static_cast<std::size_t>(kPreviewSmallWidth) * kPreviewSmallHeight * 2;
    /// Whether `bytes` is a picture of either size (and which is its width).
    inline int previewWidthOf(const std::uint8_t *bytes, std::size_t size)
    {
        if (bytes == nullptr || size < sizeof(PreviewHeader))
            return 0;
        PreviewHeader header;
        std::memcpy(&header, bytes, sizeof(header));
        if (std::memcmp(header.magic, kPreviewMagic, 4) != 0 || header.version != kPreviewVersion)
            return 0;
        if (header.width == kPreviewWidth && header.height == kPreviewHeight && size >= kPreviewBytes)
            return kPreviewWidth;
        if (header.width == kPreviewSmallWidth && header.height == kPreviewSmallHeight && size >= kPreviewSmallBytes)
            return kPreviewSmallWidth;
        return 0;
    }

    /// Fifteen colours for a picture, for the touch screen (a cell has sixteen,
    /// and its colour 0 is see-through: palette[0] is not one of them). A median
    /// cut over the picture's colours to three bits each, then each colour
    /// moved to the middle of the pixels nearest it.
    inline constexpr int kPreviewColours = 15;
    /// Which of them is nearest: 1..kPreviewColours.
    inline int previewNearest(const std::uint16_t *palette, std::uint16_t colour)
    {
        int best = 1, bestFar = 1 << 30;
        for (int p = 1; p <= kPreviewColours; ++p)
        {
            const int r = (colour & 31) - (palette[p] & 31), g = (colour >> 5 & 31) - (palette[p] >> 5 & 31),
                      b = (colour >> 10 & 31) - (palette[p] >> 10 & 31);
            const int far = r * r + g * g + b * b;
            if (far < bestFar)
            {
                bestFar = far;
                best = p;
            }
        }
        return best;
    }
    inline void previewPalette(const std::uint16_t *pixels, int count, std::uint16_t *palette)
    {
        std::uint16_t uses[512] = {};
        for (int n = 0; n < count; ++n)
        {
            const std::uint32_t c = pixels[n];
            std::uint16_t &use = uses[(c >> 2 & 7) | (c >> 7 & 7) << 3 | (c >> 12 & 7) << 6];
            use = static_cast<std::uint16_t>(use < 65535 ? use + 1 : use);
        }
        struct Box
        {
            std::uint8_t low[3], high[3];
            std::int32_t pixels;
        };
        const auto weigh = [&uses](Box &box)
        {
            // (Shrunk to what it holds.)
            std::uint8_t low[3] = {7, 7, 7}, high[3] = {0, 0, 0};
            box.pixels = 0;
            for (int b = box.low[2]; b <= box.high[2]; ++b)
                for (int g = box.low[1]; g <= box.high[1]; ++g)
                    for (int r = box.low[0]; r <= box.high[0]; ++r)
                    {
                        const int use = uses[r | g << 3 | b << 6];
                        if (use == 0)
                            continue;
                        box.pixels += use;
                        const int at[3] = {r, g, b};
                        for (int i = 0; i < 3; ++i)
                        {
                            low[i] = at[i] < low[i] ? static_cast<std::uint8_t>(at[i]) : low[i];
                            high[i] = at[i] > high[i] ? static_cast<std::uint8_t>(at[i]) : high[i];
                        }
                    }
            for (int i = 0; i < 3 && box.pixels > 0; ++i)
            {
                box.low[i] = low[i];
                box.high[i] = high[i];
            }
        };
        Box boxes[kPreviewColours];
        boxes[0] = Box{{0, 0, 0}, {7, 7, 7}, 0};
        weigh(boxes[0]);
        int made = 1;
        while (made < kPreviewColours)
        {
            // The one with the most in it that can still be cut, along its longest side.
            int which = -1, axis = 0;
            for (int n = 0; n < made; ++n)
            {
                int side = 0, along = 0;
                for (int i = 0; i < 3; ++i)
                {
                    if (boxes[n].high[i] - boxes[n].low[i] > side)
                    {
                        side = boxes[n].high[i] - boxes[n].low[i];
                        along = i;
                    }
                }
                if (side > 0 && (which < 0 || boxes[n].pixels > boxes[which].pixels))
                {
                    which = n;
                    axis = along;
                }
            }
            if (which < 0)
                break;
            // Cut where half its pixels are on either side.
            Box &box = boxes[which];
            int seen = 0, cut = box.low[axis];
            for (int v = box.low[axis]; v < box.high[axis]; ++v)
            {
                Box slice = box;
                slice.low[axis] = slice.high[axis] = static_cast<std::uint8_t>(v);
                std::int32_t inSlice = 0;
                for (int b = slice.low[2]; b <= slice.high[2]; ++b)
                    for (int g = slice.low[1]; g <= slice.high[1]; ++g)
                        for (int r = slice.low[0]; r <= slice.high[0]; ++r)
                            inSlice += uses[r | g << 3 | b << 6];
                seen += inSlice;
                cut = v;
                if (seen * 2 >= box.pixels)
                    break;
            }
            Box other = box;
            other.low[axis] = static_cast<std::uint8_t>(cut + 1);
            box.high[axis] = static_cast<std::uint8_t>(cut);
            weigh(box);
            weigh(other);
            boxes[made++] = other;
        }
        palette[0] = 0;
        for (int p = 1; p <= kPreviewColours; ++p)
        {
            const Box &box = boxes[p - 1 < made ? p - 1 : made - 1];
            palette[p] = static_cast<std::uint16_t>(0x8000 | ((box.low[0] + box.high[0]) * 2 + 2) |
                                                    ((box.low[1] + box.high[1]) * 2 + 2) << 5 |
                                                    ((box.low[2] + box.high[2]) * 2 + 2) << 10);
        }
        // Each moved to the middle of what is nearest it.
        std::int32_t sums[kPreviewColours + 1][4] = {};
        for (int n = 0; n < count; ++n)
        {
            const std::uint32_t c = pixels[n];
            std::int32_t *sum = sums[previewNearest(palette, pixels[n])];
            sum[0] += c & 31;
            sum[1] += c >> 5 & 31;
            sum[2] += c >> 10 & 31;
            ++sum[3];
        }
        for (int p = 1; p <= kPreviewColours; ++p)
        {
            const std::int32_t *sum = sums[p];
            if (sum[3] > 0)
                palette[p] = static_cast<std::uint16_t>(0x8000 | (sum[0] / sum[3]) | (sum[1] / sum[3]) << 5 |
                                                        (sum[2] / sum[3]) << 10);
        }
    }

    /// Whether `bytes` is a picture of this version and size.
    inline bool previewValid(const std::uint8_t *bytes, std::size_t size)
    {
        if (bytes == nullptr || size != kPreviewBytes)
            return false;
        PreviewHeader header;
        std::memcpy(&header, bytes, sizeof(header));
        return std::memcmp(header.magic, kPreviewMagic, 4) == 0 && header.version == kPreviewVersion &&
               header.width == kPreviewWidth && header.height == kPreviewHeight;
    }

    /// The screen (256 by 192, 15-bit colour, as the display capture writes
    /// it) shrunk by half: each pixel the average of the four it stands for.
    /// `out`: kPreviewWidth * kPreviewHeight.
    inline void previewShrink(const std::uint16_t *screen, std::uint16_t *out)
    {
        for (int y = 0; y < kPreviewHeight; ++y)
        {
            for (int x = 0; x < kPreviewWidth; ++x)
            {
                int sum[3] = {0, 0, 0};
                for (int dy = 0; dy < 2; ++dy)
                {
                    for (int dx = 0; dx < 2; ++dx)
                    {
                        const std::uint16_t c = screen[(y * 2 + dy) * 256 + x * 2 + dx];
                        sum[0] += c & 31;
                        sum[1] += (c >> 5) & 31;
                        sum[2] += (c >> 10) & 31;
                    }
                }
                out[y * kPreviewWidth + x] =
                    static_cast<std::uint16_t>(((sum[0] + 2) / 4) | ((sum[1] + 2) / 4) << 5 | ((sum[2] + 2) / 4) << 10 | 0x8000);
            }
        }
    }
} // namespace hldata
