#include "hlds/LightRefine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "HlFixed.hpp"

namespace hlds
{
    namespace
    {
        constexpr double kSpacing = 16.0; ///< texels between lightmap samples
        /// How near an edge (texels) a point inside may be: further in than
        /// that, it makes no sliver of a triangle.
        constexpr double kMargin = 12.0;

        double luma(double r, double g, double b) { return (r * 77 + g * 150 + b * 29) / 256.0; }
        double luma(const Rgb &c) { return luma(c.r, c.g, c.b); }
        /// What the console draws of a 5-bit colour, in 8-bit units.
        double expand5(int v) { return v * 255.0 / 31.0; }
        double luma555(std::uint16_t c) { return luma(expand5(c & 31), expand5((c >> 5) & 31), expand5((c >> 10) & 31)); }

        /// Weights of (s, t) in the triangle a, b, c of texel coordinates;
        /// false if the triangle has no area.
        bool weights(const FacePoint &a, const FacePoint &b, const FacePoint &c, double s, double t, double &wa, double &wb,
                     double &wc)
        {
            const double den = (b.t - c.t) * (a.s - c.s) + (c.s - b.s) * (a.t - c.t);
            if (std::fabs(den) < 1e-9)
                return false;
            wa = ((b.t - c.t) * (s - c.s) + (c.s - b.s) * (t - c.t)) / den;
            wb = ((c.t - a.t) * (s - c.s) + (a.s - c.s) * (t - c.t)) / den;
            wc = 1 - wa - wb;
            return true;
        }

        /// Where (s, t) falls in the face's fan: the triangle and its weights.
        bool inFan(const std::vector<FacePoint> &p, double s, double t, int &k, double &wa, double &wb, double &wc)
        {
            for (k = 1; k + 1 < static_cast<int>(p.size()); ++k)
            {
                if (!weights(p[0], p[static_cast<std::size_t>(k)], p[static_cast<std::size_t>(k + 1)], s, t, wa, wb, wc))
                    continue;
                if (wa >= -1e-6 && wb >= -1e-6 && wc >= -1e-6)
                    return true;
            }
            return false;
        }

        /// A BSP face's corners in texels.
        std::vector<std::array<double, 2>> outline(const BspFile &bsp, const LightBake &light, const BspFace &face)
        {
            std::vector<std::array<double, 2>> out(static_cast<std::size_t>(face.edgeCount));
            for (int i = 0; i < face.edgeCount; ++i)
                light.texCoords(face, i, out[static_cast<std::size_t>(i)][0], out[static_cast<std::size_t>(i)][1]);
            (void)bsp;
            return out;
        }

        bool insideOutline(const std::vector<std::array<double, 2>> &o, double s, double t)
        {
            // (Convex: on one side of every edge.)
            int sign = 0;
            for (std::size_t i = 0; i < o.size(); ++i)
            {
                const auto &a = o[i], &b = o[(i + 1) % o.size()];
                const double cross = (b[0] - a[0]) * (t - a[1]) - (b[1] - a[1]) * (s - a[0]);
                if (std::fabs(cross) < 1e-6)
                    continue;
                const int now = cross > 0 ? 1 : -1;
                if (sign != 0 && now != sign)
                    return false;
                sign = now;
            }
            return true;
        }
    } // namespace

    double cornerLightError(const MeshFace &face, const BspFile &bsp, const LightBake &light, const double *origin)
    {
        double sum = 0;
        int count = 0;
        for (const int f : face.sources)
        {
            const BspFace &src = bsp.faces[static_cast<std::size_t>(f)];
            if (src.lightOfs < 0 || (bsp.texInfos[static_cast<std::size_t>(src.texInfo)].flags & kTexSpecial))
                continue;
            const std::vector<std::array<double, 2>> o = outline(bsp, light, src);
            const LightExtents e = light.extents(src);
            for (int j = 0; j < e.height; ++j)
                for (int i = 0; i < e.width; ++i)
                {
                    const double ps = (e.minS + i) * kSpacing, pt = (e.minT + j) * kSpacing;
                    int k;
                    double wa, wb, wc;
                    if (!insideOutline(o, ps, pt) || !inFan(face.points, ps, pt, k, wa, wb, wc) ||
                        light.sampleInSolid(src, ps, pt, origin))
                        continue;
                    const double drawn = wa * luma555(face.points[0].colour) +
                                         wb * luma555(face.points[static_cast<std::size_t>(k)].colour) +
                                         wc * luma555(face.points[static_cast<std::size_t>(k + 1)].colour);
                    sum += std::fabs(drawn - luma(light.sample(src, ps, pt)));
                    ++count;
                }
        }
        return count > 0 ? sum / count : 0;
    }

