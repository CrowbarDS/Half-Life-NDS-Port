#pragma once

// The lamp's light on a wall, built as the game builds it: on the grid its
// light map is kept on -- a point every 16 texels of the wall's texture, in
// line with the texture's own axes (R_AddDynamicLights' luxels) -- with the
// game's light worked out at each point of it, and the hardware running one
// point's colour into the next across each square of the grid, as the game's
// light map is smoothed between its points. (Every 32 texels on a DS: four
// times fewer squares.)
//
// The game's light at a point: (rad - dist) * strength, rad = 80 - how far
// the light is off the plane, dist = max(sd, td) + min(sd, td) / 2 -- sd and
// td how far the point is from where the light falls, along the texture's two
// axes, in its texels. It is ADDED to the wall's own light before that is
// made into what is seen (HlLampPatch.hpp: lampLift).
//
// Why a grid and not a shape round where the light falls: the grid's lines
// are the same lines whichever face of the wall they cross -- they are the
// texture's -- so where two faces meet, both have the same points on their
// edge, and nothing shows between them. A square of the grid wholly on one
// of the surface's polygons goes as a square (a row of them as one strip);
// one a polygon's edge crosses is cut to it, the cut points worked out from
// the edge alone, so that the polygon on the other side makes the same ones.
//
// The texture's coordinates as the level has them (its GxStream: sixteenths
// of a texel of the texture as cooked, s low and t high): the game's texels
// are worked out from those by the cooked and the game's sizes. A group of
// faces that spans more than the hardware's coordinates hold is moved face by
// face by whole repeats of the texture (the converter's MeshOptimize): each
// polygon is brought back to the first one's, and its own put back as it is
// sent.
//
// std-only and inline: the host tests run it (test_ui).

#include <cstdint>

#include "HlFixed.hpp"
#include "HlLampPatch.hpp"

namespace hldata
{
    /// A vertex of the light, as it is sent: where (Q12 units), its colour
    /// (RGB15: the wall's light and the lamp's), and its texture coordinates
    /// in sixteenths of a cooked texel (in the first polygon's frame: the
    /// draw says by how much to move them).
    struct LampVertex
    {
        fx pos[3];
        std::uint16_t colour;
        std::int32_t s, t;
    };

    /// A surface's texture, its sizes as cooked and as the game had it.
    struct LampGridTexture
    {
        int width, height;             ///< as cooked: powers of two
        int sourceWidth, sourceHeight; ///< as the game had them (0: as cooked)
    };

    /// A corner of one of a surface's polygons: where, its colour, its
    /// texture coordinates (sixteenths of a cooked texel).
    struct LampGridCorner
    {
        fx pos[3];
        std::uint16_t colour;
        std::int32_t s, t;
    };

    /// A surface's GxStream read a polygon at a time: a triangle, or a quad
    /// (its corners in order round it).
    class LampPolygonReader
    {
    public:
        LampPolygonReader(const std::uint32_t *words, int count) : words_(words), count_(count) {}
        /// The next polygon's corners into `out` (4 of room): how many, 3 or
        /// 4; 0 at the end, or at a word that is not one of the level's.
        [[gnu::noinline]] int next(LampGridCorner *out)
        {
            for (;;)
            {
                if (slot_ >= 4 || ((header_ >> (8 * slot_)) & 0xFF) == 0)
                {
                    if (at_ >= count_)
                        return 0;
                    header_ = words_[at_++];
                    slot_ = 0;
                    continue;
                }
                const std::uint32_t code = (header_ >> (8 * slot_++)) & 0xFF;
                const int params = code == 0x23 ? 2 : 1;
                if (at_ + params > count_)
                    return 0;
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
                case 0x22:
                    pending_.s = static_cast<std::int16_t>(p0 & 0xFFFF);
                    pending_.t = static_cast<std::int16_t>(p0 >> 16);
                    continue;
                case 0x23: q_[0] = lo; q_[1] = hi; q_[2] = static_cast<std::int16_t>(p1 & 0xFFFF); break;
                case 0x24: q_[0] = ten(p0) * 64; q_[1] = ten(p0 >> 10) * 64; q_[2] = ten(p0 >> 20) * 64; break;
                case 0x25: q_[0] = lo; q_[1] = hi; break;
                case 0x26: q_[0] = lo; q_[2] = hi; break;
                case 0x27: q_[1] = lo; q_[2] = hi; break;
                case 0x28: q_[0] += ten(p0); q_[1] += ten(p0 >> 10); q_[2] += ten(p0 >> 20); break;
                default: return 0;
                }
                for (int a = 0; a < 3; ++a)
                    pending_.pos[a] = static_cast<fx>(q_[a] * 1024); // (quarter units to Q12)
                if (const int n = vertex(out))
                    return n;
            }
        }

    private:
        static std::int32_t ten(std::uint32_t v) { return static_cast<std::int32_t>((v & 0x3FF) ^ 0x200) - 0x200; }

        /// The vertex just read is one of its run's: how many corners are
        /// in `out` when it makes a polygon, else 0.
        int vertex(LampGridCorner *out)
        {
            switch (type_)
            {
            case 0: // triangles
                run_[inRun_++] = pending_;
                if (inRun_ < 3)
                    return 0;
                inRun_ = 0;
                for (int i = 0; i < 3; ++i)
                    out[i] = run_[i];
                return 3;
            case 1: // quads
                run_[inRun_++] = pending_;
                if (inRun_ < 4)
                    return 0;
                inRun_ = 0;
                for (int i = 0; i < 4; ++i)
                    out[i] = run_[i];
                return 4;
            case 2: // a strip of triangles: each vertex makes one with the two before
                if (inRun_ < 2)
                {
                    run_[inRun_++] = pending_;
                    return 0;
                }
                out[0] = run_[0];
                out[1] = run_[1];
                out[2] = pending_;
                run_[0] = run_[1];
                run_[1] = pending_;
                return 3;
            default: // a strip of quads: each two vertices make one with the two before
                run_[inRun_++] = pending_;
                if (inRun_ < 4)
                    return 0;
                // (Round it: the first two, then the new two the other way.)
                out[0] = run_[0];
                out[1] = run_[1];
                out[2] = run_[3];
                out[3] = run_[2];
                run_[0] = run_[2];
                run_[1] = run_[3];
                inRun_ = 2;
                return 4;
            }
        }

