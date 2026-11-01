// The ways between a map's path nodes: read from the game's own graphs,
// and found on the console as they are needed.
//
// What matters is that a way found is a way that can be walked NOW: not
// through a door that is locked, not by something too large for it, and
// through a door that opens only for whoever opens doors.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include "TestGame.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/NodGraph.hpp"

namespace fs = std::filesystem;
using namespace hldata;
using namespace hldstest;

namespace
{
    /// A graph by hand:   0 --- 1 ==door== 2 --- 3      (0-1-2-3 in a row)
    ///                     \_______ 4 ________/         (and the long way round, by 4)
    /// 1-2 is through a door; 0-4-3 is for small things only.
    struct Hand
    {
        PathNode nodes[5]{};
        PathLink links[10]{};
        std::uint8_t shut[10]{};
        ActorWorld a;
        Hand()
        {
            int next = 0;
            auto join = [&](int from, int to, std::uint8_t flags, int length, std::uint16_t brush)
            {
                if (nodes[from].linkCount == 0)
                    nodes[from].firstLink = static_cast<std::uint16_t>(next);
                links[next].to = static_cast<std::uint8_t>(to);
                links[next].flags = flags;
                links[next].length = static_cast<std::uint16_t>(length);
                links[next].brush = brush;
                ++nodes[from].linkCount;
                ++next;
            };
            const std::uint8_t all = kPathLinkSmall | kPathLinkHuman | kPathLinkLarge;
            join(0, 1, all, 100, kNoBrush);
            join(0, 4, kPathLinkSmall, 300, kNoBrush);
            join(1, 0, all, 100, kNoBrush);
            join(1, 2, all, 100, 7);
            join(2, 1, all, 100, 7);
            join(2, 3, all, 100, kNoBrush);
            join(3, 2, all, 100, kNoBrush);
            join(3, 4, kPathLinkSmall, 300, kNoBrush);
            join(4, 0, kPathLinkSmall, 300, kNoBrush);
            join(4, 3, kPathLinkSmall, 300, kNoBrush);
            a.nodes = nodes;
            a.links = links;
            a.nodeCount = 5;
            a.linkCount = next;
            a.linkShut = shut;
        }
        void door(std::uint8_t state)
        {
            shut[3] = shut[4] = state;
        }
    };

    void testSearch()
    {
        Hand h;
        check(pathStep(h.a, 0, 3, kPathLinkHuman, false) == 1 && pathStep(h.a, 1, 3, kPathLinkHuman, false) == 2 &&
                  pathStep(h.a, 2, 3, kPathLinkHuman, false) == 3,
              "the short way, a step at a time, with the door open");
        check(pathStep(h.a, 3, 3, kPathLinkHuman, false) == 3, "where it is going is where it is");
        h.door(kLinkDoor);
        check(pathStep(h.a, 0, 3, kPathLinkHuman, true) == 1, "whoever opens doors goes through a shut one");
        check(pathStep(h.a, 0, 3, kPathLinkHuman, false) == kNoPathNode,
              "a person who does not, and has no other way, has no way");
        check(pathStep(h.a, 0, 3, kPathLinkSmall, false) == 4,
              "something small that does not takes the long way round, which is its alone");
        check(pathStep(h.a, 0, 3, kPathLinkLarge, true) == 1 && pathStep(h.a, 0, 4, kPathLinkLarge, true) == kNoPathNode,
              "something large is not sent where only the small go");
        h.door(kLinkNoWay);
        check(pathStep(h.a, 0, 3, kPathLinkHuman, true) == kNoPathNode, "a locked door is no way for anybody");
        check(pathStep(h.a, 0, 3, kPathLinkSmall, true) == 4, "... but round it, for whoever fits");
        h.door(kLinkOpen);
        check(pathLinkBetween(h.a, 1, 2) == 3 && pathLinkBetween(h.a, 1, 3) < 0, "the link between two nodes is found");
        check(pathStep(h.a, 0, 9, kPathLinkHuman, true) == kNoPathNode && pathStep(h.a, -1, 3, kPathLinkHuman, true) == kNoPathNode,
              "a node that is not there is no way");
    }

