// Round 3's monsters on the game's own maps (HlAliens.hpp): the cockroach,
// the monstermaker's snark, the alien grunt and its hornets, the leech, the
// alien controller -- and that bringing them in moved no actor a save
// points at.
//
// Needs the game: HLDS_VALVE_DIR. Without it, nothing is run.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "TestGame.hpp"
#include "hlds/AtomicWrite.hpp"

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
            return nullptr;
        // (The game's own graph of the map, as the converter cooks with it:
        // the nodes in the air are in it.)
        std::vector<std::uint8_t> bytes;
        if (hlds::readFile(std::filesystem::path(dir) / "maps" / "graphs" / (std::string(map) + ".nod"), bytes, error))
            hlds::parseNodGraph(bytes, game->nodGraph, error);
        finish(*game, nullptr, dir);
        if (!game->ok)
            return nullptr;
        return game;
    }

    int findKind(const Game &g, ActorKind kind, int nth = 0)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(kind) && actor.script < 0 && nth-- == 0)
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

    /// Everything that can be hurt gone, but `keep`.
    void alone(Game &g, std::initializer_list<int> keep)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            ActorState &actor = g.actors.states[i];
            bool kept = false;
            for (int k : keep)
                kept = kept || k == i;
            if (!kept && (canBeHurt(actor.kind) || isTurret(actor.kind)))
            {
                actor.active = false;
                actorPlaceBox(g.actors, i);
            }
        }
    }

    /// A frame, the player held where they are.
    void tickStill(Game &g, int ms = 33)
    {
        fx at[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        monstersTick(g.logic, g.player, ms);
        g.drain();
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = at[k];
    }

    /// The player put `distance` from actor `index`, somewhere it could
    /// walk to, facing it; false if there is no such place.
    bool standAt(Game &g, int index, int distance)
    {
        ActorState &a = g.actors.states[index];
        for (int turn = 0; turn < 16; ++turn)
        {
            const std::uint16_t yaw = static_cast<std::uint16_t>(a.yaw + (turn & 1 ? 1 : -1) * ((turn + 1) / 2) * 2048);
            fx sine, cosine;
            fxSinCos(yaw, sine, cosine);
            ActorState probe;
            probe.pos[0] = a.pos[0] + cosine * distance;
            probe.pos[1] = a.pos[1] + sine * distance;
            probe.pos[2] = a.pos[2] + fxFromInt(8);
            if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(64)) ||
                !walkClear(g.move, &g.actors, index, kWalkStanding, a.pos, probe.pos, distance / 16 + 4))
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

    /// The player put `distance` from actor `index` along the first way
    /// round it that a line runs clear (at its middle) and that has a floor,
    /// facing it; false if there is none.
    bool placeInView(Game &g, int index, int distance)
    {
        ActorState &a = g.actors.states[index];
        for (int turn = 0; turn < 16; ++turn)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
            const fx from[3] = {a.pos[0], a.pos[1], a.pos[2] + fxFromInt(40)};
            const fx to[3] = {a.pos[0] + cosine * distance, a.pos[1] + sine * distance, a.pos[2] + fxFromInt(40)};
            const fx under[3] = {to[0], to[1], to[2] - fxFromInt(1024)};
            if (traceShot(g.move, from, to).fraction < kFractionOne || traceShot(g.move, to, under).fraction >= kFractionOne)
                continue;
            ActorState probe;
            probe.pos[0] = to[0];
            probe.pos[1] = to[1];
            probe.pos[2] = to[2];
            if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(1024)))
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

    int shotsOf(const Game &g, std::uint8_t kind)
    {
        int n = 0;
        for (const Projectile &shot : g.actors.shots)
            n += shot.active && shot.kind == kind ? 1 : 0;
        return n;
    }

    // ------------------------------------------------------------ the tests

    /// They are cooked, and after everything that was there before them.
    void testCooked(const char *dir)
    {
        struct Want
        {
            const char *map;
            ActorKind kind;
        };
        const Want kWant[] = {{"c1a2", ActorKind::Cockroach}, {"c2a4d", ActorKind::AlienGrunt}};
        for (const Want &want : kWant)
        {
            auto game = load(dir, want.map);
            if (!game)
                continue;
            Game &g = *game;
            int first = -1, lastOld = -1;
            for (int i = 0; i < g.actors.recordCount; ++i)
            {
                const std::uint8_t kind = g.actors.records[i].kind;
                if (kind >= static_cast<std::uint8_t>(ActorKind::Cockroach))
                    first = first < 0 ? i : first;
                else if (kind != static_cast<std::uint8_t>(ActorKind::Item))
                    lastOld = i;
            }
            const int n = countKind(g, want.kind);
            std::printf("test_aliens: %s: %d of kind %d; the first of round 3 at %d, the last before at %d\n",
                        want.map, n, static_cast<int>(want.kind), first, lastOld);
            check(n > 0, "the map's round 3 monsters are cooked");
            check(first > lastOld, "... after every actor that was there before");
        }
    }

    /// A cockroach stays where it is while nothing comes near, runs from
    /// the player who does, and is squashed by one walking over it.
    void testRoach(const char *dir)
    {
        auto game = load(dir, "c1a2");
        if (!game)
            return;
        Game &g = *game;
        const int r = findKind(g, ActorKind::Cockroach);
        if (r < 0)
        {
            check(false, "c1a2 has a cockroach");
            return;
        }
        alone(g, {r});
        ActorState &roach = g.actors.states[r];
        // The player some way off -- just put there: a roach is often where
        // a person cannot be.
        auto putPlayer = [&](int distance)
        {
            // (The first way round it with a clear line to there.)
            for (int turn = 0; turn < 16; ++turn)
            {
                fx sine, cosine;
                fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
                const fx from[3] = {roach.pos[0], roach.pos[1], roach.pos[2] + fxFromInt(8)};
                const fx to[3] = {roach.pos[0] + cosine * distance, roach.pos[1] + sine * distance,
                                  roach.pos[2] + fxFromInt(8)};
                g.player.origin[0] = to[0];
                g.player.origin[1] = to[1];
                g.player.origin[2] = roach.pos[2] + kStandingFeet;
                if (traceShot(g.move, from, to).fraction >= kFractionOne)
                    break;
            }
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        };
        putPlayer(400);
        fx at[3] = {roach.pos[0], roach.pos[1], roach.pos[2]};
        for (int t = 0; t < 30 * 3; ++t)
            tickStill(g);
        const fx stayed = aiDist2D(at, roach.pos);
        // ... then right by it.
        putPlayer(60);
        for (int k = 0; k < 3; ++k)
            at[k] = roach.pos[k];
        int moving = 0;
        for (int t = 0; t < 30 * 3; ++t)
        {
            tickStill(g);
            moving += aiFind(g.actors, r) != nullptr && aiFind(g.actors, r)->flag1 ? 1 : 0;
            if (std::getenv("HLDS_ROACH_TRACE") != nullptr && t % 10 == 0 && aiFind(g.actors, r) != nullptr)
            {
                const AiMonster *m = aiFind(g.actors, r);
                std::printf("  roach t=%d started %d mode %d go %d time1 %d think %d state %d leaf %d pos %d %d %d act %d\n", t,
                            m->started, m->count1, m->flag1, m->time1, m->thinkMs, m->state, m->leaf,
                            fxToInt(roach.pos[0]), fxToInt(roach.pos[1]), fxToInt(roach.pos[2]), roach.active);
            }
        }
        const fx ran = aiDist2D(at, roach.pos);
        std::printf("test_aliens: roach: %d units with the player 400 off, %d with them by it (moving %d frames)\n",
                    fxToInt(stayed), fxToInt(ran), moving);
        check(ran > stayed + fxFromInt(16) && moving > 0, "a roach runs from the player beside it");
        // Walked over.
        g.player.origin[0] = roach.pos[0];
        g.player.origin[1] = roach.pos[1];
        g.player.origin[2] = roach.pos[2] + kStandingFeet;
        g.player.velocity[0] = fxFromInt(100);
        tickStill(g);
        tickStill(g);
        std::printf("test_aliens: roach walked over: active %d\n", roach.active);
        check(!roach.active, "... and is squashed by one walking over it");
        check(g.times(CommonSound::RoachSmash) + g.times(CommonSound::RoachDie) >= 1, "... heard");
    }

    /// A roach goes to food it smells.
    void testRoachFood(const char *dir)
    {
        auto game = load(dir, "c1a2");
        if (!game)
            return;
        Game &g = *game;
        // A roach with level floor 80 units one way, nothing in between: the
        // food goes there.
        auto floorAt = [&](const fx *at, fx &z)
        {
            const fx from[3] = {at[0], at[1], at[2] + fxFromInt(4)};
            const fx to[3] = {at[0], at[1], at[2] - fxFromInt(12)};
            const Shot shot = traceShot(g.move, from, to);
            z = from[2] + static_cast<fx>((static_cast<std::int64_t>(to[2] - from[2]) * shot.fraction) >> kFractionShift);
            return shot.fraction < kFractionOne;
        };
        int r = -1;
        fx food[3] = {0, 0, 0};
        for (int n = 0; r < 0 && findKind(g, ActorKind::Cockroach, n) >= 0; ++n)
        {
            const int i = findKind(g, ActorKind::Cockroach, n);
            const ActorState &roach = g.actors.states[i];
            for (int turn = 0; turn < 16 && r < 0; ++turn)
            {
                fx sine, cosine;
                fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
                bool level = true;
                for (int step = 1; step <= 10 && level; ++step)
                {
                    const fx at[3] = {roach.pos[0] + cosine * (step * 8), roach.pos[1] + sine * (step * 8), roach.pos[2]};
                    const fx from[3] = {roach.pos[0], roach.pos[1], roach.pos[2] + fxFromInt(4)};
                    const fx to[3] = {at[0], at[1], at[2] + fxFromInt(4)};
                    fx z = 0;
                    level = traceShot(g.move, from, to).fraction >= kFractionOne && floorAt(at, z) &&
                            z - roach.pos[2] < fxFromInt(2) && z - roach.pos[2] > -fxFromInt(2);
                }
                if (level)
                {
                    r = i;
                    food[0] = roach.pos[0] + cosine * 80;
                    food[1] = roach.pos[1] + sine * 80;
                    food[2] = roach.pos[2];
                }
            }
        }
        if (r < 0)
        {
            check(false, "a c1a2 roach with open floor by it");
            return;
        }
        alone(g, {r});
        ActorState &roach = g.actors.states[r];
        // (The player where they may see it, not near: it thinks ten times a
        // second while it may be seen.)
        bool placed = false;
        for (int far = 340; far >= 220 && !placed; far -= 40)
        {
            for (int turn = 0; turn < 16 && !placed; ++turn)
            {
                fx sine, cosine;
                fxSinCos(static_cast<std::uint16_t>(turn * 4096), sine, cosine);
                const fx from[3] = {roach.pos[0], roach.pos[1], roach.pos[2] + fxFromInt(8)};
                const fx to[3] = {roach.pos[0] + cosine * far, roach.pos[1] + sine * far, roach.pos[2] + fxFromInt(8)};
                g.player.origin[0] = to[0];
                g.player.origin[1] = to[1];
                g.player.origin[2] = roach.pos[2] + kStandingFeet;
                placed = traceShot(g.move, from, to).fraction >= kFractionOne;
            }
        }
        tickStill(g);
        // (A gib, landed.)
        soundInsert(g.ai.sounds, g.ai.now, kSoundMeat, food, 384, 25000);
        const fx before = aiDist2D(food, roach.pos);
        for (int t = 0; t < 30 * 4; ++t)
            tickStill(g);
        const fx after = aiDist2D(food, roach.pos);
        std::printf("test_aliens: roach and food: %d units off, then %d\n", fxToInt(before), fxToInt(after));
        check(after < before - fxFromInt(30), "a roach goes to what it smells");
    }

    /// The snarks of c3a1's first egg hunt the player, bite, and burst.
    void testSnark(const char *dir)
    {
        auto game = load(dir, "c3a1");
        if (!game)
            return;
        Game &g = *game;
        const std::uint16_t name = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("snarkegg1spawn"));
        int maker = -1;
        for (int i = 0; i < g.logic.recordCount && name != 0; ++i)
        {
            if (g.logic.records[i].kind == static_cast<std::uint8_t>(LogicKind::Maker) && g.logic.records[i].name == name)
                maker = i;
        }
        if (maker < 0)
        {
            check(false, "c3a1 has its snark makers");
            return;
        }
        alone(g, {});
        // The player by where it makes them (a place it keeps: a dormant
        // snark).
        int place = -1;
        for (int i = 0; i < g.actors.recordCount && place < 0; ++i)
        {
            if (g.actors.records[i].kind == static_cast<std::uint8_t>(ActorKind::Snark) &&
                (g.actors.records[i].flags & kActorDormant))
                place = i;
        }
        if (place < 0)
        {
            check(false, "c3a1's snark makers keep places");
            return;
        }
        const fx *spot = g.actors.records[place].origin;
        g.player.origin[0] = spot[0] + fxFromInt(96);
        g.player.origin[1] = spot[1];
        g.player.origin[2] = spot[2] + kStandingFeet;
        const int health = g.player.health = 100;
        tickStill(g); // (the map begun: what it does as it starts, done)
        logicFire(g.logic, g.player, name, -1, kUseOn, 0);
        if (std::getenv("HLDS_SNARK_TRACE") != nullptr) std::printf("  fired: maker %d running %d name %d recname %d\n", maker, g.logic.states[maker].running, name, g.logic.records[maker].name);
        int most = 0, bitten = 0;
        for (int t = 0; t < 30 * 20; ++t)
        {
            tickStill(g);
            most = std::max(most, shotsOf(g, kShotSnarkFoe));
            if (std::getenv("HLDS_SNARK_TRACE") != nullptr && t % 30 == 0)
            {
                int makers = 0, running = 0, made = 0, live = 0;
                for (int i = 0; i < g.logic.recordCount; ++i)
                    if (g.logic.records[i].kind == static_cast<std::uint8_t>(LogicKind::Maker) && g.logic.records[i].name == name)
                    {
                        ++makers;
                        running += g.logic.states[i].running ? 1 : 0;
                        if (t == 0) std::printf("  maker %d dead %d phase %d flags %d extra %u %u %u\n", i, g.logic.states[i].dead, g.logic.states[i].phase, g.logic.records[i].flags, g.logic.records[i].extra[0], g.logic.records[i].extra[1], g.logic.records[i].extra[2]);
                        made += g.logic.states[i].bits;
                    }
                for (int i = 0; i < g.actors.count; ++i)
                    live += g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Snark);
                std::printf("  t=%d makers %d running %d made %d live snark actors %d\n", t, makers, running, made, live);
            }
            if (std::getenv("HLDS_SNARK_TRACE") != nullptr && t % 15 == 0)
                for (const Projectile &shot : g.actors.shots)
                    if (shot.active && shot.kind == kShotSnarkFoe)
                        std::printf("  t=%d snark at %d %d %d (player %d %d %d) vel %d %d %d left %d\n", t, fxToInt(shot.pos[0]),
                                    fxToInt(shot.pos[1]), fxToInt(shot.pos[2]), fxToInt(g.player.origin[0]),
                                    fxToInt(g.player.origin[1]), fxToInt(g.player.origin[2]), fxToInt(shot.velocity[0]),
                                    fxToInt(shot.velocity[1]), fxToInt(shot.velocity[2]), shot.leftMs);
            bitten = std::max(bitten, health - g.player.health);
            g.player.health = 100;
        }
        std::printf("test_aliens: snark: %d let go at once at most, the player bitten for %d, %d left after 20 s\n",
                    most, bitten, shotsOf(g, kShotSnarkFoe));
        check(most >= 1, "a snark maker's snark is let go");
        check(bitten >= kSnarkBite, "... it bites the player");
        check(shotsOf(g, kShotSnarkFoe) == 0, "... and is gone when its time is up");
    }

    /// An alien grunt sends hornets at the player who stands off, and
    /// punches the one who comes close.
    void testAgrunt(const char *dir)
    {
        // (The first of them, on the maps they are on, with room about it.)
        std::unique_ptr<Game> game;
        int a = -1;
        bool room = false;
        for (const char *map : {"c2a5c", "c2a5e", "c3a1", "c3a2", "c2a4e", "c4a1b", "c4a1d", "c4a1e", "c5a1"})
        {
            game = load(dir, map);
            if (!game)
                continue;
            for (int n = 0; !room && findKind(*game, ActorKind::AlienGrunt, n) >= 0; ++n)
            {
                a = findKind(*game, ActorKind::AlienGrunt, n);
                if (game->actors.states[a].name != 0)
                    continue; // (one with a name waits to be set off)
                for (int far : {400, 320, 500, 250})
                    room = room || standAt(*game, a, far) || placeInView(*game, a, far);
            }
            if (room)
            {
                std::printf("test_aliens: agrunt: %s, actor %d\n", map, a);
                break;
            }
        }
        if (!game)
            return;
        Game &g = *game;
        alone(g, {a});
        if (!room)
        {
            check(false, "room by the alien grunt");
            return;
        }
        int hornets = 0, stung = 0;
        for (int t = 0; t < 30 * 12; ++t)
        {
            const int before = g.player.health;
            tickStill(g);
            hornets = std::max(hornets, shotsOf(g, kShotHornetFoe));
            stung += before - g.player.health > 0 ? before - g.player.health : 0;
            g.player.health = 100;
        }
        std::printf("test_aliens: agrunt at 400: %d hornets in the air at most, %d taken, its schedule %s\n", hornets,
                    stung, aiFind(g.actors, a) && aiFind(g.actors, a)->schedule ? aiFind(g.actors, a)->schedule->name : "-");
        check(hornets >= 1, "an alien grunt sends hornets at the player");
        check(stung > 0, "... which find them");
        // Close by.
        if (!standAt(g, a, 70))
            placeInView(g, a, 70);
        int punched = 0;
        for (int t = 0; t < 30 * 6; ++t)
        {
            const int before = g.player.health;
            tickStill(g);
            if (before - g.player.health >= skillData(kSkillMedium).agruntDmgPunch * 4 / 5 - 1)
                ++punched;
            g.player.health = 100;
        }
        std::printf("test_aliens: agrunt at 70: %d punches\n", punched);
        check(punched > 0, "... and punches one beside it");
    }

    /// A leech swims its water and stays in it, and bites the player who
    /// comes in.
    void testLeech(const char *dir)
    {
        auto game = load(dir, "c2a3");
        if (!game)
            return;
        Game &g = *game;
        const int l = findKind(g, ActorKind::Leech);
        if (l < 0)
        {
            check(false, "c2a3 has a leech");
            return;
        }
        alone(g, {l});
        ActorState &leech = g.actors.states[l];
        const bool wet = pointContents(g.move, leech.pos) == kContentsWater;
        // The player in the water by it.
        g.player.origin[0] = leech.pos[0] + fxFromInt(60);
        g.player.origin[1] = leech.pos[1];
        g.player.origin[2] = leech.pos[2];
        g.player.waterLevel = 3;
        std::printf("test_aliens: leech at %d %d %d\n", fxToInt(leech.pos[0]), fxToInt(leech.pos[1]), fxToInt(leech.pos[2]));
        int outOfWater = 0, taken = 0;
        fx went = 0;
        fx last[3] = {leech.pos[0], leech.pos[1], leech.pos[2]};
        for (int t = 0; t < 30 * 20; ++t)
        {
            const int before = g.player.health;
            g.player.waterLevel = 3;
            tickStill(g);
            taken += before - g.player.health > 0 ? before - g.player.health : 0;
            g.player.health = 100;
            outOfWater += pointContents(g.move, leech.pos) == kContentsWater ? 0 : 1;
            went += aiDist(last, leech.pos);
            for (int k = 0; k < 3; ++k)
                last[k] = leech.pos[k];
        }
        std::printf("test_aliens: leech: in water at the start %d, %d frames out of it, swam %d units, the player bitten for %d\n",
                    wet, outOfWater, fxToInt(went), taken);
        check(wet, "a leech starts in its water");
        check(outOfWater == 0, "... and stays in it");
        check(went > fxFromInt(100), "... swimming");
        check(taken > 0, "... and bites the player in the water");
    }

    /// An alien controller flies -- by the nodes in the air where it must --
    /// throws its balls at the player, and falls when it is killed.
    void testController(const char *dir)
    {
        std::unique_ptr<Game> game;
        int k = -1;
        for (const char *map : {"c4a1a", "c4a1b", "c4a1c", "c4a3", "c3a2d"})
        {
            game = load(dir, map);
            if (!game)
                continue;
            for (int n = 0; k < 0 && findKind(*game, ActorKind::Controller, n) >= 0; ++n)
            {
                const int i = findKind(*game, ActorKind::Controller, n);
                if (game->actors.states[i].name == 0)
                    k = i;
            }
            if (k >= 0)
            {
                int air = 0;
                for (int n = 0; n < game->actors.nodeCount; ++n)
                    air += (game->actors.nodes[n].flags & kPathNodeAir) ? 1 : 0;
                std::printf("test_aliens: controller: %s, actor %d; %d of %d nodes in the air\n", map, k, air,
                            game->actors.nodeCount);
                int flyLinks = 0;
                for (int n = 0; n < game->actors.linkCount; ++n)
                    flyLinks += (game->actors.links[n].flags & kPathLinkFly) ? 1 : 0;
                std::printf("test_aliens: controller: %d ways for what flies\n", flyLinks);
                check(air > 0, "a map with a controller has the nodes in the air");
                break;
            }
        }
        if (k < 0)
        {
            check(false, "an alien controller with no name, on its maps");
            return;
        }
        Game &g = *game;
        alone(g, {k});
        ActorState &con = g.actors.states[k];
        bool room = false;
        for (int far : {500, 400, 600, 300})
            room = room || placeInView(g, k, far);
        if (!room)
        {
            check(false, "room by the controller");
            return;
        }
        const fx start[3] = {con.pos[0], con.pos[1], con.pos[2]};
        std::printf("test_aliens: controller at %d %d %d, the player at %d %d %d\n", fxToInt(con.pos[0]), fxToInt(con.pos[1]),
                    fxToInt(con.pos[2]), fxToInt(g.player.origin[0]), fxToInt(g.player.origin[1]), fxToInt(g.player.origin[2]));
        int balls = 0, taken = 0;
        for (int t = 0; t < 30 * 15; ++t)
        {
            const int before = g.player.health;
            tickStill(g);
            balls = std::max(balls, shotsOf(g, kShotZapBall) + shotsOf(g, kShotHeadBall));
            taken += before - g.player.health > 0 ? before - g.player.health : 0;
            g.player.health = 100;
        }
        const fx moved = aiDist(start, con.pos);
        std::printf("test_aliens: controller: moved %d units, %d balls in the air at most, %d taken, schedule %s\n",
                    fxToInt(moved), balls, taken,
                    aiFind(g.actors, k) && aiFind(g.actors, k)->schedule ? aiFind(g.actors, k)->schedule->name : "-");
        check(moved > fxFromInt(32), "a controller flies");
        check(balls >= 1, "... throws its balls of energy");
        check(taken > 0, "... which hurt the player");
        // Killed in the air: down it comes.
        const fx high = con.pos[2];
        monsterHurt(g.logic, k, 200, true);
        for (int t = 0; t < 30 * 4; ++t)
            tickStill(g);
        std::printf("test_aliens: controller killed at %d, came down to %d\n", fxToInt(high), fxToInt(con.pos[2]));
        check(con.ai >= kAiDying && con.pos[2] <= high, "... and when killed, falls");
    }

    /// Fifty roaches, what each frame costs this host (the console's is
    /// perhaps fifty times that).
    void benchRoaches(const char *dir)
    {
        auto game = load(dir, "c2a4a");
        if (!game)
            return;
        Game &g = *game;
        const int n = countKind(g, ActorKind::Cockroach);
        const auto start = std::chrono::steady_clock::now();
        for (int t = 0; t < 600; ++t)
            tickStill(g);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("test_aliens: c2a4a, %d roaches: %.3f ms a frame on this host, all of it\n", n, ms / 600);
    }
} // namespace

int main()
{
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve == nullptr)
    {
        std::printf("test_aliens: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 0;
    }
    testCooked(valve);
    testRoach(valve);
    testRoachFood(valve);
    testSnark(valve);
    testAgrunt(valve);
    testLeech(valve);
    testController(valve);
    benchRoaches(valve);
    if (g_failures == 0)
        std::printf("test_aliens: ok\n");
    return g_failures == 0 ? 0 : 1;
}