        const std::uint32_t *words_;
        int count_;
        int at_ = 0;
        std::uint32_t header_ = 0;
        int slot_ = 4;
        LampGridCorner pending_{};
        LampGridCorner run_[4];
        int type_ = 0, inRun_ = 0;
        std::int32_t q_[3] = {0, 0, 0}; // (the last vertex, quarter units)
    };

    /// What one surface's light came to (for the profiler and the tests).
    struct LampGridCounts
    {
        int polygons = 0; ///< of the surface's, read
        int lit = 0;      ///< ... the light reached
        int cells = 0;    ///< whole squares sent (in strips)
        int pieces = 0;   ///< squares cut to a polygon
    };

    /// The light on one surface at a time. Its working room is its own (a
    /// static one: no heap); `build` hands what it makes to `out` as it
    /// goes:
    ///
    ///   bool strip(const LampVertex *const *v, int n, std::int32_t ds, std::int32_t dt)
    ///       a strip of quads (GL_QUAD_STRIP: n even, 4 or more)
    ///   bool polygon(const LampVertex *const *v, int n, std::int32_t ds, std::int32_t dt)
    ///       a triangle or a quad (3 or 4)
    ///
    /// each with how much to move its texture coordinates by (a face moved
    /// by whole repeats); false from either stops it (no room left).
    class LampGrid
    {
    public:
        static constexpr int kLines = 13; ///< grid lines along each axis, at most
        static constexpr int kCorners = 32; ///< corners on grid lines kept, at most

        /// The light on the surface whose stream is `words` (`count` words),
        /// facing `normal` (Q12 unit, toward the eye), its texture `texture`,
        /// the grid `step` texels (16 or 32). Returns false if `out` ran out
        /// of room (what was made is made).
        template <class Out>
        bool build(const std::uint32_t *words, int count, const fx *normal, const LampLight &light,
                   const LampGridTexture &texture, int step, Out &out, LampGridCounts *counts = nullptr)
        {
            LampGridCounts mine;
            LampGridCounts &c = counts != nullptr ? *counts : mine;
            if (!begin(words, count, normal, light, texture, step))
                return true;
            // Pass 1: the corners that lie on grid lines (a square's side
            // they lie on gets them too: no edge of one with the middle of
            // another's on it).
            gatherCorners(words, count);
            // Pass 2: each polygon the light reaches.
            LampPolygonReader reader(words, count);
            LampGridCorner poly[4];
            while (const int n = reader.next(poly))
            {
                ++c.polygons;
                Polygon p;
                if (!setUp(poly, n, p))
                    continue;
                ++c.lit;
                if (!lightPolygon(p, out, c))
                    return false;
            }
            return true;
        }

        /// Whether the quad v[0..3] is one the hardware draws right from any
        /// view once its corners are rounded to the quarter units they are
        /// sent in -- not one of GBATEK's "prohibited quads" (crossed sides,
        /// a corner of more than 180 degrees). The converter's rule
        /// (MeshOptimize.cpp): each corner exactly in line with its
        /// neighbours, or turning the quad's way -- by more than 1.5 steps,
        /// unless the four are exactly in one plane. (A piece too large to
        /// judge in 64 bits is not one: it goes as triangles.)
        static bool quadSafe(const LampVertex *const *v)
        {
            std::int64_t q[4][3];
            for (int i = 0; i < 4; ++i)
                for (int k = 0; k < 3; ++k)
                {
                    q[i][k] = (v[i]->pos[k] + 512) >> 10;
                    const std::int64_t d = q[i][k] - q[0][k];
                    if (d > 4096 || d < -4096) // (1,024 units: the lamp reaches 80)
                        return false;
                }
            auto cross = [&](int a, int b, int c, std::int64_t *out)
            {
                const std::int64_t e1[3] = {q[b][0] - q[a][0], q[b][1] - q[a][1], q[b][2] - q[a][2]};
                const std::int64_t e2[3] = {q[c][0] - q[a][0], q[c][1] - q[a][1], q[c][2] - q[a][2]};
                out[0] = e1[1] * e2[2] - e1[2] * e2[1];
                out[1] = e1[2] * e2[0] - e1[0] * e2[2];
                out[2] = e1[0] * e2[1] - e1[1] * e2[0];
            };
            std::int64_t turn[4][3], facing[3] = {0, 0, 0};
            for (int i = 0; i < 4; ++i)
            {
                cross((i + 3) & 3, i, (i + 1) & 3, turn[i]);
                for (int k = 0; k < 3; ++k)
                    facing[k] += turn[i][k];
            }
            std::int64_t n0[3];
            cross(0, 1, 2, n0);
            const bool planar =
                n0[0] * (q[3][0] - q[0][0]) + n0[1] * (q[3][1] - q[0][1]) + n0[2] * (q[3][2] - q[0][2]) == 0;
            for (int i = 0; i < 4; ++i)
            {
                const std::int64_t *t = turn[i];
                if (t[0] == 0 && t[1] == 0 && t[2] == 0)
                    continue; // exactly in line
                // (Its sign as the quad faces. Corners within 2^13 of each
                // other: a turn's terms within 2^27, the facing's 2^29, every
                // product and sum here within 2^60.)
                if (t[0] * facing[0] + t[1] * facing[1] + t[2] * facing[2] <= 0)
                    return false;
                if (planar)
                    continue;
                // More than 1.5 steps off the line through its neighbours:
                // |turn|^2 > 2.25 |next - previous|^2.
                const int a = (i + 3) & 3, c = (i + 1) & 3;
                const std::int64_t e[3] = {q[c][0] - q[a][0], q[c][1] - q[a][1], q[c][2] - q[a][2]};
                const std::int64_t area2 = t[0] * t[0] + t[1] * t[1] + t[2] * t[2];
                const std::int64_t length2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
                if (4 * area2 <= 9 * length2)
                    return false;
            }
            return true;
        }

        // ---- What the tests look at ------------------------------------
        /// The lines of the last surface (stream units), and the light the
        /// game adds at each crossing (sixteenths of the light map's).
        int columns() const { return columns_; }
        int rows() const { return rows_; }
        std::int32_t lineS(int m) const { return lineS_[m]; }
        std::int32_t lineT(int n) const { return lineT_[n]; }
        int addAt(int m, int n) const { return add_[m][n]; }

        /// The game's light (sixteenths of the light map's) for a point
        /// `sd`, `td` game sixteenths of a texel from where the light falls,
        /// `rad16` sixteenths of a unit across, at `gain` (lampGain).
        static constexpr int lampAdd(std::int32_t sd, std::int32_t td, std::int32_t rad16, std::int32_t gain)
        {
            sd = sd < 0 ? -sd : sd;
            td = td < 0 ? -td : td;
            const std::int32_t over = rad16 - ((sd > td ? sd : td) + (sd > td ? td : sd) / 2);
            return over <= 0 ? 0 : static_cast<int>((over * gain) >> 16);
        }
        /// How much light (65536ths of a sixteenth of the light map's for
        /// each sixteenth of a texel inside the edge) the lamp gives at
        /// `strength` (of 256: lampFalloff): the game's 255 for each unit,
        /// in a light map that counts 264 for each 256 of a wall's own
        /// (style "m").
        static constexpr std::int32_t lampGain(int strength)
        {
            // (255 * 65536 / (264 * 16 * 16) a step of strength, in 256ths.)
            return (strength * 63302) >> 8;
        }

    private:
        /// One of the surface's polygons, ready: its corners (in the first
        /// polygon's texture frame), its box there, and how position and
        /// colour run across it.
        struct Polygon
        {
            LampGridCorner corner[4];
            int n;
            int sign; ///< which way round it goes in s, t: 1 or -1
            std::int32_t lo[2], hi[2];
            std::int32_t ds, dt; ///< what it was moved by (stream units)
            // Position at (s, t): pos0 + ((s - s0) * a + (t - t0) * b) >> 12.
            std::int32_t a[3], b[3];
            // Colour (256ths of a level) at (s, t), from the triangle (0, 1,
            // 2) and, for a quad, (0, 2, 3): c0 + ((s - s0) * ca + (t - t0) * cb) >> 12.
            std::int32_t ca[2][3], cb[2][3];
            std::int32_t c0[2][3];
        };

        // ---- Setting up --------------------------------------------------

        /// Where the light falls on the surface's plane, how big it is, the
        /// grid's lines over it and the light at each crossing. False if the
        /// light does not reach the plane, or the surface says nothing.
        bool begin(const std::uint32_t *words, int count, const fx *normal, const LampLight &light,
                   const LampGridTexture &texture, int step)
        {
            columns_ = rows_ = 0;
            cornerCount_ = 0;
            if (light.strength <= 0)
                return false;
            // The first polygon whose texture runs across it: the frame
            // every other is brought into, and where the light falls in it.
            LampPolygonReader reader(words, count);
            LampGridCorner poly[4];
            int n;
            for (;;)
            {
                n = reader.next(poly);
                if (n == 0)
                    return false;
                if (sweep(poly) != 0)
                    break;
            }
            std::int64_t off64 = 0;
            for (int k = 0; k < 3; ++k)
                off64 += static_cast<std::int64_t>(light.at[k] - poly[0].pos[k]) * normal[k];
            const fx off = static_cast<fx>(off64 >> 12);
            const fx rad = fxFromInt(kLampPatchRadius) - (off < 0 ? -off : off);
            if (rad <= fxFromInt(1))
                return false;
            rad16_ = rad >> 8;
            for (int k = 0; k < 3; ++k)
                impact_[k] = light.at[k] - static_cast<fx>((static_cast<std::int64_t>(normal[k]) * off) >> 12);
            drop_ = 0;
            for (int k = 1; k < 3; ++k)
                drop_ = absolute(normal[k]) > absolute(normal[drop_]) ? k : drop_;
            if (!impactIn(poly, impactS_, impactT_))
                return false;
            gain_ = lampGain(light.strength);

            // The texture's sizes: the game's texels are the cooked ones
            // times the game's size over the cooked.
            width_ = texture.width > 0 ? texture.width : 1;
            height_ = texture.height > 0 ? texture.height : 1;
            sourceWidth_ = texture.sourceWidth > 0 ? texture.sourceWidth : width_;
            sourceHeight_ = texture.sourceHeight > 0 ? texture.sourceHeight : height_;
            widthShift_ = shiftOf(width_);
            heightShift_ = shiftOf(height_);
            stepShift_ = step >= 32 ? 9 : 8; // (16 or 32 texels, in sixteenths)
            // Where the light falls, in the game's sixteenths of a texel.
            const std::int32_t gs = static_cast<std::int32_t>((static_cast<std::int64_t>(impactS_) * sourceWidth_) >> (4 + widthShift_));
            const std::int32_t gt = static_cast<std::int32_t>((static_cast<std::int64_t>(impactT_) * sourceHeight_) >> (4 + heightShift_));
            // The lines round it, on whole steps of the game's texels.
            const std::int32_t mLo = (gs - rad16_) >> stepShift_, mHi = -((-(gs + rad16_)) >> stepShift_);
            const std::int32_t nLo = (gt - rad16_) >> stepShift_, nHi = -((-(gt + rad16_)) >> stepShift_);
            columns_ = static_cast<int>(mHi - mLo + 1);
            rows_ = static_cast<int>(nHi - nLo + 1);
            if (columns_ < 2 || rows_ < 2 || columns_ > kLines || rows_ > kLines)
            {
                columns_ = rows_ = 0;
                return false;
            }
            std::int32_t sd[kLines], td[kLines];
            for (int m = 0; m < columns_; ++m)
            {
                const std::int32_t g = (mLo + m) << stepShift_;
                lineS_[m] = toStream(g, widthShift_, sourceWidth_);
                sd[m] = g - gs;
            }
            for (int k = 0; k < rows_; ++k)
            {
                const std::int32_t g = (nLo + k) << stepShift_;
                lineT_[k] = toStream(g, heightShift_, sourceHeight_);
                td[k] = g - gt;
            }
            for (int m = 0; m < columns_; ++m)
            {
                for (int k = 0; k < rows_; ++k)
                {
                    add_[m][k] = static_cast<std::int16_t>(lampAdd(sd[m], td[k], rad16_, gain_));
                    made_[m][k] = false;
                }
            }
            // How far across the surface the light can reach, in the world
            // (its texture's axes as the first polygon has them): a polygon
            // clear of that box is passed over unread.
            Polygon first;
            first.n = n;
            for (int i = 0; i < n; ++i)
                first.corner[i] = poly[i];
            if (!mapOf(first))
                return false;
            const std::int64_t rs = lineS_[columns_ - 1] - lineS_[0], rt = lineT_[rows_ - 1] - lineT_[0];
            for (int k = 0; k < 3; ++k)
            {
                const std::int64_t reach = (rs * absolute(first.a[k]) + rt * absolute(first.b[k])) >> 12;
                reach_[k] = static_cast<std::int32_t>(reach > 0x7FFF0000 ? 0x7FFF0000 : reach) + fxFromInt(1);
            }
            return true;
        }

        static int shiftOf(int v)
        {
            int s = 0;
            while ((1 << s) < v)
                ++s;
            return s;
        }
        static std::int32_t absolute(std::int32_t v) { return v < 0 ? -v : v; }
        /// A corner's index round a polygon of n, a in [0, 2 n): a % n
        /// without the library's divide.
        static int ring(int a, int n) { return a >= n ? a - n : a; }

        /// A line `g` game sixteenths of a texel along, in stream units (the
        /// cooked texture's sixteenths): exact when the sizes are the same.
        static std::int32_t toStream(std::int32_t g, int cookedShift, int source)
        {
            if (source == (1 << cookedShift))
                return g;
            const std::int64_t scaled = static_cast<std::int64_t>(g) << cookedShift;
            // (Rounded to the nearest, the same way either side of 0.)
            const std::int64_t half = source / 2;
            return static_cast<std::int32_t>(scaled >= 0 ? (scaled + half) / source : -((-scaled + half) / source));
        }

        /// Twice the area of a triangle's texture (s, t), signed: 0 if the
        /// texture does not run across it.
        static std::int64_t sweep(const LampGridCorner *p)
        {
            return static_cast<std::int64_t>(p[1].s - p[0].s) * (p[2].t - p[0].t) -
                   static_cast<std::int64_t>(p[2].s - p[0].s) * (p[1].t - p[0].t);
        }

        /// Where the light falls (impact_), in the texture coordinates of the
        /// triangle (p[0], p[1], p[2]) carried on across its plane, in
        /// sixteenths of a stream unit. False if it has no area seen along
        /// the plane's axis.
        bool impactIn(const LampGridCorner *p, std::int32_t &s, std::int32_t &t) const
        {
            const int u = drop_ == 0 ? 1 : 0, v = drop_ == 2 ? 1 : 2;
            // (In 64ths of a unit: room for the products below.)
            const std::int64_t qu = (impact_[u] - p[0].pos[u]) >> 6, qv = (impact_[v] - p[0].pos[v]) >> 6;
            const std::int64_t e1u = (p[1].pos[u] - p[0].pos[u]) >> 6, e1v = (p[1].pos[v] - p[0].pos[v]) >> 6;
            const std::int64_t e2u = (p[2].pos[u] - p[0].pos[u]) >> 6, e2v = (p[2].pos[v] - p[0].pos[v]) >> 6;
            const std::int64_t d = e1u * e2v - e1v * e2u;
            if (d == 0)
                return false;
            const std::int64_t l1 = qu * e2v - qv * e2u, l2 = e1u * qv - e1v * qu;
            const std::int64_t ds1 = p[1].s - p[0].s, ds2 = p[2].s - p[0].s;
            const std::int64_t dt1 = p[1].t - p[0].t, dt2 = p[2].t - p[0].t;
            s = p[0].s * 16 + static_cast<std::int32_t>(((l1 * ds1 + l2 * ds2) * 16) / d);
            t = p[0].t * 16 + static_cast<std::int32_t>(((l1 * dt1 + l2 * dt2) * 16) / d);
            return true;
        }

        /// How position and colour run across `p` (its corners set, in its
        /// own frame). False if its texture does not run across it, or runs
        /// so thin that it cannot be held.
        static bool mapOf(Polygon &p)
        {
            const LampGridCorner *c = p.corner;
            const std::int64_t det = sweep(c);
            if (det == 0)
                return false;
            const std::int64_t ds1 = c[1].s - c[0].s, ds2 = c[2].s - c[0].s;
            const std::int64_t dt1 = c[1].t - c[0].t, dt2 = c[2].t - c[0].t;
            for (int k = 0; k < 3; ++k)
            {
                const std::int64_t p1 = c[1].pos[k] - c[0].pos[k], p2 = c[2].pos[k] - c[0].pos[k];
                const std::int64_t a = ((dt2 * p1 - dt1 * p2) * 4096) / det;
                const std::int64_t b = ((ds1 * p2 - ds2 * p1) * 4096) / det;
                if (a > 0x3FFFFFFF || a < -0x3FFFFFFF || b > 0x3FFFFFFF || b < -0x3FFFFFFF)
                    return false;
                p.a[k] = static_cast<std::int32_t>(a);
                p.b[k] = static_cast<std::int32_t>(b);
            }
            // The colour: of the triangle (0, 1, 2), and of a quad's other
            // half (0, 2, 3).
            for (int half = 0; half < (p.n == 4 ? 2 : 1); ++half)
            {
                const LampGridCorner &x = c[0], &y = c[half == 0 ? 1 : 2], &z = c[half == 0 ? 2 : 3];
                const std::int64_t hs1 = y.s - x.s, hs2 = z.s - x.s, ht1 = y.t - x.t, ht2 = z.t - x.t;
                const std::int64_t hdet = hs1 * ht2 - hs2 * ht1;
                for (int ch = 0; ch < 3; ++ch)
                {
                    const std::int32_t cx = ((x.colour >> (5 * ch)) & 31) * 256;
                    p.c0[half][ch] = cx;
                    if (hdet == 0)
                    {
                        p.ca[half][ch] = p.cb[half][ch] = 0;
                        continue;
                    }
                    const std::int64_t c1 = ((y.colour >> (5 * ch)) & 31) * 256 - cx;
                    const std::int64_t c2 = ((z.colour >> (5 * ch)) & 31) * 256 - cx;
                    p.ca[half][ch] = static_cast<std::int32_t>(((ht2 * c1 - ht1 * c2) * 4096) / hdet);
                    p.cb[half][ch] = static_cast<std::int32_t>(((hs1 * c2 - hs2 * c1) * 4096) / hdet);
                }
            }
            return true;
        }

        /// Ready `p` from the polygon `corners` (n of them), if the light
        /// reaches it: brought into the first polygon's frame, its box and
        /// its map. False if it is clear of the light.
        bool setUp(const LampGridCorner *corners, int n, Polygon &p) const
        {
            // Clear of the light in the world?
            for (int k = 0; k < 3; ++k)
            {
                fx lo = corners[0].pos[k], hi = lo;
                for (int i = 1; i < n; ++i)
                {
                    lo = corners[i].pos[k] < lo ? corners[i].pos[k] : lo;
                    hi = corners[i].pos[k] > hi ? corners[i].pos[k] : hi;
                }
                if (lo > impact_[k] + reach_[k] || hi < impact_[k] - reach_[k])
                    return false;
            }
            if (sweep(corners) == 0)
                return false;
            // Moved by whole repeats from the first (a group too wide for
            // the hardware's coordinates)? Brought back.
            std::int32_t s16, t16;
            if (!impactIn(corners, s16, t16))
                return false;
            const std::int32_t repeatS16 = width_ << 8, repeatT16 = height_ << 8; // (16 units a texel, 16ths)
            const std::int32_t ks = roundDivide(s16 - impactS_, repeatS16), kt = roundDivide(t16 - impactT_, repeatT16);
            p.ds = ks * (width_ << 4);
            p.dt = kt * (height_ << 4);
            p.n = n;
            for (int i = 0; i < n; ++i)
            {
                p.corner[i] = corners[i];
                p.corner[i].s -= p.ds;
                p.corner[i].t -= p.dt;
            }
            p.lo[0] = p.hi[0] = p.corner[0].s;
            p.lo[1] = p.hi[1] = p.corner[0].t;
            for (int i = 1; i < n; ++i)
            {
                p.lo[0] = p.corner[i].s < p.lo[0] ? p.corner[i].s : p.lo[0];
                p.hi[0] = p.corner[i].s > p.hi[0] ? p.corner[i].s : p.hi[0];
                p.lo[1] = p.corner[i].t < p.lo[1] ? p.corner[i].t : p.lo[1];
                p.hi[1] = p.corner[i].t > p.hi[1] ? p.corner[i].t : p.hi[1];
            }
            if (p.hi[0] <= lineS_[0] || p.lo[0] >= lineS_[columns_ - 1] || p.hi[1] <= lineT_[0] ||
                p.lo[1] >= lineT_[rows_ - 1])
                return false;
            std::int64_t area = 0;
            for (int i = 0; i < n; ++i)
            {
                const LampGridCorner &x = p.corner[i], &y = p.corner[ring(i + 1, n)];
                area += static_cast<std::int64_t>(x.s) * y.t - static_cast<std::int64_t>(y.s) * x.t;
            }
            if (area == 0)
                return false;
            p.sign = area > 0 ? 1 : -1;
            return mapOf(p);
        }

        /// a / b rounded to the nearest (b > 0), the same either side of 0.
        static std::int32_t roundDivide(std::int32_t a, std::int32_t b)
        {
            return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b);
        }

        /// The corners of the surface's polygons (those the light can reach,
        /// not moved) that lie on one of the grid's lines inside it.
        void gatherCorners(const std::uint32_t *words, int count)
        {
            LampPolygonReader reader(words, count);
            LampGridCorner poly[4];
            while (const int n = reader.next(poly))
            {
                for (int i = 0; i < n && cornerCount_ < kCorners; ++i)
                {
                    const LampGridCorner &x = poly[i];
                    if (x.s < lineS_[0] || x.s > lineS_[columns_ - 1] || x.t < lineT_[0] || x.t > lineT_[rows_ - 1])
                        continue;
                    bool on = false;
                    for (int m = 0; m < columns_ && !on; ++m)
                        on = x.s == lineS_[m];
                    for (int k = 0; k < rows_ && !on; ++k)
                        on = x.t == lineT_[k];
                    bool known = false;
                    for (int j = 0; j < cornerCount_ && !known; ++j)
                        known = corners_[j].s == x.s && corners_[j].t == x.t;
                    if (on && !known)
                        corners_[cornerCount_++] = x;
                }
            }
        }

        // ---- Points of the light -----------------------------------------

        /// Which way round `p`'s side i -> i+1 a point (s, t) is: >= 0
        /// inside or on it.
        static std::int64_t side(const Polygon &p, int i, std::int32_t s, std::int32_t t)
        {
            const LampGridCorner &x = p.corner[i], &y = p.corner[ring(i + 1, p.n)];
            return (static_cast<std::int64_t>(y.s - x.s) * (t - x.t) - static_cast<std::int64_t>(y.t - x.t) * (s - x.s)) * p.sign;
        }
        static bool inside(const Polygon &p, std::int32_t s, std::int32_t t)
        {
            for (int i = 0; i < p.n; ++i)
            {
                if (side(p, i, s, t) < 0)
                    return false;
            }
            return true;
        }

        /// The wall's own colour at (s, t) on `p`, a channel in 256ths of a
        /// level.
        static void colourAt(const Polygon &p, std::int32_t s, std::int32_t t, std::int32_t *out)
        {
            // (A quad: the half of it (s, t) is in, across its 0 -> 2.)
            int half = 0;
            if (p.n == 4)
            {
                const LampGridCorner &x = p.corner[0], &y = p.corner[2];
                const std::int64_t across = (static_cast<std::int64_t>(y.s - x.s) * (t - x.t) -
                                             static_cast<std::int64_t>(y.t - x.t) * (s - x.s)) * p.sign;
                half = across > 0 ? 1 : 0;
            }
            const std::int32_t ds = s - p.corner[0].s, dt = t - p.corner[0].t;
            for (int ch = 0; ch < 3; ++ch)
            {
                std::int32_t v = p.c0[half][ch] + static_cast<std::int32_t>((static_cast<std::int64_t>(ds) * p.ca[half][ch] +
                                                                             static_cast<std::int64_t>(dt) * p.cb[half][ch]) >> 12);
                out[ch] = v < 0 ? 0 : (v > 31 * 256 ? 31 * 256 : v);
            }
        }

        static std::uint16_t lit(const std::int32_t *colour256, int add)
        {
            std::uint16_t colour = 0;
            for (int ch = 0; ch < 3; ++ch)
                colour |= static_cast<std::uint16_t>(lampLift(colour256[ch], add) << (5 * ch));
            return colour;
        }

        /// The crossing (m, n) of the grid, worked out on `p` (it is on it)
        /// the first time it is asked for.
        const LampVertex &gridPoint(const Polygon &p, int m, int n)
        {
            LampVertex &v = grid_[m][n];
            if (made_[m][n])
                return v;
            made_[m][n] = true;
            v.s = lineS_[m];
            v.t = lineT_[n];
            const std::int32_t ds = v.s - p.corner[0].s, dt = v.t - p.corner[0].t;
            for (int k = 0; k < 3; ++k)
                v.pos[k] = p.corner[0].pos[k] + static_cast<fx>((static_cast<std::int64_t>(ds) * p.a[k] +
                                                                 static_cast<std::int64_t>(dt) * p.b[k]) >> 12);
            std::int32_t colour[3];
            colourAt(p, v.s, v.t, colour);
            v.colour = lit(colour, add_[m][n]);
            return v;
        }

        /// The light at a point of the square (m, n) that is not one of its
        /// corners: run from theirs, as the game's light map runs from point
        /// to point.
        int addInside(int m, int n, std::int32_t s, std::int32_t t) const
        {
            const std::int64_t w = lineS_[m + 1] - lineS_[m], h = lineT_[n + 1] - lineT_[n];
            const std::int64_t x = s - lineS_[m], y = t - lineT_[n];
            const std::int64_t sum = add_[m][n] * (w - x) * (h - y) + add_[m + 1][n] * x * (h - y) +
                                     add_[m][n + 1] * (w - x) * y + add_[m + 1][n + 1] * x * y;
            return static_cast<int>(sum / (w * h));
        }

        // ---- Cutting -----------------------------------------------------

        /// A corner of a piece of a polygon cut to a square: which it is,
        /// and what the side after it runs along (one of the polygon's
        /// sides, 0..3, or one of the square's, 4..7: left, right, bottom,
        /// top).
        struct Cut
        {
            std::int32_t s, t;
            std::uint8_t kind; ///< kCorner, kEdge, kGrid
            std::uint8_t which; ///< kCorner: the polygon's corner; kEdge: its side; kGrid: 0..3 the square's corner
            std::uint8_t along; ///< the side after it
        };
        static constexpr std::uint8_t kCorner = 0, kEdge = 1, kGrid = 2, kInserted = 3;

        /// Where `p`'s side `e` crosses the line `s` = x (`onS`) or `t` = x:
        /// worked out from the side's two ends taken in one order whichever
        /// polygon asks, so that its neighbour makes the same point.
        static void crossing(const Polygon &p, int e, bool onS, std::int32_t x, LampVertex &out,
                             std::int32_t *colour256)
        {
            const LampGridCorner *a = &p.corner[e], *b = &p.corner[ring(e + 1, p.n)];
            if (b->s < a->s || (b->s == a->s && b->t < a->t))
            {
                const LampGridCorner *swap = a;
                a = b;
                b = swap;
            }
            std::int64_t along = onS ? x - a->s : x - a->t;
            std::int64_t whole = onS ? b->s - a->s : b->t - a->t;
            if (whole < 0)
            {
                // (Ends the other way along this axis: both signs turned.)
                along = -along;
                whole = -whole;
            }
            auto run = [&](std::int64_t from, std::int64_t to) {
                const std::int64_t d = (to - from) * along;
                return static_cast<std::int32_t>(from + (d >= 0 ? (d + whole / 2) / whole : -((-d + whole / 2) / whole)));
            };
            out.s = onS ? x : run(a->s, b->s);
            out.t = onS ? run(a->t, b->t) : x;
            for (int k = 0; k < 3; ++k)
                out.pos[k] = run(a->pos[k], b->pos[k]);
            for (int ch = 0; ch < 3; ++ch)
                colour256[ch] = run(((a->colour >> (5 * ch)) & 31) * 256, ((b->colour >> (5 * ch)) & 31) * 256);
        }

        /// The square (m, n)'s side `l` (0 left s = S[m], 1 right, 2 bottom
        /// t = T[n], 3 top): whether (s, t) is on its inner side or on it.
        bool within(int m, int n, int l, std::int32_t s, std::int32_t t) const
        {
            switch (l)
            {
            case 0: return s >= lineS_[m];
            case 1: return s <= lineS_[m + 1];
            case 2: return t >= lineT_[n];
            default: return t <= lineT_[n + 1];
            }
        }

        /// `p` cut to the square (m, n), into `out` (12 of room): how many
        /// corners.
        int cutToSquare(const Polygon &p, int m, int n, Cut *out) const
        {
            Cut a[12], b[12];
            int count = p.n;
            for (int i = 0; i < p.n; ++i)
                a[i] = Cut{p.corner[i].s, p.corner[i].t, kCorner, static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i)};
            Cut *in = a, *made = b;
            for (int l = 0; l < 4 && count >= 3; ++l)
            {
                int k = 0;
                for (int i = 0; i < count; ++i)
                {
                    const Cut &c = in[i], &d = in[ring(i + 1, count)];
                    const bool cIn = within(m, n, l, c.s, c.t), dIn = within(m, n, l, d.s, d.t);
                    if (cIn != dIn)
                    {
                        Cut x;
                        x.along = cIn ? static_cast<std::uint8_t>(4 + l) : c.along;
                        const bool onS = l < 2;
                        const std::int32_t line = l == 0 ? lineS_[m] : l == 1 ? lineS_[m + 1] : l == 2 ? lineT_[n] : lineT_[n + 1];
                        if (c.along >= 4)
                        {
                            // (Along one of the square's sides, across
                            // another: one of its corners.)
                            const int l2 = c.along - 4;
                            x.kind = kGrid;
                            const int right = (l == 1 || l2 == 1) ? 1 : 0, top = (l == 3 || l2 == 3) ? 1 : 0;
                            x.which = static_cast<std::uint8_t>(right + 2 * top);
                            x.s = lineS_[m + right];
                            x.t = lineT_[n + top];
                        }
                        else
                        {
                            x.kind = kEdge;
                            x.which = c.along;
                            LampVertex v;
                            std::int32_t colour[3];
                            crossing(p, c.along, onS, line, v, colour);
                            x.s = v.s;
                            x.t = v.t;
                        }
                        made[k++] = x;
                    }
                    if (dIn)
                        made[k++] = d;
                    if (k > 10)
                        break;
                }
                count = k;
                Cut *swap = in;
                in = made;
                made = swap;
            }
            // Points the same as the one before: one (a grid corner over a
            // crossing that falls on it).
            int k = 0;
            for (int i = 0; i < count; ++i)
            {
                const Cut &c = in[i];
                if (k > 0 && out[k - 1].s == c.s && out[k - 1].t == c.t)
                {
                    if (c.kind == kGrid)
                    {
                        out[k - 1].kind = kGrid;
                        out[k - 1].which = c.which;
                    }
                    out[k - 1].along = c.along;
                    continue;
                }
                out[k++] = c;
            }
            while (k > 1 && out[k - 1].s == out[0].s && out[k - 1].t == out[0].t)
            {
                if (out[k - 1].kind == kGrid)
                {
                    out[0].kind = kGrid;
                    out[0].which = out[k - 1].which;
                }
                --k;
            }
            // A crossing that falls exactly on a grid corner is that corner.
            for (int i = 0; i < k; ++i)
            {
                Cut &c = out[i];
                if (c.kind != kEdge)
                    continue;
                for (int q = 0; q < 4; ++q)
                {
                    if (c.s == lineS_[m + (q & 1)] && c.t == lineT_[n + (q >> 1)])
                    {
                        c.kind = kGrid;
                        c.which = static_cast<std::uint8_t>(q);
                    }
                }
            }
            return k;
        }

        /// Twice the signed area of the piece `c` (n corners).
        static std::int64_t areaOf(const Cut *c, int n)
        {
            std::int64_t area = 0;
            for (int i = 0; i < n; ++i)
            {
                const Cut &x = c[i], &y = c[ring(i + 1, n)];
                area += static_cast<std::int64_t>(x.s) * y.t - static_cast<std::int64_t>(y.s) * x.t;
            }
            return area;
        }

        /// The corners of other polygons in the middle of the piece's
        /// sides that run along a grid line: put in, in order. Returns the new
        /// count (`c` has 16 of room).
        int addCornersOnSides(Cut *c, int n) const
        {
            if (cornerCount_ == 0)
                return n;
            Cut out[16];
            int k = 0;
            for (int i = 0; i < n && k < 16; ++i)
            {
                out[k++] = c[i];
                const Cut &x = c[i], &y = c[ring(i + 1, n)];
                // (A side along the square's, or along the polygon's own --
                // where its edge lies on a grid line, the faces next to it
                // may have a corner in the middle of it.)
                const bool onS = x.s == y.s;
                if (!onS && x.t != y.t)
                    continue;
                // Those strictly between x and y, nearest x first.
                std::int32_t from = onS ? x.t : x.s, to = onS ? y.t : y.s;
                const int way = to > from ? 1 : -1;
                for (;;)
                {
                    int best = -1;
                    std::int32_t bestAt = 0;
                    for (int j = 0; j < cornerCount_; ++j)
                    {
                        const LampGridCorner &q = corners_[j];
                        if (onS ? q.s != x.s : q.t != x.t)
                            continue;
                        const std::int32_t at = onS ? q.t : q.s;
                        if ((at - from) * way <= 0 || (to - at) * way <= 0)
                            continue;
                        if (best < 0 || (at - bestAt) * way < 0)
                        {
                            best = j;
                            bestAt = at;
                        }
                    }
                    if (best < 0 || k >= 16)
                        break;
                    out[k++] = Cut{corners_[best].s, corners_[best].t, kInserted, static_cast<std::uint8_t>(best), x.along};
                    from = bestAt;
                }
            }
            for (int i = 0; i < k; ++i)
                c[i] = out[i];
            return k;
        }

        /// Whether any kept corner lies strictly inside one of the square
        /// (m, n)'s sides.
        bool cornerOnSquare(int m, int n) const
        {
            for (int j = 0; j < cornerCount_; ++j)
            {
                const LampGridCorner &q = corners_[j];
                const bool onV = (q.s == lineS_[m] || q.s == lineS_[m + 1]) && q.t > lineT_[n] && q.t < lineT_[n + 1];
                const bool onH = (q.t == lineT_[n] || q.t == lineT_[n + 1]) && q.s > lineS_[m] && q.s < lineS_[m + 1];
                if (onV || onH)
                    return true;
            }
            return false;
        }

        /// The vertex for the piece's corner `c` of the square (m, n) on `p`.
        void vertexOf(const Polygon &p, int m, int n, const Cut &c, LampVertex &v, const LampVertex *&use)
        {
            if (c.kind == kGrid)
            {
                use = &gridPoint(p, m + (c.which & 1), n + (c.which >> 1));
                return;
            }
            std::int32_t colour[3];
            if (c.kind == kCorner || c.kind == kInserted)
            {
                const LampGridCorner &q = c.kind == kCorner ? p.corner[c.which] : corners_[c.which];
                for (int k = 0; k < 3; ++k)
                    v.pos[k] = q.pos[k];
                v.s = c.s;
                v.t = c.t;
                for (int ch = 0; ch < 3; ++ch)
                    colour[ch] = ((q.colour >> (5 * ch)) & 31) * 256;
            }
            else
            {
                const bool onS = c.s == lineS_[m] || c.s == lineS_[m + 1];
                crossing(p, c.which, onS, onS ? c.s : c.t, v, colour);
            }
            v.colour = lit(colour, addInside(m, n, v.s, v.t));
            use = &v;
        }

        /// Whether the turn at corner i of `c` (n corners) is a true one --
        /// not three in a line.
        static bool turns(const Cut *c, int n, int i)
        {
            const Cut &x = c[ring(i + n - 1, n)], &y = c[i], &z = c[ring(i + 1, n)];
            return static_cast<std::int64_t>(y.s - x.s) * (z.t - x.t) - static_cast<std::int64_t>(y.t - x.t) * (z.s - x.s) != 0;
        }

        /// Send the piece `c` (n corners) of the square (m, n), cut from `p`.
        template <class Out>
        bool sendPiece(const Polygon &p, int m, int n, Cut *c, int count, Out &out)
        {
            LampVertex made[17];
            const LampVertex *v[17];
            for (int i = 0; i < count; ++i)
                vertexOf(p, m, n, c[i], made[i], v[i]);
            if (count <= 4)
            {
                bool flat = false;
                for (int i = 0; i < count && !flat; ++i)
                    flat = !turns(c, count, i);
                // (A quad as it is sent, rounded to the quarter unit, must
                // not be one the hardware draws wrongly: else triangles.)
                if (count == 3 || (!flat && quadSafe(v)))
                    return out.polygon(v, count, p.ds, p.dt);
            }
            // More (or a quad with a corner in line): a fan, from a corner
            // whose own turn and both neighbours' are true ones -- then no
            // triangle of it has no area, and every point on its sides is a
            // corner of one.
            int from = -1;
            for (int i = 0; i < count && from < 0; ++i)
            {
                if (turns(c, count, i) && turns(c, count, ring(i + 1, count)) && turns(c, count, ring(i + count - 1, count)))
                    from = i;
            }
            if (from >= 0)
            {
                for (int i = 1; i + 1 < count; ++i)
                {
                    const LampVertex *tri[3] = {v[from], v[ring(from + i, count)], v[ring(from + i + 1, count)]};
                    if (!out.polygon(tri, 3, p.ds, p.dt))
                        return false;
                }
                return true;
            }
            // None: from the middle of it.
            LampVertex &middle = made[16];
            std::int64_t ss = 0, tt = 0;
            for (int i = 0; i < count; ++i)
            {
                ss += c[i].s;
                tt += c[i].t;
            }
            middle.s = static_cast<std::int32_t>(ss / count);
            middle.t = static_cast<std::int32_t>(tt / count);
            const std::int32_t ds = middle.s - p.corner[0].s, dt = middle.t - p.corner[0].t;
            for (int k = 0; k < 3; ++k)
                middle.pos[k] = p.corner[0].pos[k] + static_cast<fx>((static_cast<std::int64_t>(ds) * p.a[k] +
                                                                      static_cast<std::int64_t>(dt) * p.b[k]) >> 12);
            std::int32_t colour[3];
            colourAt(p, middle.s, middle.t, colour);
            middle.colour = lit(colour, addInside(m, n, middle.s, middle.t));
            for (int i = 0; i < count; ++i)
            {
                const LampVertex *tri[3] = {&middle, v[i], v[ring(i + 1, count)]};
                if (!out.polygon(tri, 3, p.ds, p.dt))
                    return false;
            }
            return true;
        }

        /// The light on the polygon `p`: its whole squares in strips, row by
        /// row, and the squares its sides cross cut to it.
        template <class Out>
        bool lightPolygon(const Polygon &p, Out &out, LampGridCounts &counts)
        {
            // The squares its box covers.
            int m0 = 0, m1 = columns_ - 2, n0 = 0, n1 = rows_ - 2;
            while (m0 <= m1 && lineS_[m0 + 1] <= p.lo[0])
                ++m0;
            while (m1 >= m0 && lineS_[m1] >= p.hi[0])
                --m1;
            while (n0 <= n1 && lineT_[n0 + 1] <= p.lo[1])
                ++n0;
            while (n1 >= n0 && lineT_[n1] >= p.hi[1])
                --n1;
            // Which crossings are on it (1) or not (0).
            std::uint8_t on[kLines][kLines];
            for (int m = m0; m <= m1 + 1; ++m)
            {
                for (int n = n0; n <= n1 + 1; ++n)
                    on[m][n] = inside(p, lineS_[m], lineT_[n]) ? 1 : 0;
            }
            for (int n = n0; n <= n1; ++n)
            {
                int run = -1; // the first square of a run of whole ones
                auto flush = [&](int end) {
                    if (run < 0)
                        return true;
                    const LampVertex *v[2 * kLines];
                    int k = 0;
                    for (int m = run; m <= end; ++m)
                    {
                        v[k++] = &gridPoint(p, m, n + 1);
                        v[k++] = &gridPoint(p, m, n);
                    }
                    counts.cells += end - run;
                    run = -1;
                    return out.strip(v, k, p.ds, p.dt);
                };
                for (int m = m0; m <= m1; ++m)
                {
                    const bool lit = add_[m][n] > 0 || add_[m + 1][n] > 0 || add_[m][n + 1] > 0 || add_[m + 1][n + 1] > 0;
                    if (!lit)
                    {
                        if (!flush(m))
                            return false;
                        continue;
                    }
                    const bool whole = on[m][n] && on[m + 1][n] && on[m][n + 1] && on[m + 1][n + 1];
                    if (whole && !cornerOnSquare(m, n))
                    {
                        if (run < 0)
                            run = m;
                        continue;
                    }
                    if (!flush(m))
                        return false;
                    Cut c[16];
                    int count = cutToSquare(p, m, n, c);
                    if (count < 3 || areaOf(c, count) == 0)
                        continue;
                    count = addCornersOnSides(c, count);
                    ++counts.pieces;
                    if (!sendPiece(p, m, n, c, count, out))
                        return false;
                }
                if (!flush(m1 + 1))
                    return false;
            }
            return true;
        }

        // ---- The surface's -----------------------------------------------
        fx impact_[3] = {0, 0, 0};
        fx reach_[3] = {0, 0, 0};
        int drop_ = 0;
        std::int32_t impactS_ = 0, impactT_ = 0; ///< sixteenths of a stream unit
        std::int32_t rad16_ = 0, gain_ = 0;
        int width_ = 1, height_ = 1, sourceWidth_ = 1, sourceHeight_ = 1, widthShift_ = 0, heightShift_ = 0, stepShift_ = 8;
        int columns_ = 0, rows_ = 0;
        std::int32_t lineS_[kLines], lineT_[kLines];
        std::int16_t add_[kLines][kLines];
        bool made_[kLines][kLines];
        LampVertex grid_[kLines][kLines];
        LampGridCorner corners_[kCorners];
        int cornerCount_ = 0;
    };
    /// How much nearer the eye (of 4096) the lamp's light is drawn over its
    /// wall, solid, by the ordinary depth test: as much as the depth changes
    /// across one pixel there, the wall seen at `cosine` (4096ths: square on
    /// is 4096), and 8/4096 of the distance more, for the rounding. So its
    /// wall is under it whichever the hardware draws first, and what stands
    /// in front of the wall farther than that is not (tools/hwtest/lamppatch,
    /// P5 "s1px": the wall whole, a panel 4 units in front untouched down
    /// to 20 degrees, on a DSi as in melonDS). The "same depth" test cannot
    /// do this: by W it passes 8 units either side of the wall (P4).
    inline int lampPull(int cosine)
    {
        const int c = cosine < 82 ? 82 : (cosine > 4096 ? 4096 : cosine);
        const int sine = static_cast<int>(isqrt64(static_cast<std::uint64_t>(4096 * 4096 - c * c)));
        const int tangent = sine * 4096 / c;
        const int pull = 8 + tangent / 128;
        return pull > 2048 ? 2048 : pull;
    }
} // namespace hldata
