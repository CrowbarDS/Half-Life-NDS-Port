#include "hlds/MeshOptimize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "HlFixed.hpp"

namespace hlds
{
    namespace
    {
        /// A position as the DS will have it: quarter units. Two vertices are
        /// "the same place" exactly when these are equal.
        using Key = std::array<std::int32_t, 3>;

        Key keyOf(const FacePoint &p)
        {
            return Key{static_cast<std::int32_t>(std::lround(p.pos[0] * 4)),
                       static_cast<std::int32_t>(std::lround(p.pos[1] * 4)),
                       static_cast<std::int32_t>(std::lround(p.pos[2] * 4))};
        }

        /// (b - a) x (c - b), in integers: zero exactly when b is on the line
        /// through a and c.
        std::array<std::int64_t, 3> turn(const Key &a, const Key &b, const Key &c)
        {
            const std::int64_t e1[3] = {std::int64_t(b[0]) - a[0], std::int64_t(b[1]) - a[1],
                                        std::int64_t(b[2]) - a[2]};
            const std::int64_t e2[3] = {std::int64_t(c[0]) - b[0], std::int64_t(c[1]) - b[1],
                                        std::int64_t(c[2]) - b[2]};
            return {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                    e1[0] * e2[1] - e1[1] * e2[0]};
        }

        bool isZero(const std::array<std::int64_t, 3> &v)
        {
            return v[0] == 0 && v[1] == 0 && v[2] == 0;
        }

        /// b strictly between a and c on one line.
        bool midEdge(const Key &a, const Key &b, const Key &c)
        {
            if (!isZero(turn(a, b, c)))
                return false;
            const std::int64_t d = (std::int64_t(b[0]) - a[0]) * (std::int64_t(c[0]) - b[0]) +
                                   (std::int64_t(b[1]) - a[1]) * (std::int64_t(c[1]) - b[1]) +
                                   (std::int64_t(b[2]) - a[2]) * (std::int64_t(c[2]) - b[2]);
            return d > 0;
        }

        /// One channel of a colour as the DS will show it, in 8-bit units.
        double channel(const FacePoint &p, int ch)
        {
            return ((p.colour >> (5 * ch)) & 31) * 8.0;
        }

        /// Vertex RAM a face of n sides costs once cut up: a quad or a quad
        /// strip is n; an odd face needs a triangle on the end, n + 2.
        int costOf(int n)
        {
            return n <= 4 || n % 2 == 0 ? n : n + 2;
        }

        double distance(const FacePoint &a, const FacePoint &b)
        {
            const double d[3] = {a.pos[0] - b.pos[0], a.pos[1] - b.pos[1], a.pos[2] - b.pos[2]};
            return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        }

        /// Is b's colour the blend of a's and c's, by distance, within the
        /// tolerance?
        bool colourIsBlend(const FacePoint &a, const FacePoint &b, const FacePoint &c)
        {
            const double ab = distance(a, b);
            const double ac = ab + distance(b, c);
            if (ac <= 0)
                return false;
            const double w = ab / ac;
            for (int ch = 0; ch < 3; ++ch)
            {
                const double expected = channel(a, ch) + (channel(c, ch) - channel(a, ch)) * w;
                if (std::fabs(expected - channel(b, ch)) > kColourTolerance)
                    return false;
            }
            return true;
        }

        /// Clockwise seen from `normal`'s side, straight angles allowed, and
        /// not a polygon with no area.
        bool isConvex(const std::vector<FacePoint> &points, const double *normal)
        {
            const int n = static_cast<int>(points.size());
            bool anyTurn = false;
            for (int i = 0; i < n; ++i)
            {
                const auto t = turn(keyOf(points[i]), keyOf(points[(i + 1) % n]),
                                    keyOf(points[(i + 2) % n]));
                const double along = t[0] * normal[0] + t[1] * normal[1] + t[2] * normal[2];
                // Clockwise from in front: every turn points AWAY from the
                // viewer, against the normal.
                if (along > 0)
                    return false;
                if (along < 0)
                    anyTurn = true;
            }
            return anyTurn;
        }

        /// Two axes in the face's plane.
        void planeAxes(const double *normal, double *u, double *v)
        {
            const int least = std::fabs(normal[0]) < std::fabs(normal[1])
                                  ? (std::fabs(normal[0]) < std::fabs(normal[2]) ? 0 : 2)
                                  : (std::fabs(normal[1]) < std::fabs(normal[2]) ? 1 : 2);
            double axis[3] = {0, 0, 0};
            axis[least] = 1;
            u[0] = normal[1] * axis[2] - normal[2] * axis[1];
            u[1] = normal[2] * axis[0] - normal[0] * axis[2];
            u[2] = normal[0] * axis[1] - normal[1] * axis[0];
            const double len = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
            for (int i = 0; i < 3; ++i)
                u[i] /= len;
            v[0] = normal[1] * u[2] - normal[2] * u[1];
            v[1] = normal[2] * u[0] - normal[0] * u[2];
            v[2] = normal[0] * u[1] - normal[1] * u[0];
        }

        /// Does one affine function of position fit every vertex's colour to
        /// within the tolerance? Least squares per channel.
        bool colourIsAffine(const std::vector<FacePoint> &corners,
                            const std::vector<FacePoint> &ghosts, const double *normal)
        {
            std::vector<FacePoint> points = corners;
            points.insert(points.end(), ghosts.begin(), ghosts.end());
            double u[3], v[3];
            planeAxes(normal, u, v);
            const int n = static_cast<int>(points.size());
            std::vector<double> x(n), y(n);
            double mx = 0, my = 0;
            for (int i = 0; i < n; ++i)
            {
                x[i] = points[i].pos[0] * u[0] + points[i].pos[1] * u[1] + points[i].pos[2] * u[2];
                y[i] = points[i].pos[0] * v[0] + points[i].pos[1] * v[1] + points[i].pos[2] * v[2];
                mx += x[i];
                my += y[i];
            }
            mx /= n;
            my /= n;
            double sxx = 0, sxy = 0, syy = 0;
            for (int i = 0; i < n; ++i)
            {
                x[i] -= mx;
                y[i] -= my;
                sxx += x[i] * x[i];
                sxy += x[i] * y[i];
                syy += y[i] * y[i];
            }
            const double det = sxx * syy - sxy * sxy;
            if (det < 1e-6)
                return false; // no area to fit over
            for (int ch = 0; ch < 3; ++ch)
            {
                double mean = 0, sxc = 0, syc = 0;
                for (int i = 0; i < n; ++i)
                    mean += channel(points[i], ch);
                mean /= n;
                for (int i = 0; i < n; ++i)
                {
                    sxc += x[i] * (channel(points[i], ch) - mean);
                    syc += y[i] * (channel(points[i], ch) - mean);
                }
                const double gx = (sxc * syy - syc * sxy) / det;
                const double gy = (syc * sxx - sxc * sxy) / det;
                for (int i = 0; i < n; ++i)
                {
                    const double fit = mean + gx * x[i] + gy * y[i];
                    if (std::fabs(fit - channel(points[i], ch)) > kColourTolerance / 2.0)
                        return false;
                }
            }
            return true;
        }

