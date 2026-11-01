// The people who talk (shared/HlAiTalk.hpp, HlAiPeople.hpp) and the scripts
// that direct them (HlLogic.hpp), held up against the game's own code
// (dlls/talkmonster.cpp, scientist.cpp, barney.cpp, scripted.cpp), on the
// game's maps (HLDS_VALVE_DIR):
//
// a scientist met before the disaster will not follow, and says so; one
// after greets the player, follows when USEd and stays when USEd again, says
// so each time; one at a time speaks; following a hurt player he heals him,
// once a minute; a guard shot by the player says "watch it" the first time,
// is provoked when plainly aimed at, draws and kills what he hates, and drops
// his pistol when he dies; two scripts of one name start together, however
// long their actors take to walk to their marks; damage breaks a scene off;
// a scripted_sentence waits for someone able to say it; a follower comes
// along to the next map, with his health.

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "HlSaveState.hpp"
#include "HlThink.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
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

    void tick(Game &g, int ms = 33)
    {
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        monstersTick(g.logic, g.player, ms);
        g.drain();
    }

    /// The lines of a voice's group, as the map's talk table has them.
    std::vector<std::uint16_t> linesOf(const Game &g, int voice, int slot)
    {
        std::vector<std::uint16_t> out;
        const int start = kTalkTableAt + kTalkHeading;
        if (g.logic.speech == nullptr || g.ai.speechCount < start)
            return out;
        const int group = voice * kTalkSlots + slot;
        const int first = g.logic.speech[kTalkTableAt + group * 2], count = g.logic.speech[kTalkTableAt + group * 2 + 1];
        for (int n = 0; n < count; ++n)
            out.push_back(g.logic.speech[start + first + n]);
        return out;
    }
    /// How many lines of the group have been said since `from` (an index
    /// into g.said).
    int saidOf(const Game &g, int voice, int slot, std::size_t from = 0)
    {
        const auto lines = linesOf(g, voice, slot);
        int n = 0;
        for (std::size_t i = from; i < g.said.size(); ++i)
            n += std::find(lines.begin(), lines.end(), g.said[i]) != lines.end() ? 1 : 0;
        return n;
    }

    /// One of the people no scene has: unnamed if there is one, else named.
    int findPerson(const Game &g, ActorKind kind, bool preDisaster, int nth = 0)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            int n = nth;
            for (int i = 0; i < g.actors.count; ++i)
            {
                const ActorState &a = g.actors.states[i];
                if (a.active && a.kind == static_cast<std::uint8_t>(kind) && a.script < 0 && (pass == 1 || a.name == 0) &&
                    ((a.item & kPersonPreDisaster) != 0) == preDisaster && n-- == 0)
                    return i;
            }
        }
        return -1;
    }

    /// Everything that can be hurt gone but `keep`.
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

    /// The player put `distance` in front of actor `index`, facing it.
    bool standBefore(Game &g, int index, int distance)
    {
        ActorState &a = g.actors.states[index];
        for (int turn = 0; turn < 16; ++turn)
        {
            const std::uint16_t yaw = static_cast<std::uint16_t>(a.yaw + (turn & 1 ? 1 : -1) * ((turn + 1) / 2) * 4096);
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
            // (The player looks at it.)
            const fx back[3] = {a.pos[0] - probe.pos[0], a.pos[1] - probe.pos[1], 0};
            const fx length = fxLength(back);
            g.player.facingXY[0] = static_cast<fx>((static_cast<std::int64_t>(back[0]) << kFxShift) / length);
            g.player.facingXY[1] = static_cast<fx>((static_cast<std::int64_t>(back[1]) << kFxShift) / length);
            return true;
        }
        return false;
    }

    /// USE on actor `index`, from where the player stands.
    bool use(Game &g, int index)
    {
        const ActorState &a = g.actors.states[index];
        const fx eye[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2] + g.player.viewHeight};
        fx forward[3] = {a.pos[0] - eye[0], a.pos[1] - eye[1], 0};
        const fx length = fxLength(forward);
        for (fx &k : forward)
            k = static_cast<fx>((static_cast<std::int64_t>(k) << kFxShift) / length);
        return personUse(g.logic, g.player, eye, forward);
    }

    // ---------------------------------------------------------------- tests

    void testTable(const char *dir)
    {
        auto game = load(dir, "c1a2a");
        check(game != nullptr, "c1a2a cooks");
        if (!game)
            return;
        Game &g = *game;
        check(g.ai.speechCount > kTalkTableAt + kTalkHeading, "a map with people has what they say");
        int groups = 0, lines = 0;
        for (int voice = 0; voice < kVoices; ++voice)
        {
            for (int slot = 0; slot < kTalkSlots; ++slot)
            {
                const auto l = linesOf(g, voice, slot);
                groups += l.empty() ? 0 : 1;
                lines += static_cast<int>(l.size());
            }
        }
        std::printf("test_talk: c1a2a: %d groups, %d lines\n", groups, lines);
        check(!linesOf(g, kVoiceScientist, kTalkHello).empty() && !linesOf(g, kVoiceBarney, kTalkMad).empty() &&
                  linesOf(g, kVoiceScientist, kTalkPlHurt1).size() == 1 && linesOf(g, kVoiceBarney, kTalkPHello).empty(),
              "... a scientist's hello, a guard's anger, one line of 'you are hurt'; no pre-disaster hello for a guard");
    }

    void testDecline(const char *dir)
    {
        auto game = load(dir, "c1a0");
        if (!game)
        {
            check(false, "c1a0 cooks");
            return;
        }
        Game &g = *game;
        const int sci = findPerson(g, ActorKind::Scientist, true);
        check(sci >= 0, "c1a0 has a scientist from before the disaster, free");
        if (sci < 0 || !standBefore(g, sci, 60))
            return;
        for (int t = 0; t < 30 * 6; ++t)
            tick(g); // (past his hello, and the talk after it)
        const std::size_t from = g.said.size();
        g.actors.ai->talkWait = 0;
        aiOf(g.actors, sci)->talk.useTime = 0;
        check(use(g, sci), "USE reaches him");
        tick(g);
        check(aiFind(g.actors, sci)->targetEnt == kAiNone, "... and he does not follow");
        check(saidOf(g, kVoiceScientist, kTalkDecline, from) == 1, "... and says not now");
    }

    /// c1a0d's 'coffee', held at the coffee machine by a script: USE from in
    /// front of him gets "not now", as from any scientist of before.
    void testBusy(const char *dir)
    {
        auto game = load(dir, "c1a0d");
        if (!game)
        {
            check(false, "c1a0d cooks");
            return;
        }
        Game &g = *game;
        const std::uint32_t hash = nameHash("coffee");
        int sci = -1;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &a = g.actors.states[i];
            if (a.active && a.name != 0 && g.view.nameHashes[a.name] == hash)
                sci = i;
        }
        check(sci >= 0, "c1a0d has the scientist at the coffee");
        if (sci < 0)
            return;
        for (int t = 0; t < 30 * 3; ++t)
            tick(g); // (into his script, at the machine)
        {
            const ActorState &a = g.actors.states[sci];
            std::printf("test_talk: coffee at %d %d %d yaw %d\n", fxToInt(a.pos[0]), fxToInt(a.pos[1]), fxToInt(a.pos[2]), a.yaw * 360 / 65536);
        }
        // (He stands at the machine, against the counter: the nearest place
        // the player can stand, whichever side that is.)
        int room = 0;
        {
            const ActorState &a = g.actors.states[sci];
            fx best[3] = {0, 0, 0};
            for (int dx = -96; dx <= 96; dx += 4)
            {
                for (int dy = -96; dy <= 96; dy += 4)
                {
                    const int d2 = dx * dx + dy * dy;
                    if (d2 < 32 * 32 || (room != 0 && d2 >= room * room))
                        continue;
                    const fx at[3] = {a.pos[0] + fxFromInt(dx), a.pos[1] + fxFromInt(dy), a.pos[2] + kStandingFeet};
                    if (!positionFree(g.move, false, at))
                        continue;
                    room = static_cast<int>(std::lround(std::sqrt(static_cast<double>(d2))));
                    for (int k = 0; k < 3; ++k)
                        best[k] = at[k];
                }
            }
            if (room != 0)
            {
                for (int k = 0; k < 3; ++k)
                    g.player.origin[k] = best[k];
                g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            }
        }
        std::printf("test_talk: the player stands %d from him, at %d %d\n", room, fxToInt(g.player.origin[0]), fxToInt(g.player.origin[1]));
        if (room == 0)
        {
            check(false, "... with room by him");
            return;
        }
        for (int t = 0; t < 30 * 6; ++t)
            tick(g);
        const std::size_t from = g.said.size();
        // (Nothing cleared first: as the player finds him.)
        const bool reached = use(g, sci);
        std::printf("test_talk: from there USE %s him\n", reached ? "reaches" : "does not reach");
        check(reached, "USE reaches the scientist at the coffee");
        tick(g);
        check(saidOf(g, kVoiceScientist, kTalkDecline, from) == 1, "... and he says he is busy");
    }

    void testFollow(const char *dir)
    {
        auto game = load(dir, "c1a2a");
        if (!game)
            return;
        Game &g = *game;
        const int sci = findPerson(g, ActorKind::Scientist, false);
        check(sci >= 0, "c1a2a has a scientist, free");
        if (sci < 0 || !standBefore(g, sci, 120))
            return;
        alone(g, {sci});
        // Hello, first.
        int hello = 0;
        for (int t = 0; t < 30 * 8 && hello == 0; ++t)
        {
            tick(g);
            hello = saidOf(g, kVoiceScientist, kTalkHello);
        }
        check(hello == 1, "the scientist greets the player he sees");
        // One at a time: nobody starts while another talks.
        check(g.actors.ai->talkWait > g.actors.ai->now, "... and nobody else speaks for a while after");
        for (int t = 0; t < 30 * 5; ++t)
            tick(g);
        const std::size_t from = g.said.size();
        standBefore(g, sci, 60);
        check(use(g, sci), "USE on him");
        tick(g);
        check(aiFind(g.actors, sci)->targetEnt == kAiPlayer && g.actors.states[sci].ai == kPersonFollow,
              "... and he follows");
        check(saidOf(g, kVoiceScientist, kTalkUse, from) == 1, "... saying so");
        // The player goes; he comes after.
        const fx start[3] = {g.actors.states[sci].pos[0], g.actors.states[sci].pos[1], g.actors.states[sci].pos[2]};
        const ActorState &me = g.actors.states[sci];
        fx gap = 0;
        // (Back the way he faces, 300 units, if there is floor.)
        fx sine, cosine;
        fxSinCos(static_cast<std::uint16_t>(me.yaw + 32768), sine, cosine);
        ActorState probe;
        probe.pos[0] = me.pos[0] + cosine * 300;
        probe.pos[1] = me.pos[1] + sine * 300;
        probe.pos[2] = me.pos[2] + fxFromInt(8);
        if (actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(64)) &&
            walkClear(g.move, &g.actors, sci, kWalkStanding, me.pos, probe.pos, 24))
        {
            g.player.origin[0] = probe.pos[0];
            g.player.origin[1] = probe.pos[1];
            g.player.origin[2] = probe.pos[2] + kStandingFeet;
            for (int t = 0; t < 30 * 10; ++t)
                tick(g);
            const fx d[3] = {g.player.origin[0] - me.pos[0], g.player.origin[1] - me.pos[1], 0};
            gap = fxLength(d);
            const fx moved[3] = {me.pos[0] - start[0], me.pos[1] - start[1], 0};
            std::printf("test_talk: the player went 300 off; he came %d, and stopped %d from them\n",
                        static_cast<int>(fxToInt(fxLength(moved))), static_cast<int>(fxToInt(gap)));
            check(fxLength(moved) > fxFromInt(100) && gap < fxFromInt(128 + 40), "he follows to within 128");
        }
        // A hurt player is healed, once.
        g.player.health = 40;
        aiFind(g.actors, sci)->talk.healTime = 0;
        int healed = 0;
        for (int t = 0; t < 30 * 15 && healed == 0; ++t)
        {
            tick(g);
            if (g.player.health > 40)
                healed = g.player.health;
        }
        std::printf("test_talk: the player at 40: %d after his needle\n", healed);
        check(healed == 65, "following a player at 40, he heals 25");
        g.player.health = 40;
        for (int t = 0; t < 30 * 15; ++t)
            tick(g);
        check(g.player.health == 40, "... and not again within the minute");
        // USE again: he stays.
        const std::size_t before = g.said.size();
        g.actors.ai->talkWait = 0;
        standBefore(g, sci, 60);
        check(use(g, sci), "USE again");
        tick(g);
        check(aiFind(g.actors, sci)->targetEnt == kAiNone && g.actors.states[sci].ai == kPersonIdle,
              "... and he stays");
        check(saidOf(g, kVoiceScientist, kTalkUnuse, before) == 1, "... saying so");
    }

    void testGuard(const char *dir)
    {
        auto game = load(dir, "c1a2a");
        if (!game)
            return;
        Game &g = *game;
        const int guard = findPerson(g, ActorKind::Guard, false);
        check(guard >= 0, "c1a2a has a guard, free");
        if (guard < 0 || !standBefore(g, guard, 150))
            return;
        alone(g, {guard});
        for (int t = 0; t < 30 * 3; ++t)
            tick(g);
        // Shot by the player, who looks away: "careful".
        const std::size_t from = g.said.size();
        g.actors.ai->talkWait = 0;
        g.player.facingXY[0] = -g.player.facingXY[0];
        g.player.facingXY[1] = -g.player.facingXY[1];
        monsterHurt(g.logic, guard, 2, true, kHitBody);
        tick(g);
        check(saidOf(g, kVoiceBarney, kTalkShot, from) == 1 &&
                  (aiFind(g.actors, guard)->memory & kMemorySuspicious) &&
                  !(aiFind(g.actors, guard)->memory & kMemoryProvoked),
              "a guard shot by the player looking away says watch it, and is wary");
        for (int t = 0; t < 30 * 5; ++t)
            tick(g);
        monsterHurt(g.logic, guard, 2, true, kHitBody);
        tick(g);
        check((aiFind(g.actors, guard)->memory & kMemoryProvoked) != 0 && saidOf(g, kVoiceBarney, kTalkMad, from) == 1,
              "... shot again, he is provoked");
        // Provoked, the player is his enemy: he draws, and shoots.
        int drawn = 0;
        const int was = g.player.health;
        for (int t = 0; t < 30 * 8; ++t)
        {
            tick(g);
            drawn = drawn || g.actors.states[guard].gun == 2;
        }
        std::printf("test_talk: the provoked guard: drawn %d, the player %d -> %d\n", drawn, was, g.player.health);
        check(drawn && g.player.health < was, "... draws and fires on the player");
        // Killed, he drops his pistol.
        int pistols = 0;
        for (int i = 0; i < g.actors.count; ++i)
            pistols += g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                       g.actors.states[i].item == static_cast<std::uint8_t>(ItemKind::Glock);
        monsterHurt(g.logic, guard, 200, false, kHitBody);
        int after = 0;
        for (int i = 0; i < g.actors.count; ++i)
            after += g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                     g.actors.states[i].item == static_cast<std::uint8_t>(ItemKind::Glock);
        check(after == pistols + 1 && g.actors.states[guard].body == barney::kBodyGone,
              "killed, he drops his pistol");
    }

    void testGuardFights(const char *dir)
    {
        auto game = load(dir, "c1a2a");
        if (!game)
            return;
        Game &g = *game;
        const int guard = findPerson(g, ActorKind::Guard, false);
        int crab = -1;
        for (int i = 0; i < g.actors.count && crab < 0; ++i)
        {
            const ActorState &a = g.actors.states[i];
            if (a.active && (a.kind == static_cast<std::uint8_t>(ActorKind::Headcrab) ||
                             a.kind == static_cast<std::uint8_t>(ActorKind::Zombie)) && a.script < 0)
                crab = i;
        }
        if (guard < 0 || crab < 0)
        {
            check(false, "c1a2a has a guard and an alien");
            return;
        }
        alone(g, {guard, crab});
        // The alien put 300 in front of him; the player off to the side.
        ActorState &gd = g.actors.states[guard];
        ActorState &al = g.actors.states[crab];
        if (!standBefore(g, guard, 300))
            return;
        for (int k = 0; k < 3; ++k)
            al.pos[k] = g.player.origin[k];
        al.pos[2] -= kStandingFeet;
        actorPlaceBox(g.actors, crab);
        // (The player behind him, out of its way and its sight -- but about,
        // or nothing looks: FIND_CLIENT_IN_PVS.)
        {
            const fx dx = (al.pos[0] - gd.pos[0]) / 3, dy = (al.pos[1] - gd.pos[1]) / 3;
            const fx spots[4][2] = {{-dx, -dy}, {-dy, dx}, {dy, -dx}, {-dx - dy, -dy + dx}};
            for (const auto &spot : spots)
            {
                ActorState probe;
                probe.pos[0] = gd.pos[0] + spot[0];
                probe.pos[1] = gd.pos[1] + spot[1];
                probe.pos[2] = gd.pos[2] + fxFromInt(8);
                if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(64)) ||
                    !walkClear(g.move, &g.actors, guard, kWalkStanding, gd.pos, probe.pos, 12))
                    continue;
                g.player.origin[0] = probe.pos[0];
                g.player.origin[1] = probe.pos[1];
                g.player.origin[2] = probe.pos[2] + kStandingFeet;
                break;
            }
        }
        int t = 0;
        for (; t < 30 * 20 && al.active && al.ai < kAiDying; ++t)
        {
            tick(g);
            if (std::getenv("HLDS_AI_TRACE") != nullptr && t % 15 == 0)
            {
                const AiMonster *m = aiFind(g.actors, guard);
                const AiMonster *c = aiFind(g.actors, crab);
                std::printf("  t=%d guard st=%d sched=%s enemy=%d cond=%08x act=%d | crab st=%d sched=%s enemy=%d pos=%d,%d,%d gpos=%d,%d,%d\n", t * 33,
                            m ? m->state : -1, m && m->schedule ? m->schedule->name : "-", m ? m->enemy : -9,
                            m ? m->conditions : 0, m ? m->activity : 0, c ? c->state : -1,
                            c && c->schedule ? c->schedule->name : "-", c ? c->enemy : -9, fxToInt(al.pos[0]),
                            fxToInt(al.pos[1]), fxToInt(al.pos[2]), fxToInt(gd.pos[0]), fxToInt(gd.pos[1]),
                            fxToInt(gd.pos[2]));
            }
        }
        std::printf("test_talk: guard against a %s: gun %d, it is %s after %d ms\n",
                    al.kind == static_cast<std::uint8_t>(ActorKind::Headcrab) ? "headcrab" : "zombie", gd.gun,
                    !al.active || al.ai >= kAiDying ? "dead" : "alive", t * 33);
        check(gd.gun == 2 && (!al.active || al.ai >= kAiDying), "a guard draws on an alien, and kills it");
    }

    void testScriptSync(const char *dir)
    {
        auto game = load(dir, "c1a0");
        if (!game)
            return;
        Game &g = *game;
        // The two scientists sent to the board together ("write"): each
        // walks to his mark, and neither starts before both are there.
        const std::uint16_t name = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("write"));
        std::vector<int> scripts;
        for (int i = 0; i < g.logic.recordCount; ++i)
        {
            if (g.logic.records[i].kind == static_cast<std::uint8_t>(LogicKind::Script) && g.logic.records[i].name == name)
                scripts.push_back(i);
        }
        check(scripts.size() == 2, "c1a0 has two scripts called write");
        if (scripts.size() != 2)
            return;
        logicFire(g.logic, g.player, name, -1, kUseToggle, 0);
        int started[2] = {-1, -1}, arrived[2] = {-1, -1};
        for (int t = 0; t < 30 * 30; ++t)
        {
            tick(g);
            for (int s = 0; s < 2; ++s)
            {
                const LogicState &state = g.logic.states[scripts[s]];
                if (arrived[s] < 0 && state.phase >= hldata::detail::kScriptHolding && state.phase != hldata::detail::kScriptMoving)
                    arrived[s] = t;
                if (started[s] < 0 && state.phase == hldata::detail::kScriptPlaying)
                    started[s] = t;
            }
        }
        std::printf("test_talk: write: arrived at %d and %d ms, started at %d and %d ms\n", arrived[0] * 33,
                    arrived[1] * 33, started[0] * 33, started[1] * 33);
        check(started[0] >= 0 && started[1] >= 0 && std::abs(started[0] - started[1]) <= 2 &&
                  std::min(started[0], started[1]) >= std::max(arrived[0], arrived[1]),
              "two scripts of one name start together, when the last is on his mark");
    }

    void testScriptInterrupt(const char *dir)
    {
        auto game = load(dir, "c1a0");
        if (!game)
            return;
        Game &g = *game;
        // Someone a scene holds, that may be broken off: hurt, he is free.
        int who = -1;
        for (int t = 0; t < 30 * 3; ++t)
            tick(g);
        for (int i = 0; i < g.actors.count && who < 0; ++i)
        {
            const ActorState &a = g.actors.states[i];
            if (a.active && a.kind == static_cast<std::uint8_t>(ActorKind::Scientist) && a.script >= 0 &&
                hldata::detail::scriptCanInterrupt(g.logic, a.script))
                who = i;
        }
        check(who >= 0, "c1a0 has a scientist in a scene that may be broken off");
        if (who < 0)
            return;
        monsterHurt(g.logic, who, 5, true, kHitBody);
        check(g.actors.states[who].script < 0 && g.actors.states[who].ai < kAiDying,
              "hurt, he is let go, alive");
    }

    void testSentence(const char *dir)
    {
        auto game = load(dir, "c1a2a");
        if (!game)
            return;
        Game &g = *game;
        // The scientist in the dark room, told to say his line while
        // someone else is talking: he waits (FOkToSpeak), and says it once
        // nobody is.
        const std::uint16_t name = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("lightsout_speaking"));
        int record = -1;
        for (int i = 0; i < g.logic.recordCount && record < 0; ++i)
        {
            if (g.logic.records[i].name == name && g.logic.records[i].kind == static_cast<std::uint8_t>(LogicKind::Sentence))
                record = i;
        }
        int speaker = -1;
        for (int i = 0; i < g.actors.count && record >= 0 && speaker < 0; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].name == g.logic.records[record].netName)
                speaker = i;
        }
        check(record >= 0 && speaker >= 0, "c1a2a's lightsout_speaking is a line, with its speaker");
        if (record < 0 || speaker < 0)
            return;
        standBefore(g, speaker, 100);
        for (int t = 0; t < 30 * 2; ++t)
            tick(g);
        const std::uint16_t line = static_cast<std::uint16_t>(g.logic.records[record].extra[0]);
        g.actors.ai->talkWait = g.actors.ai->now + 60000; // (someone else is talking)
        const std::size_t from = g.said.size();
        logicFire(g.logic, g.player, name, -1, kUseToggle, 0);
        for (int t = 0; t < 30 * 2; ++t)
            tick(g);
        check(std::find(g.said.begin() + static_cast<long>(from), g.said.end(), line) == g.said.end(),
              "a scripted_sentence waits while someone else talks");
        g.actors.ai->talkWait = 0;
        for (int t = 0; t < 30 * 5; ++t)
            tick(g);
        check(std::find(g.said.begin() + static_cast<long>(from), g.said.end(), line) != g.said.end() &&
                  aiFind(g.actors, speaker)->talk.useTime > 0,
              "... and is said when nobody is, by its speaker");
    }

    /// c0a0e: the guard who comes in with the tram walks to it, says the
    /// player is late, and pushes the button -- and the tram's door opens
    /// after his push, not before (a scripted_sequence fires its target
    /// when its clip is done: CCineMonster::SequenceDone). `busyMs`: he is
    /// kept in another scene that long first; his scenes wait for him, as
    /// the game's do, rather than giving him up and opening the door.
    void testTramGuard(const char *dir, int busyMs)
    {
        auto game = load(dir, "c0a0d");
        if (!game)
            return;
        Game &g = *game;
        int change = -1;
        for (int i = 0; i < g.logic.recordCount; ++i)
        {
            const LogicRecord &r = g.logic.records[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::ChangeLevel))
                change = i;
        }
        const Landmark *mark = change >= 0 ? findLandmark(g.view.landmarks, g.view.landmarkCount, g.logic.records[change].extra[1])
                                           : nullptr;
        check(mark != nullptr, "c0a0d has its way on to c0a0e");
        if (mark == nullptr)
            return;
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = mark->origin[k];
        const Arrival arrival = makeArrival(g.logic, g.view.landmarks, g.view.landmarkCount, g.player, change,
                                            g.view.modelRefs);
        int guards = 0;
        for (const Arrival::Visitor &v : arrival.visitors)
            guards += v.kind == static_cast<std::uint8_t>(ActorKind::Guard);
        check(guards == 1, "the guard comes with the tram");
        auto next = load(dir, "c0a0e");
        if (!next)
            return;
        Game &n = *next;
        applyArrival(n.logic, n.view.landmarks, n.view.landmarkCount, n.view.nameHashes, n.view.nameCount, n.player,
                     arrival, n.view.modelRefs);
        int guard = -1;
        for (int i = 0; i < n.actors.count; ++i)
        {
            if (n.actors.states[i].active && n.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Guard))
                guard = i;
        }
        int door = -1;
        const std::uint32_t doorHash = nameHash("traindoor");
        for (int i = 0; i < n.logic.recordCount; ++i)
        {
            const LogicRecord &r = n.logic.records[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Train) && r.name != 0 && n.view.nameHashes[r.name] == doorHash)
                door = i;
        }
        check(guard >= 0 && door >= 0, "... into c0a0e, by its tram's door");
        if (guard < 0 || door < 0)
            return;
        const ActorState &a = n.actors.states[guard];
        const ModelView *model = actorModel(n.actors, a);
        const int push = model != nullptr ? model->findClip(nameHash("intropush")) : -1;
        std::printf("test_talk: c0a0e: the guard's model has intropush: %s\n", push >= 0 ? "yes" : "NO");
        const fx doorAt0 = n.brushStates[n.logic.records[door].brush].pos[1];
        int pushStart = -1, pushEnd = -1, doorMoves = -1, lastClip = -2;
        ActorState &held = n.actors.states[guard];
        for (int ms = 0; ms < 40000 + busyMs; ms += 33)
        {
            // (Busy: held by a scene that is not one of these.)
            if (ms < busyMs)
                held.script = static_cast<std::int16_t>(door);
            else if (ms - 33 < busyMs && busyMs > 0)
                held.script = -1;
            tick(n);
            if (a.clip != lastClip)
            {
                std::printf("test_talk: c0a0e at %d ms: the guard plays clip %d%s at %d %d, script %d\n", ms, a.clip,
                            a.clip == push ? " (intropush)" : "", fxToInt(a.pos[0]), fxToInt(a.pos[1]), a.script);
                lastClip = a.clip;
            }
            if (a.clip == push && push >= 0 && pushStart < 0)
                pushStart = ms;
            if (pushStart >= 0 && pushEnd < 0 && a.clip != push)
                pushEnd = ms;
            if (doorMoves < 0 && n.brushStates[n.logic.records[door].brush].pos[1] != doorAt0)
                doorMoves = ms;
        }
        std::printf("test_talk: c0a0e (busy %d ms): the push from %d to %d ms, the door moves at %d ms\n", busyMs, pushStart,
                    pushEnd, doorMoves);
        check(push >= 0 && pushStart >= 0, "the guard pushes the tram's button");
        check(doorMoves >= 0 && pushEnd >= 0 && doorMoves >= pushEnd, "... and its door opens after his push, not before");
        if (busyMs > 0)
            return; // (the way in: once is enough)

        // Then at the way in: the player walks up (trigger *17, x -1932..-1892),
        // he puts his card to the scanner, and the bars and the doors open
        // after it (entrymm: bars at 7 s, doors at 11 s from his push's end).
        auto moverOf = [&](const char *name)
        {
            const std::uint32_t hash = nameHash(name);
            for (int i = 0; i < n.logic.recordCount; ++i)
            {
                const LogicRecord &r = n.logic.records[i];
                if (r.kind == static_cast<std::uint8_t>(LogicKind::Door) && r.name != 0 && n.view.nameHashes[r.name] == hash)
                    return i;
            }
            return -1;
        };
        const int bars = moverOf("doorbars"), doors = moverOf("doors");
        check(bars >= 0 && doors >= 0, "c0a0e's way in has its bars and its doors");
        if (bars < 0 || doors < 0)
            return;
        n.player.origin[0] = fxFromInt(-1912);
        n.player.origin[1] = fxFromInt(-600);
        n.player.origin[2] = fxFromInt(-253 + 36);
        int scan = -1, scanEnd = -1, barsMove = -1, doorsMove = -1;
        for (int ms = 0; ms < 30000; ms += 33)
        {
            tick(n);
            if (a.clip != lastClip)
            {
                std::printf("test_talk: c0a0e, at the way in, at %d ms: the guard plays clip %d%s at %d %d, script %d\n",
                            ms, a.clip, a.clip == push ? " (intropush)" : "", fxToInt(a.pos[0]), fxToInt(a.pos[1]),
                            a.script);
                lastClip = a.clip;
            }
            if (a.clip == push && scan < 0)
                scan = ms;
            if (scan >= 0 && scanEnd < 0 && a.clip != push)
                scanEnd = ms;
            if (barsMove < 0 && n.states[bars].move != MoveState::AtBottom)
                barsMove = ms;
            if (doorsMove < 0 && n.states[doors].move != MoveState::AtBottom)
                doorsMove = ms;
        }
        std::printf("test_talk: c0a0e: the scan from %d to %d ms, the bars move at %d, the doors at %d\n", scan, scanEnd,
                    barsMove, doorsMove);
        check(scan >= 0 && scanEnd >= 0, "the guard puts his card to the scanner");
        check(barsMove >= scanEnd && doorsMove >= scanEnd, "... and the way in opens after, not before");
    }

    /// c0a0e from its autosave (made a second in): laid over the map begun
    /// afresh with the same arrival, the guard still walks to the tram and
    /// pushes, the door opens after, and it is turned as the map has it.
    void testTramGuardFromSave(const char *dir, int saveAtMs)
    {
        auto game = load(dir, "c0a0d");
        if (!game)
            return;
        Game &g = *game;
        int change = -1;
        for (int i = 0; i < g.logic.recordCount; ++i)
        {
            if (g.logic.records[i].kind == static_cast<std::uint8_t>(LogicKind::ChangeLevel))
                change = i;
        }
        const Landmark *mark = change >= 0 ? findLandmark(g.view.landmarks, g.view.landmarkCount, g.logic.records[change].extra[1])
                                           : nullptr;
        if (mark == nullptr)
            return;
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = mark->origin[k];
        const Arrival arrival = makeArrival(g.logic, g.view.landmarks, g.view.landmarkCount, g.player, change,
                                            g.view.modelRefs);
        auto first = load(dir, "c0a0e");
        if (!first)
            return;
        Game &a = *first;
        applyArrival(a.logic, a.view.landmarks, a.view.landmarkCount, a.view.nameHashes, a.view.nameCount, a.player,
                     arrival, a.view.modelRefs);
        for (int ms = 0; ms < saveAtMs; ms += 33)
            tick(a);
        const std::uint32_t level = a.view.header->crc32;
        std::vector<std::uint8_t> saved = takeMapState(a.logic, a.player, level);
        const std::size_t mapBytes = saved.size();
        aiAppendState(saved, a.actors);

        auto second = load(dir, "c0a0e");
        if (!second)
            return;
        Game &n = *second;
        applyArrival(n.logic, n.view.landmarks, n.view.landmarkCount, n.view.nameHashes, n.view.nameCount, n.player,
                     arrival, n.view.modelRefs);
        const bool laid = layMapState(saved.data(), mapBytes, n.logic, n.player, level) &&
                          aiLayState(saved.data() + mapBytes, saved.size() - mapBytes, n.actors);
        check(laid, "c0a0e's save, a moment in, lays back over it");
        int guard = -1, door = -1;
        for (int i = 0; i < n.actors.count; ++i)
        {
            if (n.actors.states[i].active && n.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Guard))
                guard = i;
        }
        const std::uint32_t doorHash = nameHash("traindoor");
        for (int i = 0; i < n.logic.recordCount; ++i)
        {
            const LogicRecord &r = n.logic.records[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Train) && r.name != 0 && n.view.nameHashes[r.name] == doorHash)
                door = i;
        }
        check(guard >= 0 && door >= 0, "... with its guard, and the tram's door");
        if (guard < 0 || door < 0)
            return;
        check(n.brushStates[n.logic.records[door].brush].angles[1] == 49152, "... which is turned as the map has it");
        const ActorState &me = n.actors.states[guard];
        const ModelView *model = actorModel(n.actors, me);
        const int push = model != nullptr ? model->findClip(nameHash("intropush")) : -1;
        const fx doorAt0 = n.brushStates[n.logic.records[door].brush].pos[1];
        int pushStart = -1, pushEnd = -1, doorMoves = -1;
        for (int ms = 0; ms < 40000; ms += 33)
        {
            tick(n);
            if (me.clip == push && push >= 0 && pushStart < 0)
                pushStart = ms;
            if (pushStart >= 0 && pushEnd < 0 && me.clip != push)
                pushEnd = ms;
            if (doorMoves < 0 && n.brushStates[n.logic.records[door].brush].pos[1] != doorAt0)
                doorMoves = ms;
        }
        std::printf("test_talk: c0a0e from a save %d ms in: the guard at %d %d, push %d..%d ms, door at %d ms\n", saveAtMs,
                    fxToInt(me.pos[0]), fxToInt(me.pos[1]), pushStart, pushEnd, doorMoves);
        check(pushStart >= 0 && doorMoves >= pushEnd && pushEnd >= 0,
              "... and after it is loaded the guard comes and pushes, and the door opens after");
    }

    void testFollowerCrosses(const char *dir)
    {
        auto game = load(dir, "c1a2");
        if (!game)
            return;
        Game &g = *game;
        // The landmark to c1a2a, and a scientist following the player there.
        int change = -1;
        for (int i = 0; i < g.logic.recordCount && change < 0; ++i)
        {
            const LogicRecord &r = g.logic.records[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::ChangeLevel))
                change = i;
        }
        const int sci = findPerson(g, ActorKind::Scientist, false);
        if (change < 0 || sci < 0)
        {
            check(false, "c1a2 has a level change and a scientist");
            return;
        }
        const Landmark *mark = findLandmark(g.view.landmarks, g.view.landmarkCount, g.logic.records[change].extra[1]);
        if (mark == nullptr)
            return;
        ActorState &s = g.actors.states[sci];
        // (By the landmark, as if he had followed the player there.)
        g.player.origin[0] = mark->origin[0];
        g.player.origin[1] = mark->origin[1];
        g.player.origin[2] = mark->origin[2];
        for (int k = 0; k < 3; ++k)
            s.pos[k] = mark->origin[k];
        s.pos[0] += fxFromInt(40);
        actorToFloor(g.move, s, fxFromInt(64), fxFromInt(128));
        tick(g);
        aiOf(g.actors, sci)->targetEnt = kAiPlayer;
        s.ai = kPersonFollow;
        s.health = 13;
        const Arrival arrival = makeArrival(g.logic, g.view.landmarks, g.view.landmarkCount, g.player, change,
                                            g.view.modelRefs);
        int carried = 0;
        for (const Arrival::Visitor &v : arrival.visitors)
            carried += v.model != 0 && v.kind == static_cast<std::uint8_t>(ActorKind::Scientist);
        std::printf("test_talk: c1a2 -> %s: the scientist at %d %d %d (mark %d %d %d), ai %d active %d script %d, %d carried\n",
                    "next", fxToInt(s.pos[0]), fxToInt(s.pos[1]), fxToInt(s.pos[2]), fxToInt(mark->origin[0]),
                    fxToInt(mark->origin[1]), fxToInt(mark->origin[2]), s.ai, s.active, s.script, carried);
        check(carried >= 1, "a scientist who follows comes along, name or none");
        auto next = load(dir, "c1a2a");
        if (!next)
            return;
        Game &n = *next;
        applyArrival(n.logic, n.view.landmarks, n.view.landmarkCount, n.view.nameHashes, n.view.nameCount, n.player,
                     arrival, n.view.modelRefs);
        int came = -1;
        for (int i = 0; i < n.actors.count; ++i)
        {
            const ActorState &a = n.actors.states[i];
            if (a.active && a.kind == static_cast<std::uint8_t>(ActorKind::Scientist) && a.health == 13)
                came = i;
        }
        check(came >= 0, "... into the next map, with his health");
        if (came < 0)
            return;
        for (int t = 0; t < 30 * 2; ++t)
            tick(n);
        check(aiFind(n.actors, came) != nullptr && aiFind(n.actors, came)->targetEnt == kAiPlayer,
              "... following still");
    }

    /// c1a1 left and come back to: the zombie killed by the way out does not
    /// come along to c1a1a, and nothing used up happens again -- the trigger
    /// that sends the maker's zombie, the maker, the zombie's scene -- nor
    /// c1a1's guard the laser cuts: two halves (monster_generic b1 and b2)
    /// put by their script onto its mark by the wall, at once. On the floor
    /// there, as the game puts them -- not on the ledge beside it, as a
    /// person's box would be.
    void testLaserGuard(const char *dir)
    {
        auto game = load(dir, "c1a1");
        if (!game)
            return;
        Game &g = *game;
        for (int ms = 0; ms < 3000; ms += 33)
            tick(g);
        int halves = 0;
        bool floor = true;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &a = g.actors.states[i];
            if (!a.active || a.kind != static_cast<std::uint8_t>(ActorKind::Generic) ||
                (a.nameHash != nameHash("b1") && a.nameHash != nameHash("b2")))
                continue;
            ++halves;
            floor = floor && a.pos[0] == fxFromInt(1632) && a.pos[1] == fxFromInt(1558) && a.pos[2] == fxFromInt(-144);
        }
        check(halves == 2 && floor, "c1a1's cut guard lies on the floor at its mark, not on the ledge by it");
    }

    /// does the killed stand up again, scientist or zombie.
    void testRevisit(const char *dir)
    {
        auto game = load(dir, "c1a1");
        if (!game)
            return;
        Game &g = *game;
        int change = -1;
        for (int i = 0; i < g.logic.recordCount && change < 0; ++i)
        {
            if (g.logic.records[i].kind == static_cast<std::uint8_t>(LogicKind::ChangeLevel))
                change = i;
        }
        const std::uint16_t zombieName = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("attacking_zombie"));
        int zombie = -1;
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].name == zombieName && zombieName != 0)
                zombie = i;
        }
        const int sci = findPerson(g, ActorKind::Scientist, false);
        const Landmark *mark = change < 0 ? nullptr
                                          : findLandmark(g.view.landmarks, g.view.landmarkCount,
                                                         g.logic.records[change].extra[1]);
        if (mark == nullptr || zombie < 0 || sci < 0)
        {
            check(false, "c1a1 has its level change, attacking_zombie and a scientist");
            return;
        }
        // The zombie by the landmark: alive, it comes; dead, it does not.
        ActorState &z = g.actors.states[zombie];
        for (int k = 0; k < 3; ++k)
            z.pos[k] = mark->origin[k];
        z.pos[0] += fxFromInt(40);
        actorToFloor(g.move, z, fxFromInt(64), fxFromInt(128));
        g.player.origin[0] = mark->origin[0];
        g.player.origin[1] = mark->origin[1];
        g.player.origin[2] = mark->origin[2];
        auto zombiesIn = [&](const Arrival &arrival) {
            int n = 0;
            for (const Arrival::Visitor &v : arrival.visitors)
                n += v.model != 0 && v.kind == static_cast<std::uint8_t>(ActorKind::Zombie);
            return n;
        };
        const int alive = zombiesIn(makeArrival(g.logic, g.view.landmarks, g.view.landmarkCount, g.player, change,
                                                g.view.modelRefs));
        z.ai = kAiDead;
        z.health = 0;
        const int dead = zombiesIn(makeArrival(g.logic, g.view.landmarks, g.view.landmarkCount, g.player, change,
                                               g.view.modelRefs));
        std::printf("test_talk: c1a1 -> c1a1a: %d zombies cross alive, %d dead\n", alive, dead);
        check(alive == dead + 1, "a dead zombie does not cross a level change");

        // Everything once-only spent, the scientist killed; left, and come
        // back to.
        g.actors.states[sci].ai = kAiDying;
        int spent = 0;
        for (int i = 0; i < g.logic.recordCount; ++i)
        {
            if (hldata::detail::staysSpent(g.logic.records[i]))
            {
                g.logic.states[i].dead = true;
                ++spent;
            }
        }
        SaveMapState slot;
        slot.map = nameHash("c1a1");
        mapRemember(slot, g.logic, g.actors);
        auto again = load(dir, "c1a1");
        if (!again)
            return;
        Game &b = *again;
        mapRestore(slot, b.logic, b.actors);
        int kept = 0, makers = 0;
        for (int i = 0; i < b.logic.recordCount; ++i)
        {
            const LogicRecord &r = b.logic.records[i];
            if (hldata::detail::staysSpent(r))
                kept += b.logic.states[i].dead ? 1 : 0;
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Maker))
                makers += b.logic.states[i].dead && !b.logic.states[i].running ? 1 : 0;
        }
        std::printf("test_talk: c1a1 again: %d of %d used up stay so, %d makers spent\n", kept, spent, makers);
        check(spent > 0 && kept == spent, "what was used up in c1a1 stays so");
        check(makers >= 1, "... the zombie's maker among them");
        check(!b.actors.states[zombie].active, "the killed zombie is not back");
        check(!b.actors.states[sci].active, "nor the killed scientist");
        // ... and played on, no zombie stands up anywhere.
        b.player.origin[0] = mark->origin[0];
        b.player.origin[1] = mark->origin[1];
        b.player.origin[2] = mark->origin[2];
        int before = 0, after = 0;
        for (int i = 0; i < b.actors.count; ++i)
            before += b.actors.states[i].active && b.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Zombie);
        for (int t = 0; t < 30 * 5; ++t)
            tick(b);
        for (int i = 0; i < b.actors.count; ++i)
            after += b.actors.states[i].active && b.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Zombie);
        check(after <= before, "no new zombie in c1a1 come back to");

        // A save from before (bits on breakables only) still reads the same.
        SaveMapState old;
        old.map = slot.map;
        old.broken.assign(slot.broken.size(), 0);
        for (int i = 0; i < g.logic.recordCount; ++i)
        {
            if (hldata::detail::isBreakable(g.logic.records[i]) && (slot.broken[i >> 3] & (1u << (i & 7))))
                old.broken[i >> 3] |= static_cast<std::uint8_t>(1u << (i & 7));
        }
        auto third = load(dir, "c1a1");
        if (!third)
            return;
        mapRestore(old, third->logic, third->actors);
        int stale = 0;
        for (int i = 0; i < third->logic.recordCount; ++i)
            stale += hldata::detail::staysSpent(third->logic.records[i]) && third->logic.states[i].dead ? 1 : 0;
        check(stale == 0, "an old save's map memory spends nothing");
    }
} // namespace

