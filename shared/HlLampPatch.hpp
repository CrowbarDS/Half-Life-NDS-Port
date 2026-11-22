#pragma once

// The lamp's light on a wall, as the game puts it there: ADDED to the wall's
// own light, before that is made into what is seen (R_AddDynamicLights). The
// hardware cannot add one picture to another, but it can draw the lit part
// of a wall again, solid, in the wall's own texture and with brighter corners
// -- a patch. What is under it is not seen, so nothing is blended.
//
// The game's light, at a point of a surface: its strength there is
//
//     (rad - dist) * strength, rad = 80 - (how far the light is off the plane)
//
// where `dist` is not the distance round but max(a, b) + min(a, b) / 2 of the
// point's offsets a and b along the texture's two directions -- an octagon
// (rad, 0), (2/3 rad, 2/3 rad), (0, rad), ... That is straight between those
// corners: a fan of triangles from the middle to them is the game's light
// exactly, as the hardware runs one colour into another across a triangle.
// A ring at half of it more, as what is seen is bent from what is added.
//
// Each of the surface's own triangles is cut to the octagon's pieces; each
// corner made has its colour and its place in the texture worked out from
// the triangle's own corners (as the hardware would have drawn them there),
// and its light: its colour taken back to the light it was made from
// (kLampLightOf), the lamp's added, and made into a colour again
// (kLampColourOf) -- the converter's curve, LightBake's: 255 * scale *
// (light / 255)^power, scale 338/256, power 0.8, the 8 bits shifted to 5.
//
// std-only and inline: the host tests run it.

#include <cstdint>

#include "HlFixed.hpp"

namespace hldata
{
    /// The light (sixteenths of the lightmap's 0..255) each of the 32 levels
    /// of a vertex's colour was made from: the middle of the 8 levels of the
    /// converter's that it stands for, back through its curve.
    inline constexpr std::uint16_t kLampLightOf[32] = {
        14,   60,   116,  178,  245,  316,  390,  468,  547,  630,  714,  801,  889,  979,  1071, 1165,
        1260, 1356, 1454, 1553, 1654, 1756, 1859, 1963, 2068, 2174, 2282, 2390, 2500, 2610, 2721, 2833};
    /// The colour (0..31) a light of 0..191 is made into; past that, full.
    inline constexpr int kLampColours = 192;
    inline constexpr std::uint8_t kLampColourOf[kLampColours] = {
        0,  0,  0,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  4,  5,  5,  5,  5,  5,  6,
        6,  6,  6,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10,
        11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 15, 15,
        15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 19, 19,
        19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22,
        23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26, 26,
        26, 26, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 30,
        30, 30, 30, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31};

    inline constexpr int kLampPatchRadius = 80; ///< the game's flashlight (CL_UpdateFlashlight)

    /// One channel's colour (0..31, in 256ths: a corner's colour run from its
    /// triangle's) with `add` sixteenths of light added: the colour seen.
    constexpr int lampLift(std::int32_t colour256, std::int32_t add)
    {
        colour256 = colour256 < 0 ? 0 : (colour256 > 31 * 256 ? 31 * 256 : colour256);
        const int low = static_cast<int>(colour256 >> 8);
        const int high = low < 31 ? low + 1 : 31;
        const std::int32_t frac = colour256 & 255;
        const std::int32_t light =
            (kLampLightOf[low] * (256 - frac) + kLampLightOf[high] * frac) / 256 + (add > 0 ? add : 0);
        const std::int32_t index = (light + 8) >> 4;
        if (add <= 0)
            return (colour256 + 128) >> 8; // (untouched: the colour it was)
        return index >= kLampColours ? 31 : kLampColourOf[index];
    }

    /// A corner of a surface's triangle, or of a patch: where (Q12 units),
    /// its colour (RGB15), and its texture coordinate as the level sends it
    /// (s in the low 16 bits, t in the high, sixteenths of a texel, signed).
    struct LampCorner
    {
        fx pos[3];
        std::uint16_t colour;
        std::uint32_t texCoord;
    };

    /// The light: where it is (Q12 units), and how strong, of 256 (the
    /// game's colour 255 * falloff: full to 500 units off, then the square
    /// of 500 over the distance).
    struct LampLight
    {
        fx at[3];
        int strength;
    };

