// A guard with something to fight: the game's own (dlls/barney.cpp, on the
// port's talkmonster, HlAiPeople.hpp): he draws before he fires, fires only
// when he faces it with nothing in the way, keeps the enemy he has, goes
// after it when it is hidden, and keeps his pistol out after; what the map
// keeps caged he leaves alone; and the player's shots turn him by the game's
// rule. On the real c1a1, with its guard and its zombie stood where the test
// wants them.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <string>
#include <vector>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    struct Scene
    {
        std::unique_ptr<Game> game;
        int guard = -1, monster = -1, other = -1;
        fx here[3] = {}, there[3] = {}, hidden[3] = {}, away[3] = {};
        bool ok = false;
    };

    void put(ActorState &actor, const fx *at)
    {
        for (int k = 0; k < 3; ++k)
            actor.pos[k] = at[k];
        actor.script = -1;
        actor.scriptPhase = kActorFree;
        actor.frozen = false;
        actor.flags &= static_cast<std::uint8_t>(~(kActorPrisoner | kActorDormant));
        actor.groundMover = -1;
        actor.airborne = false;
    }

    /// The map, its guard at one node and a monster at another he can see
    /// from there a few hundred units off; a third node he cannot see.
    Scene scene(const char *dir)
    {
        Scene s;
        s.game = std::make_unique<Game>();
        std::string error;
        if (!s.game->bsp.load(std::string(dir) + "/maps/c1a1.bsp", error))
        {
            check(false, (std::string(dir) + "/maps/c1a1.bsp loads").c_str());
            return s;
        }
        finish(*s.game, nullptr, dir);
        if (!s.game->ok)
            return s;
        Game &g = *s.game;
        ActorWorld &a = g.actors;
        for (int i = 0; i < a.count; ++i)
        {
            const ActorState &actor = a.states[i];
            if (!actor.active)
                continue;
            if (actor.kind == static_cast<std::uint8_t>(ActorKind::Guard) && s.guard < 0)
                s.guard = i;
            else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Zombie) && s.monster < 0)
                s.monster = i;
            else if (isMonster(actor.kind) && actor.kind != static_cast<std::uint8_t>(ActorKind::Barnacle) && s.other < 0 &&
                     i != s.monster)
                s.other = i;
        }
        if (s.guard < 0 || s.monster < 0)
            return s;
        // Two nodes in sight of each other, 300 to 500 units apart on one
        // floor; and one within 600 of the second that the first cannot see.
        auto eye = [&](int n, fx *out)
        {
            for (int k = 0; k < 3; ++k)
                out[k] = a.nodes[n].pos[k] + (k == 2 ? fxFromInt(50) : 0);
        };
        for (int x = 0; x < a.nodeCount && !s.ok; ++x)
        {
            for (int y = 0; y < a.nodeCount && !s.ok; ++y)
            {
                const fx d[3] = {a.nodes[y].pos[0] - a.nodes[x].pos[0], a.nodes[y].pos[1] - a.nodes[x].pos[1], 0};
                const fx gap = fxLength(d);
                const fx dz = a.nodes[y].pos[2] - a.nodes[x].pos[2];
                fx ex[3], ey[3];
                eye(x, ex);
                eye(y, ey);
                if (gap < fxFromInt(300) || gap > fxFromInt(500) || dz > fxFromInt(8) || dz < -fxFromInt(8) ||
                    !lineClear(g.move, ex, ey))
                    continue;
                for (int z = 0; z < a.nodeCount && !s.ok; ++z)
                {
                    const fx e[3] = {a.nodes[z].pos[0] - a.nodes[y].pos[0], a.nodes[z].pos[1] - a.nodes[y].pos[1], 0};
                    fx ez[3];
                    eye(z, ez);
                    if (fxLength(e) > fxFromInt(600) || lineClear(g.move, ex, ez) ||
                        pathStep(a, x, z, kPathLinkHuman, true) == kNoPathNode)
                        continue;
                    for (int k = 0; k < 3; ++k)
                    {
                        s.here[k] = a.nodes[x].pos[k];
                        s.there[k] = a.nodes[y].pos[k];
                        s.hidden[k] = a.nodes[z].pos[k];
                    }
                    s.ok = true;
                }
            }
        }
        // (The player somewhere out of the line of fire: at the third node --
        // in the same part of the map, or nobody would look about at all.)
        for (int k = 0; k < 3; ++k)
            s.away[k] = s.hidden[k] + (k == 2 ? kStandingFeet : 0);
        return s;
    }

    /// The guard at his node, the monster at its own and standing there:
    /// everything else that fights put out of it.
    void stage(Scene &s, int turnedBy)
    {
        Game &g = *s.game;
        ActorWorld &a = g.actors;
        for (int i = 0; i < a.count; ++i)
        {
            if (i != s.guard && i != s.monster && (isMonster(a.states[i].kind) || isGrunt(a.states[i].kind)))
                a.states[i].active = false;
        }
        ActorState &guard = a.states[s.guard];
        put(guard, s.here);
        guard.ai = kPersonIdle;
        guard.gun = 0;
        guard.body = 0;
        guard.health = 0;
        // (Unnamed, as most are: one with a name waits five seconds to be
        // set off first -- SCHED_WAIT_TRIGGER.)
        guard.name = 0;
        aiForget(a, s.guard);
        guard.yaw = static_cast<std::uint16_t>(fxAtan2Turns(s.there[1] - s.here[1], s.there[0] - s.here[0]) + turnedBy);
        ActorState &monster = a.states[s.monster];
        monster.active = true;
        put(monster, s.there);
        monster.ai = kAiIdle;
        // (Enough to last the scene: the guard's rounds are a zombie's whole
        // damage, as in the game; and it thinks afresh.)
        monster.health = 1000;
        aiForget(a, s.monster);
        g.player.health = 100;
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = s.away[k];
        g.heard.count = 0;
    }

    /// A tick, with the player held where the test has them and the monster
    /// where it stands (this is about the guard).
    void tick(Scene &s, const fx *player, const fx *monster)
    {
        Game &g = *s.game;
        for (int k = 0; k < 3; ++k)
        {
            g.player.origin[k] = player[k];
            g.player.velocity[k] = 0;
        }
        g.player.health = 100;
        if (monster != nullptr && g.actors.states[s.monster].active && g.actors.states[s.monster].ai < kAiDying)
        {
            for (int k = 0; k < 3; ++k)
                g.actors.states[s.monster].pos[k] = monster[k];
        }
        logicTick(g.logic, g.player, 33);
        monstersTick(g.logic, g.player, 33);
        g.drain();        static int clock = 0;
        clock += 33;
        const AiMonster *m = aiFind(g.actors, s.guard);
        if (std::getenv("HLDS_AI_TRACE") != nullptr && m != nullptr && clock % 330 < 33)
            std::printf("  t=%d %s task %d st %d enemy %d cond %08x act %d gun %d clip %d\n", clock,
                        m->schedule != nullptr ? m->schedule->name : "-", m->scheduleIndex, m->state, m->enemy,
                        m->conditions, m->activity, g.actors.states[s.guard].gun, g.actors.states[s.guard].clip);
    }

    bool faces(const ActorState &me, const fx *at, int tenths)
    {
        fx sine = 0, cosine = 0;
        fxSinCos(me.yaw, sine, cosine);
        const fx d[3] = {at[0] - me.pos[0], at[1] - me.pos[1], 0};
        const fx far = fxLength(d);
        const std::int64_t ahead = static_cast<std::int64_t>(d[0]) * cosine + static_cast<std::int64_t>(d[1]) * sine;
        return far > 0 && ahead * 10 >= static_cast<std::int64_t>(far) * kFxOne * tenths;
    }

    int shots(Game &g) { return g.times(CommonSound::GuardShot); }
    std::int16_t enemyOf(Game &g, int index)
    {
        const AiMonster *m = aiFind(g.actors, index);
        return m != nullptr ? m->enemy : kAiNone;
    }

    void testFight(const char *dir)
    {
        Scene s = scene(dir);
        check(s.ok, "c1a1 has a guard, a zombie and somewhere to stand them");
        if (!s.ok)
            return;
        Game &g = *s.game;
        ActorState &guard = g.actors.states[s.guard];
        ActorState &monster = g.actors.states[s.monster];

        // Stood side on to it: he turns, draws, and only then fires -- at
        // what he faces.
        stage(s, 16384);
        int shots0 = shots(g), firstShot = -1;
        bool drew = false, facedAtShot = false, gunAtShot = false;
        for (int t = 0; t < 5000 && firstShot < 0; t += 33)
        {
            tick(s, s.away, s.there);
            if (shots(g) > shots0)
            {
                firstShot = t;
                facedAtShot = faces(guard, s.there, 5);
                gunAtShot = guard.gun == 2 && guard.body == barney::kBodyDrawn;
            }
            drew = drew || guard.gun == 2;
        }
        std::printf("test_people: the guard's first round is %d ms after he sees it\n", firstShot);
        check(firstShot >= 0 && gunAtShot, "a guard draws before he fires");
        check(facedAtShot, "... and fires facing it, his pistol in his hand");
        check(enemyOf(g, s.guard) == s.monster && guard.ai == kPersonIdle, "it is his enemy; he follows nobody for it");

        // A round to each firing clip -- its flash at his pistol's muzzle,
        // in his hand as it is drawn, his arm aimed at what he shoots.
        shots0 = shots(g);
        const int health0 = monster.health;
        double worstFlash = -1;
        int blendAtShot = -1;
        for (int t = 0; t < 3000; t += 33)
        {
            const int before = shots(g);
            tick(s, s.away, s.there);
            if (shots(g) == before)
                continue;
            fx muzzle[3];
            if (!actorAttachment(g.actors, guard, 0, muzzle))
                continue;
            blendAtShot = guard.blend.value;
            for (const Effect &e : g.actors.effects)
            {
                if (e.kind != kEffectMuzzle || e.leftMs < e.wholeMs - 40)
                    continue;
                const fx d[3] = {e.a[0] - muzzle[0], e.a[1] - muzzle[1], e.a[2] - muzzle[2]};
                worstFlash = std::max(worstFlash, fxLength(d) / 4096.0);
            }
        }
        std::printf("test_people: his flash is %.1f units from his muzzle at worst; his aim's blend %d\n", worstFlash,
                    blendAtShot);
        check(worstFlash >= 0 && worstFlash < 6, "his flash is at his pistol's muzzle");
        check(blendAtShot > 60 && blendAtShot < 200, "... his arm aimed about level at what is about level");
        const int fired = shots(g) - shots0;
        std::printf("test_people: %d rounds in three seconds; the zombie has taken %d\n", fired, health0 - monster.health);
        check(fired >= 2 && fired <= 12, "he fires a round to each firing clip");
        check(monster.health < health0, "... and they are rounds: what they hit is hurt");

        // The player in the way: once he has looked again (a second and a
        // half), he holds his fire. Out of it: he fires.
        // (Between him and it wherever he is: he may step towards it -- he
        // would, in the game, to see it past them.)
        auto between = [&]()
        {
            static fx at[3];
            at[0] = (guard.pos[0] + s.there[0]) / 2;
            at[1] = (guard.pos[1] + s.there[1]) / 2;
            at[2] = guard.pos[2] + fxFromInt(36);
            return at;
        };
        for (int t = 0; t < 2000; t += 33)
            tick(s, between(), s.there);
        shots0 = shots(g);
        for (int t = 0; t < 2000; t += 33)
            tick(s, between(), s.there);
        check(shots(g) == shots0 && enemyOf(g, s.guard) == s.monster,
              "with the player in the line of fire he does not fire");
        for (int t = 0; t < 2500; t += 33)
            tick(s, s.away, s.there);
        check(shots(g) > shots0, "... and does again when they are out of it");

        // It goes where he cannot see it: he keeps it, and goes after it.
        const fx far0[3] = {s.there[0] - guard.pos[0], s.there[1] - guard.pos[1], 0};
        const int gap0 = fxToInt(fxLength(far0));
        for (int t = 0; t < 4000; t += 33)
            tick(s, s.here, s.hidden);
        const fx far1[3] = {s.there[0] - guard.pos[0], s.there[1] - guard.pos[1], 0};
        std::printf("test_people: hidden from him, he is %d then %d units from where he last saw it\n", gap0,
                    fxToInt(fxLength(far1)));
        // (To where he last saw it: the game's chase is to its last known
        // place.)
        check(enemyOf(g, s.guard) == s.monster && fxToInt(fxLength(far1)) < 128,
              "hidden, it is still his enemy, and he goes after it, to where he saw it last");

        // Dead: he has no enemy, and his pistol stays out.
        stage(s, 0);
        for (int t = 0; t < 2500; t += 33)
            tick(s, s.away, s.there);
        monsterHurt(g.logic, s.monster, 1000, true);
        for (int t = 0; t < 3000; t += 33)
            tick(s, s.away, nullptr);
        check(enemyOf(g, s.guard) == kAiNone, "his enemy dead, he has none");
        check(guard.gun == 2, "... his pistol still out, as the game's guard keeps his");

        // What the map keeps caged he leaves alone.
        stage(s, 0);
        monster.flags |= kActorPrisoner;
        for (int t = 0; t < 2000; t += 33)
            tick(s, s.away, s.there);
        check(enemyOf(g, s.guard) == kAiNone && guard.gun == 0, "one the map keeps caged is not his to shoot");

        // Behind him: he does not see it. (The game's wide view leaves a
        // quarter turn behind him.)
        stage(s, 32768);
        tick(s, s.away, s.there);
        tick(s, s.away, s.there);
        check(enemyOf(g, s.guard) == kAiNone, "what is straight behind him he does not see at once");

        // The player's rounds, by the game's rule. In a fight: a stray one.
        stage(s, 0);
        for (int t = 0; t < 1500; t += 33)
            tick(s, s.away, s.there);
        monsterHurt(g.logic, s.guard, 1, true);
        check(!(aiFind(g.actors, s.guard)->memory & kMemoryProvoked),
              "shot by the player in the middle of a fight, he takes it for a stray round");
        // With nothing to fight, and the player facing him: provoked at
        // once -- and he follows nobody.
        stage(s, 0);
        g.actors.states[s.monster].active = false;
        tick(s, s.away, nullptr);
        aiOf(g.actors, s.guard)->targetEnt = kAiPlayer;
        {
            const fx d[3] = {guard.pos[0] - g.player.origin[0], guard.pos[1] - g.player.origin[1], 0};
            const fx length = fxLength(d);
            g.player.facingXY[0] = static_cast<fx>((static_cast<std::int64_t>(d[0]) << kFxShift) / length);
            g.player.facingXY[1] = static_cast<fx>((static_cast<std::int64_t>(d[1]) << kFxShift) / length);
        }
        monsterHurt(g.logic, s.guard, 1, true);
        check((aiFind(g.actors, s.guard)->memory & kMemoryProvoked) && aiFind(g.actors, s.guard)->targetEnt == kAiNone,
              "aimed at with no enemy about, he turns on the player and follows no longer");
        // Looking away: wary the first time; the next, provoked.
        stage(s, 0);
        g.actors.states[s.monster].active = false;
        tick(s, s.away, nullptr);
        g.player.facingXY[0] = -g.player.facingXY[0];
        g.player.facingXY[1] = -g.player.facingXY[1];
        monsterHurt(g.logic, s.guard, 1, true);
        const std::uint32_t wary = aiFind(g.actors, s.guard)->memory;
        monsterHurt(g.logic, s.guard, 1, true);
        check((wary & kMemorySuspicious) && !(wary & kMemoryProvoked) &&
                  (aiFind(g.actors, s.guard)->memory & kMemoryProvoked),
              "a round that was not aimed at him makes him wary; a second, an enemy");

        // He cries out no more than about twice a second, however often he
        // is hit.
        stage(s, 0);
        g.actors.states[s.monster].active = false;
        tick(s, s.away, nullptr);
        auto cries = [&]
        {
            return g.times(CommonSound::GuardPain) + g.times(CommonSound::GuardPain2) +
                   g.times(CommonSound::GuardPain3);
        };
        const int cried = cries();
        for (int n = 0; n < 6; ++n)
            monsterHurt(g.logic, s.guard, 1, true);
        g.drain();
        check(cries() == cried + 1, "six hits at once are one cry");
    }

    // HLDS_SWEEP=<maps, comma separated>: each map run for four seconds
    // with nobody near, and what became of its people said -- who fell, who
    // died of a place that hurts. For seeing that the rule that people fall
    // and are hurt has not cost a map someone it needs.
    void sweep(const char *dir, const char *maps)
    {
        std::string list(maps);
        for (std::size_t at = 0; at < list.size();)
        {
            const std::size_t comma = std::min(list.find(',', at), list.size());
            const std::string map = list.substr(at, comma - at);
            at = comma + 1;
            auto game = std::make_unique<Game>();
            std::string error;
            if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
                continue;
            finish(*game, nullptr, dir);
            if (!game->ok)
                continue;
            Game &g = *game;
            std::vector<fx> startZ;
            std::vector<bool> alive;
            int people = 0;
            for (int i = 0; i < g.actors.count; ++i)
            {
                const ActorState &actor = g.actors.states[i];
                startZ.push_back(actor.pos[2]);
                alive.push_back(actor.active && actor.ai < kAiDying);
                people += actor.active && isPerson(actor.kind) ? 1 : 0;
            }
            g.player.origin[2] += fxFromInt(6000);
            for (int n = 0; n < 120; ++n)
            {
                logicTick(g.logic, g.player, 33);
                monstersTick(g.logic, g.player, 33);
                g.heard.count = 0;
            }
            int fell = 0, died = 0, onBrush = 0;
            for (int i = 0; i < g.actors.count; ++i)
            {
                const ActorState &actor = g.actors.states[i];
                if (!isPerson(actor.kind))
                    continue;
                fell += startZ[i] - actor.pos[2] > fxFromInt(8) ? 1 : 0;
                died += alive[i] && !(actor.active && actor.ai < kAiDying) ? 1 : 0;
                onBrush += actor.groundMover >= 0 ? 1 : 0;
            }
            std::printf("sweep %-8s people %2d on a brush %2d fell %2d died %2d\n", map.c_str(), people, onBrush, fell, died);
        }
    }
} // namespace

