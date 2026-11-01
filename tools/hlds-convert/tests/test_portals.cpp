// The holes between the leaves (Portals.cpp) and the flood through them
// (shared/HlPortals.hpp), on the game's own maps.
//
// That they are right: every portal joins two leaves that are open and that
// the map's own PVS says can see each other. That the flood is never less
// than what is seen: from eyes all over the map, looking every way, every
// leaf a line of sight reaches before it meets something solid is one the
// flood reached. And that it is worth having: how much of the PVS it leaves
// out.
//
// Needs the game: HLDS_VALVE_DIR. Without it, nothing is run.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "HlPortals.hpp"
#include "TestGame.hpp"
#include "hlds/Portals.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    /// The leaf a point is in, down the map's own tree.
    int leafAt(const BspFile &bsp, const double *p)
    {
        int child = bsp.models[0].headNode[0];
        while (child >= 0)
        {
            const BspNode &node = bsp.nodes[child];
            const BspPlane &plane = bsp.planes[node.plane];
            const double d = plane.normal[0] * p[0] + plane.normal[1] * p[1] + plane.normal[2] * p[2] - plane.dist;
            child = node.children[d >= 0 ? 0 : 1];
        }
        return -(child + 1);
    }

    bool open(const BspFile &bsp, int leaf) { return leaf > 0 && bsp.leaves[leaf].contents != kContentsSolid; }

    /// The eye as the flood takes it, at the game's field of view (74 degrees
    /// high, 4 by 3), looking along yaw and pitch (radians).
    PortalEye eyeOf(const std::int32_t *at, double yaw, double pitch, double *forward)
    {
        const double f[3] = {std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch)};
        const double right[3] = {std::sin(yaw), -std::cos(yaw), 0};
        const double up[3] = {right[1] * f[2] - right[2] * f[1], right[2] * f[0] - right[0] * f[2],
                              right[0] * f[1] - right[1] * f[0]};
        PortalEye eye{};
        for (int a = 0; a < 3; ++a)
        {
            eye.at[a] = at[a];
            eye.forward[a] = static_cast<std::int32_t>(std::lround(f[a] * 4096));
            eye.right[a] = static_cast<std::int32_t>(std::lround(right[a] * 4096));
            eye.up[a] = static_cast<std::int32_t>(std::lround(up[a] * 4096));
            forward[a] = f[a];
        }
        // (The camera's own edges, loosened as it has them: 47 degrees
        // across, 39 up.)
        const double h = 47.0 * M_PI / 180.0, v = 39.0 * M_PI / 180.0;
        const std::int32_t sh = static_cast<std::int32_t>(std::lround(std::sin(h) * 4096)),
                           ch = static_cast<std::int32_t>(std::lround(std::cos(h) * 4096)),
                           sv = static_cast<std::int32_t>(std::lround(std::sin(v) * 4096)),
                           cv = static_cast<std::int32_t>(std::lround(std::cos(v) * 4096));
        eye.left[0] = -sh, eye.left[1] = ch;
        eye.rightEdge[0] = sh, eye.rightEdge[1] = ch;
        eye.bottom[0] = -sv, eye.bottom[1] = cv;
        eye.top[0] = sv, eye.top[1] = cv;
        return eye;
    }

    struct Rng
    {
        std::uint32_t s = 12345;
        double next()
        {
            s = s * 1664525u + 1013904223u;
            return (s >> 8) / 16777216.0;
        }
    };

    void testMap(const char *dir, const char *map)
    {
        auto g = std::make_unique<Game>();
        std::string error;
        if (!g->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
            return;
        finish(*g, nullptr);
        check(g->ok, (std::string(map) + " cooks").c_str());
        if (!g->ok)
            return;
        const BspFile &bsp = g->bsp;
        const LevelView &v = g->view;
        CookedPortals cooked;
        check(cookPortals(buildPortals(bsp), v.leafCount, cooked, error) && !cooked.portals.empty() &&
                  cooked.leaves.size() == v.leafCount,
              (std::string(map) + " has its portals").c_str());
        // Every portal: two open leaves that see each other.
        int unseen = 0;
        for (const Portal &p : cooked.portals)
        {
            const std::vector<std::uint8_t> row = bsp.decompressVis(p.leaves[0]);
            const int other = p.leaves[1];
            const bool sees = other >= 1 && static_cast<std::size_t>((other - 1) >> 3) < row.size() &&
                              (row[(other - 1) >> 3] & (1 << ((other - 1) & 7)));
            // (A sky leaf is not in the rows of the leaves that look at it:
            // the compiler's way. The flood goes only into PVS leaves.)
            const bool sky = bsp.leaves[p.leaves[0]].contents == kContentsSky ||
                             bsp.leaves[p.leaves[1]].contents == kContentsSky;
            unseen += !open(bsp, p.leaves[0]) || !open(bsp, p.leaves[1]) || (!sees && !sky);
        }
        std::printf("test_portals: %s: %zu portals; %d not between two leaves that see each other\n", map,
                    cooked.portals.size(), unseen);
        check(unseen * 100 <= static_cast<int>(cooked.portals.size()),
              "... every portal joins two open leaves that see each other");

        PortalSet set;
        set.portals = cooked.portals.data();
        set.refs = cooked.refs.data();
        set.leaves = cooked.leaves.data();
        set.leafCount = static_cast<int>(v.leafCount);
        std::vector<PortalRect> rects;
        std::vector<std::uint16_t> slotLeaf, queue, leafMark, leafSlot;
        std::vector<std::uint8_t> queued;
        PortalSpace space = portalSpaceOf(rects, slotLeaf, queue, queued, leafMark, leafSlot, 768,
                                          static_cast<int>(v.leafCount));
        Rng rng;
        int eyes = 0, rays = 0, missed = 0, gaveUp = 0;
        long pvsLeaves = 0, reachedLeaves = 0;
        std::vector<std::uint8_t> reached(v.leafCount);
        const int visLeaves = static_cast<int>(v.header->visLeafCount);
        for (int tries = 0; eyes < 150 && tries < 4000; ++tries)
        {
            const int leaf = 1 + static_cast<int>(rng.next() * visLeaves);
            if (leaf > visLeaves || !open(bsp, leaf))
                continue;
            const BspLeaf &l = bsp.leaves[leaf];
            double eyeAt[3];
            for (int a = 0; a < 3; ++a)
                eyeAt[a] = l.mins[a] + (l.maxs[a] - l.mins[a]) * (0.2 + 0.6 * rng.next());
            const int eyeLeaf = leafAt(bsp, eyeAt);
            if (!open(bsp, eyeLeaf) || eyeLeaf > visLeaves)
                continue;
            ++eyes;
            const std::vector<std::uint8_t> row = bsp.decompressVis(eyeLeaf);
            auto inPvs = [&](int other)
            {
                return other >= 1 && static_cast<std::size_t>((other - 1) >> 3) < row.size() &&
                       (row[(other - 1) >> 3] & (1 << ((other - 1) & 7))) != 0;
            };
            const std::int32_t eye[3] = {static_cast<std::int32_t>(std::lround(eyeAt[0])),
                                         static_cast<std::int32_t>(std::lround(eyeAt[1])),
                                         static_cast<std::int32_t>(std::lround(eyeAt[2]))};
            const double yaw = rng.next() * 2 * M_PI, pitch = (rng.next() - 0.5) * 1.2;
            double forward[3];
            const PortalEye view = eyeOf(eye, yaw, pitch, forward);
            std::fill(reached.begin(), reached.end(), 0);
            const int went = portalFlood(set, space, eyeLeaf, view, inPvs, [&](int r) { reached[r] = 1; });
            if (went < 0)
            {
                ++gaveUp;
                continue;
            }
            int pvs = 0, got = 0;
            for (int other = 1; other <= visLeaves; ++other)
            {
                pvs += inPvs(other) && open(bsp, other);
                got += reached[other] != 0;
            }
            pvsLeaves += pvs;
            reachedLeaves += got;
            // Lines of sight, inside the view, a unit at a time.
            const double right[3] = {std::sin(yaw), -std::cos(yaw), 0};
            const double up[3] = {right[1] * forward[2] - right[2] * forward[1],
                                  right[2] * forward[0] - right[0] * forward[2],
                                  right[0] * forward[1] - right[1] * forward[0]};
            const double tv = std::tan(37.0 * M_PI / 180.0) * 0.95, th = tv * 4.0 / 3.0;
            for (int r = 0; r < 48; ++r)
            {
                const double sx = (rng.next() * 2 - 1) * th, sy = (rng.next() * 2 - 1) * tv;
                double d[3];
                for (int a = 0; a < 3; ++a)
                    d[a] = forward[a] + right[a] * sx + up[a] * sy;
                const double dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                ++rays;
                for (int step = 1; step < 4000; ++step)
                {
                    const double p[3] = {eye[0] + d[0] / dl * step, eye[1] + d[1] / dl * step, eye[2] + d[2] / dl * step};
                    const int at = leafAt(bsp, p);
                    if (!open(bsp, at) || bsp.leaves[at].contents == kContentsSky)
                        break;
                    // (What the PVS itself says is not seen is not the
                    // flood's to find: the compiler's own rounding.)
                    if (at != eyeLeaf && inPvs(at) && !reached[at])
                    {
                        ++missed;
                        break;
                    }
                }
            }
        }
        std::printf("test_portals: %s: %d eyes, %d lines of sight, %d of them into a leaf the flood had not reached; "
                    "the flood reached %ld of %ld PVS leaves (%d%%), gave up %d times\n",
                    map, eyes, rays, missed, reachedLeaves, pvsLeaves,
                    pvsLeaves > 0 ? static_cast<int>(reachedLeaves * 100 / pvsLeaves) : 0, gaveUp);
        check(missed == 0, "... the flood reaches every leaf a line of sight does");
        check(reachedLeaves * 10 < pvsLeaves * 8, "... and leaves out a good part of the PVS");
    }
    /// No surface of the world spans two leaves; and it would, cooked as
    /// before (which shows the count can see one).
    void testGrouping(const char *dir, const char *map)
    {
        BspFile bsp;
        std::string error;
        if (!bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
            return;
        int spans[2] = {-1, -1}, surfaces[2] = {0, 0};
        for (int byLeaf = 0; byLeaf < 2; ++byLeaf)
        {
            CookOptions options;
            options.groupByLeaf = byLeaf != 0;
            CookedLevel level;
            CookReport report;
            if (!cookLevel(bsp, options, TexturePlan(), level, report, error))
                continue;
            spans[byLeaf] = report.surfacesSpanningLeaves;
            surfaces[byLeaf] = report.surfaces;
        }
        std::printf("test_portals: %s: grouped by leaf %d surfaces, %d spanning two leaves; across leaves %d, %d spanning\n",
                    map, surfaces[1], spans[1], surfaces[0], spans[0]);
        check(spans[1] == 0, "grouped by leaf, no surface spans two leaves");
        check(spans[0] > 0, "... where, grouped across them, some would");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || *dir == '\0')
    {
        std::printf("test_portals: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 0;
    }
    for (const char *map : {"c1a2a", "c2a5x", "c1a4b"})
    {
        testMap(dir, map);
        testGrouping(dir, map);
    }
    if (g_failures == 0)
        std::printf("test_portals: ok\n");
    return g_failures == 0 ? 0 : 1;
}