    namespace detail
    {
        /// A corner of a piece, in the octagon's own plane: a and b along
        /// the texture's two directions from where the light falls on the
        /// plane (Q12 units).
        struct PatchPoint
        {
            std::int32_t a, b;
        };

        /// Which side of the line p->q `r` is, times twice the area
        /// (Q12 units squared, >> 12: Q12 units' worth).
        inline std::int64_t edgeSide(const PatchPoint &p, const PatchPoint &q, const PatchPoint &r)
        {
            return (static_cast<std::int64_t>(q.a - p.a) * (r.b - p.b) - static_cast<std::int64_t>(q.b - p.b) * (r.a - p.a)) >> 12;
        }

        /// Cut the convex polygon `in` (n corners) to the inside of the
        /// line p->q (left of it, as seen with `sign` 1; right with -1).
        inline int clipToLine(const PatchPoint *in, int n, const PatchPoint &p, const PatchPoint &q, int sign,
                              PatchPoint *out)
        {
            int made = 0;
            for (int i = 0; i < n; ++i)
            {
                const PatchPoint &c = in[i], &d = in[(i + 1) % n];
                const std::int64_t sc = edgeSide(p, q, c) * sign, sd = edgeSide(p, q, d) * sign;
                if (sc >= 0)
                    out[made++] = c;
                if ((sc >= 0) != (sd >= 0))
                {
                    // (The same crossing whichever triangle of the two that
                    // share the line asks: the sides' sizes, not their
                    // signs, go into it.)
                    const std::int64_t whole = (sc < 0 ? -sc : sc) + (sd < 0 ? -sd : sd);
                    const std::int64_t part = sc < 0 ? -sc : sc;
                    out[made].a = c.a + static_cast<std::int32_t>((static_cast<std::int64_t>(d.a - c.a) * part) / whole);
                    out[made].b = c.b + static_cast<std::int32_t>((static_cast<std::int64_t>(d.b - c.b) * part) / whole);
                    ++made;
                }
            }
            return made;
        }

        inline std::int32_t absolute(std::int32_t v) { return v < 0 ? -v : v; }
    } // namespace detail