    std::vector<MeshFace> refineLight(const MeshFace &face, const BspFile &bsp, const LightBake &light,
                                      const double *origin, const LightRefine &how)
    {
        if (face.points.size() < 3 || cornerLightError(face, bsp, light, origin) <= how.limit)
            return {};
        const std::size_t corners = face.points.size();
        std::vector<std::pair<const BspFace *, std::vector<std::array<double, 2>>>> sources;
        for (const int f : face.sources)
            sources.push_back({&bsp.faces[static_cast<std::size_t>(f)], outline(bsp, light, bsp.faces[static_cast<std::size_t>(f)])});
        // Where a point may go: each lightmap sample inside the face, seen,
        // clear of its edges -- lit as the lightmap has it there.
        auto candidate = [&](double s, double t, FacePoint &p) -> bool
        {
            for (std::size_t i = 0; i < corners; ++i)
            {
                const FacePoint &a = face.points[i], &b = face.points[(i + 1) % corners];
                const double ds = b.s - a.s, dt = b.t - a.t, len = std::sqrt(ds * ds + dt * dt);
                if (len >= 1e-6 && std::fabs(ds * (t - a.t) - dt * (s - a.s)) / len < kMargin)
                    return false;
            }
            int k;
            double wa, wb, wc;
            if (!inFan(face.points, s, t, k, wa, wb, wc))
                return false;
            const BspFace *src = nullptr;
            for (const auto &[maybe, o] : sources)
                if (src == nullptr && insideOutline(o, s, t))
                    src = maybe;
            if (src == nullptr || light.sampleInSolid(*src, s, t, origin))
                return false;
            const FacePoint &a = face.points[0], &b = face.points[static_cast<std::size_t>(k)],
                            &c = face.points[static_cast<std::size_t>(k + 1)];
            for (int q = 0; q < 3; ++q)
                p.pos[q] = wa * a.pos[q] + wb * b.pos[q] + wc * c.pos[q];
            p.s = s;
            p.t = t;
            const Rgb lit = light.sample(*src, s, t, origin);
            p.colour = hldata::rgb555Near(lit.r, lit.g, lit.b);
            return true;
        };
        double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
        for (const FacePoint &p : face.points)
        {
            lo[0] = std::min(lo[0], p.s);
            lo[1] = std::min(lo[1], p.t);
            hi[0] = std::max(hi[0], p.s);
            hi[1] = std::max(hi[1], p.t);
        }
        const double cell = kSpacing * (how.greedy ? 1 : how.step);
        std::vector<FacePoint> pool;
        for (double t = std::ceil(lo[1] / cell) * cell; t <= hi[1]; t += cell)
            for (double s = std::ceil(lo[0] / cell) * cell; s <= hi[0]; s += cell)
            {
                FacePoint p{};
                if (candidate(s, t, p))
                    pool.push_back(p);
            }
        if (pool.empty())
            return {};

        // Triangles: the face's fan, then each point put in and the edges
        // about it turned while that makes them nearer Delaunay's. An edge
        // of the face itself has one triangle only, so it is never turned.
        std::vector<FacePoint> points = face.points;
        using Tri = std::array<int, 3>;
        std::vector<Tri> tris;
        for (std::size_t k = 1; k + 1 < corners; ++k)
            tris.push_back({0, static_cast<int>(k), static_cast<int>(k + 1)});
        auto area2 = [&](int a, int b, int c)
        {
            const FacePoint &A = points[static_cast<std::size_t>(a)], &B = points[static_cast<std::size_t>(b)],
                            &C = points[static_cast<std::size_t>(c)];
            return (B.s - A.s) * (C.t - A.t) - (B.t - A.t) * (C.s - A.s);
        };
        double orient = 0;
        for (const Tri &tr : tris)
            orient += area2(tr[0], tr[1], tr[2]);
        const double sign = orient >= 0 ? 1 : -1;
        auto inCircle = [&](const Tri &tr, int d)
        {
            const FacePoint &A = points[static_cast<std::size_t>(tr[0])], &B = points[static_cast<std::size_t>(tr[1])],
                            &C = points[static_cast<std::size_t>(tr[2])], &D = points[static_cast<std::size_t>(d)];
            const double ax = A.s - D.s, ay = A.t - D.t, bx = B.s - D.s, by = B.t - D.t, cx = C.s - D.s, cy = C.t - D.t;
            const double det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) +
                               (cx * cx + cy * cy) * (ax * by - bx * ay);
            return det * sign > 1e-9;
        };
        auto across = [&](std::size_t self, int a, int b, int &third) -> int
        {
            for (std::size_t i = 0; i < tris.size(); ++i)
            {
                if (i == self)
                    continue;
                const Tri &o = tris[i];
                for (int e = 0; e < 3; ++e)
                    if (o[static_cast<std::size_t>(e)] == b && o[static_cast<std::size_t>((e + 1) % 3)] == a)
                    {
                        third = o[static_cast<std::size_t>((e + 2) % 3)];
                        return static_cast<int>(i);
                    }
            }
            return -1;
        };
        // The triangle (s, t) is strictly in, and its weights; -1 if none.
        auto locate = [&](double s, double t, double &wa, double &wb, double &wc) -> int
        {
            for (std::size_t i = 0; i < tris.size(); ++i)
            {
                const Tri &tr = tris[i];
                if (weights(points[static_cast<std::size_t>(tr[0])], points[static_cast<std::size_t>(tr[1])],
                            points[static_cast<std::size_t>(tr[2])], s, t, wa, wb, wc) &&
                    wa > 1e-3 && wb > 1e-3 && wc > 1e-3)
                    return static_cast<int>(i);
            }
            return -1;
        };
        auto insert = [&](const FacePoint &point) -> bool
        {
            double wa, wb, wc;
            const int in = locate(point.s, point.t, wa, wb, wc);
            if (in < 0)
                return false;
            points.push_back(point);
            const int p = static_cast<int>(points.size() - 1);
            const Tri was = tris[static_cast<std::size_t>(in)];
            tris[static_cast<std::size_t>(in)] = {p, was[0], was[1]};
            tris.push_back({p, was[1], was[2]});
            tris.push_back({p, was[2], was[0]});
            std::vector<std::pair<int, int>> edges = {{was[0], was[1]}, {was[1], was[2]}, {was[2], was[0]}};
            int guard = 0;
            while (!edges.empty() && ++guard < 4096)
            {
                const auto [a, b] = edges.back();
                edges.pop_back();
                std::size_t self = tris.size();
                for (std::size_t i = 0; i < tris.size(); ++i)
                    if (tris[i] == Tri{p, a, b})
                        self = i;
                if (self == tris.size())
                    continue;
                int d = -1;
                const int other = across(self, a, b, d);
                if (other < 0 || !inCircle(tris[self], d))
                    continue;
                // (Turned only if both keep the face's way round.)
                if (area2(p, a, d) * sign <= 1e-9 || area2(p, d, b) * sign <= 1e-9)
                    continue;
                tris[self] = {p, a, d};
                tris[static_cast<std::size_t>(other)] = {p, d, b};
                edges.push_back({a, d});
                edges.push_back({d, b});
            }
            return true;
        };

