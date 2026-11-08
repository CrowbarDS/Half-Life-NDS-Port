// What a map shows and shakes (shared/HlEffects.hpp): its sprites, beams,
// explosions, sparks, things thrown out, fades -- on the game's own maps.
//
// The cascade's map (c1a0e) has most of them. Each kind is found there and
// used as the map would use it; a beam that burns and a brush made unseen
// are made here from the map's own, to say exactly what is wanted of them.
//
// Needs HLDS_VALVE_DIR.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "HlLightStyle.hpp"
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
        return game->ok ? std::move(game) : nullptr;
    }

    void tick(Game &g, int ms = 33)
    {
        logicTick(g.logic, g.player, ms);
        monstersTick(g.logic, g.player, ms);
        g.drain();
    }

    int count(const Game &g, LogicKind kind)
    {
        int n = 0;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
            n += g.view.logic[i].kind == static_cast<std::uint8_t>(kind);
        return n;
    }

    void putPlayer(Game &g, const fx *at)
    {
        for (int k = 0; k < 3; ++k)
        {
            g.player.origin[k] = at[k];
            g.player.velocity[k] = 0;
        }
    }

    void testCascade(const char *dir)
    {
        auto game = load(dir, "c1a0e");
        check(game != nullptr, "c1a0e cooks");
        if (!game)
            return;
        Game &g = *game;
        std::printf("test_effects: c1a0e: %d sprites and glows, %d beams, %d explosions, %d shakes, %d sparks, "
                    "%d renders; %u sprite pictures\n",
                    count(g, LogicKind::Sprite), count(g, LogicKind::Beam), count(g, LogicKind::Explosion),
                    count(g, LogicKind::Shake), count(g, LogicKind::Spark), count(g, LogicKind::Render),
                    g.view.spriteCount);
        check(count(g, LogicKind::Sprite) >= 30 && count(g, LogicKind::Beam) >= 15 &&
                  count(g, LogicKind::Explosion) == 12 && count(g, LogicKind::Shake) == 5 &&
                  count(g, LogicKind::Spark) == 6,
              "the cascade's sprites, beams, explosions, shakes and sparks are all cooked");
        check(g.view.spriteCount > static_cast<std::uint32_t>(CommonSprite::Count) &&
                  g.logic.sprites == g.view.sprites,
              "with the pictures they are drawn from");
        // The player far from all of it.
        g.player.origin[2] += fxFromInt(20000);
        for (int n = 0; n < 10; ++n)
            tick(g);

        // Glows are there from the start; a sprite that waits to be switched
        // on is not, until it is.
        int glowsOn = 0, waiting = -1, once = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Sprite))
                continue;
            const bool seen = effectSeen(r, g.states[i]);
            glowsOn += seen && (r.flags & kLogicStartOpen);
            if (!(r.flags & kLogicStartOpen) && r.name != 0)
            {
                check(!seen, "a sprite that waits to be switched on is not seen before");
                if (waiting < 0 && !(r.extra[3] & 1))
                    waiting = static_cast<int>(i);
                if (once < 0 && (r.extra[3] & 1) && r.extra[1] != 0)
                    once = static_cast<int>(i);
            }
        }
        check(glowsOn >= 9, "the glows are lit from the start");
        if (waiting >= 0)
        {
            logicUse(g.logic, g.player, waiting, -1, kUseOn, 0);
            for (int n = 0; n < 30; ++n)
                tick(g);
            check(effectSeen(g.view.logic[waiting], g.states[waiting]) && g.states[waiting].phase > 0,
                  "switched on, it is seen, and its frames go by");
            logicUse(g.logic, g.player, waiting, -1, kUseOff, 0);
            check(!effectSeen(g.view.logic[waiting], g.states[waiting]), "switched off, it is gone");
        }
        if (once >= 0)
        {
            logicUse(g.logic, g.player, once, -1, kUseOn, 0);
            tick(g);
            const bool lit = effectSeen(g.view.logic[once], g.states[once]);
            for (int n = 0; n < 30 * 20 && effectSeen(g.view.logic[once], g.states[once]); ++n)
                tick(g);
            check(lit && !effectSeen(g.view.logic[once], g.states[once]),
                  "one that plays once plays through and goes out");
        }

        // Beams: a strike is seen for its time and then not; one that is
        // switched stays until it is switched off.
        int strike = -1, held = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Beam) || (r.flags & kLogicStartOpen))
                continue;
            if (strike < 0 && !(r.flags & kLogicToggle) && (r.extra[3] & 0xFFFF) != 0 && (r.extra[3] >> 16) >= 200)
                strike = static_cast<int>(i);
            if (held < 0 && (r.flags & kLogicToggle))
                held = static_cast<int>(i);
        }
        check(strike >= 0 || held >= 0, "the map has beams that wait to be set off");
        if (strike >= 0)
        {
            const LogicRecord &r = g.view.logic[strike];
            check(!effectSeen(r, g.states[strike]), "a beam is not there before it strikes");
            logicUse(g.logic, g.player, strike, -1, kUseToggle, 0);
            tick(g);
            const bool seen = effectSeen(r, g.states[strike]);
            const int life = static_cast<int>(r.extra[3] & 0xFFFF);
            const int again = static_cast<int>(r.extra[3] >> 16);
            int lasted = 0;
            for (; lasted < 20000 && effectSeen(r, g.states[strike]); lasted += 33)
                tick(g);
            std::printf("test_effects: a beam's strike of %d ms was seen for %d ms%s\n", life, lasted,
                        again ? ", and it strikes again" : "");
            check(seen && lasted >= life - 40 && lasted <= life + 80, "it strikes, for as long as the map says");
            if (again)
            {
                int waited = 0;
                for (; waited < again + 2000 && !effectSeen(r, g.states[strike]); waited += 33)
                    tick(g);
                check(effectSeen(r, g.states[strike]), "... and again, after the time it says");
            }
        }
        if (held >= 0)
        {
            const LogicRecord &r = g.view.logic[held];
            logicUse(g.logic, g.player, held, -1, kUseOn, 0);
            for (int n = 0; n < 90; ++n)
                tick(g);
            const bool stays = effectSeen(r, g.states[held]);
            logicUse(g.logic, g.player, held, -1, kUseOff, 0);
            check(stays && !effectSeen(r, g.states[held]), "a beam that is switched stays until it is switched off");
        }

        // An explosion: seen, heard, felt -- and, by someone standing at it,
        // suffered. Once, unless it says it can go off again.
        int blast = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount && blast < 0; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Explosion) && !(r.extra[1] & 1) && r.extra[0] >= 40)
                blast = static_cast<int>(i);
        }
        check(blast >= 0, "the map has an explosion that hurts");
        if (blast >= 0)
        {
            const LogicRecord &r = g.view.logic[blast];
            fx beside[3] = {r.move[0] + fxFromInt(40), r.move[1], r.move[2]};
            putPlayer(g, beside);
            g.player.health = 100;
            const int heard = g.times(CommonSound::Explode);
            logicUse(g.logic, g.player, blast, -1, kUseToggle, 0);
            bool burst = false;
            for (const Effect &e : g.actors.effects)
                burst = burst || e.kind == kEffectBurst;
            const bool shaken = g.logic.shakeMs > 0 && g.logic.shakeAmplitude > 0;
            g.drain();
            std::printf("test_effects: an explosion of %u from 40 units: health %d, shake %d ms\n", r.extra[0],
                        g.player.health, g.logic.shakeMs);
            check(g.player.health < 100, "an explosion hurts someone standing at it");
            check((burst || (r.extra[1] & 4)) && shaken && (g.times(CommonSound::Explode) == heard + 1 || (r.extra[1] & 64)),
                  "... and is seen, heard and felt");
            check(g.states[blast].dead == !(r.extra[1] & 2), "it goes off once, unless it says otherwise");
            for (int n = 0; n < 40; ++n)
                tick(g);
            check(g.logic.shakeMs <= 0, "the shaking dies away");
        }

        // A shake by itself.
        int shake = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount && shake < 0; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Shake))
                shake = static_cast<int>(i);
        }
        if (shake >= 0)
        {
            putPlayer(g, g.view.logic[shake].move);
            logicUse(g.logic, g.player, shake, -1, kUseToggle, 0);
            check(g.logic.shakeMs == static_cast<std::int32_t>(g.view.logic[shake].extra[1]) &&
                      g.logic.shakeAmplitude > 0,
                  "an env_shake shakes the view for as long as it says");
        }

        // Sparks: while it is on, now and then, and heard.
        int spark = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount && spark < 0; ++i)
        {
            if (g.view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Spark))
                spark = static_cast<int>(i);
        }
        if (spark >= 0)
        {
            g.states[spark].running = true;
            g.states[spark].phase = 0;
            int seen = 0;
            for (int n = 0; n < 30 * 20; ++n)
            {
                bool before = false;
                for (const Effect &e : g.actors.effects)
                    before = before || (e.kind == kEffectSprite && e.leftMs > 130);
                tick(g);
                bool after = false;
                for (const Effect &e : g.actors.effects)
                    after = after || (e.kind == kEffectSprite && e.leftMs > 130);
                seen += after && !before;
            }
            std::printf("test_effects: a spark of at most %d ms apart sparked %d times in 20 s\n",
                        g.view.logic[spark].waitMs, seen);
            check(seen >= 20000 / (g.view.logic[spark].waitMs + 100) / 2 && seen < 250,
                  "sparks come now and then while it is on");
        }
    }

    int recordNamed(const Game &g, const char *name, LogicKind kind)
    {
        const std::uint32_t hash = nameHash(name);
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(kind) && r.name != 0 && g.view.nameHashes[r.name] == hash)
                return static_cast<int>(i);
        }
        return -1;
    }

    /// c1a1's lasers: the one that follows a train across the computers
    /// stops on the first in its way and burns through it, and what the
    /// computers fire as they break goes off (CLaser::StrikeThink).
    void testLaserBurns(const char *dir)
    {
        auto game = load(dir, "c1a1");
        check(game != nullptr, "c1a1 cooks");
        if (!game)
            return;
        Game &g = *game;
        // Its spilt water (func_friction, modifier 15): a volume where the
        // ground holds them less, and a faint sheet that is seen.
        int puddles = 0;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Friction))
                continue;
            const bool seen = r.brush != kNoBrush &&
                              g.view.brushes[r.brush].renderClass == static_cast<std::uint8_t>(RenderClass::Blend) &&
                              g.view.brushes[r.brush].alpha == 7 && !(g.view.brushes[r.brush].flags & kBrushSolid);
            puddles += r.extra[0] == 15 && seen;
        }
        check(puddles == 2, "c1a1's two puddles: slippery, seen through, and walked through");
        const std::uint32_t follows = nameHash("1_beam_target");
        int laser = -1, quiet = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Beam))
                continue;
            if (r.damage != 0 && r.netName != 0 && g.view.nameHashes[r.netName] == follows)
                laser = static_cast<int>(i);
            else if (r.damage == 0 && quiet < 0)
                quiet = static_cast<int>(i);
        }
        check(laser >= 0, "c1a1's laser follows its train and burns");
        if (laser < 0)
            return;
        check((g.view.logic[laser].maxs[1] & kBeamSparkEnd) != 0, "... and throws sparks where it ends");
        // The computers that set off the explosions, and the explosions.
        const char *const fired[3] = {"cpu_fry1", "cpu_fry2", "cpu_fry3"};
        int computers[3] = {-1, -1, -1};
        for (int c = 0; c < 3; ++c)
        {
            const std::uint32_t hash = nameHash(fired[c]);
            for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
            {
                const LogicRecord &r = g.view.logic[i];
                if (r.kind == static_cast<std::uint8_t>(LogicKind::Breakable) && r.target != 0 &&
                    g.view.nameHashes[r.target] == hash)
                    computers[c] = static_cast<int>(i);
            }
            check(computers[c] >= 0, "a computer fires each of cpu_fry1..3");
        }
        const int blast = recordNamed(g, "cpu_fry2", LogicKind::Explosion);
        const int train = recordNamed(g, "1_beam_target", LogicKind::Train);
        check(blast >= 0 && train >= 0, "the explosion and the train are there");
        if (blast < 0 || train < 0 || computers[0] < 0 || computers[1] < 0 || computers[2] < 0)
            return;
        // (The player well out of its way: upstairs.)
        const fx away[3] = {fxFromInt(1120), fxFromInt(2112), fxFromInt(836)};
        putPlayer(g, away);
        g.player.health = 100;
        logicUse(g.logic, g.player, train, -1, kUseOn, 0);
        bool stopped = false;
        int brokenMs = -1;
        for (int ms = 0; ms < 30000 && brokenMs < 0; ms += 33)
        {
            putPlayer(g, away);
            tick(g);
            if (g.states[laser].hurt != 0)
            {
                fx from[3], to[3];
                beamEnds(g.logic, laser, from, to);
                // (On the computers: a little in from the train's line.)
                stopped = stopped || (to[1] > fxFromInt(1330) && to[1] < fxFromInt(1470));
            }
            bool all = true;
            for (int c : computers)
                all = all && g.states[c].dead;
            if (all)
                brokenMs = ms;
        }
        std::printf("test_effects: c1a1's laser broke the three computers in %d ms\n", brokenMs);
        check(stopped, "the laser stops on the computers in its way");
        check(brokenMs >= 0, "... burns through them");
        check(g.states[blast].dead, "... and what they fire goes off");
        check(g.player.health == 100, "... and does nothing to the player out of its way");
        if (quiet >= 0)
            check(g.states[quiet].hurt == 0, "a beam that does not burn is never cut short");
    }

    /// c1a1's: the door at the foot of the lift shaft that rises (dmg 1000)
    /// crushes whoever it comes into; and a small blast beside the end of
    /// a long computer reaches it, though its middle is out of reach.
    void testCrushAndBlast(const char *dir)
    {
        auto game = load(dir, "c1a1");
        if (!game)
            return;
        Game &g = *game;
        const fx away[3] = {fxFromInt(1120), fxFromInt(2112), fxFromInt(836)};
        putPlayer(g, away);
        g.player.health = 100;
        const int door = recordNamed(g, "vatup", LogicKind::Door);
        int victim = -1;
        for (int i = 0; i < g.actors.count && victim < 0; ++i)
        {
            if (g.actors.states[i].active && g.actors.states[i].kind == static_cast<std::uint8_t>(ActorKind::Scientist))
                victim = i;
        }
        check(door >= 0 && victim >= 0 && g.view.logic[door].damage == 1000, "c1a1's crushing door and a scientist");
        if (door < 0 || victim < 0)
            return;
        ActorState &actor = g.actors.states[victim];
        const fx above[3] = {fxFromInt(728), fxFromInt(2112), fxFromInt(-818)};
        for (int k = 0; k < 3; ++k)
            actor.pos[k] = above[k];
        actor.groundMover = -1;
        actor.airborne = false;
        actor.hurtMs = 0;
        // (The place over it that hurts is taken away first, as the map
        // does: kill_pain_relay.)
        const int pain = recordNamed(g, "kill_pain", LogicKind::TriggerHurt);
        if (pain >= 0)
            logicKill(g.logic, g.view.logic[pain].name);
        logicUse(g.logic, g.player, door, -1, kUseOn, 0);
        for (int n = 0; n < 30; ++n)
        {
            putPlayer(g, away);
            tick(g);
        }
        std::printf("test_effects: c1a1's rising door: whoever was over it %s\n",
                    !actor.active || actor.ai >= kAiDying ? "is dead" : "still stands");
        check(!actor.active || actor.ai >= kAiDying, "a door that crushes kills whoever it comes into");

        // The computer that fires cpu_fry2: 54 x 109, its middle 74 in from
        // its end at y 1339. A blast of 20 (reach 50) 20 short of that end.
        int computer = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Breakable) && r.target != 0 &&
                g.view.nameHashes[r.target] == nameHash("cpu_fry2"))
                computer = static_cast<int>(i);
        }
        check(computer >= 0, "the computer that fires cpu_fry2");
        if (computer < 0)
            return;
        const std::int32_t before = g.states[computer].phase;
        const fx beside[3] = {fxFromInt(715), fxFromInt(1319), fxFromInt(-77)};
        explodeAt(g.logic, g.player, beside, 20, false, false, true);
        std::printf("test_effects: a blast of 20 at the end of a computer: %d damage taken\n",
                    g.states[computer].phase - before);
        check(g.states[computer].phase > before, "a blast reaches a long brush near its end");
    }

    /// Made from the map's own: a laser that burns, something thrown out, a
    /// brush made unseen, a fade.
    void testMade(const char *dir)
    {
        auto game = load(dir, "c1a0e");
        if (!game)
            return;
        Game &g = *game;
        std::vector<LogicRecord> records(g.view.logic, g.view.logic + g.view.logicCount);
        g.logic.records = records.data();
        int beam = -1, brush = -1;
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            if (beam < 0 && records[i].kind == static_cast<std::uint8_t>(LogicKind::Beam))
                beam = static_cast<int>(i);
            if (brush < 0 && records[i].brush != kNoBrush && records[i].name != 0)
                brush = static_cast<int>(i);
        }
        if (beam < 0 || brush < 0)
            return;
        // A laser across a place, on: ten a second to whoever stands in it,
        // nothing to someone beside it.
        LogicRecord &laser = records[beam];
        laser.flags |= kLogicToggle;
        laser.damage = 10;
        laser.move[0] = fxFromInt(0);
        laser.move[1] = fxFromInt(-200);
        laser.move[2] = fxFromInt(9000);
        laser.mins[0] = 0;
        laser.mins[1] = 200;
        laser.mins[2] = 9000;
        g.states[beam] = LogicState();
        g.states[beam].running = true;
        const fx in[3] = {fxFromInt(4), 0, fxFromInt(9000)}, beside[3] = {fxFromInt(60), 0, fxFromInt(9000)};
        putPlayer(g, beside);
        g.player.health = 100;
        for (int n = 0; n < 60; ++n)
            effectsTick(g.logic, g.player, 33);
        check(g.player.health == 100, "a laser does nothing to someone beside it");
        putPlayer(g, in);
        for (int n = 0; n < 60; ++n)
            effectsTick(g.logic, g.player, 33);
        std::printf("test_effects: two seconds in a laser of 10 a second: health %d\n", g.player.health);
        check(g.player.health <= 82 && g.player.health >= 78, "... and its damage a second to someone in it");
        g.states[beam].running = false;
        const int then = g.player.health;
        for (int n = 0; n < 30; ++n)
            effectsTick(g.logic, g.player, 33);
        check(g.player.health == then, "... and nothing once it is off");

        // Something thrown out: so many, so far apart; they fall and are gone.
        LogicRecord &shooter = records[beam];
        shooter = LogicRecord();
        shooter.kind = static_cast<std::uint8_t>(LogicKind::Shooter);
        shooter.brush = kNoBrush;
        // (Where the map starts the player: somewhere with air in it.)
        const fx air[3] = {fxFromInt(1500), fxFromInt(-160), fxFromInt(-300)};
        for (int k = 0; k < 3; ++k)
            shooter.move[k] = air[k];
        shooter.mins[2] = 1000;
        shooter.extra[0] = 4;
        shooter.extra[1] = 200;
        shooter.extra[2] = 300;
        shooter.extra[3] = 1500;
        g.states[beam] = LogicState();
        for (Projectile &p : g.actors.shots)
            p.active = false;
        logicUse(g.logic, g.player, beam, -1, kUseToggle, 0);
        int most = 0, thrown = 0;
        bool rose = false;
        for (int n = 0; n < 120; ++n)
        {
            tick(g);
            int now = 0;
            for (const Projectile &p : g.actors.shots)
            {
                now += p.active && p.kind == kShotGib;
                rose = rose || (p.active && p.pos[2] > air[2] + fxFromInt(10));
            }
            thrown += now > most ? now - most : 0;
            most = now > most ? now : most;
        }
        int left = 0;
        for (const Projectile &p : g.actors.shots)
            left += p.active;
        std::printf("test_effects: a shooter of 4: %d in the air at once, %d left, %s\n", most, left,
                    g.states[beam].dead ? "done" : "not done");
        check(most >= 3 && most <= 4 && rose && left == 0 && g.states[beam].dead,
              "a shooter throws out what it has, one after another; they fly, and are gone");

        // A brush made unseen, and seen again.
        LogicRecord &render = records[beam];
        render = LogicRecord();
        render.kind = static_cast<std::uint8_t>(LogicKind::Render);
        render.brush = kNoBrush;
        render.target = records[brush].name;
        render.extra[0] = 0;
        g.states[beam] = LogicState();
        const std::uint16_t which = records[brush].brush;
        check(g.brushStates[which].look == 0, "a brush is drawn as it was made");
        logicUse(g.logic, g.player, beam, -1, kUseToggle, 0);
        check(g.brushStates[which].look == kBrushUnseen, "an env_render of nothing makes it unseen");
        render.extra[0] = 128;
        logicUse(g.logic, g.player, beam, -1, kUseToggle, 0);
        check(g.brushStates[which].look >= 14 && g.brushStates[which].look <= 17, "... of half, half see-through");
        render.extra[0] = 255;
        logicUse(g.logic, g.player, beam, -1, kUseToggle, 0);
        check(g.brushStates[which].look == 0, "... and of all of it, as it was made again");

        // A fade: into the dark over its time, held, and let go.
        LogicRecord &fade = records[beam];
        fade = LogicRecord();
        fade.kind = static_cast<std::uint8_t>(LogicKind::Fade);
        fade.brush = kNoBrush;
        fade.extra[0] = 1000;
        fade.extra[1] = 500;
        fade.extra[2] = 255;
        g.states[beam] = LogicState();
        logicUse(g.logic, g.player, beam, -1, kUseToggle, 0);
        int half = -1, full = -1;
        for (int ms = 0; ms < 2000; ms += 25)
        {
            effectsTick(g.logic, g.player, 25);
            if (ms == 500)
                half = g.logic.fadeLevel;
            if (ms == 1200)
                full = g.logic.fadeLevel;
        }
        check(half >= 120 && half <= 136 && full == 255 && g.logic.fadeLevel == 0 && !g.logic.fading &&
                  g.logic.fadeColour.value == 0,
              "a fade goes dark over its time, holds, and lets go (black, from a map cooked without its colour)");
        // From the colour: lifting at once, as the game's does -- its hold
        // keeps nothing dark.
        fade.extra[3] = 1 | (31u << 10 | 31u) << 8 | 0x80000000u; // from magenta
        logicUse(g.logic, g.player, beam, -1, kUseToggle, 0);
        const int start = g.logic.fadeLevel;
        for (int ms = 0; ms < 250; ms += 25)
            effectsTick(g.logic, g.player, 25);
        const int quarter = g.logic.fadeLevel;
        for (int ms = 0; ms < 2000; ms += 25)
            effectsTick(g.logic, g.player, 25);
        check(start == 255 && quarter >= 184 && quarter <= 198 && g.logic.fadeLevel == 0 && !g.logic.fading,
              "... or starts covered and clears from the first moment");
        check(g.logic.fadeColour.value == (31u << 10 | 31u), "... in its own colour");
    }

    /// Beams whose ends are not fixed: an end that follows something that
    /// moves; a strike sent wherever the world stops it; an end picked from
    /// several places of one name.
    void testBeamEnds(const char *dir)
    {
        auto game = load(dir, "c1a0e");
        if (!game)
            return;
        Game &g = *game;
        int following = 0, random = 0, follower = -1, striker = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Beam))
                continue;
            if (r.target != 0 || r.netName != 0)
            {
                ++following;
                follower = follower < 0 ? static_cast<int>(i) : follower;
            }
            if (r.maxs[1] & kBeamRandom)
            {
                ++random;
                striker = striker < 0 ? static_cast<int>(i) : striker;
            }
        }
        std::printf("test_effects: c1a0e: %d beams follow something, %d strike at random\n", following, random);
        check(following == 10 && random == 8, "the cascade's beams on its trains and cart, and its random strikes");
        if (follower >= 0)
        {
            // Wherever what it follows is, there its end is; and when that
            // is moved, the end has moved with it.
            const LogicRecord &r = g.view.logic[follower];
            const std::uint16_t name = r.netName != 0 ? r.netName : r.target;
            fx from[3], to[3], where[3];
            beamEnds(g.logic, follower, from, to);
            const bool found = placeOfName(g.logic, name, where);
            const fx *end = r.netName != 0 ? to : from;
            check(found && end[0] == where[0] && end[1] == where[1] && end[2] == where[2],
                  "a beam's end that follows something is where that is");
            for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
            {
                if (g.view.logic[i].name != name || g.view.logic[i].brush == kNoBrush)
                    continue;
                const std::uint16_t brush = g.view.logic[i].brush;
                fx moved[3] = {g.brushStates[brush].pos[0] + fxFromInt(48), g.brushStates[brush].pos[1],
                               g.brushStates[brush].pos[2] + fxFromInt(16)};
                logicMoveBrushTo(g.logic, brush, moved);
                fx after[3], other[3];
                beamEnds(g.logic, follower, r.netName != 0 ? other : after, r.netName != 0 ? after : other);
                check(after[0] == end[0] + fxFromInt(48) && after[2] == end[2] + fxFromInt(16),
                      "... and goes with it when it moves");
                break;
            }
        }
        if (striker >= 0)
        {
            // Twenty strikes: each ends on something, within its reach and
            // not nearer than a tenth of it; and they are not all one place.
            const LogicRecord &r = g.view.logic[striker];
            const int reach = r.maxs[0];
            int good = 0, struck = 0;
            fx first[3] = {0, 0, 0};
            bool varied = false;
            for (int n = 0; n < 20; ++n)
            {
                if (!beamAim(g.logic, striker))
                    continue;
                ++struck;
                fx from[3], to[3];
                beamEnds(g.logic, striker, from, to);
                const fx d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
                const int far = fxToInt(fxLength(d));
                // (On something: a little further on is not open.)
                const fx beyond[3] = {to[0] + d[0] / 64, to[1] + d[1] / 64, to[2] + d[2] / 64};
                good += far <= reach + 1 && far >= reach / 10 - 1 && !lineClear(g.move, from, beyond);
                if (struck == 1)
                    for (int k = 0; k < 3; ++k)
                        first[k] = to[k];
                varied = varied || to[0] != first[0] || to[1] != first[1];
            }
            std::printf("test_effects: a random strike of reach %d: %d of 20 found somewhere, %d of them on something "
                        "in reach\n",
                        reach, struck, good);
            check(struck >= 5 && good == struck && varied,
                  "a strike at random lands on something within its reach, somewhere else each time");
        }
        // Several places of one name (c1a1f has such a beam): each strike is
        // to one of them, and not always the same.
        auto other = load(dir, "c1a1f");
        if (!other)
            return;
        Game &f = *other;
        for (std::uint32_t i = 0; i < f.view.logicCount; ++i)
        {
            const LogicRecord &r = f.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Beam) || r.auxCount < 2)
                continue;
            int seen[16] = {0}, kinds = 0;
            bool listed = true;
            for (int n = 0; n < 60; ++n)
            {
                beamAim(f.logic, static_cast<int>(i));
                fx from[3], to[3];
                beamEnds(f.logic, static_cast<int>(i), from, to);
                int which = -1;
                for (int k = 0; k < r.auxCount; ++k)
                {
                    const LogicAux &place = f.view.logicAux[r.firstAux + k];
                    if (fxToInt(to[0]) == static_cast<std::int16_t>(place.target) &&
                        fxToInt(to[1]) == static_cast<std::int16_t>(place.pad))
                        which = k;
                }
                listed = listed && which >= 0;
                if (which >= 0 && seen[which]++ == 0)
                    ++kinds;
            }
            std::printf("test_effects: c1a1f: a beam with %d places to end at went to %d of them in 60 strikes\n",
                        r.auxCount, kinds);
            check(r.auxCount == 5 && listed && kinds >= 3, "a beam with several places to end at picks among them");
            break;
        }
    }

    /// Everything from the first map to the soldiers: how much of what the
    /// maps show is now cooked.
    void testCampaign(const char *dir)
    {
        static const char *const kMaps[] = {"c0a0", "c0a0e", "c1a0", "c1a0d", "c1a0c", "c1a1", "c1a1f",
                                            "c1a2", "c1a2b", "c1a3", "c1a3a", "c1a4"};
        int sprites = 0, beams = 0, others = 0, pictures = 0, missing = 0;
        for (const char *map : kMaps)
        {
            auto game = load(dir, map);
            if (!game)
                continue;
            sprites += count(*game, LogicKind::Sprite);
            beams += count(*game, LogicKind::Beam);
            others += count(*game, LogicKind::Explosion) + count(*game, LogicKind::Shake) +
                      count(*game, LogicKind::Spark) + count(*game, LogicKind::Shooter) +
                      count(*game, LogicKind::Render) + count(*game, LogicKind::Fade);
            for (std::uint32_t i = 0; i < game->view.logicCount; ++i)
            {
                const LogicRecord &r = game->view.logic[i];
                if (r.kind == static_cast<std::uint8_t>(LogicKind::Sprite) ||
                    r.kind == static_cast<std::uint8_t>(LogicKind::Beam))
                {
                    ++pictures;
                    missing += r.extra[0] >= game->view.spriteCount;
                }
            }
            // Ten seconds of each by itself: nothing goes wrong.
            game->player.origin[2] += fxFromInt(20000);
            for (int n = 0; n < 300; ++n)
                tick(*game);
        }
        std::printf("test_effects: twelve maps: %d sprites and glows, %d beams, %d other effects\n", sprites, beams,
                    others);
        check(sprites > 100 && beams > 40 && others > 60 && pictures == sprites + beams && missing == 0,
              "the maps' effects are cooked, each with a picture the map has");
    }
    /// What a crate has in it (its "spawnobject"): c1a2a's two give a clip
    /// of 9mm and a medical kit. Not there until it breaks; then where it
    /// was, on the floor.
    void testCrateDrops(const char *dir)
    {
        auto game = load(dir, "c1a2a");
        if (!game)
            return;
        Game &g = *game;
        const auto itemsOf = [&](std::uint32_t kind)
        {
            int n = 0;
            for (int k = 0; k < g.actors.count && k < g.actors.recordCount; ++k)
                n += g.actors.states[k].active && g.actors.records[k].kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                     g.actors.records[k].item == kind;
            return n;
        };
        int crates = 0, spilled = 0, onFloor = 0;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Breakable) || r.pad == 0 || r.brush == kNoBrush)
                continue;
            ++crates;
            const int before = itemsOf(r.pad);
            logicStrike(g.logic, g.player, static_cast<int>(i), 100000);
            spilled += itemsOf(r.pad) == before + 1;
            const Brush &b = g.view.brushes[r.brush];
            const fx mid[2] = {fxFromInt(b.mins[0] + b.maxs[0]) / 2, fxFromInt(b.mins[1] + b.maxs[1]) / 2};
            for (int k = 0; k < g.actors.count && k < g.actors.recordCount; ++k)
            {
                const ActorState &item = g.actors.states[k];
                if (!item.active || g.actors.records[k].kind != static_cast<std::uint8_t>(ActorKind::Item) ||
                    g.actors.records[k].item != r.pad)
                    continue;
                const fx d[3] = {item.pos[0] - mid[0], item.pos[1] - mid[1], 0};
                onFloor += fxLength(d) < fxFromInt(2) && item.pos[2] <= fxFromInt(b.mins[2]) + fxFromInt(2);
            }
        }
        std::printf("test_effects: c1a2a: %d crates with something in them; %d gave it, %d of those on the floor below\n",
                    crates, spilled, onFloor);
        check(crates == 2 && spilled == 2, "c1a2a's two crates with something in them give it when they break");
        check(onFloor == 2, "... where the crate was, on the floor");
    }
    /// c1a1c's wooden boxes, at the crowbar's pace (crowbar.cpp: its full
    /// 10 after a second's rest, then half while it swings on): breakables
    /// take twice a crowbar's blow, so 45 is four blows, 60 five, 125 twelve.
    void testCrateHits(const char *dir)
    {
        auto game = load(dir, "c1a1c");
        if (!game)
            return;
        Game &g = *game;
        int checked = 0, right = 0;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind != static_cast<std::uint8_t>(LogicKind::Pushable) || !(r.flags & kLogicBreaks))
                continue;
            const int health = static_cast<int>(r.extra[0]);
            const int want = health == 45 ? 4 : (health == 60 ? 5 : (health == 125 ? 12 : -1));
            if (want < 0)
                continue;
            int blows = 0;
            while (!g.states[i].dead && blows < 50)
                logicStrike(g.logic, g.player, static_cast<int>(i), blows++ == 0 ? 10 : 5, kDmgClub, true);
            ++checked;
            right += blows == want;
            if (blows != want)
                std::printf("test_effects: c1a1c: a box of %d took %d blows (the game's %d)\n", health, blows, want);
        }
        std::printf("test_effects: c1a1c: %d wooden boxes broken by the crowbar, %d in the game's blows\n", checked, right);
        check(checked >= 8 && right == checked, "c1a1c's boxes take the game's crowbar blows: 4, 5 and 12");
    }
    /// A light something switches shines by its pattern while it is on
    /// (lights.cpp): c1a1f's freak_zombie_light, "az", dark and twice as
    /// bright by turns, ten a second; off, dark.
    void testLightPatterns(const char *dir)
    {
        auto game = load(dir, "c1a1f");
        if (!game)
            return;
        Game &g = *game;
        int light = -1;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            if (r.kind == static_cast<std::uint8_t>(LogicKind::Light) && r.name != 0 &&
                g.view.nameHashes[r.name] == nameHash("freak_zombie_light"))
                light = static_cast<int>(i);
        }
        check(light >= 0, "c1a1f's freak_zombie_light");
        if (light < 0)
            return;
        const int style = static_cast<int>(g.view.logic[light].extra[0]);
        logicTick(g.logic, g.player, 33);
        const auto value = [&](std::uint32_t tick)
        { return lightStyleValue(style, tick, g.logic.stylesOff, g.logic.stylePattern.value.data()); };
        std::printf("test_effects: c1a1f's freak_zombie_light (style %d): %d then %d\n", style, value(0), value(1));
        check(value(0) == 0 && value(1) == 25 * kStyleWhole / 12, "on, a switched light shines by its pattern: a, z");
        logicFire(g.logic, g.player, g.view.logic[light].name, -1, kUseToggle, 0);
        logicTick(g.logic, g.player, 33);
        check(value(0) == 0 && value(1) == 0, "... and off, it is dark");
    }
    /// c0a0 begins in the dark (its worldspawn's "startdark"): black from
    /// the first moment -- not the tram for a third of a second, and then
    /// black -- until the map's own fade (fade_in, at 0.3 s, which takes the
    /// screen's one fade over) brings the picture up out of it.
    void testStartDark(const char *dir)
    {
        auto game = load(dir, "c0a0");
        if (!game)
            return;
        Game &g = *game;
        int at[4] = {-1, -1, -1, -1};
        const int when[4] = {200, 4500, 5400, 6200};
        // (Before the map's own fade, at 0.3 s.)
        int ms = 0;
        for (int k = 0; k < 4; ++k)
        {
            while (ms < when[k])
            {
                logicTick(g.logic, g.player, 20);
                monstersTick(g.logic, g.player, 20);
                g.drain();
                ms += 20;
                if (std::getenv("HLDS_DEBUG_FADE") && ms % 200 == 0)
                    std::printf("  %d ms: fading %d from %d ms %d whole %d level %d\n", ms, g.logic.fading, g.logic.fadeFrom,
                                g.logic.fadeMs, g.logic.fadeWholeMs, g.logic.fadeLevel);
            }
            at[k] = g.logic.fadeLevel;
        }
        std::printf("test_effects: c0a0's dark: %d, %d, %d, %d (at 0.2, 4.5, 5.4 and 6.2 s)\n", at[0], at[1], at[2], at[3]);
        check(at[0] == 255, "c0a0 begins black, from its first moment");
        check(at[1] == 0 && at[3] == 0, "... and its own fade brings the picture up out of it");
    }
    /// c0a0 begins with its music (its worldspawn's "sounds" 3): the tram
    /// ride's track under the announcer, asked for as the map starts, by
    /// nothing the player walks into. c0a0b's "sounds" 0 asks for nothing:
    /// the ride's music goes on until c0a0d stops it.
    void testStartMusic(const char *dir)
    {
        auto game = load(dir, "c0a0");
        if (!game)
            return;
        Game &g = *game;
        int asked = -1;
        for (int ms = 0; ms < 1000 && asked < 0; ms += 20)
        {
            logicTick(g.logic, g.player, 20);
            asked = g.logic.musicRequest;
        }
        bool touchable = false;
        for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
        {
            const LogicRecord &r = g.view.logic[i];
            touchable = touchable || (r.kind == static_cast<std::uint8_t>(LogicKind::Music) && r.extra[0] == 3 &&
                                      (r.flags & kLogicTouch));
        }
        std::printf("test_effects: c0a0's music: track %d asked for as it begins\n", asked);
        check(asked == 3 && !touchable, "c0a0 begins with its track, 3, by nothing walked into");
        auto next = load(dir, "c0a0b");
        if (!next)
            return;
        int music = 0;
        for (std::uint32_t i = 0; i < next->view.logicCount; ++i)
            music += next->view.logic[i].kind == static_cast<std::uint8_t>(LogicKind::Music);
        check(music == 0, "... and c0a0b, its \"sounds\" 0, asks for nothing");
    }
    /// The game's rule for what of the world is drawn, which the DSi keeps
    /// (WorldDraw): a node gone through has its own surfaces drawn, whether
    /// or not a seen leaf lists them. Through the gap of c1a1's broken sign
    /// the box's far wall shows: listed by no leaf that can be seen from
    /// where the player stands, but on a node that is gone through. And what
    /// that costs, map by map, at worst (with no view cone, no facing: more
    /// than it ever is).
    void testNodeRule(const char *dir)
    {
        for (const char *map : {"c1a1", "c0a0", "c1a0", "c1a2"})
        {
            auto game = load(dir, map);
            if (!game)
                continue;
            Game &g = *game;
            const LevelView &v = g.view;
            check(v.nodeSpans != nullptr && v.nodeSpanCount == v.nodeCount,
                  "the level has its nodes' own surfaces");
            const auto drawn = [&](int eyeLeaf, std::vector<char> &byLeaf, std::vector<char> &byNode)
            {
                byLeaf.assign(v.surfaceCount, 0);
                byNode.assign(v.surfaceCount, 0);
                std::vector<char> nodeSeen(v.nodeCount, 0);
                for (std::uint32_t l = 1; l < v.leafCount; ++l)
                {
                    if (!leafSees(g.move, eyeLeaf, static_cast<int>(l)))
                        continue;
                    for (int m = 0; m < v.leaves[l].markCount; ++m)
                        byLeaf[v.marks[v.leaves[l].firstMark + m]] = 1;
                    for (std::uint16_t n = v.leaves[l].parent; n != kNoParent && !nodeSeen[n]; n = v.nodes[n].parent)
                        nodeSeen[n] = 1;
                }
                for (std::uint32_t n = 0; n < v.nodeCount; ++n)
                {
                    if (!nodeSeen[n])
                        continue;
                    for (int m = 0; m < v.nodeSpans[n].count; ++m)
                        byNode[v.nodeMarks[v.nodeSpans[n].first + m]] = 1;
                }
            };
            if (std::string(map) == "c1a1")
            {
                const fx eye[3] = {fxFromInt(744), fxFromInt(2450), fxFromInt(792)};
                std::vector<char> byLeaf, byNode;
                drawn(worldLeaf(g.move, eye), byLeaf, byNode);
                int wall = -1;
                for (std::uint32_t i = 0; i < v.surfaceCount; ++i)
                {
                    const Surface &sf = v.surfaces[i];
                    if (sf.texture != kNoTexture && std::abs(sf.center[0] - 928) < 40 && std::abs(sf.center[1] - 2044) < 8 &&
                        std::abs(sf.center[2] - 796) < 40 && std::string(v.textures[sf.texture].name) == "c1a1w1m")
                        wall = static_cast<int>(i);
                }
                check(wall >= 0, "c1a1's sign box has its far wall");
                if (wall >= 0)
                {
                    std::printf("test_effects: c1a1: the sign box's far wall from by the sign: by a leaf %d, by a node %d\n",
                                byLeaf[wall], byNode[wall]);
                    check(!byLeaf[wall] && byNode[wall], "seen through the sign's gap: no seen leaf lists it, a node it is on does");
                }
            }
            // The cost: what more the rule draws, eye leaf by eye leaf.
            std::vector<int> extra;
            for (std::uint32_t l = 1; l < v.leafCount; l += 4)
            {
                if (v.leaves[l].contents == kContentsSolid)
                    continue;
                std::vector<char> byLeaf, byNode;
                drawn(static_cast<int>(l), byLeaf, byNode);
                int polygons = 0;
                for (std::uint32_t i = 0; i < v.surfaceCount; ++i)
                    polygons += byNode[i] && !byLeaf[i] ? v.surfaces[i].polygons : 0;
                extra.push_back(polygons);
            }
            std::sort(extra.begin(), extra.end());
            if (!extra.empty())
                std::printf("test_effects: %s: the node rule's polygons more, at most (no view cone): median %d, p90 %d, worst %d\n",
                            map, extra[extra.size() / 2], extra[extra.size() * 9 / 10], extra.back());
        }
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_effects: HLDS_VALVE_DIR not set, the maps were not played\n");
        return 77;
    }
    testCascade(dir);
    testMade(dir);
    testBeamEnds(dir);
    testLaserBurns(dir);
    testCrushAndBlast(dir);
    testCampaign(dir);
    testCrateDrops(dir);
    testCrateHits(dir);
    testLightPatterns(dir);
    testStartDark(dir);
    testStartMusic(dir);
    testNodeRule(dir);
    if (g_failures == 0)
        std::printf("test_effects: ok\n");
    return g_failures == 0 ? 0 : 1;
}