namespace
{
    /// HLDS_BENCH=<map> x y z: the map for twenty seconds with the player
    /// standing there, and how long its logic and monsters took (a host's
    /// measure of where the console's time goes: run it under perf).
    void bench(const char *dir, const char *spec)
    {
        char map[32] = {};
        int x = 0, y = 0, z = 0;
        if (std::sscanf(spec, "%31s %d %d %d", map, &x, &y, &z) != 4)
            return;
        auto game = load(dir, map);
        if (!game)
            return;
        Game &g = *game;
        g.player.origin[0] = fxFromInt(x);
        g.player.origin[1] = fxFromInt(y);
        g.player.origin[2] = fxFromInt(z);
        // HLDS_BENCH_DEFER=1: the long questions worked out between frames,
        // as the console's worker does -- only what is in the frame timed.
        std::vector<ThinkSlot> slots;
        ThinkQueue queue;
        const bool defer = std::getenv("HLDS_BENCH_DEFER") != nullptr;
        if (defer)
        {
            slots.resize(g.actors.count);
            queue.slots = slots.data();
            queue.count = g.actors.count;
            thinkClear(queue);
            queue.deferred = true;
            g.actors.think = &queue;
        }
        const auto start = std::chrono::steady_clock::now();
        double byKind[static_cast<int>(ActorKind::Count)] = {};
        std::vector<double> byActor(g.actors.count, 0.0);
        double logic = 0;
        struct Worst
        {
            double ms;
            int frame, actor, task;
            const char *before, *after;
            double part[3]; // look, hear, move (AiSpentScope), ms
            int traces, lines; // hull traces and lines through the world it made
        };
        aiClock = []() -> std::uint32_t
        {
            return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  std::chrono::steady_clock::now().time_since_epoch())
                                                  .count());
        };
        std::vector<Worst> worst;
        const int frames = std::getenv("HLDS_BENCH_FRAMES") != nullptr ? std::atoi(std::getenv("HLDS_BENCH_FRAMES")) : 600;
        for (int t = 0; t < frames; ++t)
        {
            g.player.health = 100;
            auto t0 = std::chrono::steady_clock::now();
            logicTick(g.logic, g.player, 33);
            logic += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            aiWorldTick(g.logic, g.player, 33);
            for (int i = 0; i < g.actors.count; ++i)
            {
                if (!g.actors.states[i].active || !actorThinks(g.actors.states[i].kind))
                    continue;
                const AiMonster *was = aiFind(g.actors, i);
                const char *before = was != nullptr && was->schedule != nullptr ? was->schedule->name : "-";
                const int task = was != nullptr && was->schedule != nullptr && was->scheduleIndex < was->schedule->count
                                     ? was->schedule->tasks[was->scheduleIndex].task
                                     : -1;
                const std::uint32_t parts[3] = {aiSpent[0], aiSpent[1], aiSpent[2]};
                const std::uint32_t traces = hostTraceCount, lines = hldata::detail::hostLineCount;
                t0 = std::chrono::steady_clock::now();
                aiTick(g.logic, g.player, i, 33);
                const double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                byKind[g.actors.states[i].kind] += spent;
                byActor[i] += spent;
                // (The dearest single thinks, and what they were doing.)
                const AiMonster *now = aiFind(g.actors, i);
                Worst one{spent, t, i, task, before,
                          now != nullptr && now->schedule != nullptr ? now->schedule->name : "-",
                          {(aiSpent[0] - parts[0]) / 1e6, (aiSpent[1] - parts[1]) / 1e6, (aiSpent[2] - parts[2]) / 1e6},
                          static_cast<int>(hostTraceCount - traces), static_cast<int>(hldata::detail::hostLineCount - lines)};
                worst.push_back(one);
                std::sort(worst.begin(), worst.end(),
                          [](const Worst &a, const Worst &b) { return a.ms > b.ms; });
                if (worst.size() > 12)
                    worst.pop_back();
            }
            g.drain();
            while (defer && thinkRunOne(g.logic))
            {
            }
        }
        g.actors.think = nullptr;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("bench: logic %.3f ms a frame\n", logic / frames);
        for (int i = 0; i < g.actors.count; ++i)
        {
            const AiMonster *m = aiFind(g.actors, i);
            if (byActor[i] / frames > 0.004 && m != nullptr)
                std::printf("bench: actor %d kind %d %.3f ms: state %d %s enemy %d at %d %d %d\n", i, g.actors.states[i].kind,
                            byActor[i] / frames, m->state, m->schedule != nullptr ? m->schedule->name : "-", m->enemy,
                            fxToInt(g.actors.states[i].pos[0]), fxToInt(g.actors.states[i].pos[1]),
                            fxToInt(g.actors.states[i].pos[2]));
        }
        for (int k = 0; k < static_cast<int>(ActorKind::Count); ++k)
        {
            if (byKind[k] > 0)
                std::printf("bench: kind %d %.3f ms a frame\n", k, byKind[k] / frames);
        }
        for (const Worst &w : worst)
            std::printf("bench: worst %d traces %d lines, %.3f ms (look %.3f hear %.3f move %.3f) frame %d actor %d kind %d "
                        "task %d: %s -> %s\n",
                        w.traces, w.lines, w.ms, w.part[0], w.part[1], w.part[2], w.frame, w.actor, g.actors.states[w.actor].kind, w.task,
                        w.before, w.after);
        int thinkers = 0;
        for (int i = 0; i < g.actors.count; ++i)
            thinkers += g.actors.states[i].active && aiFind(g.actors, i) != nullptr;
        std::printf("bench %s: %d minds, %.2f ms a frame on this host\n", map, thinkers, ms / frames);
    }
    /// HLDS_SCENE: c1a2a's scientist by the door, as the emulator was
    /// shown it -- what he does, a third of a second at a time.
    void scene(const char *dir)
    {
        auto game = load(dir, "c1a2a");
        if (!game)
            return;
        Game &g = *game;
        g.player.origin[0] = fxFromInt(-102);
        g.player.origin[1] = fxFromInt(-760);
        g.player.origin[2] = fxFromInt(-220);
        g.player.facingXY[0] = 0;
        g.player.facingXY[1] = -kFxOne;
        int sci = -1;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &a = g.actors.states[i];
            if (a.active && a.kind == static_cast<std::uint8_t>(ActorKind::Scientist) &&
                aiDist2D(a.pos, g.player.origin) < fxFromInt(200))
                sci = i;
        }
        if (sci < 0)
            return;
        for (int t = 0; t < 30 * 12; ++t)
        {
            if (t == 30 * 4)
                std::printf("USE: %d\n", use(g, sci));
            if (t >= 30 * 5 && t < 30 * 6)
                g.player.origin[1] += fxFromInt(6);
            tick(g);
            if (t % 10 == 0)
            {
                const AiMonster *m = aiFind(g.actors, sci);
                const ActorState &a = g.actors.states[sci];
                std::printf("t=%d %s task %d st %d enemy %d follows %d pos %d %d %d said %zu\n", t * 33,
                            m && m->schedule ? m->schedule->name : "-", m ? m->scheduleIndex : -1, m ? m->state : -1,
                            m ? m->enemy : -9, m ? m->targetEnt : -9, fxToInt(a.pos[0]), fxToInt(a.pos[1]),
                            fxToInt(a.pos[2]), g.said.size());
            }
        }
    }
    /// c1a1b's sitting scientist: USEd from where he sits (a step short of
    /// the trigger that starts his scene), he follows -- and walking on
    /// through that trigger does not take him back to his chair. Led to the
    /// retinal scanner, its scene takes him and the armoury's door opens.
    void testSittingScientist(const char *dir)
    {
        auto game = load(dir, "c1a1b");
        if (!game)
            return;
        Game &g = *game;
        const std::uint16_t id = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("sitting_scientist"));
        int sci = -1;
        for (int i = 0; id != 0 && i < g.actors.count; ++i)
            sci = g.actors.states[i].name == id ? i : sci;
        check(sci >= 0, "c1a1b's sitting scientist");
        if (sci < 0)
            return;
        ActorState &him = g.actors.states[sci];
        g.player.health = 100;
        auto standAt = [&](int x, int y, int z)
        {
            ActorState probe;
            probe.pos[0] = fxFromInt(x);
            probe.pos[1] = fxFromInt(y);
            probe.pos[2] = fxFromInt(z);
            actorToFloor(g.move, probe, fxFromInt(16), fxFromInt(128));
            g.player.origin[0] = probe.pos[0];
            g.player.origin[1] = probe.pos[1];
            g.player.origin[2] = probe.pos[2] + kStandingFeet;
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        };
        standAt(627, -1260, -60);
        for (int t = 0; t < 60; ++t)
            tick(g);
        check(him.script >= 0, "he sits, held by his scene");
        standAt(627, -1233, -60);
        for (int t = 0; t < 30 && him.ai != 1; ++t)
        {
            use(g, sci);
            tick(g);
        }
        check(him.ai == 1 && him.script < 0, "USEd from beside his chair, he follows");
        // On through the trigger his scene starts by, and away.
        bool sat = false;
        for (int t = 0; t < 30 * 4; ++t)
        {
            standAt(609, t < 30 ? -1207 : -1300, -60);
            tick(g);
            sat = sat || him.script >= 0;
        }
        check(!sat && him.ai == 1, "... and his scene does not take him back when the player walks on");
        // To the retinal scanner: its scene takes him there, and the door
        // opens.
        const std::uint16_t doorName = nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("retinal_scanner_door"));
        int door = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
            door = g.view.logic[i].name == doorName && (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Door) ||
                                                        g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::DoorRotating))
                       ? static_cast<int>(i)
                       : door;
        check(door >= 0, "the retinal scanner's door");
        // (Down the corridor, the G-man the player sees there walks off and
        // is gone -- his scene, "watch", as the player passes.)
        logicFire(g.logic, g.player, nameIdOfHash(g.view.nameHashes, g.view.nameCount, nameHash("watch")), -1, kUseToggle, 0);
        for (int t = 0; t < 30 * 15; ++t)
            tick(g);
        // (The player goes on to the scanner, in its trigger; he follows.)
        standAt(136, -650, -40);
        bool opened = false;
        for (int t = 0; t < 30 * 60 && !opened; ++t)
        {
            tick(g);
            if (std::getenv("HLDS_DEBUG_SCI") && t % 30 == 0)
            {
                const AiMonster *m = aiFind(g.actors, sci);
                std::printf("  t=%d him %.0f %.0f ai %d script %d sched %s\n", t, him.pos[0] / 4096.0, him.pos[1] / 4096.0,
                            him.ai, him.script, m != nullptr && m->schedule != nullptr ? m->schedule->name : "-");
            }
            opened = door >= 0 && g.states[door].move != MoveState::AtBottom;
        }
        std::printf("test_talk: c1a1b: the scientist led to the scanner (at %.0f %.0f): door %s\n", him.pos[0] / 4096.0,
                    him.pos[1] / 4096.0, opened ? "open" : "shut");
        check(opened, "led to the retinal scanner, he opens the armoury's door");
    }
} // namespace