        bool withinExtent(const std::vector<FacePoint> &points)
        {
            for (int a = 0; a < 3; ++a)
            {
                double lo = 1e30, hi = -1e30;
                for (const FacePoint &p : points)
                {
                    lo = std::min(lo, p.pos[a]);
                    hi = std::max(hi, p.pos[a]);
                }
                if (hi - lo > kMaxMergedExtent)
                    return false;
            }
            return true;
        }

        /// The union of two faces across the edge a[i] -> a[i+1], which b has
        /// reversed at b[j] -> b[j+1]. Empty if the result is not a simple
        /// polygon (the faces touch along more than that one edge).
        std::vector<FacePoint> joinAcross(const std::vector<FacePoint> &a, int i,
                                          const std::vector<FacePoint> &b, int j)
        {
            const int na = static_cast<int>(a.size());
            const int nb = static_cast<int>(b.size());
            std::vector<FacePoint> out;
            // All of a, starting at the edge's far end and finishing at its
            // near end...
            for (int k = 1; k <= na; ++k)
                out.push_back(a[(i + k) % na]);
            // ...then b's vertices other than the two on the edge.
            for (int k = 2; k < nb; ++k)
                out.push_back(b[(j + k) % nb]);

            std::vector<Key> keys;
            for (const FacePoint &p : out)
                keys.push_back(keyOf(p));
            std::sort(keys.begin(), keys.end());
            if (std::adjacent_find(keys.begin(), keys.end()) != keys.end())
                return {};
            return out;
        }

        bool tryMerge(std::vector<MeshFace> &faces, std::size_t ia, std::size_t ib)
        {
            MeshFace &a = faces[ia];
            MeshFace &b = faces[ib];
            const int na = static_cast<int>(a.points.size());
            const int nb = static_cast<int>(b.points.size());
            for (int i = 0; i < na; ++i)
            {
                const FacePoint &p = a.points[i];
                const FacePoint &q = a.points[(i + 1) % na];
                for (int j = 0; j < nb; ++j)
                {
                    const FacePoint &bq = b.points[j];
                    const FacePoint &bp = b.points[(j + 1) % nb];
                    if (keyOf(q) != keyOf(bq) || keyOf(p) != keyOf(bp))
                        continue;
                    // The two faces must already agree about the light on
                    // the edge they share.
                    if (q.colour != bq.colour || p.colour != bp.colour)
                        return false;
                    std::vector<FacePoint> joined = joinAcross(a.points, i, b.points, j);
                    std::vector<FacePoint> ghosts = a.ghosts;
                    ghosts.insert(ghosts.end(), b.ghosts.begin(), b.ghosts.end());
                    if (joined.empty() || joined.size() > 48 || !withinExtent(joined) ||
                        !isConvex(joined, a.normal) || !colourIsAffine(joined, ghosts, a.normal))
                        return false;
                    a.ghosts = std::move(ghosts);

                    a.points = std::move(joined);
                    a.sources.insert(a.sources.end(), b.sources.begin(), b.sources.end());
                    b.points.clear();
                    return true;
                }
            }
            return false;
        }

        int mergePass(std::vector<MeshFace> &faces, const std::vector<char> &frozen)
        {
            // Candidates: faces of one key that share a position. Indexing by
            // position keeps this from being every pair of faces in the map.
            std::map<std::pair<int, Key>, std::vector<std::size_t>> at;
            for (std::size_t f = 0; f < faces.size(); ++f)
            {
                if (frozen[f])
                    continue;
                for (const FacePoint &p : faces[f].points)
                    at[{faces[f].key, keyOf(p)}].push_back(f);
            }
            int merges = 0;
            for (const auto &entry : at)
            {
                const std::vector<std::size_t> &here = entry.second;
                for (std::size_t x = 0; x < here.size(); ++x)
                {
                    for (std::size_t y = x + 1; y < here.size(); ++y)
                    {
                        const std::size_t ia = here[x];
                        const std::size_t ib = here[y];
                        if (ia == ib || faces[ia].points.empty() || faces[ib].points.empty())
                            continue;
                        if (tryMerge(faces, ia, ib))
                            ++merges;
                    }
                }
            }
            return merges;
        }