        if (!how.greedy)
        {
            // Every one of the grid's points.
            for (const FacePoint &point : pool)
                insert(point);
        }
        else
        {
            // Each point where the face, as it is lit so far, is furthest
            // from its lightmap -- the middle of a pool of light, the edge of
            // a shadow -- until nowhere is far, or the face has its most.
            std::vector<char> used(pool.size(), 0);
            for (int added = 0; added < how.most; ++added)
            {
                int best = -1;
                double worst = how.stopAt;
                for (std::size_t c = 0; c < pool.size(); ++c)
                {
                    if (used[c])
                        continue;
                    double wa, wb, wc;
                    const int in = locate(pool[c].s, pool[c].t, wa, wb, wc);
                    if (in < 0)
                    {
                        used[c] = 1; // (on an edge now: never a point)
                        continue;
                    }
                    const Tri &tr = tris[static_cast<std::size_t>(in)];
                    const double drawn = wa * luma555(points[static_cast<std::size_t>(tr[0])].colour) +
                                         wb * luma555(points[static_cast<std::size_t>(tr[1])].colour) +
                                         wc * luma555(points[static_cast<std::size_t>(tr[2])].colour);
                    const double off = std::fabs(drawn - luma555(pool[c].colour));
                    if (off > worst)
                    {
                        worst = off;
                        best = static_cast<int>(c);
                    }
                }
                if (best < 0)
                    break;
                used[static_cast<std::size_t>(best)] = 1;
                insert(pool[static_cast<std::size_t>(best)]);
            }
        }
        if (points.size() == corners)
            return {};
        std::vector<MeshFace> out;
        for (const Tri &tr : tris)
        {
            if (area2(tr[0], tr[1], tr[2]) * sign <= 1e-9)
                continue;
            MeshFace piece;
            piece.key = face.key;
            for (int q = 0; q < 3; ++q)
                piece.normal[q] = face.normal[q];
            piece.sources = face.sources;
            for (const int i : tr)
                piece.points.push_back(points[static_cast<std::size_t>(i)]);
            out.push_back(std::move(piece));
        }
        return out;
    }
} // namespace hlds
