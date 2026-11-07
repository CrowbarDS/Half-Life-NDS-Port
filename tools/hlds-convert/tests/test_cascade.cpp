// The resonance cascade (c1a0e), played with nobody at the controls.
//
// The map's last minute is a chain: multi_managers switch on multisources,
// each of which is the master of a teleport and starts the next
// multi_manager, until the last of them changes the level. The player does
// nothing but stand where the first teleport will be. If any link of the
// chain is not understood, they are left in a room of black and green for
// good -- which is how this test came to be written.
//
// Needs HLDS_VALVE_DIR.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    int recordNamed(const Game &g, const char *name, LogicKind kind)
    {
        const std::uint32_t hash = nameHash(name);
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(kind) && r.name != 0 &&
                g.view.nameHashes[r.name] == hash)
                return static_cast<int>(i);
        }
        return -1;
    }

    void testCascade(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a0e.bsp", error))
        {
            check(false, "c1a0e is read");
            return;
        }
        finish(*game, nullptr, dir);
        check(game->ok, "c1a0e cooks");
        if (!game->ok)
            return;
        Game &g = *game;

        const int start = recordNamed(g, "probe_arm_mm", LogicKind::MultiManager);
        const int first = recordNamed(g, "killtele1", LogicKind::TriggerTeleport);
        check(start >= 0 && first >= 0, "the cascade's start and its first teleport are there");
        if (start < 0 || first < 0)
            return;
        // In the middle of the first teleport, on its floor.
        const LogicRecord &tele = g.view.logic[first];
        ActorState probe;
        probe.pos[0] = fxFromInt((tele.mins[0] + tele.maxs[0]) / 2);
        probe.pos[1] = fxFromInt((tele.mins[1] + tele.maxs[1]) / 2);
        probe.pos[2] = fxFromInt(tele.mins[2] + 40);
        actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(256));
        const fx home[3] = {probe.pos[0], probe.pos[1], probe.pos[2] + kStandingFeet};
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = home[a];

        logicUse(g.logic, g.player, start, -1, kUseToggle);
        int teleports = 0;
        int frame = 0;
        fx last[3] = {home[0], home[1], home[2]};
        for (; frame < 30 * 150 && g.logic.pendingChange < 0; ++frame)
        {
            // Nothing in the chamber is allowed to kill them: this is about
            // the chain, and they would not stand in the beam.
            g.player.health = 100;
            logicTick(g.logic, g.player, 33);
            g.logic.viewTurn = 0;
            g.logic.faceSet = false;
            monstersTick(g.logic, g.player, 33);
            playerMove(g.player, MoveCommand(), g.move, kPlayerTick);
            g.drain();
            const fx d[3] = {g.player.origin[0] - last[0], g.player.origin[1] - last[1],
                             g.player.origin[2] - last[2]};
            if (fxLength(d) > fxFromInt(200))
            {
                ++teleports;
                std::printf("test_cascade: %5.1f s: to %d %d %d\n", frame / 30.0,
                            fxToInt(g.player.origin[0]), fxToInt(g.player.origin[1]),
                            fxToInt(g.player.origin[2]));
            }
            for (int a = 0; a < 3; ++a)
                last[a] = g.player.origin[a];
        }
        check(teleports >= 2, "the player is taken through the rooms");
        int sounds = 0;
        for (int n : g.timesHeard)
            sounds += n;
        std::printf("test_cascade: %d sounds and %d spoken lines were heard on the way\n", sounds,
                    g.sentencesHeard);
        check(sounds >= 10 && g.sentencesHeard >= 3, "... to the sound of it, and of the announcer");
        const bool changed = g.logic.pendingChange >= 0;
        check(changed, "... and the chain ends in the change of level");
        if (changed)
        {
            const char *next = g.view.strings + g.view.logic[g.logic.pendingChange].extra[0];
            std::printf("test_cascade: %.1f s after the start, on to %s\n", frame / 30.0, next);
            check(std::string(next) == "c1a0c", "... to c1a0c");
        }
    }

    /// The machine itself: its button is locked until the announcer has had
    /// his say, and then it starts the rotor turning.
    void testMachine(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a0e.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c1a0e.bsp loads").c_str());
            return;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return;
        Game &g = *game;
        const int begin = recordNamed(g, "portal_begin_mm", LogicKind::MultiManager);
        const int rotor = recordNamed(g, "p_1", LogicKind::Rotating);
        int button = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Button) && r.target != 0 &&
                g.view.nameHashes[r.target] == nameHash("motorstartmm"))
                button = static_cast<int>(i);
        }
        check(begin >= 0 && rotor >= 0 && button >= 0, "the machine, its rotor and its button are there");
        if (begin < 0 || rotor < 0 || button < 0)
            return;
        g.player.origin[2] += fxFromInt(4000);
        auto run = [&](int frames)
        {
            for (int n = 0; n < frames; ++n)
            {
                logicTick(g.logic, g.player, 33);
                g.drain();
            }
        };
        run(30);
        logicUse(g.logic, g.player, button, -1, kUseSet);
        run(90);
        check(g.logic.states[rotor].speed == 0, "its button does nothing before the test is under way");
        logicUse(g.logic, g.player, begin, -1, kUseToggle);
        run(30 * 37);
        logicUse(g.logic, g.player, button, -1, kUseSet);
        run(30 * 6);
        std::printf("test_cascade: the rotor turns at %d (of %d) six seconds after its button\n",
                    static_cast<int>(g.logic.states[rotor].speed), static_cast<int>(g.view.logic[rotor].speed));
        check(g.logic.states[rotor].speed != 0, "... and once it is, starts the rotor");
    }

    /// The sample cart: it comes up on a lift, and is pushed into the beam,
    /// which is what starts it all.
    void testCart(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a0e.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c1a0e.bsp loads").c_str());
            return;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return;
        Game &g = *game;
        const int cart = g.recordOfKind(LogicKind::Pushable);
        const int lift = recordNamed(g, "sample_cart2_lift", LogicKind::Door);
        const int cover = recordNamed(g, "sample_delivery_door1", LogicKind::Door);
        const int start = recordNamed(g, "probe_arm_mm", LogicKind::MultiManager);
        check(cart >= 0 && lift >= 0 && cover >= 0 && start >= 0, "the cart, its lift and its hatch are there");
        if (cart < 0 || lift < 0 || cover < 0 || start < 0)
            return;
        const int cartBrush = g.view.logic[cart].brush, liftBrush = g.view.logic[lift].brush;
        auto run = [&](int frames)
        {
            for (int n = 0; n < frames; ++n)
            {
                logicTick(g.logic, g.player, 33);
                g.drain();
            }
        };
        // Out of the way: this is the cart's business.
        g.player.origin[2] += fxFromInt(4000);
        auto bottom = [&]() { return g.movers[cartBrush].mins[2]; };
        auto liftTop = [&]() { return g.movers[liftBrush].maxs[2]; };
        run(60);
        const fx down = bottom();
        check(down - liftTop() < fxFromInt(2) && down - liftTop() > -fxFromInt(2),
              "the cart waits on its lift, under the floor");
        // The hatch opens and the lift comes up, as the map has them do.
        logicUse(g.logic, g.player, cover, -1, kUseToggle);
        run(90);
        logicUse(g.logic, g.player, lift, -1, kUseToggle);
        run(30 * 12);
        std::printf("test_cascade: the cart comes up %d units on its lift\n", fxToInt(bottom() - down));
        check(bottom() - down > fxFromInt(100) && bottom() - liftTop() < fxFromInt(2) &&
                  bottom() - liftTop() > -fxFromInt(2),
              "... and comes up with it");
        // The fence comes down (every piece of it has the one name), and the
        // cart is shoved towards the machine.
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.name != 0 && g.view.nameHashes[r.name] == nameHash("sample_fence") &&
                r.kind == static_cast<std::uint8_t>(LogicKind::Door))
            {
                logicUse(g.logic, g.player, static_cast<int>(i), -1, kUseToggle);
                break; // (one use reaches them all only through a name; this is one of them)
            }
        }
        const std::uint16_t fence = g.view.logic[recordNamed(g, "sample_fence", LogicKind::Door)].name;
        logicFire(g.logic, g.player, fence, -1, kUseToggle, 0);
        run(30 * 4);
        bool started = false;
        for (int frame = 0; frame < 30 * 20 && !started; ++frame)
        {
            g.logic.states[cart].travel[0] = fxFromInt(60); // someone pushing, steadily
            logicTick(g.logic, g.player, 33);
            g.drain();
            started = g.logic.states[start].running;
        }
        std::printf("test_cascade: pushed, the cart is at %d %d when the machine takes it\n",
                    fxToInt((g.movers[cartBrush].mins[0] + g.movers[cartBrush].maxs[0]) / 2),
                    fxToInt((g.movers[cartBrush].mins[1] + g.movers[cartBrush].maxs[1]) / 2));
        if (!started)
        {
            // (What is in its way, for whoever reads this.)
            fx middle[3];
            hldata::detail::boxMiddle(g.logic, cartBrush, g.view.logic[cart].extra[3] != 0, middle);
            for (int dz = 0; dz <= 18; dz += 18)
            {
                const fx from[3] = {middle[0], middle[1], middle[2] + fxFromInt(dz)};
                const fx to[3] = {from[0] + fxFromInt(40), from[1], from[2]};
                const Trace t = hldata::detail::boxTrace(g.logic, cartBrush, g.view.logic[cart].extra[3] != 0, from, to);
                std::printf("test_cascade: (raised %d) forward 40: fraction %d/1000, by mover %d, normal %d %d %d, startSolid %d; bounds x %d..%d z %d..%d\n",
                            dz, static_cast<int>((static_cast<std::int64_t>(t.fraction) * 1000) >> kFractionShift), t.mover,
                            t.normal[0] >> 4, t.normal[1] >> 4, t.normal[2] >> 4, t.startSolid,
                            fxToInt(g.movers[cartBrush].mins[0]), fxToInt(g.movers[cartBrush].maxs[0]),
                            fxToInt(g.movers[cartBrush].mins[2]), fxToInt(g.movers[cartBrush].maxs[2]));
                if (t.mover >= 0)
                    std::printf("test_cascade:   that mover: x %d..%d y %d..%d z %d..%d\n", fxToInt(g.movers[t.mover].mins[0]),
                                fxToInt(g.movers[t.mover].maxs[0]), fxToInt(g.movers[t.mover].mins[1]),
                                fxToInt(g.movers[t.mover].maxs[1]), fxToInt(g.movers[t.mover].mins[2]),
                                fxToInt(g.movers[t.mover].maxs[2]));
            }
        }
        check(started, "pushed into the beam, the cart starts the cascade");
        // ... when the cart itself is in the trigger, not a box round it a
        // hundred units larger (its front is 12 ahead of its origin).
        const int front = fxToInt(fxFromInt(g.view.brushes[cartBrush].maxs[0]) + g.brushStates[cartBrush].pos[0] -
                                  g.view.brushes[cartBrush].origin[0]);
        std::printf("test_cascade: the cart's front at %d when it starts it\n", front);
        check(front >= 1522 && front <= 1600, "... with its front in the trigger");
    }

    /// What is glimpsed in the cascade -- four slaves, two bullsquids -- is
    /// there to be seen, not fought: the map keeps them as prisoners, and
    /// takes the slaves away by name when their moment is over.
    void testPrisoners(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a0e.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c1a0e.bsp loads").c_str());
            return;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return;
        Game &g = *game;
        std::vector<int> slaves;
        int prisoners = 0;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (!actor.active)
                continue;
            prisoners += (actor.flags & kActorPrisoner) != 0;
            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Slave))
                slaves.push_back(i);
        }
        check(slaves.size() == 4 && prisoners == 6, "the cascade's four slaves and two bullsquids are prisoners");
        if (slaves.empty())
            return;
        // Stood right in front of one, in plain view, for ten seconds: not
        // gone for, not hurt. Nor when it is shot at.
        const ActorState &one = g.actors.states[slaves[0]];
        fx sine, cosine;
        fxSinCos(one.yaw, sine, cosine);
        g.player.origin[0] = one.pos[0] + cosine * 80;
        g.player.origin[1] = one.pos[1] + sine * 80;
        g.player.origin[2] = one.pos[2] + kStandingFeet;
        bool roused = false;
        for (int frame = 0; frame < 300; ++frame)
        {
            if (frame == 150)
                monsterHurt(g.logic, slaves[0], 1, true, kHitBody);
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            g.drain();
            // (Shot at, it may look about -- the game's alert -- but it has
            // nobody to fight.)
            for (int i : slaves)
            {
                const AiMonster *m = aiFind(g.actors, i);
                roused = roused || (m != nullptr && (m->state == kStateCombat || m->enemy != kAiNone));
            }
        }
        check(!roused && g.player.health == 100 && g.times(CommonSound::SlaveZap) == 0,
              "a prisoner goes for nobody, in plain view or shot at");
        // The relay that ends their moment takes all four away.
        const int relay = recordNamed(g, "kill_slave_stuff", LogicKind::TriggerRelay);
        check(relay >= 0, "the relay that takes the slaves away is there");
        if (relay < 0)
            return;
        logicUse(g.logic, g.player, relay, -1, kUseToggle, 0);
        for (int frame = 0; frame < 10; ++frame)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
        }
        int left = 0;
        for (int i : slaves)
            left += g.actors.states[i].active;
        check(left == 0, "and they are gone when it fires");
    }

    /// The four slaves that come through in the cascade (each from a maker
    /// high in the chamber) fall through a monsters' trigger -- a flash and
    /// a sound -- and a monsters' teleport, which sends them into a place
    /// that hurts, far away: seen for a moment, then gone, as in the game.
    void testReinforcements(const char *dir)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/c1a0e.bsp", error))
        {
            check(false, "c1a0e is read");
            return;
        }
        finish(*game, nullptr, dir);
        if (!game->ok)
            return;
        Game &g = *game;
        int monsterTeleports = 0, monsterTriggers = 0;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (!(r.flags & kLogicMonsters))
                continue;
            monsterTeleports += r.kind == static_cast<std::uint8_t>(LogicKind::TriggerTeleport);
            monsterTriggers += r.kind == static_cast<std::uint8_t>(LogicKind::TriggerOnce);
        }
        check(monsterTeleports >= 4 && monsterTriggers >= 4, "c1a0e keeps the monsters' teleports and triggers");
        // The player in the chamber, kept alive: this is about the slaves.
        const int first = recordNamed(g, "killtele1", LogicKind::TriggerTeleport);
        const int reinforcements = recordNamed(g, "reinforcements", LogicKind::MultiManager);
        check(first >= 0 && reinforcements >= 0, "the chamber and its reinforcements are there");
        if (first < 0 || reinforcements < 0)
            return;
        const LogicRecord &room = g.view.logic[first];
        ActorState probe;
        probe.pos[0] = fxFromInt((room.mins[0] + room.maxs[0]) / 2);
        probe.pos[1] = fxFromInt((room.mins[1] + room.maxs[1]) / 2);
        probe.pos[2] = fxFromInt(room.mins[2] + 40);
        actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(256));
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = probe.pos[k] + (k == 2 ? kStandingFeet : 0);
        Cheats cheats;
        cheats.notarget = true;
        g.player.cheats = &cheats;
        // (The cart comes up first, as it does in play: the wall that
        // kept it in goes, and the fence opens.)
        const int lift = recordNamed(g, "sample_cart_liftmm", LogicKind::MultiManager);
        if (lift >= 0)
            logicUse(g.logic, g.player, lift, -1, kUseToggle);
        for (int frame = 0; frame < 30 * 6; ++frame)
        {
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            g.drain();
        }
        const int before = g.actors.count;
        logicUse(g.logic, g.player, reinforcements, -1, kUseToggle);
        int made = 0, aliveInRoom = 0;
        std::vector<char> seenInRoom(512, 0);
        for (int frame = 0; frame < 30 * 12; ++frame)
        {
            g.player.health = 100;
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            g.drain();
            for (int i = 0; i < g.actors.count && i < 512; ++i)
            {
                const ActorState &a = g.actors.states[i];
                if (!a.active || a.kind != static_cast<std::uint8_t>(ActorKind::Slave) || (a.flags & kActorPrisoner))
                    continue;
                const int x = fxToInt(a.pos[0]), y = fxToInt(a.pos[1]), z = fxToInt(a.pos[2]);
                if (x >= room.mins[0] && x <= room.maxs[0] && y >= room.mins[1] && y <= room.maxs[1] &&
                    z >= room.mins[2] && z <= room.maxs[2] + 400)
                    seenInRoom[i] = 1;
            }
        }
        for (int i = 0; i < g.actors.count && i < 512; ++i)
        {
            const ActorState &a = g.actors.states[i];
            made += seenInRoom[i];
            const int x = fxToInt(a.pos[0]), y = fxToInt(a.pos[1]), z = fxToInt(a.pos[2]);
            if (seenInRoom[i])
                std::printf("test_cascade:   slave %d at %d %d %d, active %d ai %d air %d\n", i, x, y, z, a.active, a.ai,
                            aiFind(g.actors, i) != nullptr ? aiFind(g.actors, i)->inAir : -1);
            aliveInRoom += a.active && a.ai < kAiDying && a.kind == static_cast<std::uint8_t>(ActorKind::Slave) &&
                           !(a.flags & kActorPrisoner) && x >= room.mins[0] && x <= room.maxs[0] &&
                           y >= room.mins[1] && y <= room.maxs[1] && z >= room.mins[2] && z <= room.maxs[2];
        }
        g.player.cheats = nullptr;
        std::printf("test_cascade: reinforcements: %d slaves seen in the chamber (actors %d -> %d), %d still alive there\n",
                    made, before, g.actors.count, aliveInRoom);
        int alive = 0;
        for (int i = 0; i < g.actors.count && i < 512; ++i)
            alive += seenInRoom[i] && g.actors.states[i].active && g.actors.states[i].ai < kAiDying;
        check(made >= 4 && aliveInRoom == 0 && alive == 0,
              "the cascade's slaves come through the chamber, and are sent away to die, as in the game");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testCascade(dir);
        testMachine(dir);
        testCart(dir);
        testPrisoners(dir);
        testReinforcements(dir);
    }
    else
    {
        std::printf("test_cascade: HLDS_VALVE_DIR not set, the map was not played\n");
        return 77;
    }
    if (g_failures == 0)
        std::printf("test_cascade: ok\n");
    return g_failures == 0 ? 0 : 1;
}
