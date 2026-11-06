#pragma once

// The box round a surface's vertices, read out of its packed geometry
// commands (the level's GxStream) -- for a box test against the view
// (WorldDraw's HLDS_BOX_TEST), with nothing added to the cooked level.
//
// The box is in the vertices' own numbers: 16-bit quarter-units, which the
// hardware reads as 4.12 under the world's matrix. A box test given these
// numbers under the same matrix tests what the polygons themselves would
// cover. Its corner is a vertex's coordinates and so is its far corner, so
// both fit 16 bits by construction; only the size (far less near) can be
// more than 16 bits will hold, and gxBoxFits says so.
//
// The forms are the converter's (GxStream.cpp, decodeGxStream): VTX_16 and
// VTX_10 stand alone; VTX_XY, XZ and YZ keep the third coordinate of the
// vertex before; VTX_DIFF adds 4.12 steps (10 bits signed) to it.

#include <cstdint>

namespace hldata
{
    /// Box of every vertex in `words` (`count` words of packed commands).
    /// False, with the box untouched, if there is no vertex in them.
    inline bool gxStreamBounds(const std::uint32_t *words, int count, std::int16_t mins[3], std::int16_t maxs[3])
    {
        auto ten = [](std::uint32_t v) -> int { return static_cast<int>((v & 0x3FF) ^ 0x200) - 0x200; };
        int x = 0, y = 0, z = 0;
        int lo[3] = {32767, 32767, 32767}, hi[3] = {-32768, -32768, -32768};
        bool any = false;
        int at = 0;
        while (at < count)
        {
            const std::uint32_t header = words[at++];
            for (int k = 0; k < 4; ++k)
            {
                const std::uint32_t code = (header >> (8 * k)) & 0xFF;
                if (code == 0)
                    break;
                const int params = code == 0x23 ? 2 : 1;
                if (at + params > count)
                    return false;
                const std::uint32_t p0 = words[at];
                const std::uint32_t p1 = params == 2 ? words[at + 1] : 0;
                at += params;
                const int a = static_cast<std::int16_t>(p0 & 0xFFFF);
                const int b = static_cast<std::int16_t>(p0 >> 16);
                switch (code)
                {
                case 0x23: // VTX_16
                    x = a, y = b, z = static_cast<std::int16_t>(p1 & 0xFFFF);
                    break;
                case 0x24: // VTX_10: the top ten bits of each
                    x = ten(p0) * 64, y = ten(p0 >> 10) * 64, z = ten(p0 >> 20) * 64;
                    break;
                case 0x25: x = a, y = b; break; // VTX_XY
                case 0x26: x = a, z = b; break; // VTX_XZ
                case 0x27: y = a, z = b; break; // VTX_YZ
                case 0x28:                      // VTX_DIFF
                    x += ten(p0), y += ten(p0 >> 10), z += ten(p0 >> 20);
                    break;
                default: // a colour, a texture coordinate, a begin
                    continue;
                }
                const int v[3] = {x, y, z};
                for (int i = 0; i < 3; ++i)
                {
                    lo[i] = v[i] < lo[i] ? v[i] : lo[i];
                    hi[i] = v[i] > hi[i] ? v[i] : hi[i];
                }
                any = true;
            }
        }
        if (!any)
            return false;
        for (int i = 0; i < 3; ++i)
        {
            mins[i] = static_cast<std::int16_t>(lo[i]);
            maxs[i] = static_cast<std::int16_t>(hi[i]);
        }
        return true;
    }

    /// Whether a box's size fits the box test's 16-bit sizes (its corners
    /// always do: they are a vertex's).
    inline constexpr bool gxBoxFits(const std::int16_t mins[3], const std::int16_t maxs[3])
    {
        return maxs[0] - mins[0] <= 32767 && maxs[1] - mins[1] <= 32767 && maxs[2] - mins[2] <= 32767;
    }
} // namespace hldata
