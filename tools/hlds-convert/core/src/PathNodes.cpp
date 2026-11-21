#include "hlds/PathNodes.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "HlMonsters.hpp"

namespace hlds
{
    using namespace hldata;

    namespace
    {
        struct Way
        {
            int from = 0;
            int to = 0;
            std::uint8_t flags = 0;
            double length = 0;
            std::uint16_t brush = kNoBrush;
        };

        double distance(const PathNode &a, const PathNode &b)
        {
            const double d[3] = {(b.pos[0] - a.pos[0]) / 4096.0, (b.pos[1] - a.pos[1]) / 4096.0,
                                 (b.pos[2] - a.pos[2]) / 4096.0};
            return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        }

        /// Whether something of that size walks from one to the other, by
        /// the console's own walk.
        bool walks(const MoveWorld &world, int hull, const PathNode &a, const PathNode &b)
        {
            return walkClear(world, nullptr, -1, hull, a.pos, b.pos, static_cast<int>(distance(a, b) / 16) + 4);
        }
    } // namespace

    PathReport cookPathNodes(const std::vector<Entity> &entities, CookedLevel &level, const NodGraph *graph)
    {
        PathReport report;
        level.pathNodes.clear();
        level.pathLinks.clear();
        if (level.models.empty())
            return report;

        MoveWorld world;
        world.standing = Hull{level.clipNodes.data(), level.planes.data(), level.planeTypes.data(),
                              level.models[0].clipStanding};
        world.crouched = Hull{level.clipNodes.data(), level.planes.data(), level.planeTypes.data(),
                              level.models[0].clipCrouched};
        world.large = Hull{level.clipNodes.data(), level.planes.data(), level.planeTypes.data(),
                           level.models[0].clipLarge};

        std::vector<Way> ways;
        // The brush a map's model number is (a link names its door as the
        // game does: "*12").
        std::map<int, std::uint16_t> brushOfModel;
        for (std::size_t b = 0; b < level.brushes.size(); ++b)
            brushOfModel[level.brushes[b].model] = static_cast<std::uint16_t>(b);

        // A map with something that flies (an alien controller, placed or
        // made) has the nodes in the air too, and the ways that only what
        // flies takes -- after all the rest, where there is still room.
        bool fliers = false;
        for (const Entity &entity : entities)
        {
            const std::string cls(entity.className());
            fliers = fliers || cls == "monster_alien_controller" ||
                     (cls == "monstermaker" && std::string(entity.value("monstertype")) == "monster_alien_controller");
        }
        if (graph != nullptr && !graph->nodes.empty())
        {
            // The game's own: its nodes on the ground and in the water (and
            // in the air, for what flies: above), in its order; and its links
            // for what walks (and flies).
            report.fromGraph = true;
            std::vector<int> ours(graph->nodes.size(), -1);
            // (More than the format holds: those nothing walks to or from
            // go first.)
            std::vector<char> walked(graph->nodes.size(), 0);
            for (const NodLink &link : graph->links)
            {
                if (link.info & (kNodSmall | kNodHuman | kNodLarge))
                    walked[link.from] = walked[link.to] = 1;
            }
            for (int pass = 0; pass < 3; ++pass)
            {
                for (std::size_t n = 0; n < graph->nodes.size(); ++n)
                {
                    const NodNode &src = graph->nodes[n];
                    const bool air = (src.info & kNodAir) != 0;
                    if (ours[n] >= 0)
                        continue;
                    if (pass == 2 ? !(air && fliers)
                                  : (air || !(src.info & (kNodLand | kNodWater)) || (pass == 0) != (walked[n] != 0)))
                        continue;
                    if (level.pathNodes.size() >= static_cast<std::size_t>(kMaxPathNodes))
                        continue;
                    PathNode node{};
                    for (int a = 0; a < 3; ++a)
                        node.pos[a] = fxFromDouble(src.origin[a]);
                    node.flags = (src.info & kNodWater) ? kPathNodeWater : (air ? kPathNodeAir : 0);
                    ours[n] = static_cast<int>(level.pathNodes.size());
                    level.pathNodes.push_back(node);
                }
            }
            for (std::size_t n = 0; n < graph->nodes.size(); ++n)
                report.nodesLeftOut += ours[n] < 0 ? 1 : 0;

            for (const NodLink &link : graph->links)
            {
                if (ours[link.from] < 0 || ours[link.to] < 0 || link.from == link.to)
                    continue;
                Way way;
                way.from = ours[link.from];
                way.to = ours[link.to];
                way.length = link.length;
                if (link.info & kNodSmall)
                    way.flags |= kPathLinkSmall;
                if (link.info & kNodHuman)
                    way.flags |= kPathLinkHuman;
                if (link.info & kNodLarge)
                    way.flags |= kPathLinkLarge;
                if ((link.info & kNodFly) && fliers)
                    way.flags |= kPathLinkFly;
                if (way.flags == 0)
                    continue; // (for what flies only, and nothing here does)
                if (link.model != 0)
                {
                    const auto found = brushOfModel.find(link.model);
                    // (Through something the port does not carry as a brush
                    // of its own: then it is simply a way.)
                    if (found != brushOfModel.end())
                        way.brush = found->second;
                }
                // Tried with the console's own walk, as a count only. It is
                // the game's word that is kept: where the two disagree it is
                // nearly always over a floor or a step that is a brush
                // entity (a grate, a lift at rest), which this trial -- of
                // the world alone -- has not got under it, and the game had.
                // A link the console really cannot walk costs whoever takes
                // it a second's delay and another plan, not a wall.
                const PathNode &a = level.pathNodes[way.from];
                const PathNode &b = level.pathNodes[way.to];
                const bool wet = (a.flags | b.flags) & kPathNodeWater;
                const bool walked = way.flags & (kPathLinkSmall | kPathLinkHuman | kPathLinkLarge);
                if (!wet && walked && !walks(world, (way.flags & kPathLinkHuman) ? kWalkStanding : kWalkCrouched, a, b))
                    ++report.refused;
                ways.push_back(way);
            }
        }
        else
        {
            // No graph of the game's: the map's info_nodes, onto the floor
            // under them, and every pair of them that can be walked between
            // both ways.
            for (const Entity &entity : entities)
            {
                if (entity.className() != "info_node")
                    continue;
                double origin[3] = {0, 0, 0};
                entity.vec3("origin", origin);
                ActorState probe;
                for (int a = 0; a < 3; ++a)
                    probe.pos[a] = fxFromDouble(origin[a]);
                if (!actorToFloor(world, probe, fxFromInt(8), fxFromInt(256)))
                    continue; // in a wall, or over nothing
                if (level.pathNodes.size() >= static_cast<std::size_t>(kMaxPathNodes))
                {
                    ++report.nodesLeftOut;
                    continue;
                }
                PathNode node{};
                for (int a = 0; a < 3; ++a)
                    node.pos[a] = probe.pos[a];
                level.pathNodes.push_back(node);
            }
            const int count = static_cast<int>(level.pathNodes.size());
            for (int a = 0; a < count; ++a)
            {
                for (int b = a + 1; b < count; ++b)
                {
                    const double length = distance(level.pathNodes[a], level.pathNodes[b]);
                    if (length > 1024 || !walks(world, kWalkStanding, level.pathNodes[a], level.pathNodes[b]) ||
                        !walks(world, kWalkStanding, level.pathNodes[b], level.pathNodes[a]))
                        continue;
                    Way way;
                    way.from = a;
                    way.to = b;
                    way.length = length;
                    // (For every size, as one table used to serve them all.)
                    way.flags = kPathLinkSmall | kPathLinkHuman | kPathLinkLarge;
                    ways.push_back(way);
                    std::swap(way.from, way.to);
                    ways.push_back(way);
                }
            }
        }

        // Ways down. From a node to one below it and not far off, where
        // there is no way already: out level over the edge with nothing in
        // the way, and down onto it. One way only. (Not on the game's own
        // graph: its monsters have no such ways, and a route or a cover
        // spot by one is somewhere the game's would never go.)
        const int count = static_cast<int>(level.pathNodes.size());
        for (int a = 0; a < (report.fromGraph ? 0 : count); ++a)
        {
            for (int b = 0; b < count; ++b)
            {
                const PathNode &top = level.pathNodes[a];
                const PathNode &foot = level.pathNodes[b];
                const fx fall = top.pos[2] - foot.pos[2];
                if (a == b || fall < fxFromInt(40) || fall > fxFromInt(280) ||
                    ((top.flags | foot.flags) & kPathNodeWater))
                    continue;
                const fx d[3] = {foot.pos[0] - top.pos[0], foot.pos[1] - top.pos[1], 0};
                if (fxLength(d) > fxFromInt(224))
                    continue;
                bool known = false;
                for (const Way &way : ways)
                    known = known || (way.from == a && way.to == b);
                if (known)
                    continue;
                const fx lift = kStandingFeet + fxFromInt(2);
                const fx from[3] = {top.pos[0], top.pos[1], top.pos[2] + lift};
                const fx over[3] = {foot.pos[0], foot.pos[1], top.pos[2] + lift};
                const fx down[3] = {foot.pos[0], foot.pos[1], foot.pos[2] + lift - fxFromInt(1)};
                const Trace across = traceAll(world, false, from, over);
                if (across.startSolid || across.fraction < kFractionOne)
                    continue;
                const Trace descent = traceAll(world, false, over, down);
                if (descent.startSolid || descent.fraction < kFractionOne)
                    continue;
                Way way;
                way.from = a;
                way.to = b;
                way.length = distance(top, foot);
                way.flags = kPathLinkSmall | kPathLinkHuman | kPathLinkDrop;
                ways.push_back(way);
                ++report.dropLinks;
            }
        }

        // Grouped by where they start, the nearest first; a node has room
        // for 255.
        std::stable_sort(ways.begin(), ways.end(), [](const Way &x, const Way &y)
                         { return x.from != y.from ? x.from < y.from : x.length < y.length; });
        for (const Way &way : ways)
        {
            PathNode &node = level.pathNodes[way.from];
            if (node.linkCount == 0)
                node.firstLink = static_cast<std::uint16_t>(level.pathLinks.size());
            if (node.linkCount == 255 || level.pathLinks.size() >= 0xFFFF)
                continue;
            // (One link a pair: the game's graph has none twice, and neither
            // has the working out.)
            PathLink link{};
            link.to = static_cast<std::uint8_t>(way.to);
            link.flags = way.flags;
            // (In sixteenths: the longest of the game's, c1a4's 3,544, is
            // 56,697 of them. One longer than the field holds would be a way
            // made out sixteen times too short: refused, not cut down.)
            const long sixteenths = std::lround(way.length * (1 << kPathLinkShift));
            if (sixteenths > 65535)
            {
                ++report.linksTooLong;
                continue;
            }
            link.length = static_cast<std::uint16_t>(sixteenths < 1 ? 1 : sixteenths);
            if (way.length > report.longestLink)
                report.longestLink = way.length;
            link.brush = way.brush;
            level.pathLinks.push_back(link);
            ++node.linkCount;
            report.doorLinks += way.brush != kNoBrush ? 1 : 0;
        }
        report.nodes = count;
        report.links = static_cast<int>(level.pathLinks.size());
        return report;
    }
} // namespace hlds