        int collinearPass(std::vector<MeshFace> &faces, const std::map<Key, char> &pinned)
        {
            struct Use
            {
                std::size_t face;
            };
            std::map<Key, std::vector<std::size_t>> users;
            for (std::size_t f = 0; f < faces.size(); ++f)
            {
                for (const FacePoint &p : faces[f].points)
                    users[keyOf(p)].push_back(f);
            }

            int removed = 0;
            for (const auto &entry : users)
            {
                const Key &key = entry.first;
                if (pinned.count(key) != 0)
                    continue;
                // Where this position sits in each face that has it, as those
                // faces are NOW: earlier removals have changed them.
                bool ok = true;
                std::vector<std::pair<std::size_t, int>> spots;
                Key ends[2];
                bool haveEnds = false;
                for (std::size_t f : entry.second)
                {
                    const std::vector<FacePoint> &pts = faces[f].points;
                    const int n = static_cast<int>(pts.size());
                    int at = -1;
                    for (int i = 0; i < n; ++i)
                    {
                        if (keyOf(pts[i]) == key)
                            at = i;
                    }
                    if (at < 0)
                        continue; // merged away since the index was built
                    const FacePoint &prev = pts[(at + n - 1) % n];
                    const FacePoint &next = pts[(at + 1) % n];
                    Key a = keyOf(prev);
                    Key c = keyOf(next);
                    // Mid-edge in THIS face, with light that is the blend of
                    // its neighbours', and a face left over afterwards.
                    if (n <= 3 || !midEdge(a, key, c) || !colourIsBlend(prev, pts[at], next))
                    {
                        ok = false;
                        break;
                    }
                    // Vertices dropped from this edge earlier lie on the
                    // longer edge this would make: it must still give each of
                    // them its own colour.
                    for (const FacePoint &ghost : faces[f].ghosts)
                    {
                        if (midEdge(a, keyOf(ghost), c) && !colourIsBlend(prev, ghost, next))
                            ok = false;
                    }
                    if (!ok)
                        break;
                    // And every face must see it between the SAME two points:
                    // otherwise dropping it leaves one face's longer edge
                    // passing through another face's vertex.
                    if (c < a)
                        std::swap(a, c);
                    if (!haveEnds)
                    {
                        ends[0] = a;
                        ends[1] = c;
                        haveEnds = true;
                    }
                    else if (ends[0] != a || ends[1] != c)
                    {
                        ok = false;
                        break;
                    }
                    spots.push_back({f, at});
                }
                if (!ok || spots.empty())
                    continue;
                for (const auto &spot : spots)
                {
                    std::vector<FacePoint> &pts = faces[spot.first].points;
                    faces[spot.first].ghosts.push_back(pts[spot.second]);
                    pts.erase(pts.begin() + spot.second);
                }
                ++removed;
            }
            return removed;
        }
    } // namespace

    void optimizeFaces(std::vector<MeshFace> &faces, OptimizeReport &report)
    {
        // Whether a merge pays cannot be judged one merge at a time. Four
        // squares round a point become one quad -- sixteen vertices to four --
        // but only by way of two five-sided faces, each of which is WORSE than
        // the pair of squares it came from. So: merge and thin as far as the
        // rules allow, THEN judge each result against what its original faces
        // would have cost as a strip. A result that did not come out ahead has
        // its originals frozen (and, for a face that merely lost a vertex and
        // became an expensive odd shape, its vertices pinned), and the whole
        // thing is run again from the map's own faces. A few rounds settle it.
        const std::vector<MeshFace> originals = faces;
        std::vector<char> frozen(originals.size(), 0);
        std::map<Key, char> pinned;

        for (int attempt = 0; attempt < 12; ++attempt)
        {
            // `sources` is borrowed to carry each face's originals through the
            // merges; the caller's lists are put back at the end.
            std::vector<MeshFace> work = originals;
            for (std::size_t i = 0; i < work.size(); ++i)
                work[i].sources = {static_cast<int>(i)};

            OptimizeReport round;
            // Each kind of step makes room for the other: a merge turns two
            // corners into mid-edge vertices, and dropping one can make the
            // next merge convex. Repeat until neither finds anything.
            for (int pass = 0; pass < 16; ++pass)
            {
                const int merges = mergePass(work, frozen);
                const int removed = collinearPass(work, pinned);
                round.merges += merges;
                round.removedVertices += removed;
                if (merges == 0 && removed == 0)
                    break;
            }

            bool changed = false;
            for (const MeshFace &result : work)
            {
                if (result.points.empty())
                    continue;
                const int parts = static_cast<int>(result.sources.size());
                int baseline = -2 * (parts - 1); // a strip shares two vertices a join
                for (int origin : result.sources)
                    baseline += costOf(static_cast<int>(originals[origin].points.size()));
                const int cost = costOf(static_cast<int>(result.points.size()));
                if (parts > 1 ? cost < baseline : cost <= baseline)
                    continue;
                for (int origin : result.sources)
                {
                    if (parts > 1 && !frozen[origin])
                    {
                        frozen[origin] = 1;
                        changed = true;
                    }
                    for (const FacePoint &p : originals[origin].points)
                    {
                        if (pinned.insert({keyOf(p), 1}).second)
                            changed = true;
                    }
                }
            }
            if (!changed || attempt == 11)
            {
                // Hand back the result, with the caller's own source lists
                // joined up in place of the indices.
                faces.clear();
                for (MeshFace &result : work)
                {
                    if (result.points.empty())
                        continue;
                    std::vector<int> sources;
                    for (int origin : result.sources)
                        sources.insert(sources.end(), originals[origin].sources.begin(),
                                       originals[origin].sources.end());
                    result.sources = std::move(sources);
                    faces.push_back(std::move(result));
                }
                report.merges += round.merges;
                report.removedVertices += round.removedVertices;
                return;
            }
        }
    }

    std::vector<std::vector<int>> groupFaces(const std::vector<MeshFace> &faces)
    {
        // Union-find over faces: joined when they have the same key, share an
        // edge with the same light at both ends, and fall in the same cell of
        // a coarse grid (so a group stays small enough to cull well).
        std::vector<int> parent(faces.size());
        for (std::size_t i = 0; i < faces.size(); ++i)
            parent[i] = static_cast<int>(i);
        auto find = [&](int x)
        {
            while (parent[x] != x)
                x = parent[x] = parent[parent[x]];
            return x;
        };

        auto cellOf = [&](const MeshFace &face)
        {
            double centre[3] = {0, 0, 0};
            for (const FacePoint &p : face.points)
                for (int a = 0; a < 3; ++a)
                    centre[a] += p.pos[a];
            Key cell;
            for (int a = 0; a < 3; ++a)
                cell[a] = static_cast<std::int32_t>(
                    std::floor(centre[a] / face.points.size() / kGroupCell));
            return cell;
        };

        struct EdgeUse
        {
            int face;
            std::uint16_t colourFrom, colourTo;
        };
        // (key, cell, from, to) -> the face with that directed edge.
        std::map<std::tuple<int, Key, Key, Key>, EdgeUse> edges;
        std::vector<Key> cells(faces.size());
        for (std::size_t f = 0; f < faces.size(); ++f)
        {
            cells[f] = cellOf(faces[f]);
            const auto &pts = faces[f].points;
            for (std::size_t i = 0; i < pts.size(); ++i)
            {
                const FacePoint &p = pts[i];
                const FacePoint &q = pts[(i + 1) % pts.size()];
                edges[{faces[f].key, cells[f], keyOf(p), keyOf(q)}] =
                    EdgeUse{static_cast<int>(f), p.colour, q.colour};
            }
        }
        for (std::size_t f = 0; f < faces.size(); ++f)
        {
            const auto &pts = faces[f].points;
            for (std::size_t i = 0; i < pts.size(); ++i)
            {
                const FacePoint &p = pts[i];
                const FacePoint &q = pts[(i + 1) % pts.size()];
                // The neighbour has this edge the other way round.
                const auto it = edges.find({faces[f].key, cells[f], keyOf(q), keyOf(p)});
                if (it == edges.end() || it->second.colourFrom != q.colour ||
                    it->second.colourTo != p.colour)
                    continue;
                parent[find(static_cast<int>(f))] = find(it->second.face);
            }
        }

        std::map<int, std::vector<int>> byRoot;
        for (std::size_t f = 0; f < faces.size(); ++f)
            byRoot[find(static_cast<int>(f))].push_back(static_cast<int>(f));
        std::vector<std::vector<int>> groups;
        for (auto &entry : byRoot)
            groups.push_back(std::move(entry.second));
        return groups;
    }

