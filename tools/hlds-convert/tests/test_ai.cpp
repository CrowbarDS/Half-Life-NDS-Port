// The game's monster mind (shared/HlAi.hpp, HlAiCore.hpp, HlAiMonsters.hpp),
// held up against its own code (dlls/monsters.cpp and the rest).
//
// What needs no map first: who is whose enemy, by the game's table; the
// sound list. Then, with HLDS_VALVE_DIR set, the game's maps: a monster
// hears a shot behind it and turns; a headcrab leaps and leaps again almost
// at once (the code as it shipped); bullets are a zombie's whole damage and
// never tear anything apart, a crowbar on a body does; a bullsquid whips
// only what it can finish, hates headcrabs; a houndeye pack's blast is the
// stronger for each of it; a barnacle hauls up and eats a scientist, and
// kills the player at its mouth.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    void testTable()
    {
        auto rel = [](ActorKind self, ActorKind target)
        {
            return classRelationship(kindClass(static_cast<std::uint8_t>(self)),
                                     kindClass(static_cast<std::uint8_t>(target)));
        };
        check(rel(ActorKind::Headcrab, ActorKind::Player) == kRelDislike &&
                  rel(ActorKind::Zombie, ActorKind::Player) == kRelDislike &&
                  rel(ActorKind::Slave, ActorKind::Player) == kRelHate,
              "the aliens dislike the player; the slaves hate them");
        check(rel(ActorKind::Bullsquid, ActorKind::Headcrab) == kRelHate &&
                  rel(ActorKind::Headcrab, ActorKind::Bullsquid) == kRelFear,
              "a bullsquid hates a headcrab, which fears it");
        check(rel(ActorKind::Grunt, ActorKind::Slave) == kRelHate && rel(ActorKind::Slave, ActorKind::Grunt) == kRelHate &&
                  rel(ActorKind::Grunt, ActorKind::Guard) == kRelHate,
              "soldiers and slaves hate each other; the soldiers hate the guards");
        check(rel(ActorKind::Zombie, ActorKind::Zombie) == kRelNone && rel(ActorKind::Houndeye, ActorKind::Zombie) == kRelNone &&
                  rel(ActorKind::Slave, ActorKind::Houndeye) == kRelNone,
              "the aliens leave each other alone");
        check(rel(ActorKind::Zombie, ActorKind::Scientist) == kRelDislike &&
                  rel(ActorKind::Scientist, ActorKind::Zombie) == kRelHate &&
                  rel(ActorKind::Scientist, ActorKind::Player) == kRelAlly,
              "the people are the aliens' too; a scientist is the player's");
        check(rel(ActorKind::Turret, ActorKind::Player) == kRelDislike && rel(ActorKind::Gman, ActorKind::Player) == kRelNone,
              "a turret goes for the player; the man in the suit for nobody");
    }

    void testSounds()
    {
        SoundEnt s;
        soundEntInit(s);
        const fx at[3] = {0, 0, 0};
        soundInsert(s, 0, kSoundCombat, at, 600, 300);
        int active = 0;
        for (std::int8_t i = s.active; i != kSoundListEmpty; i = s.pool[i].next)
            ++active;
        check(active == 2 && s.pool[s.active].type == kSoundCombat && s.pool[s.active].volume == 600,
              "a sound goes in at the head of the list, after the player's own");
        soundEntTick(s, 0, 0);       // (swept at once: nothing due)
        soundEntTick(s, 300, 300);   // (due, and swept)
        active = 0;
        for (std::int8_t i = s.active; i != kSoundListEmpty; i = s.pool[i].next)
            ++active;
        check(active == 1, "... and is gone when its time is up and the list is next swept");
        // The player: their steps by their speed, their gun when louder.
        playerSoundTick(s, at, 300, false, false, 33);
        check(playerSound(s).volume == 300 && playerSound(s).type == kSoundPlayer, "running at 300, heard 300 off");
        playerWeaponVolume(s, kLoudGunVolume);
        playerSoundTick(s, at, 300, false, false, 33);
        check(playerSound(s).volume >= 990 && (playerSound(s).type & kSoundCombat), "a shotgun is heard 1000 off, as a shot");
        for (int n = 0; n < 60; ++n)
            playerSoundTick(s, at, 0, false, false, 33);
        check(playerSound(s).volume > 400 && playerSound(s).volume < 600, "... and fades at 250 a second");
        for (int n = 0; n < 90; ++n)
            playerSoundTick(s, at, 0, false, false, 33);
        check(playerSound(s).volume == 0, "... to nothing");
    }

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
            const ActorState &actor = g.actors.states[i];
            if (actor.active && actor.kind == static_cast<std::uint8_t>(kind) && actor.script < 0 && nth-- == 0)
                return i;
        }
        return -1;
    }

    /// Everything that fights or can be hurt gone, but `keep` (-1 ends).
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

    void tick(Game &g, int ms = 33)
    {
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        monstersTick(g.logic, g.player, ms);
        playerMove(g.player, MoveCommand(), g.move, kPlayerTick);
        g.drain();
    }
    /// ... with the player held where they are.
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

    /// A place `distance` units from actor `index`, along `yaw`, that it
    /// could walk to: the player is put there. False if there is none
    /// about it.
    bool standAt(Game &g, int index, int distance, bool inFront = true)
    {
        ActorState &a = g.actors.states[index];
        const int hull = monsterHull(a.kind);
        for (int turn = 0; turn < 16; ++turn)
        {
            const std::uint16_t yaw = static_cast<std::uint16_t>(a.yaw + (inFront ? 0 : 32768) +
                                                                 (turn & 1 ? 1 : -1) * ((turn + 1) / 2) * 2048);
            fx sine, cosine;
            fxSinCos(yaw, sine, cosine);
            ActorState probe;
            probe.pos[0] = a.pos[0] + cosine * distance;
            probe.pos[1] = a.pos[1] + sine * distance;
            probe.pos[2] = a.pos[2] + fxFromInt(8);
            if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(64)) ||
                !walkClear(g.move, &g.actors, index, hull, a.pos, probe.pos, distance / 16 + 4))
                continue;
            g.player.origin[0] = probe.pos[0];
            g.player.origin[1] = probe.pos[1];
            g.player.origin[2] = probe.pos[2] + kStandingFeet;
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            // (Facing it.)
            a.yaw = fxAtan2Turns(probe.pos[1] - a.pos[1], probe.pos[0] - a.pos[0]);
            if (!inFront)
                a.yaw = static_cast<std::uint16_t>(a.yaw + 32768);
            return true;
        }
        return false;
    }

    /// With HLDS_AI_TRACE set: what monster `index` is doing, each call.
    void trace(Game &g, int index, int ms)
    {
        static const bool on = std::getenv("HLDS_AI_TRACE") != nullptr;
        if (!on)
            return;
        const AiMonster *m = aiFind(g.actors, index);
        const ActorState &a = g.actors.states[index];
        if (m == nullptr)
            return;
        std::printf("  t=%5d st=%d sched=%-24s task=%d/%d act=%d ai=%d hp=%d pos=%.0f,%.0f,%.0f yaw=%u enemy=%d cond=%08x clip=%d fr=%u\n",
                    ms, m->state, m->schedule != nullptr ? m->schedule->name : "-", m->scheduleIndex,
                    m->taskStatus, m->activity, a.ai, a.health, a.pos[0] / 4096.0, a.pos[1] / 4096.0,
                    a.pos[2] / 4096.0, a.yaw, m->enemy, m->conditions, a.clip, a.frame >> 8);
    }

    AiMonster &mind(Game &g, int index)
    {
        return *aiOf(g.actors, index);
    }

    const char *scheduleName(Game &g, int index)
    {
        const AiMonster *m = aiFind(g.actors, index);
        return m != nullptr && m->schedule != nullptr ? m->schedule->name : "";
    }

    void testHearing(const char *dir)
    {
        auto game = load(dir, "c1a4d");
        if (!game)
        {
            check(false, "c1a4d cooks");
            return;
        }
        Game &g = *game;
        // (An unnamed one: a named monster waits to be set off.)
        int z = -1;
        for (int nth = 0; nth < 8 && z < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Zombie, nth);
            if (candidate >= 0 && g.actors.states[candidate].name == 0 && standAt(g, candidate, 300, false))
                z = candidate;
        }
        check(z >= 0, "c1a4d has a zombie with room behind it");
        if (z < 0)
            return;
        alone(g, {z});
        ActorState &zombie = g.actors.states[z];
        // The player behind it, where it cannot see them; then a shot.
        g.player.health = 100;
        for (int n = 0; n < 30; ++n)
        {
            tickStill(g);
            trace(g, z, n * 33);
        }
        check(mind(g, z).state == kStateIdle, "with nobody about a zombie stands idle");
        fx sine, cosine;
        fxSinCos(static_cast<std::uint16_t>(zombie.yaw + 32768), sine, cosine);
        const fx shot[3] = {zombie.pos[0] + cosine * 200, zombie.pos[1] + sine * 200, zombie.pos[2] + fxFromInt(40)};
        aiSound(g.actors, kSoundCombat, shot, kNormalGunVolume, 300);
        const std::uint16_t before = zombie.yaw;
        bool alerted = false;
        for (int n = 0; n < 60; ++n)
        {
            tickStill(g);
            alerted = alerted || mind(g, z).state == kStateAlert;
            trace(g, z, 1000 + n * 33);
        }
        const int turned = static_cast<std::int16_t>(zombie.yaw - before);
        check(alerted, "a shot heard behind it: it is alert");
        check(turned > 16384 || turned < -16384, "... and has turned to where it was");
    }

    void testHeadcrab(const char *dir)
    {
        auto game = load(dir, "c1a1f");
        if (!game)
            return;
        Game &g = *game;
        int crab = -1;
        for (int nth = 0; nth < 8 && crab < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Headcrab, nth);
            if (candidate >= 0 && standAt(g, candidate, 180))
                crab = candidate;
        }
        check(crab >= 0, "a headcrab with room before it");
        if (crab < 0)
            return;
        alone(g, {crab});
        ActorState &actor = g.actors.states[crab];
        g.player.health = 100;
        int leaps = 0, landedAt = -1, againAt = -1, bites = 0, lastHealth = 100;
        bool wasLeaping = false;
        for (int ms = 0; ms < 8000 && againAt < 0; ms += 33)
        {
            tickStill(g);
            trace(g, crab, ms);
            const bool leaping = actor.ai == kAiLeap;
            if (leaping && !wasLeaping)
            {
                ++leaps;
                if (leaps == 2)
                    againAt = ms;
            }
            if (!leaping && wasLeaping && landedAt < 0)
                landedAt = ms;
            wasLeaping = leaping;
            if (g.player.health < lastHealth)
            {
                ++bites;
                check(lastHealth - g.player.health == 10, "a headcrab's bite is 10");
                lastHealth = g.player.health;
            }
            g.player.health = lastHealth;
        }
        check(leaps >= 2, "a headcrab leaps at the player, and again");
        check(againAt >= 0 && landedAt >= 0 && againAt - landedAt < 2000,
              "... and again well within two seconds of landing (the game's m_flNextAttack is never read)");
        check(bites >= 1, "... and bites what it lands on");
        // Acid does nothing to it.
        AiDamage acid;
        acid.amount = 50;
        acid.type = kDmgAcid;
        const int health = actor.health;
        aiActorHurt(g.logic, &g.player, crab, acid);
        check(actor.health == health, "acid does a headcrab no harm");
    }

    void testZombie(const char *dir)
    {
        auto game = load(dir, "c1a4d");
        if (!game)
            return;
        Game &g = *game;
        int z = -1;
        for (int nth = 0; nth < 8 && z < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Zombie, nth);
            if (candidate >= 0 && g.actors.states[candidate].name == 0)
                z = candidate;
        }
        if (z < 0)
            return;
        alone(g, {z});
        ActorState &zombie = g.actors.states[z];
        g.player.origin[2] += fxFromInt(5000);
        for (int n = 0; n < 10; ++n)
            tickStill(g);
        check(zombie.health == 50, "a zombie has 50");
        monsterHurt(g.logic, z, 8, true);
        check(zombie.health == 42, "a round of the pistol is all of its 8 to a zombie (the 30% never applies)");
        monsterHurt(g.logic, z, 8, true, kHitHead);
        check(zombie.health == 18, "... three times that to the head");
        // Shot past dead: a body, never pieces.
        monsterHurt(g.logic, z, 200, true);
        check(zombie.active && zombie.ai >= kAiDying, "a bullet never tears a zombie apart");
        // A body blown on is torn apart, by enough.
        const int drop3 = g.timesHeard[static_cast<int>(CommonSound::BodyDrop3)],
                  drop4 = g.timesHeard[static_cast<int>(CommonSound::BodyDrop4)];
        for (int n = 0; n < 100; ++n)
        {
            tickStill(g);
            trace(g, z, n * 33);
        }
        check(zombie.ai == kAiDead && zombie.health == 25, "dead, what is left of it is half its health");
        check(g.timesHeard[static_cast<int>(CommonSound::BodyDrop3)] + g.timesHeard[static_cast<int>(CommonSound::BodyDrop4)] >
                  drop3 + drop4,
              "... and it was heard hitting the floor");
        monsterHurt(g.logic, z, 20, false, kHitBody, kDmgBlast);
        check(zombie.active, "... a blast of less leaves it");
        monsterHurt(g.logic, z, 30, false, kHitBody, kDmgBlast);
        check(!zombie.active, "... a blast of more than that takes it apart");
    }

    void testFlinch(const char *dir)
    {
        auto game = load(dir, "c1a4d");
        if (!game)
            return;
        Game &g = *game;
        int z = -1;
        for (int nth = 0; nth < 8 && z < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Zombie, nth);
            if (candidate >= 0 && g.actors.states[candidate].name == 0 && standAt(g, candidate, 300))
                z = candidate;
        }
        if (z < 0)
        {
            check(false, "c1a4d has a zombie with room before it");
            return;
        }
        alone(g, {z});
        g.player.health = 100;
        for (int n = 0; n < 40; ++n)
        {
            tickStill(g);
            trace(g, z, n * 33);
        }
        check(mind(g, z).state == kStateCombat, "a zombie that sees the player fights");
        int flinches = 0;
        bool wasFlinching = false;
        for (int round = 0; round < 4; ++round)
        {
            monsterHurt(g.logic, z, 2, true);
            for (int n = 0; n < 45; ++n)
            {
                tickStill(g);
                g.player.health = 100;
                const bool flinching = std::strcmp(scheduleName(g, z), "Small Flinch") == 0;
                flinches += flinching && !wasFlinching ? 1 : 0;
                wasFlinching = flinching;
            }
        }
        check(flinches == 1, "it flinches at the first blow of its life, and at no other");
    }

    void testBullsquid(const char *dir)
    {
        auto game = load(dir, "c1a4k");
        if (!game)
        {
            check(false, "c1a4k cooks");
            return;
        }
        Game &g = *game;
        int squid = -1;
        for (int nth = 0; nth < 4 && squid < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Bullsquid, nth);
            if (candidate >= 0 && standAt(g, candidate, 72))
                squid = candidate;
        }
        check(squid >= 0, "a bullsquid with room before it");
        if (squid < 0)
            return;
        alone(g, {squid});
        auto melee = [&](int health)
        {
            int whips = 0, bites = 0;
            for (int n = 0; n < 150; ++n)
            {
                g.player.health = health;
                tickStill(g);
                const char *name = scheduleName(g, squid);
                whips += std::strcmp(name, "Primary Melee Attack") == 0 ? 1 : 0;
                bites += std::strcmp(name, "Secondary Melee Attack") == 0 ? 1 : 0;
            }
            return std::make_pair(whips, bites);
        };
        const auto healthy = melee(100);
        check(healthy.second > 0 && healthy.first == 0, "up close to someone in health a bullsquid bites");
        const auto weak = melee(30);
        check(weak.first > 0, "... and to someone with 35 or less, it whips");
    }

    void testSquidAndCrab(const char *dir)
    {
        auto game = load(dir, "c1a4");
        if (!game)
            return;
        Game &g = *game;
        const int crab = findKind(g, ActorKind::Headcrab);
        int squid = -1;
        for (int nth = 0; nth < 4 && squid < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Bullsquid, nth);
            // (The player behind it -- in what it can see of the map, not in
            // its eyes -- and room in front for the crab.)
            if (candidate >= 0 && standAt(g, candidate, 200, false))
            {
                const ActorState &s = g.actors.states[candidate];
                fx forward[3];
                aiForward(s.yaw, forward);
                ActorState probe;
                for (int k = 0; k < 3; ++k)
                    probe.pos[k] = s.pos[k] + forward[k] * 180;
                probe.pos[2] = s.pos[2] + fxFromInt(8);
                if (actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(64)) &&
                    walkClear(g.move, &g.actors, candidate, kWalkCrouched, s.pos, probe.pos, 16) && crab >= 0)
                {
                    squid = candidate;
                    ActorState &c = g.actors.states[crab];
                    for (int k = 0; k < 3; ++k)
                        c.pos[k] = probe.pos[k];
                    c.yaw = static_cast<std::uint16_t>(s.yaw + 32768);
                    actorPlaceBox(g.actors, crab);
                }
            }
        }
        if (squid < 0)
        {
            check(false, "c1a4 has a bullsquid with room about it, and a headcrab");
            return;
        }
        alone(g, {squid, crab});
        // (The player is not to be seen by either: nobody notices them.)
        static Cheats quiet;
        quiet.notarget = true;
        g.player.cheats = &quiet;
        // (For its first seconds it pays a headcrab no mind -- the game's
        // last time it was hurt starts at nothing.)
        bool danced = false;
        for (int n = 0; n < 300 && !danced; ++n)
        {
            tickStill(g);
            trace(g, squid, n * 33);
            danced = std::strcmp(scheduleName(g, squid), "SquidSeeCrab") == 0;
        }
        g.player.cheats = nullptr;
        check(mind(g, squid).enemy == crab, "a bullsquid that sees a headcrab goes for it");
        check(danced, "... with its dance at the sight of one (once in its life)");
    }

    void testHoundeyes(const char *dir)
    {
        auto game = load(dir, "c1a4b");
        if (!game)
        {
            check(false, "c1a4b cooks");
            return;
        }
        Game &g = *game;
        g.player.origin[2] += fxFromInt(8000);
        for (int n = 0; n < 20; ++n)
            tickStill(g);
        int inSquads = 0, biggest = 0;
        int leader = -1;
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].kind != static_cast<std::uint8_t>(ActorKind::Houndeye) || !g.actors.states[i].active)
                continue;
            AiMonster &m = mind(g, i);
            if (m.squadLeader == kAiNone)
                continue;
            ++inSquads;
            const AiKind &kind = *aiKindOf(g.actors.states[i].kind);
            AiCtx c{g.logic, g.player, g.actors, g.ai, i, g.actors.states[i], m, kind};
            const int n = aiSquadCount(c);
            if (n > biggest)
            {
                biggest = n;
                leader = m.squadLeader;
            }
        }
        check(inSquads >= 2 && biggest >= 2 && biggest <= 4, "houndeyes go about in packs of up to four");
        if (leader < 0)
            return;
        // The pack's blast: the stronger for each of it, less the further off.
        AiMonster &m = mind(g, leader);
        ActorState &me = g.actors.states[leader];
        const AiKind &kind = *aiKindOf(me.kind);
        AiCtx c{g.logic, g.player, g.actors, g.ai, leader, me, m, kind};
        const int n = aiSquadCount(c);
        g.player.origin[0] = me.pos[0];
        g.player.origin[1] = me.pos[1];
        g.player.origin[2] = me.pos[2] + kStandingFeet;
        g.player.health = 100;
        g.player.inv.armor = 0;
        houndeye::sonicAttack(c);
        // (Less by how far the player's middle is from its feet: 36.)
        const int full = 15 + (15 * 110 * (n - 1)) / 100;
        check(100 - g.player.health == full - (36 * full) / 384,
              "a pack's blast: 15, and 1.1 times that again for each other one, less with distance");
        // Another houndeye in it is not hurt.
        int other = -1;
        for (int i = 0; i < kMaxSquadMembers && other < 0; ++i)
        {
            const std::int16_t id = aiSquadMember(c, i);
            if (id != kAiNone && id != leader)
                other = id;
        }
        if (other >= 0)
        {
            const int before = g.actors.states[other].health;
            g.actors.states[other].pos[0] = me.pos[0] + fxFromInt(40);
            g.actors.states[other].pos[1] = me.pos[1];
            houndeye::sonicAttack(c);
            check(g.actors.states[other].health == before, "... and does nothing to another houndeye");
        }
    }

    void testBarnacle(const char *dir)
    {
        auto game = load(dir, "c1a2a");
        if (!game)
        {
            check(false, "c1a2a cooks");
            return;
        }
        Game &g = *game;
        // A barnacle with floor under it within reach of its tongue.
        int b = -1;
        for (int nth = 0; nth < 12 && b < 0; ++nth)
        {
            const int candidate = findKind(g, ActorKind::Barnacle, nth);
            if (candidate < 0)
                break;
            const ActorState &it = g.actors.states[candidate];
            const fx from[3] = {it.pos[0], it.pos[1], it.pos[2] - fxFromInt(4)};
            const fx down[3] = {it.pos[0], it.pos[1], it.pos[2] - fxFromInt(400)};
            const Shot shot = traceShot(g.move, from, down);
            if (shot.fraction < kFractionOne && shot.fraction > kFractionOne / 8)
                b = candidate;
        }
        int sci = -1;
        for (int i = 0; i < g.actors.count && sci < 0; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Scientist) &&
                g.actors.states[i].name == 0)
                sci = i;
        }
        if (std::getenv("HLDS_AI_TRACE") != nullptr)
        {
            for (int i = 0; i < g.actors.count; ++i)
            {
                const ActorState &x = g.actors.states[i];
                if (x.kind == static_cast<std::uint8_t>(ActorKind::Barnacle) || x.kind == static_cast<std::uint8_t>(ActorKind::Scientist))
                    std::printf("  actor %d kind %d active %d name %d script %d pos %.0f %.0f %.0f\n", i, x.kind, x.active,
                                x.name, x.script, x.pos[0] / 4096.0, x.pos[1] / 4096.0, x.pos[2] / 4096.0);
            }
        }
        check(b >= 0 && sci >= 0, "c1a2a has a barnacle over a floor, and a scientist");
        if (b < 0 || sci < 0)
            return;
        alone(g, {b, sci});
        ActorState &barnacle = g.actors.states[b];
        g.player.origin[2] += fxFromInt(8000);
        // Its tongue down to the floor first.
        for (int n = 0; n < 150; ++n)
            tickStill(g);
        check(barnacle.reach > fxFromInt(40), "a barnacle lets its tongue down");
        // A scientist under it is hauled up, at 80 units a second.
        ActorState &s = g.actors.states[sci];
        const fx under[3] = {barnacle.pos[0], barnacle.pos[1], barnacle.pos[2] - fxFromInt(4)};
        const fx down[3] = {barnacle.pos[0], barnacle.pos[1], barnacle.pos[2] - fxFromInt(2048)};
        const Shot floor = traceShot(g.move, under, down);
        s.pos[0] = barnacle.pos[0];
        s.pos[1] = barnacle.pos[1];
        s.pos[2] = under[2] - static_cast<fx>((static_cast<std::int64_t>(fxFromInt(2044)) * floor.fraction) >> kFractionShift);
        actorPlaceBox(g.actors, sci);
        bool held = false;
        fx was = 0, rose = 0;
        int heldMs = 0;
        for (int n = 0; n < 900 && s.active; ++n)
        {
            tickStill(g);
            if (std::getenv("HLDS_AI_TRACE") != nullptr && n % 5 == 0)
            {
                const AiMonster &bm = mind(g, b);
                std::printf("  barnacle t=%d enemy=%d lifting=%d alt=%.0f adj=%.0f kill=%d gibs=%d sci z=%.0f held=%d clip=%d\n",
                            n * 33, bm.enemy, bm.flag2, bm.value1 / 4096.0, bm.value2 / 4096.0, bm.time1, bm.count1,
                            s.pos[2] / 4096.0, aiHeld(g.actors, sci), s.clip);
            }
            if (aiHeld(g.actors, sci))
            {
                if (!held)
                    was = s.pos[2];
                held = true;
                heldMs += 33;
                if (heldMs == 990)
                    rose = s.pos[2] - was;
            }
        }
        check(held, "a barnacle takes hold of a scientist under it");
        check(rose >= fxFromInt(60) || heldMs < 990, "... and hauls them up, 80 units a second");
        check(!s.active, "... and in the end eats them, in pieces");
        // The player is killed at its mouth.
        g.player.origin[0] = barnacle.pos[0];
        g.player.origin[1] = barnacle.pos[1];
        g.player.origin[2] = s.pos[2] + kStandingFeet;
        g.player.health = 100;
        g.player.inv.armor = 0;
        for (int n = 0; n < 900 && g.player.health > 0; ++n)
            tick(g);
        check(g.player.health <= 0, "the player, taken up, dies at its mouth");
    }

    int actorNamed(const Game &g, const char *name)
    {
        const std::uint16_t id = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash(name));
        for (int i = 0; id != 0 && i < g.actors.count; ++i)
        {
            if (g.actors.states[i].name == id)
                return i;
        }
        return -1;
    }

    int recordNamed(const Game &g, const char *name)
    {
        const std::uint32_t hash = nameHash(name);
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.name != 0 && g.view.nameHashes[r.name] == hash)
                return static_cast<int>(i);
        }
        return -1;
    }

    /// c1a1f's two zombies in scripts: the freak behind the computers, held
    /// by a script nothing interrupts, ends it (and dies) by its own
    /// half-health trigger; the one that slides down the wall, hurt before
    /// its scene, is let go and stays let go -- its script takes it once.
    void testC1a1fScripts(const char *dir)
    {
        auto game = load(dir, "c1a1f");
        if (!game)
            return;
        Game &g = *game;
        const int freak = actorNamed(g, "freakin_zombie");
        const int slide = actorNamed(g, "slide_zombie");
        const int suspense = recordNamed(g, "suspense_track_trigger");
        check(freak >= 0 && slide >= 0 && suspense >= 0, "c1a1f's freak and slide zombies, and the suspense trigger");
        if (freak < 0 || slide < 0 || suspense < 0)
            return;
        g.player.health = 100;
        for (int n = 0; n < 60; ++n)
            tickStill(g);
        ActorState &f = g.actors.states[freak];
        ActorState &z = g.actors.states[slide];
        check(f.script >= 0 && z.script >= 0, "both are held by their scripts from the start");
        // Shot: past half its health, and its trigger plays it its death.
        monsterHurt(g.logic, freak, 30, true);
        bool died = false;
        for (int n = 0; n < 90 && !died; ++n)
        {
            tickStill(g);
            trace(g, freak, n * 33);
            died = f.ai == kAiDead;
        }
        check(died, "the freak zombie, shot past half its health, plays its death");
        check(g.states[suspense].dead, "... and its scene kills the suspense trigger");
        for (int n = 0; n < 150; ++n)
            tickStill(g);
        check(f.active, "... and, as its script says, leaves its body");
        // The slide zombie, hurt before its scene: let go, and not taken back.
        monsterHurt(g.logic, slide, 5, true);
        for (int n = 0; n < 30; ++n)
        {
            tickStill(g);
            trace(g, slide, n * 33);
        }
        check(z.script < 0 && z.ai != kAiDead, "the slide zombie, hurt, is let go of its scene");
        for (int n = 0; n < 90; ++n)
            tickStill(g);
        check(z.script < 0, "... and its script does not take it back");
        // (Let go, it is idle, as CineCleanup leaves it: it fights what it sees.)
        bool fights = false;
        if (standAt(g, slide, 200))
        {
            for (int n = 0; n < 90 && !fights; ++n)
            {
                tickStill(g);
                trace(g, slide, 2000 + n * 33);
                fights = mind(g, slide).state == kStateCombat;
            }
        }
        check(fights, "... and goes for the player when it sees them");
    }
    /// c1a1c's crabs: five monstermakers under the ceiling (hck1..hck5,
    /// two alive at a time) and a push under them that carries what they
    /// drop out from under them -- the game's MakeMonster will not build a
    /// stack. And the bullsquid its teleporter makes over the pit falls into
    /// it (made monsters are let fall).
    void testC1a1cMakers(const char *dir)
    {
        auto game = load(dir, "c1a1c");
        if (!game)
            return;
        Game &g = *game;
        g.player.health = 100;
        const std::uint16_t crabber = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("hcrabber"));
        check(crabber != 0, "c1a1c's hcrabber");
        logicFire(g.logic, g.player, crabber, -1, kUseToggle, 0);
        for (int n = 0; n < 30 * 60; ++n) // (a minute)
            tickStill(g);
        int makers = 0, made = 0;
        bool stacked = false, underIt = false;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Maker) || r.name == 0)
                continue;
            const std::uint32_t hash = g.view.nameHashes[r.name];
            bool crab = false;
            for (const char *name : {"hck1", "hck2", "hck3", "hck4", "hck5"})
                crab = crab || hash == nameHash(name);
            if (!crab)
                continue;
            ++makers;
            made += static_cast<int>(g.states[i].bits);
            const ActorRecord &born = g.actors.records[r.extra[0]];
            for (int n = 0; n < static_cast<int>(r.extra[1]); ++n)
            {
                const ActorState &crabState = g.actors.states[r.extra[0] + n];
                if (!crabState.active || crabState.ai >= kAiDying)
                    continue;
                const fx d[3] = {crabState.pos[0] - born.origin[0], crabState.pos[1] - born.origin[1], 0};
                underIt = underIt || fxLength(d) < fxFromInt(20);
                for (int m = 0; m < n; ++m)
                {
                    const ActorState &other = g.actors.states[r.extra[0] + m];
                    const fx e[3] = {crabState.pos[0] - other.pos[0], crabState.pos[1] - other.pos[1], 0};
                    stacked = stacked || (other.active && fxLength(e) < fxFromInt(8));
                }
            }
        }
        std::printf("test_ai: c1a1c: %d crab makers made %d\n", makers, made);
        check(makers == 5 && made >= 9, "c1a1c's crab makers make their crabs (two alive each, as room allows)");
        check(!underIt, "... each carried out from under its maker by the push");
        check(!stacked, "... and none made on top of another");

        // The bullsquid over the pit.
        const std::uint16_t teleport = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("teleport_mm1"));
        logicFire(g.logic, g.player, teleport, -1, kUseToggle, 0);
        int squid = -1;
        fx highest = 0;
        for (int n = 0; n < 30 * 3; ++n)
        {
            tickStill(g);
            for (int k = 0; squid < 0 && k < g.actors.count; ++k)
            {
                const ActorState &s2 = g.actors.states[k];
                if (s2.active && s2.kind == static_cast<std::uint8_t>(ActorKind::Bullsquid) &&
                    std::abs(fxToInt(s2.pos[0]) + 629) < 4 && std::abs(fxToInt(s2.pos[1]) + 386) < 4)
                {
                    squid = k;
                    highest = s2.pos[2];
                }
            }
        }
        check(squid >= 0, "the teleporter makes its bullsquid");
        if (squid >= 0)
        {
            std::printf("test_ai: c1a1c: the bullsquid made at %.0f is at %.0f three seconds on\n", highest / 4096.0,
                        g.actors.states[squid].pos[2] / 4096.0);
            check(g.actors.states[squid].pos[2] < highest - fxFromInt(200), "... which falls into the pit");
        }
    }
} // namespace

int main()
{
    testTable();
    testSounds();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testHearing(dir);
        testHeadcrab(dir);
        testZombie(dir);
        testFlinch(dir);
        testBullsquid(dir);
        testSquidAndCrab(dir);
        testHoundeyes(dir);
        testBarnacle(dir);
        testC1a1fScripts(dir);
        testC1a1cMakers(dir);
    }
    else
    {
        std::printf("test_ai: HLDS_VALVE_DIR not set, the game's maps were not read\n");
    }
    if (g_failures == 0)
        std::printf("test_ai: ok\n");
    return g_failures == 0 ? 0 : 1;
}
