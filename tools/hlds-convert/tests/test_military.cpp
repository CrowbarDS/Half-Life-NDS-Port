// Round 4's monsters on the game's own maps (HlMilitary.hpp, HlAliens.hpp):
// the osprey of c1a3b, which flies its corners, lets soldiers down on ropes
// in the places of those who have died, and comes down when it is shot
// down; the gargantua of c2a1, whom bullets do not hurt, who burns what is
// near, stomps at what is far, and goes up in blasts when it dies; c4a2b's
// baby crabs, which its makers make, and which bite; the dead in their suits
// that c4a1 has; c2a3d's assassins, who shoot, leap, and die of bullets; and
// the ichthyosaurs of c2a3a and c2a5, who keep to their water, go for what
// comes into it, and float up when they die.
//
// Needs the game: HLDS_VALVE_DIR. Without it, nothing is run.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "TestGame.hpp"

using namespace hldata;
using namespace hlds;
using namespace hldstest;

namespace
{
    double toUnits(fx v) { return v / 4096.0; }

    std::unique_ptr<Game> load(const char *dir, const char *map)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return nullptr;
        return game;
    }

    int findKind(const Game &g, ActorKind kind)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(kind))
                return i;
        }
        return -1;
    }

    /// What the player has lost to the frames since this was last asked.
    int g_lost = 0;

    /// A frame, the player held where they are, and kept alive.
    void tickStill(Game &g, int ms = 33)
    {
        const fx at[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        monstersTick(g.logic, g.player, ms);
        g.drain();
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = at[k];
        g_lost += 100 - g.player.health;
        g.player.health = 100;
        g.player.inv.armor = 0;
    }

    bool onRope(const Game &g, int i)
    {
        return g.actors.ai != nullptr && (g.actors.ai->held[i] & kHeldRope) != 0;
    }

    double nearest(const fx *at, double x, double y, double z, double best)
    {
        const double d[3] = {toUnits(at[0]) - x, toUnits(at[1]) - y, toUnits(at[2]) - z};
        const double far = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        return far < best ? far : best;
    }

    void testOsprey(const char *dir)
    {
        auto game = load(dir, "c1a3b");
        check(game != nullptr, "c1a3b loads");
        if (!game)
            return;
        Game &g = *game;
        const int k = findKind(g, ActorKind::Osprey);
        check(k >= 0, "c1a3b has its osprey");
        if (k < 0)
            return;
        check((g.view.actors[k].item & 0xFFFF) != 0, "... which knows the corner it flies to first");
        // (Out of everyone's way: at the top of the lift.)
        g.player.origin[0] = fxFromInt(192);
        g.player.origin[1] = fxFromInt(-1024);
        g.player.origin[2] = fxFromInt(700);
        ActorState &osprey = g.actors.states[k];

        // Its round: os1, os2, and -- with nobody to let down -- straight
        // on to os4, past the corners where it would slow.
        double toOs2 = 1e9, toOs4 = 1e9, toOs3b = 1e9;
        for (int t = 0; t < 30 * 40; ++t)
        {
            tickStill(g);
            toOs2 = nearest(osprey.pos, 1977, -1524, 1704, toOs2);
            toOs4 = nearest(osprey.pos, -1159, -1556, 1672, toOs4);
            toOs3b = nearest(osprey.pos, 194, -1524, 1152, toOs3b);
        }
        std::printf("test_military: osprey came within %.0f of os2, %.0f of os4, %.0f of os3b\n", toOs2, toOs4,
                    toOs3b);
        check(osprey.active, "the osprey is still there (it has soldiers to look after)");
        check(toOs2 < 64 && toOs4 < 64, "it flies its corners");
        check(toOs3b > 200, "... and does not stop to let anyone down while no one has died");

        // Its soldiers killed: it comes round to os3b and lets others down.
        int killed = 0;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Grunt) && actor.ai < kAiDying)
            {
                monsterHurt(g.logic, i, 500, true);
                ++killed;
            }
        }
        std::printf("test_military: %d soldiers killed\n", killed);
        int roped = 0, landed = 0, rope = -1;
        bool beam = false;
        for (int t = 0; t < 30 * 90 && roped == 0; ++t)
        {
            tickStill(g);
            for (int i = 0; i < g.actors.count; ++i)
            {
                if (onRope(g, i))
                {
                    ++roped;
                    rope = i;
                }
            }
        }
        std::printf("test_military: %d on ropes, the osprey at %.0f %.0f %.0f\n", roped, toUnits(osprey.pos[0]),
                    toUnits(osprey.pos[1]), toUnits(osprey.pos[2]));
        check(roped > 0, "with its soldiers dead, the osprey lets others down on ropes");
        check(nearest(osprey.pos, 194, -1524, 1152, 1e9) < 64, "... where the map has it stop: os3b");
        const fx hover[3] = {osprey.pos[0], osprey.pos[1], osprey.pos[2]};
        bool stayed = true;
        for (int t = 0; t < 30 * 30; ++t)
        {
            tickStill(g);
            for (const Effect &effect : g.actors.effects)
                beam = beam || (effect.kind == kEffectBeam && effect.leftMs > 0);
            bool any = false;
            for (int i = 0; i < g.actors.count; ++i)
                any = any || onRope(g, i);
            if (!any)
                break;
            stayed = stayed && aiDist(hover, osprey.pos) < fxFromInt(2);
        }
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            landed += actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Grunt) && actor.ai < kAiDying &&
                              !onRope(g, i)
                          ? 1
                          : 0;
        }
        std::printf("test_military: %d soldiers about after the drop\n", landed);
        check(beam, "... the ropes are seen");
        check(stayed, "... it hovers while they are on them");
        check(rope >= 0 && !onRope(g, rope) && landed >= roped, "... and they come down to the ground");
        if (rope >= 0)
        {
            const ActorState &grunt = g.actors.states[rope];
            const fx under[3] = {grunt.pos[0], grunt.pos[1], grunt.pos[2] - fxFromInt(8)};
            const fx feet[3] = {grunt.pos[0], grunt.pos[1], grunt.pos[2] + fxFromInt(4)};
            check(traceShot(g.move, feet, under).fraction < kFractionOne, "... onto it, not above it");
        }
        const fx after[3] = {osprey.pos[0], osprey.pos[1], osprey.pos[2]};
        for (int t = 0; t < 30 * 5; ++t)
            tickStill(g);
        check(aiDist(after, osprey.pos) > fxFromInt(200), "then it flies on");

        // Shot down: it falls, and is gone in a blast.
        const fx high = osprey.pos[2];
        monsterHurt(g.logic, k, 1000, false, kHitBody, kDmgBlast);
        check(osprey.ai >= kAiDying, "a rocket's worth brings it down");
        bool fell = false;
        for (int t = 0; t < 30 * 12 && osprey.active; ++t)
        {
            tickStill(g);
            fell = fell || osprey.pos[2] < high - fxFromInt(64);
        }
        check(fell && !osprey.active, "... it falls, and is gone");
        // A few rounds of a pistol do nothing to it.
        auto fresh = load(dir, "c1a3b");
        if (fresh)
        {
            const int o = findKind(*fresh, ActorKind::Osprey);
            for (int t = 0; t < 30 * 2; ++t)
                tickStill(*fresh);
            const int health = fresh->actors.states[o].health;
            monsterHurt(fresh->logic, o, 8, true);
            check(fresh->actors.states[o].health == health, "a pistol round off its body only sparks");
        }
    }
    /// The player put `distance` from actor `index` along the first way
    /// round it that a line runs clear and that has a floor, facing it.
    bool placeInView(Game &g, int index, int distance)
    {
        ActorState &a = g.actors.states[index];
        for (int turn = 0; turn < 16; ++turn)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
            const fx from[3] = {a.pos[0], a.pos[1], a.pos[2] + fxFromInt(40)};
            const fx to[3] = {a.pos[0] + cosine * distance, a.pos[1] + sine * distance, a.pos[2] + fxFromInt(40)};
            const fx under[3] = {to[0], to[1], to[2] - fxFromInt(256)};
            if (traceShot(g.move, from, to).fraction < kFractionOne || traceShot(g.move, to, under).fraction >= kFractionOne)
                continue;
            ActorState probe;
            for (int k = 0; k < 3; ++k)
                probe.pos[k] = to[k];
            if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(256)))
                continue;
            g.player.origin[0] = probe.pos[0];
            g.player.origin[1] = probe.pos[1];
            g.player.origin[2] = probe.pos[2] + kStandingFeet;
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            a.yaw = fxAtan2Turns(probe.pos[1] - a.pos[1], probe.pos[0] - a.pos[0]);
            return true;
        }
        return false;
    }

    /// Everything that can be hurt gone, but `keep`.
    void alone(Game &g, int keep)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            ActorState &actor = g.actors.states[i];
            if (i != keep && (canBeHurt(actor.kind) || isTurret(actor.kind)))
            {
                actor.active = false;
                actorPlaceBox(g.actors, i);
            }
        }
    }

    /// How much the player takes in `seconds` (and is given back).
    int takenIn(Game &g, int seconds, bool *flame = nullptr, bool *stomp = nullptr)
    {
        g_lost = 0;
        for (int t = 0; t < 30 * seconds; ++t)
        {
            g.player.health = 100;
            tickStill(g);
            for (const Effect &effect : g.actors.effects)
            {
                if (flame != nullptr && effect.kind == kEffectBeam && effect.leftMs > 0 && effect.colour == 0x2E1F)
                    *flame = true;
            }
            for (const Projectile &shot : g.actors.shots)
            {
                if (stomp != nullptr && shot.active && shot.kind == kShotStomp)
                    *stomp = true;
            }
        }
        return g_lost;
    }

    void testGargantua(const char *dir)
    {
        auto game = load(dir, "c2a1");
        check(game != nullptr, "c2a1 loads");
        if (!game)
            return;
        Game &g = *game;
        const int k = findKind(g, ActorKind::Gargantua);
        check(k >= 0, "c2a1 has its gargantua");
        if (k < 0)
            return;
        alone(g, k);
        ActorState &garg = g.actors.states[k];
        for (int t = 0; t < 30; ++t)
            tickStill(g);
        // Bullets glance off it.
        const int health = garg.health;
        for (int n = 0; n < 10; ++n)
            monsterHurt(g.logic, k, 8, true);
        std::printf("test_military: gargantua health %d, after ten rounds %d\n", health, garg.health);
        check(health >= 800 && garg.health == health, "bullets do not hurt a gargantua");
        // Near: it burns, or swipes.
        bool room = false;
        for (int far : {220, 260, 180, 300})
            room = room || placeInView(g, k, far);
        check(room, "room by the gargantua");
        if (!room)
            return;
        garg.switched = 1 + kUseToggle; // (it has a name: it waits to be triggered, as the game's)
        bool flame = false, stomp = false;
        const int near = takenIn(g, 15, &flame, &stomp);
        std::printf("test_military: gargantua near: the player lost %d, flames %d\n", near, flame ? 1 : 0);
        check(flame, "up close, it throws its flames");
        check(near > 0, "... which burn the player");
        // Far: it stomps.
        room = false;
        for (int far : {700, 600, 800, 500})
            room = room || placeInView(g, k, far);
        if (room)
        {
            AiMonster *m = aiFind(g.actors, k);
            if (m != nullptr)
                m->time2 = 0; // (it may stomp at once)
            stomp = false;
            const int farTaken = takenIn(g, 15, nullptr, &stomp);
            std::printf("test_military: gargantua far: a stomp %d, the player lost %d\n", stomp ? 1 : 0,
                        farTaken);
            check(stomp, "from further off, it stomps");
        }
        // What blows up kills it, in blasts, and it is gone.
        monsterHurt(g.logic, k, 900, false, kHitBody, kDmgBlast);
        check(garg.ai >= kAiDying, "a blast kills it");
        int bursts = 0;
        for (int t = 0; t < 30 * 4; ++t)
        {
            tickStill(g);
            for (const Effect &effect : g.actors.effects)
                bursts += effect.kind == kEffectBurst && effect.leftMs > 400 ? 1 : 0;
        }
        std::printf("test_military: gargantua dying: %d blasts, active %d\n", bursts, garg.active ? 1 : 0);
        check(bursts >= 3, "... it goes up in blasts");
        check(!garg.active || garg.health < 0, "... and is gone in pieces");
    }
    void testBabyCrab(const char *dir)
    {
        auto game = load(dir, "c4a2b");
        check(game != nullptr, "c4a2b loads");
        if (!game)
            return;
        Game &g = *game;
        const std::uint16_t babies = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("babies"));
        check(babies != 0, "c4a2b's makers of baby crabs are there");
        logicEnqueue(g.logic, babies, 0, kUseToggle, -1, 0);
        int made = -1;
        for (int t = 0; t < 30 * 5 && made < 0; ++t)
        {
            tickStill(g);
            made = findKind(g, ActorKind::BabyCrab);
        }
        check(made >= 0, "... and make them");
        if (made < 0)
            return;
        // (That one alone: the makers make no more.)
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Maker))
                g.states[i].dead = true;
        }
        alone(g, made);
        bool room = false;
        for (int far : {120, 150, 100, 200})
            room = room || placeInView(g, made, far);
        const int lost = room ? takenIn(g, 10) : 0;
        std::printf("test_military: baby crab, health %d: the player lost %d in ten seconds\n",
                    g.actors.states[made].health, lost);
        check(g.actors.states[made].health <= 3, "a baby crab is weaker than a grown one");
        check(lost > 0 && lost < 100, "... and leaps, and bites, less than a grown one");
    }

    void testHevDead(const char *dir)
    {
        auto game = load(dir, "c4a1");
        if (!game)
            return;
        int suits = 0;
        for (int i = 0; i < game->actors.count; ++i)
        {
            const ActorState &actor = game->actors.states[i];
            const char *model = game->view.modelRefs[actor.model].name;
            suits += actor.active && actor.kind == static_cast<std::uint8_t>(ActorKind::Pose) &&
                             std::string(model) == "gordon"
                         ? 1
                         : 0;
        }
        std::printf("test_military: c4a1 has %d dead in their suits\n", suits);
        check(suits == 2, "c4a1's two dead in their suits lie there");
    }
    void testAssassin(const char *dir)
    {
        auto game = load(dir, "c2a3d");
        check(game != nullptr, "c2a3d loads");
        if (!game)
            return;
        Game &g = *game;
        const int k = findKind(g, ActorKind::Assassin);
        check(k >= 0, "c2a3d has its assassins");
        if (k < 0)
            return;
        alone(g, k);
        ActorState &a = g.actors.states[k];
        bool room = false;
        for (int far : {400, 300, 500, 200})
            room = room || placeInView(g, k, far);
        check(room, "room by the assassin");
        if (!room)
            return;
        int tracers = 0, brass = 0;
        bool leapt = false;
        g_lost = 0;
        for (int t = 0; t < 30 * 20; ++t)
        {
            tickStill(g);
            for (const Piece &piece : g.actors.pieces)
                brass += piece.kind == kPieceShell ? 1 : 0;
            for (const Effect &effect : g.actors.effects)
                tracers += effect.kind == kEffectTracer && effect.leftMs > 0 ? 1 : 0;
            const AiMonster *m = aiFind(g.actors, k);
            leapt = leapt || (m != nullptr && m->inAir && m->flag2);
            // (Near enough, now and then, that it leaps.)
            if (t == 30 * 10)
                placeInView(g, k, 100);
        }
        std::printf("test_military: assassin: %d frames of tracers, the player lost %d, leapt %d, schedule %s\n",
                    tracers, g_lost, leapt ? 1 : 0,
                    aiFind(g.actors, k) && aiFind(g.actors, k)->schedule ? aiFind(g.actors, k)->schedule->name : "-");
        check(tracers > 0 && g_lost > 0, "an assassin shoots the player");
        check(brass > 0, "... and her pistol throws out its brass");
        check(leapt, "... and leaps");
        for (int n = 0; n < 12 && a.ai < kAiDying; ++n)
            monsterHurt(g.logic, k, 8, true);
        check(a.ai >= kAiDying, "... and dies of a few rounds");
    }
    void testIchthyosaur(const char *dir)
    {
        for (const char *map : {"c2a5", "c2a3a"})
        {
            auto game = load(dir, map);
            if (!game)
                continue;
            Game &g = *game;
            const int k = findKind(g, ActorKind::Ichthyosaur);
            check(k >= 0, "the map has its ichthyosaur");
            if (k < 0)
                continue;
            alone(g, k);
            ActorState &icky = g.actors.states[k];
            const fx start[3] = {icky.pos[0], icky.pos[1], icky.pos[2]};
            check(pointContents(g.move, icky.pos) == kContentsWater, "it starts in its water");
            // (The player far off and out of sight: it swims about.)
            g.player.origin[2] += fxFromInt(4000);
            bool dry = false;
            fx most = 0;
            for (int t = 0; t < 30 * 10; ++t)
            {
                tickStill(g);
                dry = dry || pointContents(g.move, icky.pos) != kContentsWater;
                const fx moved = aiDist(start, icky.pos);
                most = moved > most ? moved : most;
            }
            std::printf("test_military: %s ichthyosaur swam up to %d units off, %s\n", map, fxToInt(most),
                        dry ? "out of the water" : "in its water");
            check(most > fxFromInt(64), "it swims about");
            check(!dry, "... and keeps to its water");
            // In the water by it: it comes for the player, and bites.
            int lost = 0;
            for (int far : {160, 220, 120, 300})
            {
                for (int turn = 0; turn < 8 && lost == 0; ++turn)
                {
                    fx sine, cosine;
                    fxSinCos(static_cast<std::uint16_t>(turn * 8192), sine, cosine);
                    const fx at[3] = {icky.pos[0] + cosine * far, icky.pos[1] + sine * far, icky.pos[2]};
                    if (pointContents(g.move, at) != kContentsWater || !lineClear(g.move, icky.pos, at))
                        continue;
                    for (int k2 = 0; k2 < 3; ++k2)
                        g.player.origin[k2] = at[k2];
                    lost = takenIn(g, 20);
                }
                if (lost > 0)
                    break;
            }
            std::printf("test_military: %s ichthyosaur: the player lost %d\n", map, lost);
            check(lost > 0, "it bites the player in its water");
            // Killed: it floats.
            monsterHurt(g.logic, k, 1000, true);
            const fx died = icky.pos[2];
            for (int t = 0; t < 30 * 8; ++t)
                tickStill(g);
            std::printf("test_military: %s ichthyosaur died at %d, floats at %d\n", map, fxToInt(died),
                        fxToInt(icky.pos[2]));
            check(icky.ai >= kAiDying && icky.pos[2] > died, "... and dead, it floats up");
            return;
        }
        check(false, "an ichthyosaur on its maps");
    }
} // namespace

int main()
{
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve == nullptr || *valve == '\0')
    {
        std::printf("test_military: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 77;
    }
    testOsprey(valve);
    testGargantua(valve);
    testBabyCrab(valve);
    testHevDead(valve);
    testAssassin(valve);
    testIchthyosaur(valve);
    if (g_failures == 0)
        std::printf("test_military: ok\n");
    return g_failures == 0 ? 0 : 1;
}