    int weldTJunctions(std::vector<MeshFace> &faces)
    {
        // Every place a vertex is, in cells, so that a side only looks at the
        // vertices around it.
        constexpr std::int32_t kCell = 256; // quarter units
        auto cellOf = [](std::int32_t v)
        {
            return v >= 0 ? v / kCell : -((-v + kCell - 1) / kCell);
        };
        std::map<Key, std::vector<Key>> cells;
        for (const MeshFace &face : faces)
        {
            for (const FacePoint &p : face.points)
            {
                const Key k = keyOf(p);
                std::vector<Key> &cell = cells[Key{cellOf(k[0]), cellOf(k[1]), cellOf(k[2])}];
                if (std::find(cell.begin(), cell.end(), k) == cell.end())
                    cell.push_back(k);
            }
        }
        auto grid = [](const Key &k)
        {
            return GridPoint{k[0], k[1], k[2]};
        };

        int added = 0;
        for (MeshFace &face : faces)
        {
            std::vector<FacePoint> out;
            const std::size_t n = face.points.size();
            // A face's own corners are never given to another of its sides:
            // on a face a fraction of a unit wide, the far side's corners are
            // "on" the near side as far as the grid can tell, and taking them
            // folds the face over itself.
            std::vector<Key> own;
            for (const FacePoint &p : face.points)
                own.push_back(keyOf(p));
            for (std::size_t i = 0; i < n; ++i)
            {
                const FacePoint &from = face.points[i];
                const FacePoint &to = face.points[(i + 1) % n];
                out.push_back(from);
                const Key a = keyOf(from), b = keyOf(to);
                if (a == b)
                    continue;

                std::vector<std::pair<double, Key>> between; // how far along, and where
                Key lo, hi;
                for (int c = 0; c < 3; ++c)
                {
                    lo[c] = cellOf(std::min(a[c], b[c]) - 2);
                    hi[c] = cellOf(std::max(a[c], b[c]) + 2);
                }
                const double d[3] = {double(b[0] - a[0]), double(b[1] - a[1]),
                                     double(b[2] - a[2])};
                const double length2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                for (std::int32_t x = lo[0]; x <= hi[0]; ++x)
                {
                    for (std::int32_t y = lo[1]; y <= hi[1]; ++y)
                    {
                        for (std::int32_t z = lo[2]; z <= hi[2]; ++z)
                        {
                            const auto cell = cells.find(Key{x, y, z});
                            if (cell == cells.end())
                                continue;
                            for (const Key &v : cell->second)
                            {
                                if (std::find(own.begin(), own.end(), v) != own.end() ||
                                    !onGridSide(grid(a), grid(v), grid(b)))
                                    continue;
                                const double along = ((v[0] - a[0]) * d[0] + (v[1] - a[1]) * d[1] +
                                                      (v[2] - a[2]) * d[2]) / length2;
                                between.push_back({along, v});
                            }
                        }
                    }
                }
                std::sort(between.begin(), between.end());
                for (const auto &[along, v] : between)
                {
                    FacePoint p;
                    for (int c = 0; c < 3; ++c)
                        p.pos[c] = v[c] / 4.0;
                    p.s = from.s + (to.s - from.s) * along;
                    p.t = from.t + (to.t - from.t) * along;
                    // The colour the side has there: what the hardware would
                    // have blended to at that point anyway.
                    p.colour = 0;
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        const double c0 = (from.colour >> (5 * ch)) & 31;
                        const double c1 = (to.colour >> (5 * ch)) & 31;
                        const int c = static_cast<int>(std::lround(c0 + (c1 - c0) * along));
                        p.colour |= static_cast<std::uint16_t>(std::clamp(c, 0, 31) << (5 * ch));
                    }
                    out.push_back(p);
                    ++added;
                }
            }
            face.points = std::move(out);
        }
        return added;
    }

    BuiltSurface buildGroupSurface(const std::vector<const MeshFace *> &group, TextureSize cooked,
                                   std::uint32_t sourceWidth, std::uint32_t sourceHeight)
    {
        BuiltSurface out;

        // Texture coordinates in cooked texels, moved to within one repeat of
        // zero for the GROUP as a whole: the faces share a texture mapping, so
        // one offset keeps a shared vertex's coordinates the same in each.
        const double scaleS = static_cast<double>(cooked.width) / sourceWidth;
        const double scaleT = static_cast<double>(cooked.height) / sourceHeight;
        double minS = 1e30, minT = 1e30;
        double lo[3] = {1e30, 1e30, 1e30};
        double hi[3] = {-1e30, -1e30, -1e30};
        for (const MeshFace *face : group)
        {
            for (const FacePoint &p : face->points)
            {
                minS = std::min(minS, p.s * scaleS);
                minT = std::min(minT, p.t * scaleT);
                for (int a = 0; a < 3; ++a)
                {
                    lo[a] = std::min(lo[a], p.pos[a]);
                    hi[a] = std::max(hi[a], p.pos[a]);
                }
            }
        }
        const double groupS = std::floor(minS / cooked.width) * cooked.width;
        const double groupT = std::floor(minT / cooked.height) * cooked.height;
        // ... unless the group spans more texels than 12.4 holds (faces with
        // one texture far apart on it: c1a1's broken sign, its tilted slats
        // thousands of texels from each other). Then each face is moved by
        // its own whole repeats -- the same picture, the texture repeating --
        // and only faces moved alike share vertices.
        bool tooWide = false;
        for (const MeshFace *face : group)
        {
            for (const FacePoint &p : face->points)
                tooWide = tooWide || (p.s * scaleS - groupS) * 16 > 32767 || (p.t * scaleT - groupT) * 16 > 32767;
        }
        double baseS = groupS, baseT = groupT;
        auto faceBase = [&](const MeshFace &face)
        {
            if (!tooWide)
                return;
            double faceS = 1e30, faceT = 1e30;
            for (const FacePoint &p : face.points)
            {
                faceS = std::min(faceS, p.s * scaleS);
                faceT = std::min(faceT, p.t * scaleT);
            }
            baseS = std::floor(faceS / cooked.width) * cooked.width;
            baseT = std::floor(faceT / cooked.height) * cooked.height;
        };

        double centre[3];
        for (int a = 0; a < 3; ++a)
        {
            centre[a] = std::round((lo[a] + hi[a]) / 2);
            out.center[a] = static_cast<std::int16_t>(std::clamp(centre[a], -32768.0, 32767.0));
        }
        double radius = 0;

        // The group's vertices, each once: same place, same light, same
        // texture coordinates.
        std::vector<GxVertex> verts;
        std::map<std::array<int, 6>, int> vertexIds;
        auto vertexId = [&](const FacePoint &p)
        {
            GxVertex v;
            std::int16_t *xyz[3] = {&v.x, &v.y, &v.z};
            for (int a = 0; a < 3; ++a)
            {
                const double q = std::round(p.pos[a] * (1 << hldata::kVertexShift));
                if (q < -32768 || q > 32767)
                    out.outOfRange = true;
                *xyz[a] = static_cast<std::int16_t>(std::clamp(q, -32768.0, 32767.0));
            }
            const double s = std::round((p.s * scaleS - baseS) * 16);
            const double t = std::round((p.t * scaleT - baseT) * 16);
            if (s < -32768 || s > 32767 || t < -32768 || t > 32767)
                out.texCoordsClamped = true;
            v.s = static_cast<std::int16_t>(std::clamp(s, -32768.0, 32767.0));
            v.t = static_cast<std::int16_t>(std::clamp(t, -32768.0, 32767.0));
            v.colour = p.colour;
            const double d[3] = {p.pos[0] - centre[0], p.pos[1] - centre[1], p.pos[2] - centre[2]};
            radius = std::max(radius, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));

            const std::array<int, 6> id = {v.x, v.y, v.z, v.s, v.t, v.colour};
            const auto it = vertexIds.find(id);
            if (it != vertexIds.end())
                return it->second;
            vertexIds[id] = static_cast<int>(verts.size());
            verts.push_back(v);
            return static_cast<int>(verts.size()) - 1;
        };

        // Every face cut into quads (and a triangle when it has an odd number
        // of sides), in the DS's winding: counter-clockwise, so the BSP's
        // order reversed.
        std::vector<std::array<int, 4>> quads;
        std::vector<std::array<int, 3>> triangles;
        for (const MeshFace *face : group)
        {
            faceBase(*face);
            const std::vector<FacePoint> polygon(face->points.rbegin(), face->points.rend());
            const int n = static_cast<int>(polygon.size());
            std::vector<int> ids(n);
            for (int i = 0; i < n; ++i)
                ids[i] = vertexId(polygon[i]);
            for (const std::vector<int> &piece : decompose(polygon))
            {
                if (piece.size() == 4)
                    quads.push_back({ids[piece[0]], ids[piece[1]], ids[piece[2]], ids[piece[3]]});
                else
                    triangles.push_back({ids[piece[0]], ids[piece[1]], ids[piece[2]]});
            }
        }
        out.radius = static_cast<std::uint16_t>(std::min(65535.0, std::ceil(radius)));

        // What the hardware judges a polygon's facing by is its first three
        // vertices, and many quads here are a triangle with a vertex in the
        // middle of one side (the compiler's T-junction fixes, kept so that
        // neighbours meet). Sent with that vertex second, the three are in one
        // line and the facing is rounding noise: the polygon blinks. So every
        // polygon is sent in an order whose leading three TURN, the way the
        // group faces.
        using Grid = std::array<std::int64_t, 3>;
        auto corner = [&](int a, int b, int c)
        {
            const std::int64_t e1[3] = {verts[b].x - verts[a].x, verts[b].y - verts[a].y,
                                        verts[b].z - verts[a].z};
            const std::int64_t e2[3] = {verts[c].x - verts[a].x, verts[c].y - verts[a].y,
                                        verts[c].z - verts[a].z};
            return Grid{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                        e1[0] * e2[1] - e1[1] * e2[0]};
        };
        Grid facing{0, 0, 0};
        for (const auto &q : quads)
        {
            for (int half = 0; half < 2; ++half)
            {
                const Grid c = corner(q[0], q[1 + half], q[2 + half]);
                for (int a = 0; a < 3; ++a)
                    facing[a] += c[a];
            }
        }
        for (const auto &t : triangles)
        {
            const Grid c = corner(t[0], t[1], t[2]);
            for (int a = 0; a < 3; ++a)
                facing[a] += c[a];
        }
        // How far three vertices turn the group's way: twice the triangle's
        // area, signed; zero for three in line. A double is exact enough to
        // order them and its sign is the integers'.
        auto leads = [&](int a, int b, int c)
        {
            const Grid n = corner(a, b, c);
            if (n == Grid{0, 0, 0})
                return 0.0;
            return static_cast<double>(n[0]) * static_cast<double>(facing[0]) +
                   static_cast<double>(n[1]) * static_cast<double>(facing[1]) +
                   static_cast<double>(n[2]) * static_cast<double>(facing[2]);
        };
        // Whether three vertices are fit to LEAD a polygon: they turn the
        // group's way, and by more than the grid's rounding -- the triangle
        // they make is more than 1.5 grid steps high. Three vertices that are
        // in line in the map are seldom exactly in line once snapped, and
        // "turning" by a fraction of a step is as much noise as not turning.
        auto fitToLead = [&](int a, int b, int c)
        {
            if (leads(a, b, c) <= 0)
                return false;
            const Grid n = corner(a, b, c);
            const double area2 = double(n[0]) * double(n[0]) + double(n[1]) * double(n[1]) +
                                 double(n[2]) * double(n[2]);
            double longest2 = 0;
            const int ring[3] = {a, b, c};
            for (int i = 0; i < 3; ++i)
            {
                const GxVertex &p = verts[ring[i]], &q = verts[ring[(i + 1) % 3]];
                const double e[3] = {double(q.x - p.x), double(q.y - p.y), double(q.z - p.z)};
                longest2 = std::max(longest2, e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            }
            return area2 > 1.5 * 1.5 * longest2;
        };

        // A quad with nothing to draw (its corners fell together on the grid)
        // is dropped: it has no order worth sending.
        std::erase_if(quads, [&](const std::array<int, 4> &q)
        {
            for (int c = 0; c < 4; ++c)
            {
                if (leads(q[c], q[(c + 1) % 4], q[(c + 2) % 4]) > 0)
                    return false;
            }
            return true;
        });
        // A quad the hardware may draw wrongly -- GBATEK's "prohibited quads":
        // crossed sides, a corner of more than 180 degrees -- goes as two
        // triangles instead. Judged on the grid, with no allowance: each corner
        // must be exactly in line with its neighbours (in line however it is
        // seen) or turn the group's way by more than 1.5 steps. A vertex on a
        // side (a T-junction welded in), snapped a step off its line, is a
        // corner that turns a little either way: inward, the quad is concave
        // whatever the view; outward and off the plane, from some views.
        // (Cut through its worst corner, so that neither triangle has it as a
        // middle vertex off its line.)
        {
            // (Whether the four lie exactly in one plane, on the grid: then a
            // quad whose corners all turn its way, however little, is convex
            // from every view -- a level floor, a pool of liquid.)
            auto flat = [&](const std::array<int, 4> &q)
            {
                const Grid n = corner(q[0], q[1], q[2]);
                const std::int64_t d[3] = {verts[q[3]].x - verts[q[0]].x, verts[q[3]].y - verts[q[0]].y,
                                           verts[q[3]].z - verts[q[0]].z};
                return n[0] * d[0] + n[1] * d[1] + n[2] * d[2] == 0;
            };
            std::vector<std::array<int, 4>> kept;
            kept.reserve(quads.size());
            for (const std::array<int, 4> &q : quads)
            {
                const bool planar = flat(q);
                int worst = -1;
                double worstTurn = 0;
                for (int c = 0; c < 4; ++c)
                {
                    const int p = q[(c + 3) % 4], v = q[c], n = q[(c + 1) % 4];
                    const Grid turn = corner(p, v, n);
                    if (turn == Grid{0, 0, 0})
                        continue; // exactly in line
                    // How far it turns the group's way: its height off the line
                    // through its neighbours, signed, in grid steps squared.
                    const double e[3] = {double(verts[n].x - verts[p].x), double(verts[n].y - verts[p].y),
                                         double(verts[n].z - verts[p].z)};
                    const double length2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
                    const double area2 = double(turn[0]) * turn[0] + double(turn[1]) * turn[1] +
                                         double(turn[2]) * turn[2];
                    const double height2 = length2 > 0 ? area2 / length2 : 0;
                    const double signedTurn = leads(p, v, n) > 0 ? height2 : -height2;
                    if (signedTurn > 1.5 * 1.5 || (planar && signedTurn > 0))
                        continue;
                    if (worst < 0 || signedTurn < worstTurn)
                    {
                        worst = c;
                        worstTurn = signedTurn;
                    }
                }
                if (worst < 0)
                {
                    kept.push_back(q);
                    continue;
                }
                const int a = q[worst], b = q[(worst + 1) % 4], c = q[(worst + 2) % 4], d = q[(worst + 3) % 4];
                triangles.push_back({a, b, c});
                triangles.push_back({a, c, d});
            }
            quads = std::move(kept);
        }
        std::erase_if(triangles, [&](const std::array<int, 3> &t)
        {
            return leads(t[0], t[1], t[2]) <= 0;
        });

        // Strips. A quad strip is rows of two vertices; quad k is
        // (L[k], R[k], R[k+1], L[k+1]), and the hardware judges it by
        // (L[k], R[k], R[k+1]): it swaps the last two of the four it was sent
        // before it looks. The quad after a row (L, R) is the one that
        // has the edge L -> R, and the quad before a row is the one that has
        // R -> L. Each quad can start a strip from any of its four corners;
        // the longest is taken.
        std::map<std::pair<int, int>, std::pair<int, int>> edgeOwner; // (from, to) -> (quad, corner)
        for (std::size_t q = 0; q < quads.size(); ++q)
        {
            for (int c = 0; c < 4; ++c)
                edgeOwner.insert({{quads[q][c], quads[q][(c + 1) % 4]}, {static_cast<int>(q), c}});
        }
        std::vector<char> used(quads.size(), 0);

        // The rows of the strip through quad `q` entered at corner `c`, using
        // only quads not in `taken`. Marks nothing. Empty when the quad itself
        // cannot be sent from that corner.
        auto trace = [&](int q, int c, const std::vector<char> &taken)
        {
            std::vector<std::pair<int, int>> rows;
            std::vector<int> members;
            if (!fitToLead(quads[q][c], quads[q][(c + 1) % 4], quads[q][(c + 2) % 4]))
                return std::make_pair(rows, members);
            members.push_back(q);
            rows.push_back({quads[q][c], quads[q][(c + 1) % 4]});
            rows.push_back({quads[q][(c + 3) % 4], quads[q][(c + 2) % 4]});
            auto isFree = [&](int quad)
            {
                return !taken[quad] &&
                       std::find(members.begin(), members.end(), quad) == members.end();
            };
            for (;;) // forwards
            {
                const auto it = edgeOwner.find({rows.back().first, rows.back().second});
                if (it == edgeOwner.end() || !isFree(it->second.first))
                    break;
                const auto &next = quads[it->second.first];
                const int k = it->second.second;
                if (!fitToLead(next[k], next[(k + 1) % 4], next[(k + 2) % 4]))
                    break;
                rows.push_back({next[(k + 3) % 4], next[(k + 2) % 4]});
                members.push_back(it->second.first);
            }
            for (;;) // backwards
            {
                const auto it = edgeOwner.find({rows.front().second, rows.front().first});
                if (it == edgeOwner.end() || !isFree(it->second.first))
                    break;
                const auto &prev = quads[it->second.first];
                const int k = it->second.second;
                // Its rows are (prev[k+2], prev[k+3]) then (prev[k+1], prev[k]).
                if (!fitToLead(prev[(k + 2) % 4], prev[(k + 3) % 4], prev[k]))
                    break;
                rows.insert(rows.begin(), {prev[(k + 2) % 4], prev[(k + 3) % 4]});
                members.push_back(it->second.first);
            }
            return std::make_pair(rows, members);
        };

        GxPacker packer;
        std::vector<int> singles;
        for (std::size_t q = 0; q < quads.size(); ++q)
        {
            if (used[q])
                continue;
            auto best = trace(static_cast<int>(q), 0, used);
            for (int c = 1; c < 4; ++c)
            {
                auto other = trace(static_cast<int>(q), c, used);
                if (other.second.size() > best.second.size())
                    best = std::move(other);
            }
            if (best.second.size() <= 1)
            {
                used[q] = 1;
                singles.push_back(static_cast<int>(q));
                continue;
            }
            for (int member : best.second)
                used[member] = 1;
            packer.begin(GxPrimitive::QuadStrip);
            for (const auto &row : best.first)
            {
                packer.vertex(verts[row.first]);
                packer.vertex(verts[row.second]);
            }
            out.polygons += static_cast<int>(best.second.size());
            out.vertices += static_cast<int>(best.first.size()) * 2;
        }
        // Quads that joined nothing go out together under one BEGIN.
        if (!singles.empty())
        {
            packer.begin(GxPrimitive::Quads);
            for (int q : singles)
            {
                // From the corner whose leading three turn the most.
                int from = 0;
                double most = 0;
                for (int c = 0; c < 4; ++c)
                {
                    const double turnOf =
                        leads(quads[q][c], quads[q][(c + 1) % 4], quads[q][(c + 2) % 4]);
                    if (turnOf > most)
                    {
                        most = turnOf;
                        from = c;
                    }
                }
                for (int c = 0; c < 4; ++c)
                    packer.vertex(verts[quads[q][(from + c) % 4]]);
            }
            out.polygons += static_cast<int>(singles.size());
            out.vertices += static_cast<int>(singles.size()) * 4;
        }
        if (!triangles.empty())
        {
            packer.begin(GxPrimitive::Triangles);
            for (const auto &tri : triangles)
            {
                for (int c = 0; c < 3; ++c)
                    packer.vertex(verts[tri[c]]);
            }
            out.polygons += static_cast<int>(triangles.size());
            out.vertices += static_cast<int>(triangles.size()) * 3;
        }
        // What was sent, measured: the cooker holds this against the faces
        // it was given, so that a piece left out is an error and not a hole
        // somebody finds in a floor.
        auto areaOf = [&](const int *ring, int n)
        {
            Grid sum{0, 0, 0};
            for (int i = 1; i + 1 < n; ++i)
            {
                const Grid c = corner(ring[0], ring[i], ring[i + 1]);
                for (int a = 0; a < 3; ++a)
                    sum[a] += c[a];
            }
            return 0.5 * std::sqrt(double(sum[0]) * double(sum[0]) + double(sum[1]) * double(sum[1]) +
                                   double(sum[2]) * double(sum[2])) / 16.0;
        };
        for (const auto &q : quads)
            out.area += areaOf(q.data(), 4);
        for (const auto &t : triangles)
            out.area += areaOf(t.data(), 3);

        out.words = packer.finish();
        return out;
    }

    namespace
    {
        double dot3(const double *a, const double *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

        /// A point a fraction `t` of the way from `a` to `b`: its place, its
        /// texture coordinates and its colour, channel by channel.
        FacePoint between(const FacePoint &a, const FacePoint &b, double t)
        {
            FacePoint p;
            for (int i = 0; i < 3; ++i)
                p.pos[i] = a.pos[i] + (b.pos[i] - a.pos[i]) * t;
            p.s = a.s + (b.s - a.s) * t;
            p.t = a.t + (b.t - a.t) * t;
            std::uint16_t colour = 0;
            for (int shift = 0; shift <= 10; shift += 5)
            {
                const int ca = (a.colour >> shift) & 31, cb = (b.colour >> shift) & 31;
                colour = static_cast<std::uint16_t>(colour | (static_cast<int>(std::lround(ca + (cb - ca) * t)) << shift));
            }
            p.colour = colour;
            return p;
        }

        /// `poly` split by the plane through `at` square to `across` (in the
        /// face's own plane): what is on the side `across` points to, and
        /// what is not. A point within a 64th of a unit of the line is on it.
        void splitBy(const std::vector<FacePoint> &poly, const double *at, const double *across,
                     std::vector<FacePoint> &in, std::vector<FacePoint> &out)
        {
            constexpr double kOn = 1.0 / 64;
            in.clear();
            out.clear();
            const std::size_t n = poly.size();
            std::vector<double> side(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                const double d[3] = {poly[i].pos[0] - at[0], poly[i].pos[1] - at[1], poly[i].pos[2] - at[2]};
                side[i] = dot3(d, across);
            }
            for (std::size_t i = 0; i < n; ++i)
            {
                const std::size_t j = (i + 1) % n;
                const double si = side[i], sj = side[j];
                if (si >= -kOn)
                    in.push_back(poly[i]);
                if (si <= kOn)
                    out.push_back(poly[i]);
                if ((si > kOn && sj < -kOn) || (si < -kOn && sj > kOn))
                {
                    const FacePoint cut = between(poly[i], poly[j], si / (si - sj));
                    in.push_back(cut);
                    out.push_back(cut);
                }
            }
        }

        /// Area of a polygon lying in the plane facing `normal`.
        double areaOf(const std::vector<FacePoint> &poly, const double *normal)
        {
            double sum[3] = {0, 0, 0};
            for (std::size_t i = 0; i < poly.size(); ++i)
            {
                const double *a = poly[i].pos, *b = poly[(i + 1) % poly.size()].pos;
                sum[0] += a[1] * b[2] - a[2] * b[1];
                sum[1] += a[2] * b[0] - a[0] * b[2];
                sum[2] += a[0] * b[1] - a[1] * b[0];
            }
            return std::fabs(dot3(sum, normal)) / 2;
        }

        /// The same polygon without a corner that is the one before it.
        void dropRepeats(std::vector<FacePoint> &poly)
        {
            std::vector<FacePoint> kept;
            for (const FacePoint &p : poly)
            {
                if (!kept.empty() && keyOf(kept.back()) == keyOf(p))
                    continue;
                kept.push_back(p);
            }
            while (kept.size() > 1 && keyOf(kept.front()) == keyOf(kept.back()))
                kept.pop_back();
            poly.swap(kept);
        }
    } // namespace

    std::vector<std::pair<MeshFace, int>> cutOverFloors(const MeshFace &face, const std::vector<MeshFace> &floors,
                                                        double reach)
    {
        std::vector<std::pair<MeshFace, int>> pieces;
        if (face.points.size() < 3)
        {
            pieces.emplace_back(face, -1);
            return pieces;
        }
        const double *n = face.normal;
        const double height = dot3(face.points[0].pos, n);
        double lo[3], hi[3];
        for (int a = 0; a < 3; ++a)
        {
            lo[a] = hi[a] = face.points[0].pos[a];
            for (const FacePoint &p : face.points)
            {
                lo[a] = std::min(lo[a], p.pos[a]);
                hi[a] = std::max(hi[a], p.pos[a]);
            }
        }
        // (A floor polygon the piece is over by less than a square unit
        // does not cut it: what that would leave are slivers the grid bends
        // out of line. What a cut leaves is never thrown away: a hole.)
        constexpr double kOver = 1.0;           // square units
        constexpr double kNothing = 1.0 / 256;  // square units
        std::vector<std::vector<FacePoint>> left = {face.points};
        std::vector<std::pair<std::vector<FacePoint>, int>> over;
        for (std::size_t f = 0; f < floors.size(); ++f)
        {
            const MeshFace &floor = floors[f];
            if (floor.points.size() < 3 || dot3(floor.normal, n) < 0.999)
                continue;
            const double below = height - dot3(floor.points[0].pos, n);
            if (below <= 1.0 / 64 || below > reach)
                continue;
            // (Brought up onto the sheet's plane; and near it at all.)
            std::vector<std::array<double, 3>> outline;
            bool apart = true;
            double flo[3] = {1e30, 1e30, 1e30}, fhi[3] = {-1e30, -1e30, -1e30};
            for (const FacePoint &p : floor.points)
            {
                const double off = height - dot3(p.pos, n);
                std::array<double, 3> q = {p.pos[0] + n[0] * off, p.pos[1] + n[1] * off, p.pos[2] + n[2] * off};
                for (int a = 0; a < 3; ++a)
                {
                    flo[a] = std::min(flo[a], q[a]);
                    fhi[a] = std::max(fhi[a], q[a]);
                }
                outline.push_back(q);
            }
            apart = false;
            for (int a = 0; a < 3; ++a)
                apart = apart || fhi[a] < lo[a] - 0.5 || flo[a] > hi[a] + 0.5;
            if (apart)
                continue;
            double middle[3] = {0, 0, 0};
            for (const auto &q : outline)
                for (int a = 0; a < 3; ++a)
                    middle[a] += q[a] / outline.size();
            // Its sides as half-planes, each with the way in.
            std::vector<std::array<double, 6>> sides;
            for (std::size_t i = 0; i < outline.size(); ++i)
            {
                const auto &a = outline[i], &b = outline[(i + 1) % outline.size()];
                const double e[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                double in[3] = {n[1] * e[2] - n[2] * e[1], n[2] * e[0] - n[0] * e[2], n[0] * e[1] - n[1] * e[0]};
                const double length = std::sqrt(dot3(in, in));
                if (length < 1e-9)
                    continue;
                const double toMiddle[3] = {middle[0] - a[0], middle[1] - a[1], middle[2] - a[2]};
                const double way = dot3(in, toMiddle) < 0 ? -1.0 / length : 1.0 / length;
                sides.push_back({a[0], a[1], a[2], in[0] * way, in[1] * way, in[2] * way});
            }
            std::vector<std::vector<FacePoint>> still;
            std::vector<FacePoint> in, out;
            for (const std::vector<FacePoint> &piece : left)
            {
                // Over this floor at all? (Else left whole: not cut along
                // lines that only run past it.)
                std::vector<FacePoint> inside = piece;
                for (const auto &side : sides)
                {
                    splitBy(inside, side.data(), side.data() + 3, in, out);
                    inside = in;
                    if (inside.size() < 3)
                        break;
                }
                if (inside.size() < 3 || areaOf(inside, n) < kOver)
                {
                    still.push_back(piece);
                    continue;
                }
                inside = piece;
                for (const auto &side : sides)
                {
                    splitBy(inside, side.data(), side.data() + 3, in, out);
                    dropRepeats(out);
                    if (out.size() >= 3 && areaOf(out, n) > kNothing)
                        still.push_back(out);
                    inside = in;
                }
                dropRepeats(inside);
                over.emplace_back(inside, static_cast<int>(f));
            }
            left.swap(still);
        }
        if (over.empty())
        {
            pieces.emplace_back(face, -1);
            return pieces;
        }
        auto make = [&](std::vector<FacePoint> &&points, int floor) {
            MeshFace piece;
            piece.points = std::move(points);
            piece.key = face.key;
            for (int a = 0; a < 3; ++a)
                piece.normal[a] = face.normal[a];
            piece.sources = face.sources;
            pieces.emplace_back(std::move(piece), floor);
        };
        for (auto &piece : over)
            make(std::move(piece.first), piece.second);
        for (auto &piece : left)
        {
            dropRepeats(piece);
            if (piece.size() >= 3)
                make(std::move(piece), -1);
        }
        return pieces;
    }
} // namespace hlds