namespace
{
    /// A monster that goes for one of the people: it comes to them and
    /// hits them (the game counts a step into what it goes for as a step
    /// made), rather than stopping at the node nearest them; and a second
    /// that goes for them too is beside it, neither in nor on the other.
    void testComesToPeople(const char *dir)
    {
        Scene s = scene(dir);
        check(s.ok, "c1a1 has a guard, a zombie and somewhere to stage them");
        if (!s.ok)
            return;
        Game &g = *s.game;
        stage(s, 0);
        ActorWorld &a = g.actors;
        ActorState &guard = a.states[s.guard];
        ActorState &zombie = a.states[s.monster];
        // (He does not fight back: this is about the zombie. And he stands
        // between the nodes, not on one: walking to the node nearest him
        // would be no test.)
        guard.gun = 0;
        fx spot[3];
        for (int k = 0; k < 3; ++k)
            spot[k] = s.here[k] + (s.there[k] - s.here[k]) * 7 / 10;
        spot[2] = s.here[2];
        put(guard, spot);
        Cheats cheats;
        cheats.notarget = true;
        g.player.cheats = &cheats;
        int closest = 1 << 30, hitAt = -1;
        int guardHealth = 0;
        for (int t = 0; t < 20000 && hitAt < 0; t += 33)
        {
            if (t == 66)
                guardHealth = guard.health; // (once his first tick has given it him)
            for (int k = 0; k < 3; ++k)
                g.player.origin[k] = s.away[k];
            // (He stays where he is.)
            put(guard, spot);
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            g.drain();
            const fx d[3] = {zombie.pos[0] - guard.pos[0], zombie.pos[1] - guard.pos[1], 0};
            closest = std::min(closest, fxToInt(fxLength(d)));
            if (t > 66 && guard.health != guardHealth)
                hitAt = t;
        }
        std::printf("test_people: a zombie going for the guard: came within %d units, hit him at %d ms\n", closest, hitAt);
        check(closest <= 72 && hitAt >= 0, "a monster going for one of the people comes to them and hits them");

        // A second at him too, from beside the first: they end side by
        // side, not one in or on the other.
        std::printf("test_people: the second for the guard: actor %d\n", s.other);
        if (s.other < 0)
            return;
        ActorState &second = a.states[s.other];
        second.active = true;
        fx beside[3] = {s.there[0], s.there[1], s.there[2]};
        beside[0] += fxFromInt(48);
        put(second, beside);
        second.ai = kAiIdle;
        second.health = 1000;
        aiForget(a, s.other);
        put(zombie, s.there);
        aiForget(a, s.monster);
        actorPlaceBox(a, s.other);
        actorPlaceBox(a, s.monster);
        int worstOverlap = 0, worstStack = 0;
        for (int t = 0; t < 12000; t += 33)
        {
            for (int k = 0; k < 3; ++k)
                g.player.origin[k] = s.away[k];
            put(guard, spot);
            logicTick(g.logic, g.player, 33);
            monstersTick(g.logic, g.player, 33);
            g.drain();
            if (!second.active || !zombie.active)
                break;
            const SolidBox &p = a.boxes[s.monster];
            const SolidBox &q = a.boxes[s.other];
            if (!p.active || !q.active)
                continue;
            int overlap = 1 << 30;
            for (int k = 0; k < 2; ++k)
                overlap = std::min(overlap, fxToInt(std::min(p.maxs[k], q.maxs[k]) - std::max(p.mins[k], q.mins[k])));
            worstOverlap = std::max(worstOverlap, overlap);
            const int stack = std::abs(fxToInt(zombie.pos[2] - second.pos[2]));
            if (overlap > 0)
                worstStack = std::max(worstStack, stack);
        }
        g.player.cheats = nullptr;
        std::printf("test_people: two going for him: overlapped by %d units at most, one %d above the other at most\n",
                    worstOverlap, worstStack);
        check(worstOverlap <= 12 && worstStack <= 8, "two going for the same one are side by side, not in or on each other");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0' && std::getenv("HLDS_SWEEP") != nullptr)
    {
        sweep(dir, std::getenv("HLDS_SWEEP"));
        return 0;
    }
    if (dir != nullptr && dir[0] != '\0')
    {
        testFight(dir);
        testComesToPeople(dir);
    }
    else
    {
        std::printf("test_people: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 77;
    }
    if (g_failures == 0)
        std::printf("test_people: ok\n");
    return g_failures == 0 ? 0 : 1;
}
