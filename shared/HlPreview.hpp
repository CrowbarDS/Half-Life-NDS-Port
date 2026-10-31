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
        std::uint16_t pad;
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
        header.pad = 0;
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

    /// Sixteen colours for a picture, for the touch screen (a cell has no more):
    /// the commonest, each to three bits of red, green and blue.
    inline void previewPalette(const std::uint16_t *pixels, int count, std::uint16_t *palette)
    {
        std::uint16_t uses[512] = {};
        for (int n = 0; n < count; ++n)
        {
            const std::uint32_t c = pixels[n];
            std::uint16_t &use = uses[(c >> 2 & 7) | (c >> 7 & 7) << 3 | (c >> 12 & 7) << 6];
            use = static_cast<std::uint16_t>(use < 65535 ? use + 1 : use);
        }
        for (int p = 0; p < 16; ++p)
        {
            int best = 0;
            for (int b = 1; b < 512; ++b)
                best = uses[b] > uses[best] ? b : best;
            uses[best] = 0;
            palette[p] = static_cast<std::uint16_t>(0x8000 | ((best & 7) << 2 | 2) | ((best >> 3 & 7) << 2 | 2) << 5 |
                                                    ((best >> 6 & 7) << 2 | 2) << 10);
        }
    }
    /// Which of them is nearest.
    inline int previewNearest(const std::uint16_t *palette, std::uint16_t colour)
    {
        int best = 0, bestFar = 1 << 30;
        for (int p = 0; p < 16; ++p)
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