    void testNodFile(const char *valve)
    {
        std::vector<std::uint8_t> bytes;
        std::string error;
        hlds::NodGraph graph;
        check(hlds::readFile(fs::path(valve) / "maps" / "graphs" / "c1a1a.nod", bytes, error) &&
                  hlds::parseNodGraph(bytes, graph, error),
              "c1a1a's graph is read");
        check(graph.nodes.size() == 25 && graph.links.size() == 71, "25 nodes and 71 links, as the file has them");
        check(!graph.links.empty() && graph.links[0].from == 0 && graph.links[0].to == 1 && graph.links[0].model == 5 &&
                  graph.links[0].info == 7 && graph.links[0].length > 359 && graph.links[0].length < 361,
              "its first link goes through the door that is model 5, for all but what flies, 360 units");
        check(!graph.nodes.empty() && graph.nodes[0].info == hlds::kNodLand && graph.nodes[0].origin[0] == -2272.0f,
              "its first node is on the ground, where the file says");
        // Damage.
        hlds::NodGraph bad;
        auto cut = bytes;
        cut.resize(cut.size() - 2);
        check(!hlds::parseNodGraph(cut, bad, error), "a graph cut short is refused");
        cut = bytes;
        cut[0] = 15;
        check(!hlds::parseNodGraph(cut, bad, error), "... and one of another version");
        cut = bytes;
        cut[4 + 24] = 99; // (says it has more nodes than it has)
        check(!hlds::parseNodGraph(cut, bad, error), "... and one that says it holds what it does not");
    }

