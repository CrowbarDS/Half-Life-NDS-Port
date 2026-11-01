// The mesh optimiser: fewer vertices, same picture.
//
// "Same picture" is the whole claim, so it is what is tested, three ways, on
// what the cooker actually emits:
//
//   AREA    every plane is covered by exactly as much polygon as before;
//   CRACKS  no emitted edge passes through another emitted vertex that is not
//           one of its ends -- the T-junction that shows as a line of
//           background on the DS;
//   LIGHT   at every vertex the map had, the light is what it was: the same
//           colour if the vertex is still there, and the blend of its edge's
//           ends, to within one colour step, if it was dropped.
//
// The hand-built cases pin the rules one at a time; with HLDS_VALVE_DIR set the
// same three checks run over real maps, where the cases nobody thought of are.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "HlLevelReader.hpp"
#include "TestBspBuilder.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/GxStream.hpp"
#include "hlds/MeshOptimize.hpp"

using namespace hlds;
using namespace hldstest;

namespace
{
    using Key = std::array<int, 3>;

    FacePoint point(double x, double y, int grey)
    {
        FacePoint p{};
        p.pos[0] = x;
        p.pos[1] = y;
        p.pos[2] = 0;
        p.s = x;
        p.t = y;
        p.colour = hldata::rgb555(grey, grey, grey);
        return p;
    }

    /// A face in the plane z = 0 seen from +z, corners given CLOCKWISE (the
    /// BSP's order), each with a grey level.
    MeshFace face(std::initializer_list<std::array<double, 3>> corners, int key = 0)
    {
        MeshFace f;
        f.key = key;
        f.normal[2] = 1;
        for (const auto &c : corners)
            f.points.push_back(point(c[0], c[1], static_cast<int>(c[2])));
        return f;
    }

    /// Cook a set of faces the way the cooker does and decode the result.
    struct Built
    {
        int surfaces = 0;
        int polygons = 0;
        int vertices = 0;
        std::vector<std::vector<GxVertex>> pieces; // every emitted polygon
        std::vector<std::vector<std::vector<GxVertex>>> bySurface;
    };

    void addPieces(const std::vector<GxRun> &runs, std::vector<std::vector<GxVertex>> &pieces)
    {
        for (const GxRun &run : runs)
        {
            const auto &v = run.vertices;
            if (run.type == GxPrimitive::Triangles)
                for (std::size_t i = 0; i + 2 < v.size(); i += 3)
                    pieces.push_back({v[i], v[i + 1], v[i + 2]});
            else if (run.type == GxPrimitive::Quads)
                for (std::size_t i = 0; i + 3 < v.size(); i += 4)
                    pieces.push_back({v[i], v[i + 1], v[i + 2], v[i + 3]});
            else if (run.type == GxPrimitive::QuadStrip)
                for (std::size_t i = 0; i + 3 < v.size(); i += 2)
                    pieces.push_back({v[i], v[i + 1], v[i + 3], v[i + 2]});
        }
    }

    Built build(std::vector<MeshFace> faces, bool guarded)
    {
        Built b;
        if (guarded)
        {
            OptimizeReport report;
            optimizeFaces(faces, report);
        }
        for (const std::vector<int> &group : groupFaces(faces))
        {
            std::vector<const MeshFace *> members;
            for (int i : group)
                members.push_back(&faces[i]);
            const BuiltSurface s = buildGroupSurface(members, TextureSize{64, 64}, 64, 64);
            std::vector<GxRun> runs;
            std::string error;
            check(decodeGxStream(s.words.data(), s.words.size(), runs, error), "group decodes");
            int polygons = 0, vertices = 0;
            for (const GxRun &run : runs)
            {
                polygons += run.polygons();
                vertices += static_cast<int>(run.vertices.size());
            }
            check(polygons == s.polygons && vertices == s.vertices, "group counts are honest");
            ++b.surfaces;
            b.polygons += polygons;
            b.vertices += vertices;
            addPieces(runs, b.pieces);
            b.bySurface.emplace_back();
            addPieces(runs, b.bySurface.back());
        }
        return b;
    }

