#pragma once

// A player's colours on the player model, as the game puts them there: the
// "top" and "bottom" colours of a player's settings (0..255, a hue each)
// laid over two runs of the colours of the model's skin. A texture named
// "DM_Base" has them at 160..191 (the top) and 192..223 (the bottom); one
// named "RemapN_aaa_bbb_ccc" says where they are, aaa..bbb and bbb+1..ccc.
// Each colour of a run keeps how bright and how strong it is, and takes the
// new hue.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace hldata
{
    /// Where a skin texture's two runs are; `top` false for one that has none.
    struct RemapRuns
    {
        bool any = false;
        int topFirst = 0, topLast = 0;       ///< the top colour's
        int bottomFirst = 0, bottomLast = 0; ///< the bottom colour's
    };

    inline RemapRuns remapRunsOf(const char *textureName)
    {
        RemapRuns runs;
        auto starts = [&](const char *prefix)
        {
            for (std::size_t i = 0; prefix[i] != '\0'; ++i)
            {
                const char a = textureName[i], b = prefix[i];
                if (a == '\0' || (a | 0x20) != (b | 0x20))
                    return false;
            }
            return true;
        };
        if (starts("DM_Base"))
        {
            runs.any = true;
            runs.topFirst = 160;
            runs.topLast = 191;
            runs.bottomFirst = 192;
            runs.bottomLast = 223;
        }
        else if (starts("remap") && std::strlen(textureName) >= 18)
        {
            // "RemapN_aaa_bbb_ccc": three numbers of three figures.
            auto number = [&](int at)
            {
                char text[4] = {textureName[at], textureName[at + 1], textureName[at + 2], '\0'};
                const int n = std::atoi(text);
                return n < 0 ? 0 : (n > 255 ? 255 : n);
            };
            runs.any = true;
            runs.topFirst = number(7);
            runs.topLast = number(11);
            runs.bottomFirst = runs.topLast + 1;
            runs.bottomLast = number(15);
        }
        return runs;
    }

    /// The colours `first`..`last` of `palette` (RGB, three bytes each) given
    /// the hue `colour` (0..255 for the whole circle). In HSV terms: V and S
    /// stay, so the largest and smallest channels keep their values, and H
    /// says which channel has which. The circle is six sectors of 60 degrees:
    /// in each, one channel is at the top, one at the bottom, and the third
    /// goes in a straight line between them.
    inline void hueReplace(std::uint8_t (*palette)[3], int colour, int first, int last)
    {
        const int degrees = colour * 360 / 255;
        const int sector = degrees / 60 % 6;
        const int along = degrees % 60;
        for (int i = first; i <= last && i < 256; ++i)
        {
            std::uint8_t *rgb = palette[i];
            const int high = std::max({rgb[0], rgb[1], rgb[2]});
            const int low = std::min({rgb[0], rgb[1], rgb[2]});
            const int span = high - low;
            const auto up = static_cast<std::uint8_t>(low + span * along / 60);
            const auto down = static_cast<std::uint8_t>(high - span * along / 60);
            const auto top = static_cast<std::uint8_t>(high);
            const auto bottom = static_cast<std::uint8_t>(low);
            const std::uint8_t sectors[6][3] = {
                {top, up, bottom},   // red to yellow
                {down, top, bottom}, // yellow to green
                {bottom, top, up},   // green to cyan
                {bottom, down, top}, // cyan to blue
                {up, bottom, top},   // blue to magenta
                {top, bottom, down}, // magenta to red
            };
            std::memcpy(rgb, sectors[sector], 3);
        }
    }

    /// A player's skin palette: the model's own 256 colours (`base`, RGB) with
    /// the two runs in the player's colours, as the console's 15-bit colours.
    inline void playerPalette(const std::uint8_t (*base)[3], const RemapRuns &runs, int top, int bottom,
                              std::uint16_t *out)
    {
        std::uint8_t palette[256][3];
        std::memcpy(palette, base, sizeof(palette));
        if (runs.any)
        {
            hueReplace(palette, top, runs.topFirst, runs.topLast);
            hueReplace(palette, bottom, runs.bottomFirst, runs.bottomLast);
        }
        for (int i = 0; i < 256; ++i)
            out[i] = static_cast<std::uint16_t>((palette[i][0] >> 3) | ((palette[i][1] >> 3) << 5) |
                                                ((palette[i][2] >> 3) << 10) | 0x8000);
    }
} // namespace hldata