    void testCooked(const char *valve)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        std::vector<std::uint8_t> bytes;
        const bool read = game->bsp.load(std::string(valve) + "/maps/c1a1a.bsp", error) &&
                          hlds::readFile(fs::path(valve) / "maps" / "graphs" / "c1a1a.nod", bytes, error) &&
                          hlds::parseNodGraph(bytes, game->nodGraph, error);
        if (read)
            finish(*game, nullptr, valve);
        check(read && game->ok, "c1a1a cooks");
        if (!read || !game->ok)
            return;
        const LevelView &v = game->view;
        check(v.pathNodeCount == 25, "all 25 of the game's nodes are kept");
        int doors = 0, bad = 0;
        for (std::uint32_t l = 0; l < v.pathLinkCount; ++l)
        {
            const PathLink &link = v.pathLinks[l];
            bad += link.to >= v.pathNodeCount || link.flags == 0 || link.length == 0;
            if (link.brush == kNoBrush)
                continue;
            ++doors;
            // What it passes through is a door of the map's.
            bool isDoor = false;
            for (std::uint32_t r = 0; r < v.logicCount; ++r)
                isDoor = isDoor || (v.logic[r].brush == link.brush &&
                                    (v.logic[r].kind == static_cast<std::uint8_t>(LogicKind::Door) ||
                                     v.logic[r].kind == static_cast<std::uint8_t>(LogicKind::DoorRotating)));
            bad += !isDoor;
        }
        std::printf("test_graph: c1a1a: %u nodes, %u links, %d of them through doors\n", v.pathNodeCount, v.pathLinkCount,
                    doors);
        check(bad == 0 && doors >= 2, "its links are sound, and those through a door know which door");
        // The two halves of this map are joined by its doors and nothing
        // else: with them shut, whoever does not open doors stays in theirs.
        ActorWorld &a = game->actors;
        int throughDoors = 0, without = 0;
        for (int l = 0; l < a.linkCount; ++l)
            a.linkShut[l] = a.links[l].brush != kNoBrush ? kLinkDoor : kLinkOpen;
        for (int from = 0; from < a.nodeCount; ++from)
        {
            for (int to = 0; to < a.nodeCount; ++to)
            {
                throughDoors += pathStep(a, from, to, kPathLinkHuman, true) != kNoPathNode;
                without += pathStep(a, from, to, kPathLinkHuman, false) != kNoPathNode;
            }
        }
        check(throughDoors == a.nodeCount * a.nodeCount, "by its doors every node is reached from every other");
        check(without < throughDoors, "and without them not");
        // The logic keeps the links: a tick, and the doors are as they are.
        for (int n = 0; n < 4; ++n)
            logicTick(game->logic, game->player, 100);
        int shutNow = 0;
        for (int l = 0; l < a.linkCount; ++l)
            shutNow += a.links[l].brush != kNoBrush && a.linkShut[l] != kLinkOpen;
        check(shutNow > 0, "the game's logic says which ways are shut just now");
    }
    /// One of the people, taken along: he follows by the map's nodes, and
    /// stays when he is told to.
    void testFollow(const char *valve)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        std::vector<std::uint8_t> bytes;
        const bool read = game->bsp.load(std::string(valve) + "/maps/c1a2a.bsp", error) &&
                          hlds::readFile(fs::path(valve) / "maps" / "graphs" / "c1a2a.nod", bytes, error) &&
                          hlds::parseNodGraph(bytes, game->nodGraph, error);
        if (read)
            finish(*game, nullptr, valve);
        check(read && game->ok, "c1a2a cooks");
        if (!read || !game->ok)
            return;
        Game &g = *game;
        ActorWorld &a = g.actors;
        // A scientist no scene has, standing by a node, and a node a little
        // way off that there is a way to: the player goes and stands there.
        int who = -1, far = -1;
        for (int i = 0; i < a.count && who < 0; ++i)
        {
            const ActorState &actor = a.states[i];
            // (Not one met before the disaster: he would not follow.)
            if (!actor.active || actor.kind != static_cast<std::uint8_t>(ActorKind::Scientist) || actor.script >= 0 ||
                (actor.item & kPersonPreDisaster))
                continue;
            const int mine = pathNearest(a, g.move, actor.pos, kWalkStanding, i);
            for (int n = 0; mine >= 0 && n < a.nodeCount && who < 0; ++n)
            {
                const fx d[3] = {a.nodes[n].pos[0] - actor.pos[0], a.nodes[n].pos[1] - actor.pos[1], 0};
                const fx gap = fxLength(d);
                const fx rise = a.nodes[n].pos[2] - actor.pos[2];
                if (gap > fxFromInt(260) && gap < fxFromInt(700) && n != mine && rise < fxFromInt(24) &&
                    rise > -fxFromInt(24) &&
                    walkClear(g.move, &a, i, kWalkStanding, actor.pos, a.nodes[n].pos, 48) &&
                    pathStep(a, mine, n, kPathLinkHuman, true) != kNoPathNode)
                {
                    who = i;
                    far = n;
                }
            }
        }
        check(who >= 0, "c1a2a has a scientist with somewhere to be led to");
        if (who < 0)
            return;
        ActorState &sci = a.states[who];
        // (Nothing about that would eat him on the way: c1a2a's barnacles.)
        for (int i = 0; i < a.count; ++i)
        {
            if (a.states[i].active && fights(a.states[i].kind))
            {
                a.states[i].active = false;
                actorPlaceBox(a, i);
            }
        }
        std::printf("test_graph: the scientist is at %d %d %d, facing %d degrees\n", static_cast<int>(fxToInt(sci.pos[0])),
                    static_cast<int>(fxToInt(sci.pos[1])), static_cast<int>(fxToInt(sci.pos[2])), sci.yaw * 360 / 65536);
        int held = 0, freeOnes = 0;
        for (int i = 0; i < a.count; ++i)
        {
            if (a.states[i].active && a.states[i].kind == static_cast<std::uint8_t>(ActorKind::Scientist))
                (a.states[i].script >= 0 ? held : freeOnes) += 1;
        }
        std::printf("test_graph: c1a2a has %d scientists a scene holds and %d it does not\n", held, freeOnes);
        // In front of him, at arm's length, looking at him: USE.
        const fx eye[3] = {sci.pos[0] + fxFromInt(48), sci.pos[1], sci.pos[2] + fxFromInt(60)};
        const fx forward[3] = {-kFxOne, 0, 0};
        // (The player is where the USE comes from: he follows someone there.)
        g.player.origin[0] = eye[0];
        g.player.origin[1] = eye[1];
        g.player.origin[2] = sci.pos[2] + kStandingFeet;
        check(personUse(g.logic, g.player, eye, forward) && sci.ai == kPersonFollow, "USE on a scientist, and he follows");
        const fx away[3] = {kFxOne, 0, 0};
        check(!personUse(g.logic, g.player, eye, away), "USE with nobody in front is nobody's");
        g.player.origin[0] = a.nodes[far].pos[0];
        g.player.origin[1] = a.nodes[far].pos[1];
        g.player.origin[2] = a.nodes[far].pos[2] + kStandingFeet;
        const fx start[3] = {sci.pos[0], sci.pos[1], sci.pos[2]};
        std::printf("test_graph: the player goes to node %d at %d %d %d\n", far, fxToInt(a.nodes[far].pos[0]),
                    fxToInt(a.nodes[far].pos[1]), fxToInt(a.nodes[far].pos[2]));
        fx gap = 0;
        for (int tick = 0; tick < 30 * 25; ++tick)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            const AiMonster *m = aiFind(a, who);
            if (std::getenv("HLDS_AI_TRACE") != nullptr && m != nullptr && tick % 10 == 0)
                std::printf("  t=%d %s task %d st %d follows %d cond %08x goal %d act %d pos %d %d %d\n", tick * 33,
                            m->schedule != nullptr ? m->schedule->name : "-", m->scheduleIndex, m->state,
                            m->targetEnt, m->conditions, m->movementGoal, m->movementActivity, fxToInt(sci.pos[0]),
                            fxToInt(sci.pos[1]), fxToInt(sci.pos[2]));
            if (std::getenv("HLDS_AI_TRACE") != nullptr && tick % 10 == 0)
                std::printf("     goal %d (%d %d %d) from %d has %d; player %d %d %d\n", sci.goalNode,
                            fxToInt(sci.goal[0]), fxToInt(sci.goal[1]), fxToInt(sci.goal[2]), sci.fromNode, sci.hasGoal,
                            fxToInt(g.player.origin[0]), fxToInt(g.player.origin[1]), fxToInt(g.player.origin[2]));
            const fx d[3] = {g.player.origin[0] - sci.pos[0], g.player.origin[1] - sci.pos[1], 0};
            gap = fxLength(d);
            if (gap < fxFromInt(128 + 24))
                break;
        }
        const fx moved[3] = {sci.pos[0] - start[0], sci.pos[1] - start[1], 0};
        std::printf("test_graph: the scientist went %d units and stopped %d from the player\n",
                    static_cast<int>(fxToInt(fxLength(moved))), static_cast<int>(fxToInt(gap)));
        check(gap < fxFromInt(128 + 24), "he comes to where the player is, by the nodes");
        check(gap > fxFromInt(40), "... and no nearer than a stride or two");
        const fx face[3] = {sci.pos[0] + fxFromInt(48), sci.pos[1], sci.pos[2] + fxFromInt(60)};
        // (Up to him: the game's USE reaches 64 from where the player is.)
        g.player.origin[0] = face[0];
        g.player.origin[1] = face[1];
        g.player.origin[2] = sci.pos[2] + kStandingFeet;
        check(personUse(g.logic, g.player, face, forward) && sci.ai == kPersonIdle, "USE again, and he stays");
    }
} // namespace

int main()
{
    testSearch();
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve == nullptr || *valve == '\0')
    {
        std::printf("test_graph: HLDS_VALVE_DIR not set, the game's graphs were not read\n");
    }
    else if (!fs::exists(fs::path(valve) / "maps" / "graphs" / "c1a1a.nod"))
    {
        // (The game writes its graphs the first time a map is played: an
        // install that has not been may have none.)
        std::printf("test_graph: this game has no graphs yet, they were not read\n");
    }
    else
    {
        testNodFile(valve);
        testCooked(valve);
        testFollow(valve);
    }
    if (g_failures == 0)
        std::printf("test_graph: ok\n");
    return g_failures == 0 ? 0 : 1;
}