int main()
{
    const char *valve = std::getenv("HLDS_VALVE_DIR");
    if (valve != nullptr && *valve == '\0')
        valve = nullptr;
    if (valve != nullptr && std::getenv("HLDS_SCENE") != nullptr)
    {
        scene(valve);
        return 0;
    }
    if (valve != nullptr && std::getenv("HLDS_BENCH") != nullptr)
    {
        bench(valve, std::getenv("HLDS_BENCH"));
        return 0;
    }
    if (valve == nullptr)
    {
        std::printf("test_talk: HLDS_VALVE_DIR not set, the game's maps were not read\n");
        return 77;
    }
    else
    {
        testTable(valve);
        testDecline(valve);
        testBusy(valve);
        testTramGuard(valve, 0);
        testTramGuard(valve, 12000);
        testTramGuardFromSave(valve, 1000);
        testTramGuardFromSave(valve, 5000);
        testFollow(valve);
        testSittingScientist(valve);
        testGuard(valve);
        testGuardFights(valve);
        testScriptSync(valve);
        testScriptInterrupt(valve);
        testSentence(valve);
        testFollowerCrosses(valve);
        testRevisit(valve);
        testLaserGuard(valve);
    }
    if (g_failures == 0)
        std::printf("test_talk: ok\n");
    return g_failures == 0 ? 0 : 1;
}
