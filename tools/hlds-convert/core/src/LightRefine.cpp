#include "hlds/LightRefine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>

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

    namespace
    {
        /// A point of a piece, as it is cut.
        struct Spot
        {
            FacePoint p;
        };

        /// `poly` (convex, in s/t) cut to one side of the line `axis` =
        /// `at`: the side above it when `above`.
        std::vector<Spot> clipTo(const std::vector<Spot> &poly, int axis, double at, bool above)
        {
            auto value = [&](const Spot &x) { return axis == 0 ? x.p.s : x.p.t; };
            auto keep = [&](const Spot &x) { return above ? value(x) >= at - 1e-9 : value(x) <= at + 1e-9; };
            std::vector<Spot> out;
            for (std::size_t i = 0; i < poly.size(); ++i)
            {
                const Spot &a = poly[i], &b = poly[(i + 1) % poly.size()];
                if (keep(a))
                    out.push_back(a);
                const double va = value(a) - at, vb = value(b) - at;
                if ((va < -1e-9 && vb > 1e-9) || (va > 1e-9 && vb < -1e-9))
                {
                    const double u = va / (va - vb);
                    Spot m;
                    for (int q = 0; q < 3; ++q)
                        m.p.pos[q] = a.p.pos[q] + (b.p.pos[q] - a.p.pos[q]) * u;
                    m.p.s = a.p.s + (b.p.s - a.p.s) * u;
                    m.p.t = a.p.t + (b.p.t - a.p.t) * u;
                    // (Exactly on the line: no rounding off it.)
                    (axis == 0 ? m.p.s : m.p.t) = at;
                    m.p.colour = 0;
                    out.push_back(m);
                }
            }
            // (Points that fell together: one.)
            std::vector<Spot> kept;
            for (const Spot &x : out)
                if (kept.empty() || std::fabs(kept.back().p.s - x.p.s) + std::fabs(kept.back().p.t - x.p.t) > 1e-6)
                    kept.push_back(x);
            while (kept.size() > 1 &&
                   std::fabs(kept.back().p.s - kept.front().p.s) + std::fabs(kept.back().p.t - kept.front().p.t) <= 1e-6)
                kept.pop_back();
            return kept;
        }

        /// The colour of a 5-bit channel `shift` blended from `a` to `b`.
        std::uint16_t blend555(std::uint16_t a, std::uint16_t b, double u)
        {
            std::uint16_t out = 0;
            for (const int shift : {0, 5, 10})
            {
                const double v = ((a >> shift) & 31) * (1 - u) + ((b >> shift) & 31) * u;
                out = static_cast<std::uint16_t>(out | (std::clamp(static_cast<int>(std::lround(v)), 0, 31) << shift));
            }
            return out;
        }

        /// A piece laid over a face, as it is drawn.
        struct Piece
        {
            std::vector<FacePoint> points;
            bool whole = false; ///< the whole square: blended across it both ways
            double s0 = 0, t0 = 0;
        };
    } // namespace

    LightOverlay lightOverlay(const std::vector<const MeshFace *> &members, const BspFile &bsp,
                              const LightBake &light, const double *origin, const LightRefine &how)
    {
        // Each face's outline in s/t, its corners' colours as its run has them.
        auto outlineColour = [](const MeshFace &face, double s, double t, std::uint16_t &out)
        {
            const std::size_t n = face.points.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                const FacePoint &a = face.points[i], &b = face.points[(i + 1) % n];
                const double ds = b.s - a.s, dt = b.t - a.t, len2 = ds * ds + dt * dt;
                if (len2 < 1e-12)
                    continue;
                const double u = ((s - a.s) * ds + (t - a.t) * dt) / len2;
                const double off = std::fabs((s - a.s) * dt - (t - a.t) * ds) / std::sqrt(len2);
                if (u >= -1e-6 && u <= 1 + 1e-6 && off <= 1e-4)
                {
                    out = blend555(a.colour, b.colour, std::clamp(u, 0.0, 1.0));
                    return true;
                }
            }
            return false;
        };

        // A face's pieces for squares of `size` texels.
        auto piecesOf = [&](const MeshFace &face, double size, std::vector<Piece> &out)
        {
            std::vector<std::pair<const BspFace *, std::vector<std::array<double, 2>>>> sources;
            for (const int f : face.sources)
                sources.push_back({&bsp.faces[static_cast<std::size_t>(f)], outline(bsp, light, bsp.faces[static_cast<std::size_t>(f)])});
            double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
            std::vector<Spot> whole;
            for (const FacePoint &p : face.points)
            {
                lo[0] = std::min(lo[0], p.s);
                lo[1] = std::min(lo[1], p.t);
                hi[0] = std::max(hi[0], p.s);
                hi[1] = std::max(hi[1], p.t);
                whole.push_back({p});
            }
            for (double t0 = std::floor(lo[1] / size) * size; t0 < hi[1] - 1e-9; t0 += size)
                for (double s0 = std::floor(lo[0] / size) * size; s0 < hi[0] - 1e-9; s0 += size)
                {
                    std::vector<Spot> poly = clipTo(whole, 0, s0, true);
                    poly = clipTo(poly, 0, s0 + size, false);
                    poly = clipTo(poly, 1, t0, true);
                    poly = clipTo(poly, 1, t0 + size, false);
                    if (poly.size() < 3)
                        continue;
                    double area = 0;
                    for (std::size_t k = 1; k + 1 < poly.size(); ++k)
                        area += (poly[k].p.s - poly[0].p.s) * (poly[k + 1].p.t - poly[0].p.t) -
                                (poly[k + 1].p.s - poly[0].p.s) * (poly[k].p.t - poly[0].p.t);
                    if (std::fabs(area) / 2 < 0.05)
                        continue; // (a sliver: what it would cover the wall shows)
                    Piece piece;
                    piece.s0 = s0;
                    piece.t0 = t0;
                    bool square = poly.size() == 4;
                    for (Spot &x : poly)
                    {
                        square = square && (x.p.s == s0 || x.p.s == s0 + size) && (x.p.t == t0 || x.p.t == t0 + size);
                        std::uint16_t colour = 0;
                        // On the outline: as the face's own run has it there.
                        if (outlineColour(face, x.p.s, x.p.t, colour))
                            x.p.colour = colour;
                        else
                        {
                            // Inside: the lightmap there, or the run's own light
                            // where the lightmap has only solid to say.
                            const BspFace *src = nullptr;
                            for (const auto &[maybe, o] : sources)
                                if (insideOutline(o, x.p.s, x.p.t))
                                {
                                    src = maybe;
                                    break;
                                }
                            int k;
                            double wa, wb, wc;
                            if (src != nullptr && !light.sampleInSolid(*src, x.p.s, x.p.t, origin))
                            {
                                const Rgb lit = light.sample(*src, x.p.s, x.p.t, origin);
                                x.p.colour = hldata::rgb555Near(lit.r, lit.g, lit.b);
                            }
                            else if (inFan(face.points, x.p.s, x.p.t, k, wa, wb, wc))
                            {
                                const std::uint16_t ab = blend555(face.points[0].colour,
                                                                  face.points[static_cast<std::size_t>(k)].colour,
                                                                  wb / std::max(1e-9, wa + wb));
                                x.p.colour = blend555(ab, face.points[static_cast<std::size_t>(k + 1)].colour, wc);
                            }
                        }
                        piece.points.push_back(x.p);
                    }
                    piece.whole = square;
                    out.push_back(std::move(piece));
                }
        };

        // How far a set of pieces is from the lightmap: each sample of the
        // faces, read in the piece it falls in (a square blended both ways,
        // as the hardware draws a quad; a cut piece by its triangles).
        auto drawnIn = [](const Piece &piece, double size, double s, double t, double &out)
        {
            if (piece.whole)
            {
                const double u = (s - piece.s0) / size, w = (t - piece.t0) / size;
                if (u < -1e-9 || w < -1e-9 || u > 1 + 1e-9 || w > 1 + 1e-9)
                    return false;
                double v[4] = {0, 0, 0, 0}; // (s0,t0) (s1,t0) (s1,t1) (s0,t1)
                for (const FacePoint &p : piece.points)
                    v[p.t > piece.t0 ? (p.s > piece.s0 ? 2 : 3) : (p.s > piece.s0 ? 1 : 0)] = luma555(p.colour);
                out = (1 - w) * ((1 - u) * v[0] + u * v[1]) + w * ((1 - u) * v[3] + u * v[2]);
                return true;
            }
            int k;
            double wa, wb, wc;
            if (!inFan(piece.points, s, t, k, wa, wb, wc))
                return false;
            out = wa * luma555(piece.points[0].colour) + wb * luma555(piece.points[static_cast<std::size_t>(k)].colour) +
                  wc * luma555(piece.points[static_cast<std::size_t>(k + 1)].colour);
            return true;
        };

        LightOverlay best;
        bool any = false;
        for (const double size : {4 * kSpacing, 2 * kSpacing, kSpacing})
        {
            std::vector<std::vector<Piece>> pieces(members.size());
            int polygons = 0;
            for (std::size_t m = 0; m < members.size(); ++m)
            {
                piecesOf(*members[m], size, pieces[m]);
                for (const Piece &piece : pieces[m])
                    polygons += static_cast<int>(piece.points.size() - 1) / 2; // (an n-gon: quads, and a triangle if odd)
            }
            if (any && polygons > how.mostPolygons)
                break;
            // Its error, over every sample of the faces that is seen.
            double sum = 0;
            int count = 0;
            for (std::size_t m = 0; m < members.size(); ++m)
            {
                const MeshFace &face = *members[m];
                std::map<std::pair<long, long>, std::vector<std::size_t>> byCell;
                for (std::size_t i = 0; i < pieces[m].size(); ++i)
                    byCell[{std::lround(pieces[m][i].s0 / size), std::lround(pieces[m][i].t0 / size)}].push_back(i);
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
                            // (On a square's side or corner: any square about it.)
                            bool found = false;
                            double drawn = 0;
                            for (int dj = 0; dj < 2 && !found; ++dj)
                                for (int di = 0; di < 2 && !found; ++di)
                                {
                                    const auto cell = byCell.find({static_cast<long>(std::floor(ps / size)) - di,
                                                                   static_cast<long>(std::floor(pt / size)) - dj});
                                    if (cell == byCell.end())
                                        continue;
                                    for (const std::size_t at : cell->second)
                                        if (!found && drawnIn(pieces[m][at], size, ps, pt, drawn))
                                            found = true;
                                }
                            if (!found)
                                continue;
                            sum += std::fabs(drawn - luma(light.sample(src, ps, pt)));
                            ++count;
                        }
                }
            }
            LightOverlay trial;
            trial.size = size;
            trial.polygons = polygons;
            trial.error = count > 0 ? sum / count : 0;
            for (std::size_t m = 0; m < members.size(); ++m)
                for (Piece &piece : pieces[m])
                {
                    MeshFace out;
                    out.key = members[m]->key;
                    for (int q = 0; q < 3; ++q)
                        out.normal[q] = members[m]->normal[q];
                    out.sources = members[m]->sources;
                    out.points = std::move(piece.points);
                    trial.pieces.push_back(std::move(out));
                }
            if (trial.pieces.empty())
                continue;
            best = std::move(trial);
            any = true;
            if (best.error <= how.stopAt)
                break;
        }
        return best;
    }
} // namespace hlds
