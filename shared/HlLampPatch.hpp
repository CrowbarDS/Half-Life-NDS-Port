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

    /// How strong the game's light is (of 256) when it lands `distance`
    /// units off: full to 500, then 500 over the distance, squared
    /// (CL_UpdateFlashlight's falloff); none past its reach of 2000.
    constexpr int lampFalloff(int distance)
    {
        if (distance >= 2000)
            return 0;
        if (distance <= 500)
            return 256;
        return static_cast<int>(256LL * 500 * 500 / (static_cast<long long>(distance) * distance));
    }

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
        [[gnu::noinline]] inline int clipToLine(const PatchPoint *in, int n, const PatchPoint &p, const PatchPoint &q,
                                                int sign, PatchPoint *out)
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

        /// One triangle with the light on it: where the light falls on its
        /// plane, the texture's two directions there, its corners in them,
        /// and how big the light is on it.
        struct PatchFrame
        {
            const LampCorner *tri;
            fx impact[3], sAxis[3], tAxis[3];
            PatchPoint corner[3];
            std::int64_t area;
            int sign;
            fx rad;
            int strength;
        };

        /// Set up `f` for the triangle `tri` (facing `normal`, Q12 unit).
        /// False if the light is too far off its plane, or not on it.
        [[gnu::noinline]] inline bool patchFrame(const LampCorner *tri, const fx *normal, const LampLight &light,
                                                 PatchFrame &f)
        {
            if (light.strength <= 0)
                return false;
            f.tri = tri;
            f.strength = light.strength;
            // How far the light is off the plane, and where it falls on it.
            std::int64_t off64 = 0;
            for (int k = 0; k < 3; ++k)
                off64 += static_cast<std::int64_t>(light.at[k] - tri[0].pos[k]) * normal[k];
            const fx off = static_cast<fx>(off64 >> 12);
            f.rad = fxFromInt(kLampPatchRadius) - (off < 0 ? -off : off);
            if (f.rad <= fxFromInt(1))
                return false;
            for (int k = 0; k < 3; ++k)
                f.impact[k] = light.at[k] - static_cast<fx>((static_cast<std::int64_t>(normal[k]) * off) >> 12);

            // The texture's direction "s" in the plane (the gradient of s
            // over the triangle: s1 (e2 x n) + s2 (n x e1), turned by the
            // triangle's own turn), and "t" square to it in the plane.
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
            std::int64_t turn = 0;
            std::int64_t g[3];
            for (int k = 0; k < 3; ++k)
            {
                const int k1 = (k + 1) % 3, k2 = (k + 2) % 3;
                turn += (e1[k1] * e2[k2] - e1[k2] * e2[k1]) * n[k];
                g[k] = sd1 * (e2[k1] * n[k2] - e2[k2] * n[k1]) + sd2 * (n[k1] * e1[k2] - n[k2] * e1[k1]);
            }
            if (turn == 0)
                return false; // (no area)
            // (To Q12 for its length, its largest part made a unit first;
            // one along an edge if the texture says nothing -- its
            // coordinates all the same.)
            std::int64_t big = 0;
            for (int pass = 0; pass < 2 && big == 0; ++pass)
            {
                for (int k = 0; k < 3; ++k)
                {
                    if (pass == 1)
                        g[k] = e1[k];
                    else if (turn < 0)
                        g[k] = -g[k];
                    big = (g[k] < 0 ? -g[k] : g[k]) > big ? (g[k] < 0 ? -g[k] : g[k]) : big;
                }
            }
            if (big == 0)
                return false;
            fx raw[3];
            for (int k = 0; k < 3; ++k)
                raw[k] = static_cast<fx>((g[k] << 12) / big);
            const fx length = fxLength(raw);
            if (length <= 0)
                return false;
            for (int k = 0; k < 3; ++k)
                f.sAxis[k] = static_cast<fx>((static_cast<std::int64_t>(raw[k]) << 12) / length);
            for (int k = 0; k < 3; ++k)
            {
                const int k1 = (k + 1) % 3, k2 = (k + 2) % 3;
                f.tAxis[k] = static_cast<fx>((normal[k1] * static_cast<std::int64_t>(f.sAxis[k2]) -
                                              normal[k2] * static_cast<std::int64_t>(f.sAxis[k1])) >> 12);
            }
            std::int32_t lo[2] = {0, 0}, hi[2] = {0, 0};
            for (int c = 0; c < 3; ++c)
            {
                std::int64_t a = 0, b = 0;
                for (int k = 0; k < 3; ++k)
                {
                    a += static_cast<std::int64_t>(tri[c].pos[k] - f.impact[k]) * f.sAxis[k];
                    b += static_cast<std::int64_t>(tri[c].pos[k] - f.impact[k]) * f.tAxis[k];
                }
                f.corner[c] = PatchPoint{static_cast<std::int32_t>(a >> 12), static_cast<std::int32_t>(b >> 12)};
                lo[0] = c == 0 || f.corner[c].a < lo[0] ? f.corner[c].a : lo[0];
                hi[0] = c == 0 || f.corner[c].a > hi[0] ? f.corner[c].a : hi[0];
                lo[1] = c == 0 || f.corner[c].b < lo[1] ? f.corner[c].b : lo[1];
                hi[1] = c == 0 || f.corner[c].b > hi[1] ? f.corner[c].b : hi[1];
            }
            // Not on this triangle at all: the octagon's square box clear of it.
            if (lo[0] >= f.rad || hi[0] <= -f.rad || lo[1] >= f.rad || hi[1] <= -f.rad)
                return false;
            f.area = edgeSide(f.corner[0], f.corner[1], f.corner[2]);
            f.sign = f.area > 0 ? 1 : -1;
            return f.area != 0;
        }

        /// A corner of the patch at `p`: where, its colour, its place in the
        /// texture -- the triangle's, run to here -- and the light added.
        [[gnu::noinline]] inline LampCorner patchCorner(const PatchFrame &f, const PatchPoint &p)
        {
            const LampCorner *tri = f.tri;
            // (Its share of each of the triangle's corners.)
            std::int64_t w[3];
            w[0] = edgeSide(f.corner[1], f.corner[2], p);
            w[1] = edgeSide(f.corner[2], f.corner[0], p);
            w[2] = f.area - w[0] - w[1];
            LampCorner out{};
            for (int k = 0; k < 3; ++k)
                out.pos[k] = f.impact[k] + static_cast<fx>((static_cast<std::int64_t>(f.sAxis[k]) * p.a +
                                                            static_cast<std::int64_t>(f.tAxis[k]) * p.b) >> 12);
            // The light the game adds here: (rad - dist) * strength, in
            // sixteenths of the lightmap's, which counts 264 for each 256 of
            // a wall's own (style "m").
            const std::int32_t a = absolute(p.a), b = absolute(p.b);
            const std::int32_t over = f.rad - ((a > b ? a : b) + (a > b ? b : a) / 2); // (Q12 units)
            const std::int32_t add =
                over <= 0 ? 0 : static_cast<std::int32_t>((static_cast<std::int64_t>(over) * f.strength * 255 * 16) / (264LL * 256 * 4096));
            // Five things run from the corners: s, t, and three channels.
            std::int32_t mixed[5];
            for (int m = 0; m < 5; ++m)
            {
                std::int64_t sum = 0;
                for (int c = 0; c < 3; ++c)
                {
                    const std::int32_t v = m == 0   ? static_cast<std::int16_t>(tri[c].texCoord & 0xFFFF)
                                           : m == 1 ? static_cast<std::int16_t>(tri[c].texCoord >> 16)
                                                    : ((tri[c].colour >> (5 * (m - 2))) & 31) * 256;
                    sum += v * w[c];
                }
                mixed[m] = static_cast<std::int32_t>(sum / f.area);
            }
            out.texCoord = (static_cast<std::uint32_t>(mixed[0]) & 0xFFFF) | (static_cast<std::uint32_t>(mixed[1]) << 16);
            std::uint16_t colour = 0;
            for (int c = 0; c < 3; ++c)
                colour |= static_cast<std::uint16_t>(lampLift(mixed[2 + c], add) << (5 * c));
            out.colour = colour;
            return out;
        }

        /// The k-th of the patch's 16 pieces (the fan of eight in the middle,
        /// then the ring of eight round it) cut to the triangle, its corners
        /// into `out`: how many (fewer than 3: none of it on the triangle).
        [[gnu::noinline]] inline int patchPiece(const PatchFrame &f, int k, LampCorner *out)
        {
            // The octagon's corners, and the ring's at half of it.
            const std::int32_t r = f.rad, r23 = f.rad * 2 / 3, h = f.rad / 2, h23 = f.rad / 3;
            const std::int8_t kWay[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
            auto ring = [&](int j, bool outer) {
                const std::int8_t *way = kWay[j % 8];
                const std::int32_t along = way[0] != 0 && way[1] != 0 ? (outer ? r23 : h23) : (outer ? r : h);
                return PatchPoint{way[0] * along, way[1] * along};
            };
            const int j = k % 8;
            PatchPoint a[12], b[12];
            int count;
            if (k < 8)
            {
                a[0] = PatchPoint{0, 0};
                a[1] = ring(j, false);
                a[2] = ring(j + 1, false);
                count = 3;
            }
            else
            {
                a[0] = ring(j, false);
                a[1] = ring(j, true);
                a[2] = ring(j + 1, true);
                a[3] = ring(j + 1, false);
                count = 4;
            }
            count = clipToLine(a, count, f.corner[0], f.corner[1], f.sign, b);
            if (count >= 3)
                count = clipToLine(b, count, f.corner[1], f.corner[2], f.sign, a);
            if (count >= 3)
                count = clipToLine(a, count, f.corner[2], f.corner[0], f.sign, b);
            for (int i = 0; count >= 3 && i < count; ++i)
                out[i] = patchCorner(f, b[i]);
            return count;
        }
    } // namespace detail

    /// The patch of the lamp's light on one triangle of a surface (corners
    /// `tri`, facing `normal`, Q12 unit): each piece is given to `emit` as a
    /// convex polygon of 3..9 LampCorners, in order. Returns how many
    /// triangles that is (a polygon of n corners is n - 2), or 0 for none --
    /// the light too far off the plane, or not on this triangle.
    template <class Emit>
    inline int lampPatchTriangle(const LampCorner *tri, const fx *normal, const LampLight &light, Emit &&emit)
    {
        detail::PatchFrame f;
        if (!detail::patchFrame(tri, normal, light, f))
            return 0;
        int triangles = 0;
        for (int k = 0; k < 16; ++k)
        {
            LampCorner made[12];
            const int count = detail::patchPiece(f, k, made);
            if (count < 3)
                continue;
            emit(static_cast<const LampCorner *>(made), count);
            triangles += count - 2;
        }
        return triangles;
    }

    /// A surface's geometry, as the level packs it for the hardware (its
    /// GxStream: runs of triangles, quads, strips of either, each vertex
    /// with its colour and texture coordinate, in quarter units in one of
    /// six forms -- the converter's GxPacker), read a triangle at a time
    /// (three LampCorners, Q12 units).
    class LampStreamReader
    {
    public:
        LampStreamReader(const std::uint32_t *words, int count) : words_(words), count_(count) {}
        /// The next triangle into `out`; false at the end, or at a word that
        /// is not one of the level's.
        [[gnu::noinline]] bool next(LampCorner *out)
        {
            if (queued_)
            {
                queued_ = false;
                for (int i = 0; i < 3; ++i)
                    out[i] = queue_[i];
                return true;
            }
            for (;;)
            {
                if (slot_ >= 4 || ((header_ >> (8 * slot_)) & 0xFF) == 0)
                {
                    if (at_ >= count_)
                        return false;
                    header_ = words_[at_++];
                    slot_ = 0;
                    continue;
                }
                const std::uint32_t code = (header_ >> (8 * slot_++)) & 0xFF;
                const int params = code == 0x23 ? 2 : 1;
                if (at_ + params > count_)
                    return false;
                const std::uint32_t p0 = words_[at_], p1 = params == 2 ? words_[at_ + 1] : 0;
                at_ += params;
                const auto lo = static_cast<std::int16_t>(p0 & 0xFFFF), hi = static_cast<std::int16_t>(p0 >> 16);
                switch (code)
                {
                case 0x40: // a run begins: triangles, quads, a strip of each
                    type_ = static_cast<int>(p0 & 3);
                    inRun_ = 0;
                    continue;
                case 0x20: pending_.colour = static_cast<std::uint16_t>(p0 & 0x7FFF); continue;
                case 0x22: pending_.texCoord = p0; continue;
                case 0x23: q_[0] = lo; q_[1] = hi; q_[2] = static_cast<std::int16_t>(p1 & 0xFFFF); break;
                case 0x24: q_[0] = ten(p0) * 64; q_[1] = ten(p0 >> 10) * 64; q_[2] = ten(p0 >> 20) * 64; break;
                case 0x25: q_[0] = lo; q_[1] = hi; break;
                case 0x26: q_[0] = lo; q_[2] = hi; break;
                case 0x27: q_[1] = lo; q_[2] = hi; break;
                case 0x28: q_[0] += ten(p0); q_[1] += ten(p0 >> 10); q_[2] += ten(p0 >> 20); break;
                default: return false;
                }
                for (int a = 0; a < 3; ++a)
                    pending_.pos[a] = static_cast<fx>(q_[a] * 1024); // (quarter units to Q12)
                if (vertex(out))
                    return true;
            }
        }

    private:
        static std::int32_t ten(std::uint32_t v) { return static_cast<std::int32_t>((v & 0x3FF) ^ 0x200) - 0x200; }

        /// The vertex just read is one of its run's: true with a triangle
        /// in `out` (and perhaps another queued) when there are enough.
        bool vertex(LampCorner *out)
        {
            auto give = [&](const LampCorner &x, const LampCorner &y, const LampCorner &z) {
                out[0] = x;
                out[1] = y;
                out[2] = z;
            };
            switch (type_)
            {
            case 0: // triangles
                run_[inRun_++] = pending_;
                if (inRun_ < 3)
                    return false;
                inRun_ = 0;
                give(run_[0], run_[1], run_[2]);
                return true;
            case 1: // quads
                run_[inRun_++] = pending_;
                if (inRun_ < 4)
                    return false;
                inRun_ = 0;
                give(run_[0], run_[1], run_[2]);
                queue_[0] = run_[0];
                queue_[1] = run_[2];
                queue_[2] = run_[3];
                queued_ = true;
                return true;
            case 2: // a strip of triangles: each vertex makes one with the two before
                if (inRun_ < 2)
                {
                    run_[inRun_++] = pending_;
                    return false;
                }
                give(run_[0], run_[1], pending_);
                run_[0] = run_[1];
                run_[1] = pending_;
                return true;
            default: // a strip of quads: each two vertices make one with the two before
                run_[inRun_++] = pending_;
                if (inRun_ < 4)
                    return false;
                // (Its corners in order are the first two, then the new two
                // the other way round.)
                give(run_[0], run_[1], run_[3]);
                queue_[0] = run_[0];
                queue_[1] = run_[3];
                queue_[2] = run_[2];
                queued_ = true;
                run_[0] = run_[2];
                run_[1] = run_[3];
                inRun_ = 2;
                return true;
            }
        }

        const std::uint32_t *words_;
        int count_;
        int at_ = 0;
        std::uint32_t header_ = 0;
        int slot_ = 4;
        LampCorner pending_{};
        LampCorner run_[4];
        LampCorner queue_[3];
        bool queued_ = false;
        int type_ = 0, inRun_ = 0;
        std::int32_t q_[3] = {0, 0, 0}; // (the last vertex, quarter units)
    };

    /// Each triangle of a surface's geometry given to `emit` (LampStreamReader).
    template <class Emit>
    inline void lampStreamTriangles(const std::uint32_t *words, int count, Emit &&emit)
    {
        LampStreamReader reader(words, count);
        LampCorner tri[3];
        while (reader.next(tri))
            emit(static_cast<const LampCorner *>(tri));
    }
} // namespace hldata