    /// The patch of the lamp's light on one triangle of a surface (corners
    /// `tri`, facing `normal`, Q12 unit): each piece is given to `emit` as a
    /// convex polygon of 3..9 LampCorners, in order. Returns how many
    /// triangles that is (a polygon of n corners is n - 2), or 0 for none --
    /// the light too far off the plane, or not on this triangle.
    template <class Emit>
    inline int lampPatchTriangle(const LampCorner *tri, const fx *normal, const LampLight &light, Emit &&emit)
    {
        using detail::PatchPoint;
        if (light.strength <= 0)
            return 0;
        // How far the light is off the plane, and where it falls on it.
        std::int64_t off64 = 0;
        for (int k = 0; k < 3; ++k)
            off64 += static_cast<std::int64_t>(light.at[k] - tri[0].pos[k]) * normal[k];
        const fx off = static_cast<fx>(off64 >> 12);
        const fx rad = fxFromInt(kLampPatchRadius) - (off < 0 ? -off : off);
        if (rad <= fxFromInt(1))
            return 0;
        fx impact[3];
        for (int k = 0; k < 3; ++k)
            impact[k] = light.at[k] - static_cast<fx>((static_cast<std::int64_t>(normal[k]) * off) >> 12);

        // The texture's direction "s" in the plane (the gradient of s over
        // the triangle: s1 (e2 x n) + s2 (n x e1), turned by the triangle's
        // own turn), and "t" square to it in the plane.
        const std::int32_t s0 = static_cast<std::int16_t>(tri[0].texCoord & 0xFFFF);
        const std::int32_t sd1 = static_cast<std::int16_t>(tri[1].texCoord & 0xFFFF) - s0;
        const std::int32_t sd2 = static_cast<std::int16_t>(tri[2].texCoord & 0xFFFF) - s0;
        std::int64_t e1[3], e2[3];
        for (int k = 0; k < 3; ++k)
        {
            e1[k] = (tri[1].pos[k] - tri[0].pos[k]) >> 8; // (sixteenths of a unit)
            e2[k] = (tri[2].pos[k] - tri[0].pos[k]) >> 8;
        }
        const std::int64_t n[3] = {normal[0], normal[1], normal[2]};
        const std::int64_t e2xn[3] = {e2[1] * n[2] - e2[2] * n[1], e2[2] * n[0] - e2[0] * n[2], e2[0] * n[1] - e2[1] * n[0]};
        const std::int64_t nxe1[3] = {n[1] * e1[2] - n[2] * e1[1], n[2] * e1[0] - n[0] * e1[2], n[0] * e1[1] - n[1] * e1[0]};
        const std::int64_t cross[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const std::int64_t turn = cross[0] * n[0] + cross[1] * n[1] + cross[2] * n[2];
        if (turn == 0)
            return 0; // (no area)
        std::int64_t g[3];
        for (int k = 0; k < 3; ++k)
            g[k] = (sd1 * e2xn[k] + sd2 * nxe1[k]) * (turn > 0 ? 1 : -1);
        fx sAxis[3];
        {
            // (To Q12 for its length; one along an edge if the texture says
            // nothing -- its coordinates all the same.)
            std::int64_t big = 0;
            for (int k = 0; k < 3; ++k)
                big = (g[k] < 0 ? -g[k] : g[k]) > big ? (g[k] < 0 ? -g[k] : g[k]) : big;
            if (big == 0)
            {
                for (int k = 0; k < 3; ++k)
                    g[k] = e1[k];
                big = 1;
                for (int k = 0; k < 3; ++k)
                    big = (g[k] < 0 ? -g[k] : g[k]) > big ? (g[k] < 0 ? -g[k] : g[k]) : big;
            }
            // (Its largest part made a unit before its length is taken.)
            fx raw[3];
            for (int k = 0; k < 3; ++k)
                raw[k] = static_cast<fx>((g[k] << 12) / big);
            const fx length = fxLength(raw);
            if (length <= 0)
                return 0;
            for (int k = 0; k < 3; ++k)
                sAxis[k] = static_cast<fx>((static_cast<std::int64_t>(raw[k]) << 12) / length);
        }
        const fx tAxis[3] = {static_cast<fx>((normal[1] * static_cast<std::int64_t>(sAxis[2]) - normal[2] * static_cast<std::int64_t>(sAxis[1])) >> 12),
                             static_cast<fx>((normal[2] * static_cast<std::int64_t>(sAxis[0]) - normal[0] * static_cast<std::int64_t>(sAxis[2])) >> 12),
                             static_cast<fx>((normal[0] * static_cast<std::int64_t>(sAxis[1]) - normal[1] * static_cast<std::int64_t>(sAxis[0])) >> 12)};
        auto toPlane = [&](const fx *p) {
            std::int64_t a = 0, b = 0;
            for (int k = 0; k < 3; ++k)
            {
                a += static_cast<std::int64_t>(p[k] - impact[k]) * sAxis[k];
                b += static_cast<std::int64_t>(p[k] - impact[k]) * tAxis[k];
            }
            return PatchPoint{static_cast<std::int32_t>(a >> 12), static_cast<std::int32_t>(b >> 12)};
        };
        const PatchPoint corner[3] = {toPlane(tri[0].pos), toPlane(tri[1].pos), toPlane(tri[2].pos)};

        // Not on this triangle at all: the octagon's square box clear of it.
        {
            std::int32_t lo[2] = {corner[0].a, corner[0].b}, hi[2] = {corner[0].a, corner[0].b};
            for (const PatchPoint &c : corner)
            {
                lo[0] = c.a < lo[0] ? c.a : lo[0];
                hi[0] = c.a > hi[0] ? c.a : hi[0];
                lo[1] = c.b < lo[1] ? c.b : lo[1];
                hi[1] = c.b > hi[1] ? c.b : hi[1];
            }
            if (lo[0] >= rad || hi[0] <= -rad || lo[1] >= rad || hi[1] <= -rad)
                return 0;
        }
        const std::int64_t area = detail::edgeSide(corner[0], corner[1], corner[2]);
        if (area == 0)
            return 0;
        const int sign = area > 0 ? 1 : -1;

        // The octagon's corners, and the ring's at half of it.
        const std::int32_t r = rad, r23 = rad * 2 / 3, h = rad / 2, h23 = rad / 3;
        const PatchPoint outer[8] = {{r, 0}, {r23, r23}, {0, r}, {-r23, r23}, {-r, 0}, {-r23, -r23}, {0, -r}, {r23, -r23}};
        const PatchPoint inner[8] = {{h, 0}, {h23, h23}, {0, h}, {-h23, h23}, {-h, 0}, {-h23, -h23}, {0, -h}, {h23, -h23}};

        // A corner's colour, texture place and light.
        auto make = [&](const PatchPoint &p) {
            // (Its share of each of the triangle's corners.)
            const std::int64_t w0 = detail::edgeSide(corner[1], corner[2], p);
            const std::int64_t w1 = detail::edgeSide(corner[2], corner[0], p);
            const std::int64_t w2 = area - w0 - w1;
            LampCorner out{};
            for (int k = 0; k < 3; ++k)
                out.pos[k] = impact[k] + static_cast<fx>((static_cast<std::int64_t>(sAxis[k]) * p.a + static_cast<std::int64_t>(tAxis[k]) * p.b) >> 12);
            auto mix = [&](std::int32_t v0, std::int32_t v1, std::int32_t v2) {
                return static_cast<std::int32_t>((v0 * w0 + v1 * w1 + v2 * w2) / area);
            };
            // The texture: the triangle's, run to here.
            const std::int32_t s = mix(static_cast<std::int16_t>(tri[0].texCoord & 0xFFFF),
                                       static_cast<std::int16_t>(tri[1].texCoord & 0xFFFF),
                                       static_cast<std::int16_t>(tri[2].texCoord & 0xFFFF));
            const std::int32_t t = mix(static_cast<std::int16_t>(tri[0].texCoord >> 16),
                                       static_cast<std::int16_t>(tri[1].texCoord >> 16),
                                       static_cast<std::int16_t>(tri[2].texCoord >> 16));
            out.texCoord = (static_cast<std::uint32_t>(s) & 0xFFFF) | (static_cast<std::uint32_t>(t) << 16);
            // The light the game adds here: (rad - dist) * strength, in
            // sixteenths of the lightmap's, which counts 264 for each 256 of
            // a wall's own (style "m").
            const std::int32_t a = detail::absolute(p.a), b = detail::absolute(p.b);
            const std::int32_t dist = (a > b ? a : b) + (a > b ? b : a) / 2;
            const std::int32_t over = rad - dist; // (Q12 units)
            const std::int32_t add =
                over <= 0 ? 0 : static_cast<std::int32_t>((static_cast<std::int64_t>(over) * light.strength * 255 * 16) / (264LL * 256 * 4096));
            std::uint16_t colour = 0;
            for (int c = 0; c < 3; ++c)
            {
                // (Each channel run to here, in 256ths of a level.)
                const std::int64_t v = (static_cast<std::int64_t>((tri[0].colour >> (5 * c)) & 31) * w0 +
                                        static_cast<std::int64_t>((tri[1].colour >> (5 * c)) & 31) * w1 +
                                        static_cast<std::int64_t>((tri[2].colour >> (5 * c)) & 31) * w2) *
                                       256 / area;
                colour |= static_cast<std::uint16_t>(lampLift(static_cast<std::int32_t>(v), add) << (5 * c));
            }
            out.colour = colour;
            return out;
        };

        int triangles = 0;
        auto piece = [&](const PatchPoint *shape, int n) {
            PatchPoint a[12], b[12];
            for (int i = 0; i < n; ++i)
                a[i] = shape[i];
            int count = n;
            count = detail::clipToLine(a, count, corner[0], corner[1], sign, b);
            if (count < 3)
                return;
            count = detail::clipToLine(b, count, corner[1], corner[2], sign, a);
            if (count < 3)
                return;
            count = detail::clipToLine(a, count, corner[2], corner[0], sign, b);
            if (count < 3)
                return;
            LampCorner made[12];
            for (int i = 0; i < count; ++i)
                made[i] = make(b[i]);
            emit(static_cast<const LampCorner *>(made), count);
            triangles += count - 2;
        };
        const PatchPoint middle{0, 0};
        for (int k = 0; k < 8; ++k)
        {
            const PatchPoint fan[3] = {middle, inner[k], inner[(k + 1) % 8]};
            piece(fan, 3);
            const PatchPoint ring[4] = {inner[k], outer[k], outer[(k + 1) % 8], inner[(k + 1) % 8]};
            piece(ring, 4);
        }
        return triangles;
    }
} // namespace hldata
