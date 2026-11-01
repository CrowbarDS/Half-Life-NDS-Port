// Where the people and monsters of the game's maps stand, and where they
// walk: nobody inside the world.
//
// Every map from the tram ride to the first fights is cooked and started;
// each actor that stands on its feet must have room for its hull where the
// map puts it. Then the map is left to run for half a minute -- the scripts
// that start by themselves send people walking -- and on no frame may anyone
// who is WALKING be inside the world.
//
// (Someone a script has put on its mark may well overlap what is there: the
// scientist leaning on the lobby's desk has his hull in the desk, as he does
// in the game. Marks are the map's business; getting to them is ours.)
//
// Needs HLDS_VALVE_DIR.

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    bool stands(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Scientist) ||
               kind == static_cast<std::uint8_t>(ActorKind::Guard) ||
               kind == static_cast<std::uint8_t>(ActorKind::Gman) ||
               kind == static_cast<std::uint8_t>(ActorKind::Headcrab) ||
               kind == static_cast<std::uint8_t>(ActorKind::Zombie);
    }

    bool inWorld(const Game &g, const ActorState &actor)
    {
        const bool small = actor.kind == static_cast<std::uint8_t>(ActorKind::Headcrab);
        const fx at[3] = {actor.pos[0], actor.pos[1],
                          actor.pos[2] + (small ? kCrouchedFeet : kStandingFeet)};
        const Hull &hull = small ? g.move.crouched : g.move.standing;
        return hullPointContents(hull, hull.root, at) == kContentsSolid;
    }

    /// The seated and the dead: each rests on what is under it, as the game
    /// lets them down when it puts them in the map -- nobody sits or lies in
    /// the air. And those who sit do not sit like statues.
    struct Rest
    {
        int seated = 0, bodies = 0, inAir = 0, shifted = 0;
    };

    void auditRest(const char *map, Game &g, Rest &rest)
    {
        std::vector<int> sitters;
        for (int i = 0; i < g.actors.count; ++i)
        {
            ActorState actor = g.actors.states[i];
            if (!actor.active)
                continue;
            int air = 0;
            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Sitter))
            {
                ++rest.seated;
                sitters.push_back(i);
                const fx from[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + kCrouchedFeet + fxFromInt(1)};
                const fx to[3] = {actor.pos[0], actor.pos[1], actor.pos[2] + kCrouchedFeet - fxFromInt(64)};
                // (What is under him counts whatever it is: his chair is often a
                // func_wall.)
                const Trace tr = traceAll(g.move, true, from, to);
                air = tr.allSolid || tr.startSolid ? 0 : fxToInt(actor.pos[2] + kCrouchedFeet - tr.endPos[2]);
            }
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Pose))
            {
                ++rest.bodies;
                const fx was = actor.pos[2];
                if (pointToFloor(g.move, actor.pos, fxFromInt(64)))
                    air = fxToInt(was - actor.pos[2]);
            }
            else
            {
                continue;
            }
            if (air > 1)
            {
                ++rest.inAir;
                std::printf("test_actors: %s: actor %d (kind %d) is %d above what is under it, at %d %d %d\n",
                            map, i, actor.kind, air, fxToInt(actor.pos[0]), fxToInt(actor.pos[1]),
                            fxToInt(actor.pos[2]));
            }
        }
        if (sitters.empty())
            return;
        // Ten seconds: each of them is moving, and some have changed how
        // they sit.
        std::vector<int> first;
        for (int i : sitters)
            first.push_back(g.actors.states[i].clip);
        int moving = 0, changed = 0;
        for (int frame = 0; frame < 300; ++frame)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
        }
        for (std::size_t n = 0; n < sitters.size(); ++n)
        {
            const ActorState &actor = g.actors.states[sitters[n]];
            // (One may be at the end of a way of sitting, for the tenth of
            // a second until he next thinks: SittingThink.)
            moving += !actor.frozen;
            changed += actor.clip != first[n];
        }
        rest.shifted += changed;
        check(moving == static_cast<int>(sitters.size()), "those who sit are not held still");
    }

    void audit(const char *dir, const char *map, int &atStart, int &walking, int &people, Rest &rest)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
        {
            check(false, "a map cooks");
            return;
        }
        Game &g = *game;
        int here = 0, moved = 0;
        std::vector<bool> bad(g.actors.count, false);
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (!actor.active || !stands(actor.kind))
                continue;
            ++people;
            if (inWorld(g, actor))
            {
                // (A headcrab the map has tucked into a duct too small for
                // the box it moves in is said, not counted: it sits there in
                // the game as well, until a script or a leap has it out.)
                here += actor.kind != static_cast<std::uint8_t>(ActorKind::Headcrab);
                bad[i] = true;
                std::printf("test_actors: %s: actor %d (kind %d) starts inside the world at %d %d %d\n",
                            map, i, actor.kind, fxToInt(actor.pos[0]), fxToInt(actor.pos[1]),
                            fxToInt(actor.pos[2]));
            }
        }
        // Half a minute of the map by itself, the player out of the way.
        g.player.origin[2] += fxFromInt(8000);
        auditRest(map, g, rest);
        for (int frame = 0; frame < 30 * 30; ++frame)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            for (int i = 0; i < g.actors.count; ++i)
            {
                const ActorState &actor = g.actors.states[i];
                if (!actor.active || !stands(actor.kind) || bad[i] ||
                    actor.scriptPhase != kActorMoving || !inWorld(g, actor))
                    continue;
                // The last steps to a mark that is hard against something
                // are the mark's doing (see above).
                const fx d[3] = {actor.target[0] - actor.pos[0], actor.target[1] - actor.pos[1], 0};
                const int off = fxToInt(fxLength(d));
                if (off <= 64)
                    continue;
                bad[i] = true;
                ++moved;
                std::printf("test_actors: %s: actor %d (kind %d) is inside the world at %d %d %d after %.1f s, %d from its mark\n",
                            map, i, actor.kind, fxToInt(actor.pos[0]), fxToInt(actor.pos[1]),
                            fxToInt(actor.pos[2]), frame / 30.0, off);
            }
        }
        atStart += here;
        walking += moved;
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_actors: HLDS_VALVE_DIR not set, the maps were not played\n");
        return 77;
    }
    static const char *const kMaps[] = {"c0a0",  "c0a0a", "c0a0b", "c0a0c", "c0a0d", "c0a0e",
                                        "c1a0",  "c1a0d", "c1a0a", "c1a0b", "c1a0e", "c1a0c",
                                        "c1a1",  "c1a1a", "c1a1f", "c1a1b", "c1a1c", "c1a1d",
                                        "c1a2",  "c1a2a", "c1a2b", "c1a2c", "c1a2d"};
    int atStart = 0, walking = 0, people = 0;
    Rest rest;
    for (const char *map : kMaps)
        audit(dir, map, atStart, walking, people, rest);
    std::printf("test_actors: %d sit and %d lie dead; %d of them in the air; %d of the seated changed how they sit\n",
                rest.seated, rest.bodies, rest.inAir, rest.shifted);
    check(rest.seated >= 3 && rest.bodies >= 20, "the seated and the dead are there");
    check(rest.inAir == 0, "nobody sits or lies in the air");
    check(rest.shifted > 0, "the seated go from one way of sitting to another");
    std::printf("test_actors: %d who stand; %d start inside the world, %d get inside it\n", people,
                atStart, walking);
    check(people > 100, "the maps' people are there");
    check(atStart == 0, "nobody starts inside the world");
    check(walking == 0, "nobody walks into it");
    if (g_failures == 0)
        std::printf("test_actors: ok\n");
    return g_failures == 0 ? 0 : 1;
}
