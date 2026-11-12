// Round 5's monsters on the game's own maps (HlAliens.hpp, HlMilitary.hpp):
// the tentacles of c1a4i, which rise to what they hear and strike at it, and
// are put down only by the map; the apache of c2a5, which flies, fires at
// the player, and comes down to enough of a blast; big momma in c4a2, who
// walks her path node by node, is sent on by being hurt, spits, and lays her
// young; and the nihilanth of c4a3, its spheres about it, which zaps, takes
// the player away, opens its head once its crystals are gone, and dies of a
// blow to it; the flocks of c5a1, which fly together and keep out of the
// walls; and a soldier who comes down a rope when told (in a room of its own:
// no map of the game's has one).
//
// Needs the game: HLDS_VALVE_DIR. Without it, nothing is run.

#include <algorithm>
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

    int findKind(const Game &g, ActorKind kind, int nth = 0)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(kind) && nth-- == 0)
                return i;
        }
        return -1;
    }
    int countKind(const Game &g, ActorKind kind)
    {
        int n = 0;
        for (int i = 0; i < g.actors.count; ++i)
            n += g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(kind) ? 1 : 0;
        return n;
    }

    int lost = 0;

    /// A frame, the player held where they are, and kept alive.
    void tickStill(Game &g, int ms = 33)
    {
        const fx at[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        logicTick(g.logic, g.player, ms);
        monstersTick(g.logic, g.player, ms);
        g.drain();
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = at[k];
        lost += 100 - g.player.health;
        g.player.health = 100;
        g.player.inv.armor = 0;
    }

    /// The clip actor `index` is playing, by its name.
    bool playing(const Game &g, int index, const char *name)
    {
        const ActorState &actor = g.actors.states[index];
        const ModelView *model = actorModel(g.actors, actor);
        return model != nullptr && actor.clip >= 0 && model->clips[actor.clip].name == nameHash(name);
    }

    void testTentacle(const char *dir)
    {
        auto game = load(dir, "c1a4i");
        check(game != nullptr, "c1a4i loads");
        if (!game)
            return;
        Game &g = *game;
        check(countKind(g, ActorKind::Tentacle) == 3, "c1a4i has its three tentacles");
        const int k = findKind(g, ActorKind::Tentacle);
        if (k < 0)
            return;
        ActorState &tent = g.actors.states[k];
        // Out of the way, high up: nothing to hear.
        g.player.origin[0] = tent.pos[0];
        g.player.origin[1] = tent.pos[1];
        g.player.origin[2] = tent.pos[2] + fxFromInt(4000);
        for (int t = 0; t < 30 * 2; ++t)
            tickStill(g);
        check(playing(g, k, "pit_idle"), "with nothing heard, a tentacle waits in its pit");
        // A noise in front of it, on its floor: it comes up, and strikes --
        // at the player, standing there.
        fx sine, cosine;
        fxSinCos(tent.yaw, sine, cosine);
        ActorState probe;
        probe.pos[0] = tent.pos[0] + cosine * 220;
        probe.pos[1] = tent.pos[1] + sine * 220;
        probe.pos[2] = tent.pos[2] + fxFromInt(200);
        const bool floor = actorToFloor(g.move, probe, fxFromInt(64), fxFromInt(400));
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = probe.pos[a];
        g.player.origin[2] += kStandingFeet;
        lost = 0;
        bool rose = false, struck = false;
        for (int t = 0; t < 30 * 25; ++t)
        {
            if (t % 10 == 0)
                aiSound(g.actors, kSoundCombat, g.player.origin, 1024, 500);
            tickStill(g);
            rose = rose || playing(g, k, "rise_to_temp1");
            struck = struck || playing(g, k, "floor_strike") || playing(g, k, "lev1_strike");
        }
        std::printf("test_bosses: tentacle (floor %d): rose %d, struck %d, the player lost %d, health %d\n", floor ? 1 : 0,
                    rose ? 1 : 0, struck ? 1 : 0, lost, tent.health);
        check(rose, "a noise brings it up out of its pit");
        check(struck, "... and it strikes at it");
        check(lost > 0, "... which hurts whoever is there");
        // Shot: never killed by it.
        monsterHurt(g.logic, k, 500, true);
        check(tent.active && tent.ai < kAiDying, "gunfire does not kill it");
        // The map's own end of it (the rocket test: tentacle_die, off).
        const std::uint16_t die = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("tentacle_die"));
        check(die != 0, "c1a4i says how its tentacles die");
        g.player.origin[2] += fxFromInt(4000);
        logicFire(g.logic, g.player, die, -1, kUseToggle, 0);
        for (int t = 0; t < 30 * 40 && countKind(g, ActorKind::Tentacle) > 0; ++t)
            tickStill(g);
        std::printf("test_bosses: after tentacle_die, %d tentacles\n", countKind(g, ActorKind::Tentacle));
        check(countKind(g, ActorKind::Tentacle) == 0, "... which the map does");
    }
    void testApache(const char *dir)
    {
        auto game = load(dir, "c2a5");
        check(game != nullptr, "c2a5 loads");
        if (!game)
            return;
        Game &g = *game;
        const int k = findKind(g, ActorKind::Apache);
        check(k >= 0, "c2a5 has its apache");
        if (k < 0)
            return;
        ActorState &heli = g.actors.states[k];
        {
            // Its gun from its barrel (GetAttachment(0)), tipped as it
            // flies: nosed down, the barrel is lower and further back.
            ActorState level = heli, nosed = heli;
            level.wantPitch = level.wantYaw = 0;
            nosed.wantPitch = -4096; // (nosed down: actorTilt's pitch, up positive)
            nosed.wantYaw = 0;
            fx a[3], b[3];
            const bool has = actorAttachment(g.actors, level, 0, a) && actorAttachment(g.actors, nosed, 0, b);
            const fx d[3] = {a[0] - heli.pos[0], a[1] - heli.pos[1], a[2] - heli.pos[2]};
            const fx moved[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            std::printf("test_bosses: apache's barrel %s, %d from its middle; nosed down it moves %d (down %d)\n",
                        has ? "posed" : "MISSING", fxToInt(fxLength(d)), fxToInt(fxLength(moved)), fxToInt(-moved[2]));
            check(has && fxLength(d) < fxFromInt(300), "the apache's gun is where its model has its barrel");
            check(has && fxLength(moved) > fxFromInt(8), "... tipped as it flies");
        }
        const fx start[3] = {heli.pos[0], heli.pos[1], heli.pos[2]};
        // The player on the ground some way off, in the open.
        bool placed = false;
        for (int turn = 0; turn < 16 && !placed; ++turn)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
            ActorState probe;
            probe.pos[0] = heli.pos[0] + cosine * 600;
            probe.pos[1] = heli.pos[1] + sine * 600;
            probe.pos[2] = heli.pos[2];
            if (!actorToFloor(g.move, probe, 0, fxFromInt(2000)))
                continue;
            const fx eye[3] = {probe.pos[0], probe.pos[1], probe.pos[2] + fxFromInt(64)};
            if (traceShot(g.move, heli.pos, eye).fraction < kFractionOne)
                continue;
            for (int a = 0; a < 3; ++a)
                g.player.origin[a] = probe.pos[a];
            g.player.origin[2] += kStandingFeet;
            placed = true;
        }
        check(placed, "somewhere in the open it can see");
        for (int t = 0; t < 30 * 2; ++t)
            tickStill(g);
        check(aiDist(start, heli.pos) < fxFromInt(4), "it waits to be called");
        heli.switched = 1 + kUseToggle;
        lost = 0;
        int tracers = 0, rockets = 0;
        fx most = 0;
        double nearest = 1e9; // (how near a round of its passes the player's middle)
        for (int t = 0; t < 30 * 30; ++t)
        {
            tickStill(g);
            for (const Effect &effect : g.actors.effects)
            {
                tracers += effect.kind == kEffectTracer && effect.leftMs > 0 ? 1 : 0;
                if (effect.kind != kEffectTracer || effect.leftMs <= 0)
                    continue;
                double ab[3], ap[3], len2 = 0, along = 0;
                for (int k = 0; k < 3; ++k)
                {
                    ab[k] = (effect.b[k] - effect.a[k]) / 4096.0;
                    ap[k] = (g.player.origin[k] - effect.a[k]) / 4096.0;
                    len2 += ab[k] * ab[k];
                    along += ab[k] * ap[k];
                }
                const double u = len2 > 0 ? std::max(0.0, std::min(1.0, along / len2)) : 0;
                double d2 = 0;
                for (int k = 0; k < 3; ++k)
                    d2 += (ap[k] - ab[k] * u) * (ap[k] - ab[k] * u);
                nearest = std::min(nearest, std::sqrt(d2));
            }
            for (const Projectile &shot : g.actors.shots)
                rockets += shot.active && shot.kind == kShotRocketFoe && shot.leftMs > 30000 - 40 ? 1 : 0;
            const fx d = aiDist(start, heli.pos);
            most = d > most ? d : most;
        }
        std::printf("test_bosses: apache flew up to %d units, %d frames of tracers, %d rockets, the player lost %d; "
                    "its nearest round passed %.0f units from them\n",
                    fxToInt(most), tracers, rockets, lost, nearest);
        check(most > fxFromInt(300), "called, it flies");
        check(tracers > 0 || rockets > 0, "... fires at the player");
        // (Its rounds in the game's four degree cone: from far off, one in
        // many goes home -- what is asked is that they are aimed at them.)
        check(lost > 0 || nearest < 48.0, "... its rounds at them, through or by the player's box");
        check(heli.pos[2] > start[2] - fxFromInt(2000), "... and stays in the air");
        // Two rockets' worth of blast: down it comes.
        monsterHurt(g.logic, k, 150, false, kHitBody, kDmgBlast);
        check(heli.ai >= kAiDying, "a blast brings it down");
        for (int t = 0; t < 30 * 17 && heli.active; ++t)
            tickStill(g);
        check(!heli.active, "... and it is gone in a blast");
    }
    void testMomma(const char *dir)
    {
        auto game = load(dir, "c4a2");
        check(game != nullptr, "c4a2 loads");
        if (!game)
            return;
        Game &g = *game;
        const int k = findKind(g, ActorKind::BigMomma);
        check(k >= 0, "c4a2 has big momma");
        if (k < 0)
            return;
        ActorState &mom = g.actors.states[k];
        check((g.view.actors[k].item & 0xFFFF) != 0, "... who knows her first node");
        int places = 0;
        for (int i = k + 1; i <= k + 6 && i < g.actors.count; ++i)
            places += g.view.actors[i].kind == static_cast<std::uint8_t>(ActorKind::BabyCrab) ? 1 : 0;
        std::printf("test_bosses: big momma has %d places for her young\n", places);
        check(places == 6, "... and places for her young");
        // The player a way off in front of her, in her view.
        fx sine, cosine;
        fxSinCos(mom.yaw, sine, cosine);
        ActorState probe;
        // (Out of reach of her claws: what she does from further off.)
        probe.pos[0] = mom.pos[0] + cosine * 650;
        probe.pos[1] = mom.pos[1] + sine * 650;
        probe.pos[2] = mom.pos[2] + fxFromInt(64);
        actorToFloor(g.move, probe, fxFromInt(64), fxFromInt(512));
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = probe.pos[a];
        g.player.origin[2] += kStandingFeet;
        // (Out of her way while she walks: high above.)
        g.player.origin[2] += fxFromInt(4000);
        mom.switched = 1 + kUseToggle; // (she has a name: she waits to be triggered)
        const fx start[3] = {mom.pos[0], mom.pos[1], mom.pos[2]};
        lost = 0;
        int children = 0, spits = 0;
        fx most = 0;
        int placedAt = -1;
        for (int t = 0; t < 30 * 150; ++t)
        {
            // Once she stops at a node that holds her (her path is long, and
            // she waits on the way), the player is before her, out of reach
            // of her claws.
            const AiMonster *now = aiFind(g.actors, k);
            const bool standing = now != nullptr && now->schedule != nullptr &&
                                  std::string(now->schedule->name) != "Big Node" && t > 30 * 10;
            if (placedAt < 0 && standing)
            {
                placedAt = t;
                fx s2, c2;
                fxSinCos(mom.yaw, s2, c2);
                ActorState there;
                there.pos[0] = mom.pos[0] + c2 * 450;
                there.pos[1] = mom.pos[1] + s2 * 450;
                there.pos[2] = mom.pos[2] + fxFromInt(64);
                if (actorToFloor(g.move, there, fxFromInt(64), fxFromInt(512)))
                {
                    for (int a = 0; a < 3; ++a)
                        g.player.origin[a] = there.pos[a];
                    g.player.origin[2] += kStandingFeet;
                }
            }
            tickStill(g);
            const fx d = aiDist(start, mom.pos);
            most = d > most ? d : most;
            for (int i = k + 1; i <= k + 6 && i < g.actors.count; ++i)
                children = std::max(children, g.actors.states[i].active ? i - k : 0);
            for (const Projectile &shot : g.actors.shots)
                spits += shot.active && shot.kind == kShotMommaSpit ? 1 : 0;
            if (placedAt >= 0 && t > placedAt + 30 * 32)
                break;
            // Left alone a while (to fight); then hurt now and then in her
            // sack: on to the next node.
            if (placedAt >= 0 && t > placedAt + 30 * 25 && t % 30 == 29)
                monsterHurt(g.logic, k, 100, false, 1, kDmgBlast);
        }
        const AiMonster *m = aiFind(g.actors, k);
        std::printf("test_bosses: big momma walked %d units, node %d, %d young out, %d frames of spit, the player lost %d,"
                    " health %d, schedule %s\n",
                    fxToInt(most), m != nullptr ? m->other : -1, children, spits, lost, mom.health,
                    m != nullptr && m->schedule != nullptr ? m->schedule->name : "-");
        check(most > fxFromInt(150), "she walks her path");
        check(children > 0, "... lays her young");
        check(spits > 0 || lost > 0, "... and goes for the player");
        check(mom.active && mom.ai < kAiDying, "... and does not die while there is more of her path");
    }
    void testNihilanth(const char *dir)
    {
        auto game = load(dir, "c4a3");
        check(game != nullptr, "c4a3 loads");
        if (!game)
            return;
        Game &g = *game;
        const int k = findKind(g, ActorKind::Nihilanth);
        check(k >= 0, "c4a3 has the nihilanth");
        if (k < 0)
            return;
        ActorState &boss = g.actors.states[k];
        // The player some way off, on the arena's floor below it.
        ActorState probe;
        probe.pos[0] = boss.pos[0] + fxFromInt(700);
        probe.pos[1] = boss.pos[1];
        probe.pos[2] = boss.pos[2] + fxFromInt(200);
        actorToFloor(g.move, probe, fxFromInt(64), fxFromInt(1200));
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = probe.pos[a];
        g.player.origin[2] += kStandingFeet;
        for (int t = 0; t < 30; ++t)
            tickStill(g);
        std::printf("test_bosses: nihilanth with %d spheres\n", static_cast<int>(boss.reach));
        check(boss.reach == 20, "it has its twenty spheres about it");
        boss.switched = 1 + kUseOn; // (the map's trigger_auto, after fifteen seconds)
        lost = 0;
        int zaps = 0, portals = 0, moved = 0;
        for (int t = 0; t < 30 * 60; ++t)
        {
            const fx at[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            g.drain();
            const fx d[3] = {g.player.origin[0] - at[0], g.player.origin[1] - at[1], g.player.origin[2] - at[2]};
            if (fxLength(d) > fxFromInt(200))
                ++moved; // (taken away)
            for (int a = 0; a < 3; ++a)
                g.player.origin[a] = at[a];
            lost += 100 - g.player.health;
            g.player.health = 100;
            for (const Projectile &shot : g.actors.shots)
            {
                zaps += shot.active && shot.kind == kShotNihilZap ? 1 : 0;
                portals += shot.active && shot.kind == kShotNihilPortal ? 1 : 0;
            }
        }
        std::printf("test_bosses: nihilanth: %d frames of zaps, %d of portal balls, the player taken away %d times, lost %d\n",
                    zaps, portals, moved, lost);
        check(zaps > 0 || portals > 0, "switched on, it throws its balls at the player");
        check(lost > 0 || moved > 0, "... which hurt them, or take them away");
        // Shot while it has its spheres and its crystals: it does not die.
        for (int n = 0; n < 40; ++n)
            monsterHurt(g.logic, k, 50, true, 2);
        check(boss.active && boss.ai < kAiDying && boss.health >= 1, "it does not die while its head is shut");
        // Its crystals gone (the map's killtargets): its head opens, and a
        // blow to it there is the end of it.
        for (const char *name : {"n_recharger1", "n_recharger2", "n_recharger3"})
            logicKill(g.logic, nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash(name)));
        for (int t = 0; t < 30 * 120 && boss.mood < 2; ++t)
        {
            tickStill(g);
            if (t % 15 == 0)
                monsterHurt(g.logic, k, 20, true, 0);
        }
        check(boss.mood >= 2, "with its crystals gone, its head opens");
        for (int n = 0; n < 60 && boss.ai < kAiDying; ++n)
        {
            monsterHurt(g.logic, k, 60, true, 2);
            tickStill(g);
        }
        check(boss.ai >= kAiDying, "... and shot there, it dies");
        const fx died = boss.pos[2];
        for (int t = 0; t < 30 * 30; ++t)
            tickStill(g);
        std::printf("test_bosses: nihilanth died at %d, rose to %d\n", fxToInt(died), fxToInt(boss.pos[2]));
        check(boss.pos[2] > died, "... rising as it goes");
    }
    void testFlock(const char *dir)
    {
        auto game = load(dir, "c5a1");
        check(game != nullptr, "c5a1 loads");
        if (!game)
            return;
        Game &g = *game;
        const int boids = countKind(g, ActorKind::Boid);
        std::printf("test_bosses: c5a1 has %d boids\n", boids);
        check(boids == 24, "c5a1's three flocks of eight");
        const int k = findKind(g, ActorKind::Boid);
        if (k < 0)
            return;
        // The player where they can see the first of them.
        for (int a = 0; a < 3; ++a)
            g.player.origin[a] = g.actors.states[k].pos[a];
        g.player.origin[2] += fxFromInt(64);
        fx start[3] = {g.actors.states[k].pos[0], g.actors.states[k].pos[1], g.actors.states[k].pos[2]};
        int startSolid = 0;
        for (int i = 0; i < g.actors.count; ++i)
            startSolid += g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Boid) &&
                                  pointContents(g.move, g.actors.states[i].pos) == kContentsSolid
                              ? 1
                              : 0;
        std::printf("test_bosses: %d boids start in the world\n", startSolid);
        fx most = 0, apart = 0;
        bool walled = false;
        for (int t = 0; t < 30 * 20; ++t)
        {
            tickStill(g);
            const ActorState &head = g.actors.states[k];
            most = std::max(most, aiDist(start, head.pos));
            for (int i = k + 1; i < k + 8; ++i)
            {
                const ActorState &b = g.actors.states[i];
                apart = std::max(apart, aiDist(head.pos, b.pos));
                walled = walled || pointContents(g.move, b.pos) == kContentsSolid;
            }
            walled = walled || pointContents(g.move, head.pos) == kContentsSolid;
        }
        std::printf("test_bosses: flock flew %d units; at most %d from its leader; in a wall %d\n", fxToInt(most),
                    fxToInt(apart), walled ? 1 : 0);
        check(most > fxFromInt(200), "a flock flies");
        check(apart < fxFromInt(1200), "... together");
        check(!walled, "... and none of it into the walls");
    }
    void testRepel(const char *dir)
    {
        BoxRoom room;
        room.maxs[2] = 256;
        room.extraEntities = "{\n\"classname\" \"monster_grunt_repel\"\n\"targetname\" \"rope\"\n"
                             "\"origin\" \"0 0 200\"\n\"angle\" \"90\"\n}\n"
                             "{\n\"classname\" \"info_player_start\"\n\"origin\" \"100 100 36\"\n}\n";
        auto game = std::make_unique<Game>();
        std::string error;
        check(game->bsp.parse(buildBoxRoom(room), error), "the rope's room parses");
        finish(*game, nullptr, dir);
        check(game->ok, "... and cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        int rope = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Repel))
                rope = static_cast<int>(i);
        }
        check(rope >= 0, "the soldier's rope is a record");
        if (rope < 0)
            return;
        const int k = static_cast<int>(g.view.logic[rope].extra[0]);
        check(k < g.actors.count && !g.actors.states[k].active, "... and his place is kept, empty");
        g.player.origin[0] = fxFromInt(100);
        g.player.origin[1] = fxFromInt(100);
        g.player.origin[2] = fxFromInt(36);
        g.player.origin[2] += fxFromInt(1000); // (out of his sight: he only comes down)
        for (int t = 0; t < 30; ++t)
            tickStill(g);
        check(!g.actors.states[k].active, "untold, he is not there");
        logicUse(g.logic, g.player, rope, -1, kUseToggle);
        bool roping = false, beam = false;
        fx highest = 0;
        for (int t = 0; t < 30 * 4; ++t)
        {
            tickStill(g);
            const ActorState &grunt = g.actors.states[k];
            roping = roping || (g.actors.ai != nullptr && (g.actors.ai->held[k] & kHeldRope));
            highest = std::max(highest, grunt.pos[2]);
            for (const Effect &effect : g.actors.effects)
                beam = beam || (effect.kind == kEffectBeam && effect.leftMs > 0);
        }
        const ActorState &grunt = g.actors.states[k];
        std::printf("test_bosses: the soldier came down from %d to %d on his rope (seen %d)\n", fxToInt(highest),
                    fxToInt(grunt.pos[2]), beam ? 1 : 0);
        check(grunt.active && roping, "told, he comes down a rope");
        check(beam, "... which is seen");
        check(grunt.pos[2] < fxFromInt(8) && highest > fxFromInt(100) &&
                  !(g.actors.ai != nullptr && (g.actors.ai->held[k] & kHeldRope)),
              "... to the floor, and off it");
    }
    /// A soldier on a rope with the player in sight: he turns to them and
    /// shoots on the way down (slGruntRepelAttack), and lands
    /// (slGruntRepelLand) once he is down.
    void testRepelFight(const char *dir)
    {
        BoxRoom room;
        room.mins[0] = -512;
        room.maxs[0] = 512;
        room.maxs[2] = 640;
        room.extraEntities = "{\n\"classname\" \"monster_grunt_repel\"\n\"targetname\" \"rope\"\n"
                             "\"origin\" \"-300 0 600\"\n\"angle\" \"0\"\n}\n"
                             "{\n\"classname\" \"info_player_start\"\n\"origin\" \"300 0 36\"\n}\n";
        auto game = std::make_unique<Game>();
        std::string error;
        check(game->bsp.parse(buildBoxRoom(room), error), "the fighting rope's room parses");
        finish(*game, nullptr, dir);
        check(game->ok, "... and cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        int rope = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Repel))
                rope = static_cast<int>(i);
        }
        check(rope >= 0, "the fighting soldier's rope is a record");
        if (rope < 0)
            return;
        const int k = static_cast<int>(g.view.logic[rope].extra[0]);
        g.player.origin[0] = fxFromInt(300);
        g.player.origin[1] = 0;
        g.player.origin[2] = fxFromInt(36);
        lost = 0;
        logicUse(g.logic, g.player, rope, -1, kUseToggle);
        bool attacked = false, landed = false, shotFromRope = false;
        int lostOnRope = 0;
        for (int t = 0; t < 30 * 8; ++t)
        {
            const int before = lost;
            const bool wasOnRope = g.actors.ai != nullptr && (g.actors.ai->held[k] & kHeldRope);
            tickStill(g);
            if (wasOnRope)
                lostOnRope += lost - before;
            const bool onRope = g.actors.ai != nullptr && (g.actors.ai->held[k] & kHeldRope);
            const AiMonster *m = aiFind(g.actors, k);
            if (m == nullptr)
                continue;
            if (onRope && m->schedule == &grunt::slRepelAttack)
                attacked = true;
            if (!onRope && m->schedule == &grunt::slRepelLand)
                landed = true;
        }
        shotFromRope = lostOnRope > 0;
        std::printf("test_bosses: on his rope he fought %d; the player lost %d to him there, %d in all; landed %d\n",
                    attacked ? 1 : 0, lostOnRope, lost, landed ? 1 : 0);
        check(attacked, "a soldier on a rope turns on the player he sees");
        check(shotFromRope, "... and shoots on the way down");
        check(landed, "... and lands at the foot of it");
    }
    /// The hazard course's rats (CRat): there, thinking as a plain monster
    /// does, and they die.
    void testRats(const char *dir)
    {
        auto game = load(dir, "t0a0b");
        check(game != nullptr, "t0a0b loads");
        if (!game)
            return;
        Game &g = *game;
        const int rats = countKind(g, ActorKind::Rat);
        const int k = findKind(g, ActorKind::Rat);
        std::printf("test_bosses: t0a0b has %d rats\n", rats);
        check(rats == 3 && k >= 0, "t0a0b's three rats");
        {
            // After every other actor the map places (a save made before
            // they were points at those by where they are; the places kept
            // for what is dropped come last, as they always have).
            int lastOther = -1, firstRat = -1;
            for (std::uint32_t i = 0; i < g.view.actorCount; ++i)
            {
                if (g.view.actors[i].kind == static_cast<std::uint8_t>(ActorKind::Rat))
                    firstRat = firstRat < 0 ? static_cast<int>(i) : firstRat;
                else if (!(g.view.actors[i].flags & kActorDormant))
                    lastOther = static_cast<int>(i);
            }
            check(firstRat > lastOther, "... cooked after every other actor the map places");
        }
        if (k < 0)
            return;
        const ModelView *model = actorModel(g.actors, g.actors.states[k]);
        check(model != nullptr && model->findClip(nameHash("die1")) >= 0, "... with their model and its death");
        const std::uint16_t ref = g.view.actors[k].model;
        check(ref < g.view.modelRefCount && modelRefDsiOnly(g.view.modelRefs[ref]),
              "... which is a DSi's only (a DS has no room for it there)");
        {
            // On a DS: not there.
            auto ds = load(dir, "t0a0b");
            if (ds)
            {
                consoleActors(ds->logic, false);
                int there = 0;
                for (int i = 0; i < ds->actors.count; ++i)
                    there += ds->actors.states[i].active &&
                                     ds->actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Rat)
                                 ? 1
                                 : 0;
                check(there == 0, "... and a DS leaves them out");
            }
        }
        for (int t = 0; t < 30 * 3; ++t)
            tickStill(g);
        const AiMonster *m = aiFind(g.actors, k);
        check(g.actors.states[k].active && m != nullptr && m->schedule != nullptr, "... thinking as the game's do");
        const fx at[3] = {g.actors.states[k].pos[0], g.actors.states[k].pos[1], g.actors.states[k].pos[2] + fxFromInt(4)};
        radiusDamage(g.logic, g.player, at, 50, 64, -1);
        for (int t = 0; t < 30 * 3; ++t)
            tickStill(g);
        std::printf("test_bosses: the rat's state after a blast: ai %d\n", g.actors.states[k].ai);
        check(g.actors.states[k].ai >= kAiDying, "... and a blast kills one");
    }
    /// The alien slave by its other name, and a flyer on its own (a flock
    /// of one, its own leader): cooked, and after what was there before.
    void testOtherNames(const char *dir)
    {
        BoxRoom room;
        room.mins[0] = room.mins[1] = -512;
        room.maxs[0] = room.maxs[1] = 512;
        room.maxs[2] = 384;
        room.extraEntities = "{\n\"classname\" \"monster_vortigaunt\"\n\"origin\" \"-300 0 0\"\n}\n"
                             "{\n\"classname\" \"monster_flyer\"\n\"origin\" \"200 200 200\"\n}\n"
                             "{\n\"classname\" \"monstermaker\"\n\"monstertype\" \"monster_vortigaunt\"\n"
                             "\"targetname\" \"more\"\n\"monstercount\" \"2\"\n\"origin\" \"-300 200 0\"\n}\n"
                             "{\n\"classname\" \"monster_headcrab\"\n\"origin\" \"300 -300 0\"\n}\n"
                             "{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 36\"\n}\n";
        auto game = std::make_unique<Game>();
        std::string error;
        check(game->bsp.parse(buildBoxRoom(room), error), "the room of other names parses");
        finish(*game, nullptr, dir);
        check(game->ok, "... and cooks");
        if (!game->ok)
            return;
        Game &g = *game;
        int slaves = 0, boids = 0, boid = -1, crab = -1, firstSlave = -1;
        for (std::uint32_t i = 0; i < g.view.actorCount; ++i)
        {
            const std::uint8_t kind = g.view.actors[i].kind;
            if (kind == static_cast<std::uint8_t>(ActorKind::Boid))
            {
                ++boids;
                boid = static_cast<int>(i);
            }
            if (kind == static_cast<std::uint8_t>(ActorKind::Headcrab))
                crab = static_cast<int>(i);
            if (kind == static_cast<std::uint8_t>(ActorKind::Slave))
            {
                ++slaves; // (the maker's places among them, empty until it makes one)
                firstSlave = firstSlave < 0 ? static_cast<int>(i) : firstSlave;
            }
        }
        std::printf("test_bosses: other names: %d slaves, %d boids, the crab at %d, the first slave at %d\n", slaves,
                    boids, crab, firstSlave);
        check(slaves == 3, "a vortigaunt is an alien slave, and so is what its maker makes");
        check(boids == 1 && g.view.actors[boid].item == 0, "a flyer on its own is one boid, its own leader");
        check(crab >= 0 && firstSlave > crab, "... both after what a build before cooked");
        if (boid < 0)
            return;
        // The player in sight of it: it flies, and not into the walls.
        g.player.origin[0] = 0;
        g.player.origin[1] = 0;
        g.player.origin[2] = fxFromInt(36);
        const fx start[3] = {g.actors.states[boid].pos[0], g.actors.states[boid].pos[1], g.actors.states[boid].pos[2]};
        fx most = 0;
        bool walled = false;
        for (int t = 0; t < 30 * 20; ++t)
        {
            tickStill(g);
            const ActorState &b = g.actors.states[boid];
            most = std::max(most, aiDist(start, b.pos));
            walled = walled || pointContents(g.move, b.pos) == kContentsSolid;
        }
        std::printf("test_bosses: the lone flyer flew %d units; in a wall %d\n", fxToInt(most), walled ? 1 : 0);
        check(most > fxFromInt(200), "the lone flyer flies");
        check(!walled, "... and not into the walls");
    }
} // namespace

int main()
{
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve == nullptr || *valve == '\0')
    {
        std::printf("test_bosses: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 77;
    }
    testTentacle(valve);
    testApache(valve);
    testMomma(valve);
    testNihilanth(valve);
    testFlock(valve);
    testRepel(valve);
    testRepelFight(valve);
    testRats(valve);
    testOtherNames(valve);
    if (g_failures == 0)
        std::printf("test_bosses: ok\n");
    return g_failures == 0 ? 0 : 1;
}