    double pieceArea(const std::vector<GxVertex> &p)
    {
        // Area of a planar polygon in 3D, in quarter units squared.
        double n[3] = {0, 0, 0};
        for (std::size_t i = 1; i + 1 < p.size(); ++i)
        {
            const double e1[3] = {double(p[i].x - p[0].x), double(p[i].y - p[0].y),
                                  double(p[i].z - p[0].z)};
            const double e2[3] = {double(p[i + 1].x - p[0].x), double(p[i + 1].y - p[0].y),
                                  double(p[i + 1].z - p[0].z)};
            n[0] += e1[1] * e2[2] - e1[2] * e2[1];
            n[1] += e1[2] * e2[0] - e1[0] * e2[2];
            n[2] += e1[0] * e2[1] - e1[1] * e2[0];
        }
        return 0.5 * std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) / 16.0;
    }

    double totalArea(const Built &b)
    {
        double sum = 0;
        for (const auto &piece : b.pieces)
            sum += pieceArea(piece);
        return sum;
    }

    /// Is b strictly inside the segment a..c?
    bool strictlyBetween(const GxVertex &a, const GxVertex &b, const GxVertex &c)
    {
        const std::int64_t e1[3] = {std::int64_t(b.x) - a.x, std::int64_t(b.y) - a.y,
                                    std::int64_t(b.z) - a.z};
        const std::int64_t e2[3] = {std::int64_t(c.x) - b.x, std::int64_t(c.y) - b.y,
                                    std::int64_t(c.z) - b.z};
        if (e1[1] * e2[2] - e1[2] * e2[1] != 0 || e1[2] * e2[0] - e1[0] * e2[2] != 0 ||
            e1[0] * e2[1] - e1[1] * e2[0] != 0)
            return false;
        return e1[0] * e2[0] + e1[1] * e2[1] + e1[2] * e2[2] > 0;
    }

    /// If `p` lies in the triangle (a, b, c) -- in its plane and not outside
    /// it -- its weights on the three corners.
    bool barycentric(const GxVertex &a, const GxVertex &b, const GxVertex &c, const GxVertex &p,
                     double *w)
    {
        const double e1[3] = {double(b.x - a.x), double(b.y - a.y), double(b.z - a.z)};
        const double e2[3] = {double(c.x - a.x), double(c.y - a.y), double(c.z - a.z)};
        const double ep[3] = {double(p.x - a.x), double(p.y - a.y), double(p.z - a.z)};
        const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                             e1[0] * e2[1] - e1[1] * e2[0]};
        const double nn = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
        if (nn <= 0)
            return false;
        // Within half a unit of the plane: sloping planes are only that flat
        // once their vertices are snapped to the grid.
        const double off = (ep[0] * n[0] + ep[1] * n[1] + ep[2] * n[2]) / std::sqrt(nn);
        if (std::fabs(off) > 2.0)
            return false;
        const double d11 = e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2];
        const double d12 = e1[0] * e2[0] + e1[1] * e2[1] + e1[2] * e2[2];
        const double d22 = e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2];
        const double dp1 = ep[0] * e1[0] + ep[1] * e1[1] + ep[2] * e1[2];
        const double dp2 = ep[0] * e2[0] + ep[1] * e2[1] + ep[2] * e2[2];
        const double det = d11 * d22 - d12 * d12;
        if (det <= 0)
            return false;
        w[1] = (dp1 * d22 - dp2 * d12) / det;
        w[2] = (dp2 * d11 - dp1 * d12) / det;
        w[0] = 1 - w[1] - w[2];
        const double eps = 1e-6;
        return w[0] >= -eps && w[1] >= -eps && w[2] >= -eps;
    }

    /// Cracks: OUTLINE edges that pass through a vertex without stopping at
    /// it. `surfaces` is the pieces of each surface in turn.
    ///
    /// Only outlines can crack. An edge two pieces of one surface share -- the
    /// diagonal a face is cut along, the seam between two faces of a group --
    /// has the same two ends on both sides, so nothing shows through it
    /// whatever else happens to lie on it.
    int countCracks(const std::vector<std::vector<std::vector<GxVertex>>> &surfaces)
    {
        // Vertices bucketed on a coarse grid so that each edge only looks at
        // the vertices near it.
        constexpr int kCell = 256; // quarter units
        std::map<Key, std::vector<GxVertex>> grid;
        std::map<Key, char> seen;
        for (const auto &pieces : surfaces)
            for (const auto &piece : pieces)
                for (const GxVertex &v : piece)
                {
                    if (seen.insert({Key{v.x, v.y, v.z}, 1}).second)
                        grid[Key{v.x / kCell, v.y / kCell, v.z / kCell}].push_back(v);
                }
        auto cell = [](int v) { return v / kCell; };
        int cracks = 0;
        for (const auto &pieces : surfaces)
        {
            // An edge used once is on the outline; used in both directions it
            // is internal.
            std::map<std::array<int, 6>, int> uses;
            auto edgeId = [](const GxVertex &a, const GxVertex &c)
            {
                std::array<int, 6> id = {a.x, a.y, a.z, c.x, c.y, c.z};
                if (Key{c.x, c.y, c.z} < Key{a.x, a.y, a.z})
                    id = {c.x, c.y, c.z, a.x, a.y, a.z};
                return id;
            };
            for (const auto &piece : pieces)
                for (std::size_t i = 0; i < piece.size(); ++i)
                    ++uses[edgeId(piece[i], piece[(i + 1) % piece.size()])];
            for (const auto &use : uses)
            {
                if (use.second != 1)
                    continue;
                GxVertex a, c;
                a.x = static_cast<std::int16_t>(use.first[0]);
                a.y = static_cast<std::int16_t>(use.first[1]);
                a.z = static_cast<std::int16_t>(use.first[2]);
                c.x = static_cast<std::int16_t>(use.first[3]);
                c.y = static_cast<std::int16_t>(use.first[4]);
                c.z = static_cast<std::int16_t>(use.first[5]);
                bool crack = false;
                for (int gx = std::min(cell(a.x), cell(c.x)) - 1;
                     gx <= std::max(cell(a.x), cell(c.x)) + 1 && !crack; ++gx)
                    for (int gy = std::min(cell(a.y), cell(c.y)) - 1;
                         gy <= std::max(cell(a.y), cell(c.y)) + 1 && !crack; ++gy)
                        for (int gz = std::min(cell(a.z), cell(c.z)) - 1;
                             gz <= std::max(cell(a.z), cell(c.z)) + 1 && !crack; ++gz)
                        {
                            const auto it = grid.find(Key{gx, gy, gz});
                            if (it == grid.end())
                                continue;
                            for (const GxVertex &b : it->second)
                            {
                                if (strictlyBetween(a, b, c))
                                {
                                    crack = true;
                                    break;
                                }
                            }
                        }
                if (crack)
                    ++cracks;
            }
        }
        return cracks;
    }

    int grey8(std::uint16_t colour) { return (colour & 31) << 3; }

    /// Faces of one texture whose mappings put them thousands of texels
    /// apart (c1a1's broken sign: tilted slats, the same picture on each):
    /// cooked together they do not fit 12.4, so each is moved by its own
    /// whole repeats -- every vertex's coordinates within range, and each
    /// the same place on the texture as before.
    void testFarApartTexture()
    {
        MeshFace near = face({{0, 0, 20}, {0, 16, 20}, {16, 16, 20}, {16, 0, 20}});
        MeshFace far = face({{32, 0, 20}, {32, 16, 20}, {48, 16, 20}, {48, 0, 20}});
        for (FacePoint &p : far.points)
            p.t += 7936; // (124 repeats of 64 texels on)
        for (FacePoint &p : near.points)
            p.t -= 4000;
        const BuiltSurface s = buildGroupSurface({&near, &far}, TextureSize{64, 64}, 64, 64);
        std::vector<GxRun> runs;
        std::string error;
        check(decodeGxStream(s.words.data(), s.words.size(), runs, error), "the far-apart group decodes");
        check(!s.texCoordsClamped, "faces of one texture far apart on it are not clamped");
        bool same = true;
        for (const GxRun &run : runs)
        {
            for (const GxVertex &v : run.vertices)
            {
                // Its place on the texture, as it was: y (a corner's t
                // before the move) the same within a repeat.
                const double y = v.y / static_cast<double>(1 << hldata::kVertexShift);
                const double t = v.t / 16.0;
                const double wanted = std::fmod(std::fmod(y - (v.x / double(1 << hldata::kVertexShift) < 24 ? 4000 : -7936), 64) + 64, 64);
                same = same && std::fabs(std::fmod(std::fmod(t, 64) + 64, 64) - wanted) < 0.1;
            }
        }
        check(same, "... and each vertex is the same place on the texture as before");
    }

    void testRules()
    {
        // Two flat-lit squares side by side: one quad, with the two points on
        // the shared edge gone -- nothing else has a vertex there.
        std::vector<MeshFace> pair = {
            face({{0, 0, 80}, {0, 64, 80}, {64, 64, 80}, {64, 0, 80}}),
            face({{64, 0, 80}, {64, 64, 80}, {128, 64, 80}, {128, 0, 80}})};
        Built b = build(pair, true);
        check(b.surfaces == 1 && b.polygons == 1 && b.vertices == 4,
              "two flat squares merge into one quad");
        check(std::fabs(totalArea(b) - 2 * 64 * 64) < 1e-6, "of the same area");
        check(countCracks(b.bySurface) == 0, "with no cracks");

        b = build(pair, false);
        check(b.surfaces == 1 && b.polygons == 2 && b.vertices == 6,
              "lossless: the same two squares are one strip of two quads, 6 vertices not 8");

        // A third face whose CORNER is the middle of the long edge: after the
        // merge that vertex is mid-edge in the big face but a real corner in
        // the third. It must stay, or the edge passes through a corner.
        std::vector<MeshFace> withCorner = pair;
        withCorner.push_back(face({{64, -64, 80}, {64, 0, 80}, {128, 0, 80}, {128, -64, 80}}, 1));
        b = build(withCorner, true);
        check(countCracks(b.bySurface) == 0, "a vertex another face's corner touches is kept");
        check(std::fabs(totalArea(b) - 3 * 64 * 64) < 1e-6, "area kept with three faces");
        bool kept = false;
        for (const auto &piece : b.pieces)
            for (const GxVertex &v : piece)
                kept = kept || (v.x == 64 * 4 && v.y == 0);
        check(kept, "the touched vertex (64, 0) is still emitted");

        // A linear ramp of light across both squares is affine: still one quad.
        std::vector<MeshFace> ramp = {
            face({{0, 0, 40}, {0, 64, 40}, {64, 64, 104}, {64, 0, 104}}),
            face({{64, 0, 104}, {64, 64, 104}, {128, 64, 168}, {128, 0, 168}})};
        b = build(ramp, true);
        check(b.polygons == 1 && b.vertices == 4, "a linear ramp of light merges");
        for (const GxVertex &v : b.pieces[0])
            check(grey8(v.colour) == (v.x == 0 ? 40 : 168), "keeping the ramp's end colours");

        // A bright stripe down the middle is not affine: no merge, and the
        // middle vertices stay. They still share the stripe's vertices.
        std::vector<MeshFace> stripe = {
            face({{0, 0, 40}, {0, 64, 40}, {64, 64, 200}, {64, 0, 200}}),
            face({{64, 0, 200}, {64, 64, 200}, {128, 64, 40}, {128, 0, 40}})};
        b = build(stripe, true);
        check(b.polygons == 2 && b.vertices == 6, "a stripe of light is not merged away");

        // The two faces disagree about the light on their shared edge (the
        // lightmaps were sampled separately): they may not share vertices.
        std::vector<MeshFace> seam = {
            face({{0, 0, 80}, {0, 64, 80}, {64, 64, 80}, {64, 0, 80}}),
            face({{64, 0, 160}, {64, 64, 160}, {128, 64, 160}, {128, 0, 160}})};
        b = build(seam, true);
        check(b.surfaces == 2 && b.vertices == 8, "faces lit differently at a seam stay apart");

        // Different keys (another texture or plane) never merge or strip.
        std::vector<MeshFace> twoKeys = pair;
        twoKeys[1].key = 5;
        b = build(twoKeys, true);
        check(b.surfaces == 2 && b.vertices == 8, "faces of different textures stay apart");

        // An L of three squares. Merging any two leaves the corner the third
        // touches in the middle of the long side: a five-sided face, seven
        // vertices, where the two squares as a strip are six. So nothing is
        // merged, and the L is a strip of two and a square.
        std::vector<MeshFace> ell = {
            face({{0, 0, 80}, {0, 64, 80}, {64, 64, 80}, {64, 0, 80}}),
            face({{64, 0, 80}, {64, 64, 80}, {128, 64, 80}, {128, 0, 80}}),
            face({{0, 64, 80}, {0, 128, 80}, {64, 128, 80}, {64, 64, 80}})};
        b = build(ell, true);
        check(std::fabs(totalArea(b) - 3 * 64 * 64) < 1e-6, "an L keeps its area");
        check(countCracks(b.bySurface) == 0, "an L has no cracks");
        check(b.polygons == 3 && b.vertices == 10,
              "an L is not merged: a merge that costs vertices is not made");

        // Four squares round a point, flat light: one quad. The middle point
        // ends up INSIDE it, which is fine -- nothing has a corner there.
        std::vector<MeshFace> four = {
            face({{0, 0, 80}, {0, 64, 80}, {64, 64, 80}, {64, 0, 80}}),
            face({{64, 0, 80}, {64, 64, 80}, {128, 64, 80}, {128, 0, 80}}),
            face({{0, 64, 80}, {0, 128, 80}, {64, 128, 80}, {64, 64, 80}}),
            face({{64, 64, 80}, {64, 128, 80}, {128, 128, 80}, {128, 64, 80}})};
        b = build(four, true);
        check(b.polygons == 1 && b.vertices == 4, "four flat squares round a point are one quad");
        check(std::fabs(totalArea(b) - 4 * 64 * 64) < 1e-6, "of the same area");

        // A square with THREE extra points along one side: every zig-zag start
        // leaves some piece running past one of them. The fallback cut must
        // not.
        MeshFace comb = face({{0, 0, 80}, {0, 64, 80}, {64, 64, 80}, {64, 0, 80},
                              {48, 0, 80}, {32, 0, 80}, {16, 0, 80}});
        b = build({comb}, false);
        check(std::fabs(totalArea(b) - 64 * 64) < 1e-6, "a side with three extra points: area kept");
        check(countCracks(b.bySurface) == 0, "and no piece runs past any of them");

        // The shape that caught the fallback out on a real map: a rectangle
        // with one extra point on its top edge and one on its bottom edge. The
        // zig-zag cannot cut it cleanly, and an ear clipped in the wrong order
        // leaves three points in a line.
        MeshFace offset = face({{0, 0, 80}, {0, 24, 80}, {448, 24, 80}, {672, 24, 80},
                                {672, 0, 80}, {224, 0, 80}});
        b = build({offset}, false);
        check(std::fabs(totalArea(b) - 672 * 24) < 1e-6, "a point on each long edge: area kept");
        check(countCracks(b.bySurface) == 0, "and every piece stops at both of them");

        // A 4 x 2 grid with uneven light: lossless strips. 8 quads would be 32
        // vertices one at a time; two strips of four are 20.
        std::vector<MeshFace> grid;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
            {
                auto light = [](int gx, int gy) { return double(40 + ((gx * 7 + gy * 13) % 5) * 40); };
                grid.push_back(face({{x * 32.0, y * 32.0, light(x, y)},
                                     {x * 32.0, y * 32.0 + 32, light(x, y + 1)},
                                     {x * 32.0 + 32, y * 32.0 + 32, light(x + 1, y + 1)},
                                     {x * 32.0 + 32, y * 32.0, light(x + 1, y)}}));
            }
        b = build(grid, false);
        check(b.surfaces == 1 && b.polygons == 8, "a grid is one surface of eight quads");
        check(b.vertices == 20, "sent as two strips of four: 20 vertices, not 32");
        check(countCracks(b.bySurface) == 0 && std::fabs(totalArea(b) - 8 * 32 * 32) < 1e-6,
              "covering the same area without cracks");

        // Far apart faces are not grouped even with the same key.
        std::vector<MeshFace> far = {
            face({{0, 0, 80}, {0, 64, 80}, {64, 64, 80}, {64, 0, 80}}),
            face({{2000, 0, 80}, {2000, 64, 80}, {2064, 64, 80}, {2064, 0, 80}})};
        check(build(far, true).surfaces == 2, "faces that do not touch are separate surfaces");
    }

    // ---- whole maps --------------------------------------------------------

    /// A plane as itself, not as an index: the two cooks number planes in
    /// the order they first use them, which differs.
    using PlaneId = std::array<std::int32_t, 5>; // normal, dist, back flag

    struct ModelGeometry
    {
        std::vector<std::vector<GxVertex>> pieces;
        std::vector<std::vector<std::vector<GxVertex>>> bySurface;
        std::map<PlaneId, double> areaByPlane;
        int vertices = 0;
    };

    struct MapGeometry
    {
        std::vector<ModelGeometry> models;
        GxShapeAudit shapes;
        double lostArea = 0;
        int vertices = 0;
        bool ok = false;
    };

    MapGeometry cookAndDecode(const BspFile &bsp, bool guarded)
    {
        MapGeometry g;
        CookOptions options;
        options.guardedMerges = guarded;
        CookedLevel level;
        CookReport report;
        std::string error;
        if (!cookLevel(bsp, options, {}, level, report, error))
            return g;
        g.lostArea = report.lostArea;
        for (const hldata::Model &model : level.models)
        {
            ModelGeometry m;
            for (int i = model.firstSurface; i < model.firstSurface + model.surfaceCount; ++i)
            {
                const hldata::Surface &s = level.surfaces[i];
                std::vector<GxRun> runs;
                if (!decodeGxStream(level.gxStream.data() + s.gxOfs, s.gxWords, runs, error))
                    return g;
                const hldata::Plane &plane = level.planes[s.planeRef & hldata::kSurfacePlaneMask];
                const PlaneId id = {plane.normal[0], plane.normal[1], plane.normal[2], plane.dist,
                                    (s.planeRef & hldata::kSurfacePlaneBack) ? 1 : 0};
                const double sign = (s.planeRef & hldata::kSurfacePlaneBack) ? -1.0 : 1.0;
                const double normal[3] = {sign * plane.normal[0], sign * plane.normal[1],
                                          sign * plane.normal[2]};
                g.shapes += auditGxShapes(runs, normal);
                const std::size_t before = m.pieces.size();
                addPieces(runs, m.pieces);
                m.bySurface.emplace_back();
                addPieces(runs, m.bySurface.back());
                for (std::size_t k = before; k < m.pieces.size(); ++k)
                    m.areaByPlane[id] += pieceArea(m.pieces[k]);
                m.vertices += s.vertices;
            }
            g.vertices += m.vertices;
            g.models.push_back(std::move(m));
        }
        g.ok = true;
        return g;
    }

    /// The three claims, on a whole map: guarded against lossless, where
    /// lossless is the map's own vertices merely shared. Model by model: a
    /// crack is two faces of ONE object failing to meet, and a door's vertices
    /// have nothing to say about the wall's.
    void auditMap(const BspFile &bsp, const char *name)
    {
        const MapGeometry plain = cookAndDecode(bsp, false);
        const MapGeometry guarded = cookAndDecode(bsp, true);
        check(plain.ok && guarded.ok && plain.models.size() == guarded.models.size(),
              "map cooks both ways");
        if (!plain.ok || !guarded.ok || plain.models.size() != guarded.models.size())
            return;

        // AREA against the map itself. The plane-by-plane comparison below
        // holds one cook against the other, and both go through the same
        // cutter: a piece it leaves out is missing from both.
        if (plain.lostArea > 4.0 || guarded.lostArea > 4.0)
            std::printf("  %s: %.1f (lossless) and %.1f (guarded) square units of the faces "
                        "are not covered\n", name, plain.lostArea, guarded.lostArea);
        check(plain.lostArea <= 4.0 && guarded.lostArea <= 4.0,
              "the surfaces cover every face of the map");

        // SHAPE: what the hardware judges a polygon's facing by must be a
        // turn, and the right way round.
        for (const MapGeometry *g : {&plain, &guarded})
        {
            if (!g->shapes.clean())
            {
                std::printf("  %s: of %d polygons %d lead with three in line, %d are reversed, "
                            "%d have a reflex corner, %d have no area\n",
                            name, g->shapes.polygons, g->shapes.flatLead, g->shapes.reversed,
                            g->shapes.reflex, g->shapes.noArea);
            }
            check(g->shapes.clean(), "every polygon leads with a turn the way it faces");
        }

        int badPlanes = 0, cracksBefore = 0, cracksAfter = 0, dropped = 0, wrong = 0;
        for (std::size_t m = 0; m < plain.models.size(); ++m)
        {
            const ModelGeometry &pm = plain.models[m];
            const ModelGeometry &gm = guarded.models[m];

            // AREA, plane by plane. The tolerance is for the grid, not for
            // the optimiser: a vertex on a sloping plane snaps to the nearest
            // quarter unit and ends up slightly off its plane, after which
            // the area of a face depends a little on how it is cut. A face
            // going missing is units, not hundredths.
            for (const auto &entry : pm.areaByPlane)
            {
                const auto it = gm.areaByPlane.find(entry.first);
                const double after = it == gm.areaByPlane.end() ? 0.0 : it->second;
                if (std::fabs(entry.second - after) > 0.5 + 0.005 * entry.second)
                    ++badPlanes;
            }
            for (const auto &entry : gm.areaByPlane)
            {
                if (pm.areaByPlane.find(entry.first) == pm.areaByPlane.end() && entry.second > 0.5)
                    ++badPlanes;
            }

            // CRACKS: the map as compiled has some of its own (and snapping
            // makes a few more); merging must not add any.
            const int before = countCracks(pm.bySurface);
            const int after = countCracks(gm.bySurface);
            cracksBefore += before;
            cracksAfter += after;
            if (after > before)
                check(false, "merging adds no cracks to any model");

            // LIGHT: at every vertex the plain cook has, the guarded cook
            // shows the same colour -- because the vertex is still there with
            // it, or because the polygon now covering that point interpolates
            // to it, to within a colour step.
            std::map<Key, std::vector<std::uint16_t>> guardedAt;
            for (const auto &piece : gm.pieces)
                for (const GxVertex &v : piece)
                    guardedAt[Key{v.x, v.y, v.z}].push_back(v.colour);
            std::map<std::array<int, 4>, char> done;
            for (const auto &piece : pm.pieces)
            {
                for (const GxVertex &v : piece)
                {
                    if (!done.insert({{v.x, v.y, v.z, v.colour}, 1}).second)
                        continue;
                    const auto it = guardedAt.find(Key{v.x, v.y, v.z});
                    if (it != guardedAt.end() &&
                        std::find(it->second.begin(), it->second.end(), v.colour) !=
                            it->second.end())
                        continue;
                    ++dropped;
                    // What colour does the guarded cook draw at this point?
                    //
                    // The DS does not split a polygon into triangles: it walks
                    // the polygon's own edges, scanline by scanline. So a point
                    // ON an edge of a piece gets the blend of that edge's two
                    // ends, and that is checked first and exactly. Only a
                    // point strictly inside a piece is interpolated across it,
                    // and for that a triangle of the piece is a fair model:
                    // pieces only swallow a vertex when their colour is a
                    // plane to within half a step.
                    auto closeTo = [&](const double *blend)
                    {
                        for (int c = 0; c < 3; ++c)
                        {
                            // One colour step (8), and a hair for arithmetic.
                            if (std::fabs(blend[c] - ((v.colour >> (5 * c)) & 31) * 8.0) > 8.5)
                                return false;
                        }
                        return true;
                    };
                    bool explained = false;
                    bool onAnEdge = false;
                    for (const auto &gp : gm.pieces)
                    {
                        for (std::size_t i = 0; i < gp.size() && !explained; ++i)
                        {
                            const GxVertex &p = gp[i];
                            const GxVertex &q = gp[(i + 1) % gp.size()];
                            if (!strictlyBetween(p, v, q))
                                continue;
                            onAnEdge = true;
                            const double pv = std::hypot(
                                std::hypot(double(v.x - p.x), double(v.y - p.y)), double(v.z - p.z));
                            const double pq = std::hypot(
                                std::hypot(double(q.x - p.x), double(q.y - p.y)), double(q.z - p.z));
                            double blend[3];
                            for (int c = 0; c < 3; ++c)
                            {
                                const double cp = ((p.colour >> (5 * c)) & 31) * 8.0;
                                const double cq = ((q.colour >> (5 * c)) & 31) * 8.0;
                                blend[c] = cp + (cq - cp) * pv / pq;
                            }
                            explained = closeTo(blend);
                        }
                        if (explained)
                            break;
                    }
                    for (std::size_t g = 0; g < gm.pieces.size() && !explained && !onAnEdge; ++g)
                    {
                        const auto &gp = gm.pieces[g];
                        for (std::size_t i = 1; i + 1 < gp.size() && !explained; ++i)
                        {
                            const GxVertex *tri[3] = {&gp[0], &gp[i], &gp[i + 1]};
                            double w[3];
                            if (!barycentric(*tri[0], *tri[1], *tri[2], v, w))
                                continue;
                            double blend[3] = {0, 0, 0};
                            for (int c = 0; c < 3; ++c)
                                for (int k = 0; k < 3; ++k)
                                    blend[c] += w[k] * ((tri[k]->colour >> (5 * c)) & 31) * 8.0;
                            explained = closeTo(blend);
                        }
                    }
                    if (!explained)
                        ++wrong;
                }
            }
        }
        check(badPlanes == 0, "every plane is covered by the same area");
        check(wrong == 0, "the light at every original vertex is what it was");
        std::printf("test_mesh: %s: %d -> %d vertices, %d vertex colours re-derived, "
                    "cracks %d -> %d, planes off %d, light off %d\n",
                    name, plain.vertices, guarded.vertices, dropped, cracksBefore, cracksAfter,
                    badPlanes, wrong);
    }

    void testMaps()
    {
        BspFile box;
        std::string error;
        check(box.parse(buildBoxRoom(), error), "box room parses");
        auditMap(box, "box room");

        const char *dir = std::getenv("HLDS_VALVE_DIR");
        if (dir == nullptr || *dir == '\0')
        {
            std::printf("test_mesh: HLDS_VALVE_DIR not set, real-map audit skipped\n");
            return;
        }
        for (const char *map : {"c1a0", "c0a0", "c2a5"})
        {
            BspFile bsp;
            if (!bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
            {
                check(false, (std::string(map) + " loads").c_str());
                continue;
            }
            auditMap(bsp, map);
        }
    }
} // namespace

int main()
{
    testRules();
    testFarApartTexture();
    testMaps();
    if (g_failures == 0)
        std::printf("test_mesh: ok\n");
    return g_failures == 0 ? 0 : 1;
}
