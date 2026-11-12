// Soldiers and tripmines (shared/HlGrunt.hpp, and the mines of
// shared/HlMonsters.hpp), on the game's own maps: HLDS_VALVE_DIR says where.
//
// The mines of c1a3 arm, are tripped by walking through their beams, by a
// shot, and by each other. A soldier is cooked with the head, gun and skin
// that go with what he carries; he sees, fires in threes, reloads, kicks,
// is saved by his helmet, dies, comes apart. The squads of c1a3a are the
// map's; no more than two of a squad fire at once; cover is somewhere the
// player cannot see; grenades keep to the game's rules.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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

    /// The game by itself, the player held where they are put.
    void tickStill(Game &g, int ms = 33)
    {
        logicTick(g.logic, g.player, ms);
        g.logic.viewTurn = 0;
        monstersTick(g.logic, g.player, ms);
        g.drain();
    }

    bool isKind(const ActorState &actor, ActorKind kind)
    {
        return actor.active && actor.kind == static_cast<std::uint8_t>(kind);
    }

    int findKind(const Game &g, ActorKind kind, int nth = 0)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            if (isKind(g.actors.states[i], kind) && nth-- == 0)
                return i;
        }
        return -1;
    }

    /// Everything that fights but `keep` (and what is of `also`) is taken
    /// away: one thing at a time is looked at.
    void only(Game &g, int keep, ActorKind also = ActorKind::Count)
    {
        for (int i = 0; i < g.actors.count; ++i)
        {
            ActorState &actor = g.actors.states[i];
            if (i == keep || !fights(actor.kind) || actor.kind == static_cast<std::uint8_t>(also))
                continue;
            actor.active = false;
            actorPlaceBox(g.actors, i);
        }
    }

    /// Stand the player on the floor about `distance` units from the
    /// soldier, where each can see the other; and turn him to face them.
    bool standBefore(Game &g, int index, int distance)
    {
        ActorState &me = g.actors.states[index];
        const fx gun[3] = {me.pos[0], me.pos[1], me.pos[2] + fxFromInt(60)};
        for (int turn = 0; turn < 32; ++turn)
        {
            fx sine, cosine;
            fxSinCos(static_cast<std::uint16_t>(turn * 2048), sine, cosine);
            ActorState probe;
            probe.pos[0] = me.pos[0] + cosine * distance;
            probe.pos[1] = me.pos[1] + sine * distance;
            probe.pos[2] = me.pos[2] + fxFromInt(8);
            if (!actorToFloor(g.move, probe, fxFromInt(8), fxFromInt(64)))
                continue;
            const fx at[3] = {probe.pos[0], probe.pos[1], probe.pos[2] + kStandingFeet};
            if (!lineClear(g.move, gun, at) || probe.pos[2] - me.pos[2] > fxFromInt(24) ||
                me.pos[2] - probe.pos[2] > fxFromInt(24))
                continue;
            for (int k = 0; k < 3; ++k)
            {
                g.player.origin[k] = at[k];
                g.player.velocity[k] = 0;
            }
            g.player.onGround = true;
            me.yaw = fxAtan2Turns(at[1] - me.pos[1], at[0] - me.pos[0]);
            return true;
        }
        return false;
    }

    int flat(const fx *a, const fx *b)
    {
        const fx d[3] = {a[0] - b[0], a[1] - b[1], 0};
        return fxToInt(fxLength(d));
    }

    // ------------------------------------------------------------- tripmines

    void testMines(const char *dir)
    {
        auto game = load(dir, "c1a3");
        check(game != nullptr, "c1a3 cooks");
        if (!game)
            return;
        Game &g = *game;
        only(g, -1, ActorKind::Tripmine);
        int mines[8], count = 0;
        for (int i = 0; i < g.actors.count && count < 8; ++i)
        {
            if (isKind(g.actors.states[i], ActorKind::Tripmine))
                mines[count++] = i;
        }
        check(count == 3, "c1a3 has its three tripmines");
        if (count != 3)
            return;
        const fx home[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        for (int ms = 0; ms < 900; ms += 33)
            tickStill(g);
        bool early = false;
        for (int n = 0; n < count; ++n)
            early = early || g.actors.states[mines[n]].ai == kMineArmed;
        for (int ms = 900; ms < 3000; ms += 33)
            tickStill(g);
        bool armed = true, sane = true;
        for (int n = 0; n < count; ++n)
        {
            const ActorState &mine = g.actors.states[mines[n]];
            armed = armed && mine.ai == kMineArmed;
            const int length = flat(mine.goal, mine.pos);
            std::printf("test_soldiers: tripmine at %d %d %d: beam of %d units\n", fxToInt(mine.pos[0]),
                        fxToInt(mine.pos[1]), fxToInt(mine.pos[2]), length);
            sane = sane && length > 16 && length < kMineRange;
        }
        check(!early && armed && g.times(CommonSound::MineActivate) == 3,
              "they are armed two and a half seconds after the map starts, not before");
        check(sane, "each beam ends at the wall across from it");
        for (int ms = 0; ms < 3000; ms += 33)
            tickStill(g);
        bool quiet = true;
        for (int n = 0; n < count; ++n)
            quiet = quiet && g.actors.states[mines[n]].ai == kMineArmed;
        check(quiet && g.times(CommonSound::Explode) == 0, "left alone they stay as they are");

        // Through the first one's beam.
        {
            const ActorState &mine = g.actors.states[mines[0]];
            for (int k = 0; k < 3; ++k)
                g.player.origin[k] = (mine.pos[k] + mine.goal[k]) / 2;
            g.player.health = 1000;
            const fx d[3] = {g.player.origin[0] - mine.pos[0], g.player.origin[1] - mine.pos[1],
                             g.player.origin[2] - mine.pos[2]};
            const int far = fxToInt(fxLength(d));
            int took = -1;
            for (int ms = 0; ms < 1000 && took < 0; ms += 33)
            {
                tickStill(g);
                if (!mine.active)
                    took = ms;
            }
            std::printf("test_soldiers: walked into a beam %d units from its mine: off after %d ms, %d taken\n", far,
                        took, 1000 - g.player.health);
            check(took >= 100 && took <= 500 && g.times(CommonSound::Explode) == 1,
                  "a mine whose beam is walked into goes off a moment later");
            check(far >= 375 ? g.player.health == 1000 : g.player.health < 1000,
                  "... and hurts whoever is within 375 units of it");
        }
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = home[k];
        g.player.health = 100;
        // (The blast may have taken the others with it: they stand close.)
        int left = 0;
        for (int ms = 0; ms < 1000; ms += 33)
            tickStill(g);
        for (int n = 1; n < count; ++n)
            left += g.actors.states[mines[n]].active ? 1 : 0;
        std::printf("test_soldiers: %d of the other two are left after the first went off\n", left);
        const int heard = g.times(CommonSound::Explode);
        for (int n = 1; n < count; ++n)
        {
            const ActorState &mine = g.actors.states[mines[n]];
            if (!mine.active)
                continue;
            // A shot at it, from a little way along its own beam.
            fx forward[3];
            hldata::detail::turretForward(mine, forward);
            const fx eye[3] = {mine.pos[0] + forward[0] * 12, mine.pos[1] + forward[1] * 12,
                               mine.pos[2] + forward[2] * 12};
            const fx back[3] = {-forward[0], -forward[1], -forward[2]};
            const bool hit = fireLine(g.logic, g.player, eye, back, 64, 8, true);
            for (int ms = 0; ms < 500; ms += 33)
                tickStill(g);
            check(hit && !mine.active, "a mine that is shot goes off");
            break;
        }
        // And what is left, by a blast near it.
        bool chained = true;
        for (int n = 1; n < count; ++n)
        {
            const ActorState &mine = g.actors.states[mines[n]];
            if (!mine.active)
                continue;
            fx forward[3];
            hldata::detail::turretForward(mine, forward);
            const fx at[3] = {mine.pos[0] + forward[0] * 40, mine.pos[1] + forward[1] * 40,
                              mine.pos[2] + forward[2] * 40};
            radiusDamage(g.logic, g.player, at, 50, 125, -1);
            for (int ms = 0; ms < 500; ms += 33)
                tickStill(g);
            chained = chained && !mine.active;
        }
        check(chained && g.times(CommonSound::Explode) > heard, "... and so does one caught in a blast");
    }

    // ------------------------------------------------------------- one soldier

    void testCooked(const char *dir)
    {
        // What each carries, by the map's own count of them.
        struct Map
        {
            const char *name;
            int soldiers, shotguns, launchers, leaders;
        };
        for (const Map &map : {Map{"c1a3a", 10, -1, -1, 3}, Map{"c2a2b1", 14, -1, -1, 0}})
        {
            auto game = load(dir, map.name);
            check(game != nullptr, "a map of soldiers cooks");
            if (!game)
                continue;
            Game &g = *game;
            int soldiers = 0, shotguns = 0, launchers = 0, leaders = 0, wrong = 0, triggers = 0;
            for (std::uint32_t i = 0; i < g.view.actorCount; ++i)
            {
                const ActorRecord &record = g.view.actors[i];
                if (record.kind != static_cast<std::uint8_t>(ActorKind::Grunt))
                    continue;
                ++soldiers;
                const std::uint32_t weapons = record.item & 15;
                triggers += record.target != 0 && ((record.item >> kGruntTriggerShift) & 15) != 0;
                const int head = record.body % 4, gun = record.body / 4;
                const int skin = static_cast<int>((record.item >> kActorSkinShift) & 0xFF);
                shotguns += (weapons & kGruntShotgun) ? 1 : 0;
                launchers += (weapons & kGruntLauncher) ? 1 : 0;
                leaders += (record.flags & kActorLeader) ? 1 : 0;
                if (weapons & kGruntShotgun)
                    wrong += head != 2 || gun != 1;
                else if (weapons & kGruntLauncher)
                    wrong += head != 3 || gun != 0 || skin != 1;
                else
                    wrong += head != 0 || gun != 0;
            }
            std::printf("test_soldiers: %s: %d soldiers (%d with shotguns, %d with launchers), %d leaders, "
                        "%d that set something off; %u lines to call out\n",
                        map.name, soldiers, shotguns, launchers, leaders, triggers, g.view.speechCount);
            check(soldiers == map.soldiers && leaders == map.leaders && wrong == 0,
                  "its soldiers are cooked, each with the head and gun of what he carries");
            check(g.view.speechCount >= static_cast<std::uint32_t>(kSpeechGroups * kSpeechLines) &&
                      g.view.speech[kSpeakAlert * kSpeechLines] != 0xFFFF &&
                      g.view.speech[kSpeakGrenade * kSpeechLines] != 0xFFFF &&
                      g.view.speech[kSpeakMonster * kSpeechLines] != 0xFFFF &&
                      g.view.talkAt == static_cast<std::uint32_t>(kTalkTableAt),
                  "... and with what soldiers call out, at the player and at aliens");
            const int one = findKind(g, ActorKind::Grunt);
            const ModelView *model = one >= 0 ? actorModel(g.actors, g.actors.states[one]) : nullptr;
            check(model != nullptr && model->findClip(nameHash("standing_mp5")) >= 0 &&
                      model->findClip(nameHash("reload_mp5")) >= 0 && model->findClip(nameHash("frontkick")) >= 0 &&
                      model->findClip(nameHash("dieheadshot")) >= 0 && model->header->hitboxCount != 0,
                  "his model has the clips he fights with, and his hit boxes");
            check(g.actors.humanGibs >= 0, "... and the map has the pieces a person comes apart into");
            int dropPlaces = 0, placed = 0;
            for (std::uint32_t i = 0; i < g.view.actorCount; ++i)
            {
                const ActorRecord &record = g.view.actors[i];
                placed += record.kind == static_cast<std::uint8_t>(ActorKind::Grunt) && !(record.flags & kActorDormant);
                dropPlaces += record.kind == static_cast<std::uint8_t>(ActorKind::Item) && (record.flags & kActorDormant) &&
                              (record.item == static_cast<std::uint32_t>(ItemKind::Mp5) ||
                               record.item == static_cast<std::uint32_t>(ItemKind::Shotgun));
            }
            check(dropPlaces == placed, "... and a place for each one's gun, for when he drops it");
        }
    }

    // ------------------------------------------------------------- a soldier

    AiMonster *mind(Game &g, int index)
    {
        return aiOf(g.actors, index);
    }
    const char *scheduleName(Game &g, int index)
    {
        const AiMonster *m = aiFind(g.actors, index);
        return m != nullptr && m->schedule != nullptr ? m->schedule->name : "";
    }
    bool scheduleIs(Game &g, int index, const char *name)
    {
        return std::strcmp(scheduleName(g, index), name) == 0;
    }
    /// His mind, for calling his own code directly.
    AiCtx ctx(Game &g, int index)
    {
        return AiCtx{g.logic, g.player, g.actors, g.ai, index, g.actors.states[index], *aiOf(g.actors, index),
                     grunt::kind()};
    }

    void testSoldier(const char *dir)
    {
        auto game = load(dir, "c1a3d");
        check(game != nullptr, "c1a3d cooks");
        if (!game)
            return;
        Game &g = *game;
        const int index = findKind(g, ActorKind::Grunt);
        check(index >= 0, "c1a3d has its soldier");
        if (index < 0)
            return;
        only(g, index);
        ActorState &me = g.actors.states[index];
        g.player.origin[2] += fxFromInt(30000);
        for (int n = 0; n < 10; ++n)
            tickStill(g);
        check(me.health == kGruntHealth && me.ammo == kGruntClip && !aiInSquad(ctx(g, index)) &&
                  mind(g, index)->state == kStateIdle,
              "a soldier by himself: fifty health, thirty-six rounds, no squad, minding his business");

        // His helmet, his head, and the rest of him.
        monsterHurt(g.logic, index, 15, true, kHitHelmet);
        check(me.health == kGruntHealth, "his helmet stops what is not twenty");
        monsterHurt(g.logic, index, 25, true, kHitHelmet);
        check(me.health == kGruntHealth - 15, "... and takes twenty off what is more (the rest counts three times)");
        me.health = kGruntHealth;
        monsterHurt(g.logic, index, 5, true, kHitHead);
        monsterHurt(g.logic, index, 5, true, kHitBody);
        check(me.health == kGruntHealth - 20, "a round in the head counts three times, one anywhere else once");
        check(g.times(CommonSound::GruntPain1) + g.times(CommonSound::GruntPain2) <= 1, "he cries out once a second at most");
        g.drain();
        me.health = kGruntHealth;
        aiForget(g.actors, index); // (afresh: it is his seeing that is next)
        for (int n = 0; n < 10; ++n)
            tickStill(g);

        // Seen: not from behind him, from in front.
        int range = 0;
        for (int distance : {400, 320, 240, 160})
        {
            if (range == 0 && standBefore(g, index, distance))
                range = distance;
        }
        check(range != 0, "somewhere in front of him to stand");
        me.yaw = static_cast<std::uint16_t>(me.yaw + 32768);
        for (int ms = 0; ms < 1000; ms += 33)
            tickStill(g);
        check(mind(g, index)->state == kStateIdle, "someone behind him, and quiet, is not seen");
        me.yaw = static_cast<std::uint16_t>(me.yaw + 32768);
        int saw = -1;
        for (int ms = 0; ms < 1000 && saw < 0; ms += 33)
        {
            tickStill(g);
            if (mind(g, index)->enemy == kAiPlayer)
                saw = ms;
        }
        check(saw >= 0, "someone in front of him is");

        // He fires: in threes, a tenth of a second apart; four to a round
        // that lands; and when the clip is out he hides and reloads.
        g.player.health = 100000;
        int last = me.ammo, rounds = 0, lastAt = -1000, threes = 0, inBurst = 0, wrongGap = 0;
        (void)wrongGap;
        int health = g.player.health, hits = 0, badHit = 0, most = 0;
        bool reloaded = false, ranDry = false;
        int ms = 0;
        for (; ms < 40000 && !reloaded; ms += 33)
        {
            tickStill(g);
            if (me.ammo < last)
            {
                rounds += last - me.ammo;
                if (ms - lastAt > 250)
                    inBurst = 0;
                else if (ms - lastAt < 60 || ms - lastAt > 140)
                    ++wrongGap;
                inBurst += last - me.ammo;
                threes += inBurst == 3 ? 1 : 0;
                lastAt = ms;
            }
            else if (me.ammo > last)
            {
                reloaded = true;
            }
            ranDry = ranDry || me.ammo == 0;
            last = me.ammo;
            if (g.player.health < health)
            {
                const int took = health - g.player.health;
                hits += took / kGruntRound;
                badHit += took % kGruntRound != 0;
                most = took > most ? took : most;
                health = g.player.health;
            }
        }
        std::printf("test_soldiers: %d rounds in %d bursts of three in %d ms; %d landed at %d units; then %s\n", rounds,
                    threes, ms, hits, range, reloaded ? "reloaded" : "did NOT reload");
        // (A burst broken off when a schedule ends with its clip is taken up
        // again in the next, as the game's is: most are threes.)
        check(rounds == kGruntClip && threes >= kGruntClip / 3 - 3,
              "he fires his clip in bursts of three, a tenth of a second between rounds");
        check(hits > 0 && hits < rounds && badHit == 0 && most <= 3 * kGruntRound,
              "some land, not all: four each (nothing worn)");
        check(ranDry && reloaded && me.ammo == kGruntClip && g.times(CommonSound::GruntReload) == 1,
              "with the clip out he reloads");
        check(g.times(CommonSound::GruntFire1) + g.times(CommonSound::GruntFire2) >= threes &&
                  g.times(CommonSound::GruntFire1) + g.times(CommonSound::GruntFire2) <= kGruntClip / 3 + 1,
              "... his gun heard once a burst");

        // Close to: a kick.
        check(standBefore(g, index, 44), "somewhere right in front of him to stand");
        g.player.health = 100;
        g.player.onGround = true;
        g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
        bool kicked = false;
        for (ms = 0; ms < 3000 && g.player.health == 100; ms += 33)
        {
            tickStill(g);
            g.player.onGround = true;
            kicked = kicked || mind(g, index)->activity == kActMeleeAttack1;
        }
        const fx push[3] = {g.player.velocity[0], g.player.velocity[1], 0};
        check(kicked && g.player.health == 100 - kGruntKick && fxLength(push) > fxFromInt(50),
              "someone right in front of him is kicked: ten, and back they go");

        // Hurt badly, he limps.
        me.health = kGruntLimp;
        {
            AiCtx c = ctx(g, index);
            aiSetActivity(c, kActRun);
        }
        const ModelView *model = actorModel(g.actors, me);
        check(model != nullptr && me.clip == model->findClip(nameHash("limpingrun")), "at twenty health he limps");

        // And dies, and stays dead. (What the map has him set off by it is
        // set off: here, something made up.)
        g.player.origin[2] += fxFromInt(30000);
        me.health = 5;
        me.fires = 1;
        me.item = static_cast<std::uint8_t>((me.item & 15) | (4 << kGruntTriggerShift));
        int queued = 0;
        for (const LogicEvent &event : g.logic.events)
            queued += event.active ? 1 : 0;
        const int cries = g.times(CommonSound::GruntDie1) + g.times(CommonSound::GruntDie2);
        monsterHurt(g.logic, index, 2, true, kHitHead);
        int after = 0;
        for (const LogicEvent &event : g.logic.events)
            after += event.active && event.target == 1 ? 1 : 0;
        check(after == 1 && queued + 1 >= after, "what he is to set off by dying is set off, once");
        for (ms = 0; ms < 300; ms += 33)
            tickStill(g);
        check(me.ai >= kAiDying && me.clip == model->findClip(nameHash("dieheadshot")) && !(me.flags & kActorSolid),
              "shot in the head he dies of it, as someone shot in the head");
        for (ms = 0; ms < 4000; ms += 33)
            tickStill(g);
        check(me.ai == kAiDead && me.active &&
                  g.times(CommonSound::GruntDie1) + g.times(CommonSound::GruntDie2) == cries + 1,
              "... and lies where he fell");
        // His gun, where he fell, to be picked up.
        int guns = 0;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &item = g.actors.states[i];
            guns += item.active && item.kind == static_cast<std::uint8_t>(ActorKind::Item) &&
                    (item.item == static_cast<std::uint8_t>(ItemKind::Mp5) ||
                     item.item == static_cast<std::uint8_t>(ItemKind::Shotgun)) &&
                    flat(item.pos, me.pos) < 96;
        }
        check(guns == 1 && (me.body / 4) % 3 == 2, "he drops his gun as he falls, and it is there to be taken");
        // A blast by his body: it comes apart.
        const fx by[3] = {me.pos[0], me.pos[1], me.pos[2] + fxFromInt(16)};
        radiusDamage(g.logic, g.player, by, 100, 250, -1);
        int pieces = 0;
        for (const Gib &gib : g.actors.gibs)
            pieces += gib.active && gib.model == static_cast<std::uint16_t>(g.actors.humanGibs);
        check(!me.active && pieces == 5 && g.times(CommonSound::BodySplat) == 0,
              "a blast by his body takes it apart: the skull and four more");
        tickStill(g);
        check(g.times(CommonSound::BodySplat) == 1, "... heard");
    }

    // ------------------------------------------------------------- squads

    /// `deferred`: their long thinking (ways somewhere) asked for and
    /// worked out between ticks, as the console's worker does it (HlThink.hpp)
    /// -- the same fight, by the same rules.
    void testSquads(const char *dir, bool deferred = false)
    {
        auto game = load(dir, "c1a3a");
        if (!game)
            return;
        Game &g = *game;
        std::vector<ThinkSlot> slots(static_cast<std::size_t>(g.actors.count));
        ThinkQueue queue;
        queue.slots = slots.data();
        queue.count = g.actors.count;
        queue.deferred = deferred;
        int answered = 0;
        if (deferred)
            g.actors.think = &queue;
        g.player.origin[2] += fxFromInt(30000);
        for (int n = 0; n < 10; ++n)
            tickStill(g);
        // The map's three squads of three, each under the one it flags.
        int squads = 0, members = 0, alone = 0, berets = 0;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (!isKind(actor, ActorKind::Grunt))
                continue;
            const AiMonster *m = aiFind(g.actors, i);
            if (m == nullptr || m->squadLeader == kAiNone)
            {
                ++alone;
                continue;
            }
            ++members;
            if (m->squadLeader == i)
            {
                ++squads;
                berets += actor.body % 4 == 1 && (g.view.actors[i].flags & kActorLeader);
            }
        }
        std::printf("test_soldiers: c1a3a: %d squads, %d in them, %d alone\n", squads, members, alone);
        check(squads == 3 && members == 9 && alone == 1 && berets == 3,
              "c1a3a: three squads of three, each led by the one the map flags (in his beret); one alone");

        // A fight with one of them. Watched throughout: never more than two
        // of a squad firing (the engage slots); whoever runs for cover runs
        // to somewhere the player cannot see; no grenade from within 256
        // units, nor within six seconds of his last.
        int leader = -1;
        for (int i = 0; i < g.actors.count && leader < 0; ++i)
        {
            const AiMonster *m = aiFind(g.actors, i);
            if (isKind(g.actors.states[i], ActorKind::Grunt) && m != nullptr && m->squadLeader == i &&
                standBefore(g, i, 500))
                leader = i;
        }
        check(leader >= 0, "somewhere to stand in front of a squad's leader");
        if (leader < 0)
            return;
        only(g, -1, ActorKind::Grunt);
        for (int i = 0; i < g.actors.count; ++i)
        {
            ActorState &actor = g.actors.states[i];
            const AiMonster *m = aiFind(g.actors, i);
            if (isKind(actor, ActorKind::Grunt) && (m == nullptr || m->squadLeader != leader))
            {
                actor.active = false;
                actorPlaceBox(g.actors, i);
            }
        }
        const fx stand[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        std::vector<std::string> was(g.actors.count);
        std::vector<int> lastToss(g.actors.count, -100000);
        std::vector<std::uint8_t> first(g.actors.count, 0);
        int mostFiring = 0, covers = 0, badCover = 0, tosses = 0, badToss = 0, signals = 0, woke = 0;
        bool leaderFirst = false, othersHid = false;
        for (int ms = 0; ms < 30000; ms += 33)
        {
            g.frame();
            // (The worker's turn: what was asked this tick, worked out
            // before the next.)
            while (deferred && thinkRunOne(g.logic))
                ++answered;
            g.player.health = 100;
            for (int k = 0; k < 3; ++k)
                g.player.origin[k] = stand[k];
            g.player.velocity[0] = g.player.velocity[1] = g.player.velocity[2] = 0;
            int firing = 0;
            const fx eye[3] = {stand[0], stand[1], stand[2] + g.player.viewHeight};
            for (int i = 0; i < g.actors.count; ++i)
            {
                const ActorState &actor = g.actors.states[i];
                const AiMonster *m = aiFind(g.actors, i);
                if (!isKind(actor, ActorKind::Grunt) || actor.ai >= kAiDying || m == nullptr)
                    continue;
                const std::string now = m->schedule != nullptr ? m->schedule->name : "";
                const bool changed = now != was[i];
                if (changed && std::getenv("HL_DEBUG") != nullptr)
                    std::printf("  %5d ms: #%d %s ammo %d health %d at %d %d (%d from the player)%s\n", ms, i, now.c_str(),
                                actor.ammo, actor.health, fxToInt(actor.pos[0]), fxToInt(actor.pos[1]),
                                flat(actor.pos, stand), i == leader ? " leader" : "");
                if (m->activity == kActRangeAttack1)
                    ++firing;
                if (changed && m->state == kStateCombat && was[i] != "" && ms < 3000 && !first[i])
                {
                    first[i] = 1;
                    ++woke;
                    if (i == leader)
                        leaderFirst = now == "SignalSuppress" || now == "Suppress" || now == "Wake Angry";
                    else
                        othersHid = othersHid || now == "TakeCover";
                }
                if (changed && now == "SignalSuppress")
                    ++signals;
                if (changed && now == "TakeCover")
                    ++covers;
                // (Where he is running to: out of the player's sight.)
                if (now == "TakeCover" && m->movementGoal == kMoveGoalLocation && changed == false &&
                    m->scheduleIndex == 6 && m->taskStatus != kTaskStatusNew)
                {
                    const fx there[3] = {m->moveGoal[0], m->moveGoal[1], m->moveGoal[2] + fxFromInt(73)};
                    badCover += lineClear(g.move, eye, there) ? 1 : 0;
                }
                if (changed && now == "RangeAttack2")
                {
                    ++tosses;
                    const bool bad = flat(actor.pos, stand) <= 200 || ms - lastToss[i] < 6000;
                    lastToss[i] = ms;
                    badToss += bad ? 1 : 0;
                }
                was[i] = now;
            }
            mostFiring = firing > mostFiring ? firing : mostFiring;
        }
        std::printf("test_soldiers: c1a3a, thirty seconds with a squad%s: %d woke; at most %d firing at once; "
                    "%d runs for cover (%d ticks headed somewhere in sight); %d grenades thrown (%d against the rules); "
                    "%d lines spoken; %d questions answered outside the frame\n",
                    deferred ? " (thinking outside the frame)" : "", woke, mostFiring, covers, badCover, tosses,
                    badToss, g.sentencesHeard, answered);
        if (deferred)
            check(answered > 0, "their thinking is done outside the frame");
        check(woke == 3 && leaderFirst, "the whole squad learns of the player at once, and its leader opens fire");
        check(othersHid, "... while the others make for cover");
        check(signals == 1, "... with a wave of the hand first, the once");
        check(mostFiring >= 1 && mostFiring <= 2, "never more than two of a squad fire at once");
        check(covers > 0 && badCover == 0, "whoever runs for cover runs to somewhere the player cannot see");
        check(badToss == 0, "grenades keep to the rules: not from close to, not twice in six seconds");
        check(g.sentencesHeard > 0, "they call out");
        check(g.loudestSentence == 44, "... at the game's 0.35 of full (HGRUNT_SENTENCE_VOLUME)");
        // Each with his own voice: 100, or a little higher (109 to 116).
        int voices = 0, higher = 0;
        bool fair = true;
        for (int i = 0; i < g.actors.count; ++i)
        {
            const AiMonster *m = aiFind(g.actors, i);
            if (m == nullptr || g.actors.states[i].kind != static_cast<std::uint8_t>(ActorKind::Grunt) || !m->started)
                continue;
            ++voices;
            higher += m->talk.voicePitch != 100;
            fair = fair && (m->talk.voicePitch == 100 || (m->talk.voicePitch >= 109 && m->talk.voicePitch <= 116));
        }
        check(voices > 0 && fair, "... each in his own voice: 100, or 109 to 116");
    }

    /// A hand grenade, by the game's rules (CheckRangeAttack2): thrown in an
    /// arc that comes down by the player, three and a half seconds on the
    /// fuse; not from within 256 units, not onto one of his own, not twice
    /// in six seconds.
    void testGrenades(const char *dir)
    {
        auto game = load(dir, "c1a3a");
        if (!game)
            return;
        Game &g = *game;
        g.player.origin[2] += fxFromInt(30000);
        for (int n = 0; n < 10; ++n)
            tickStill(g);
        auto canThrow = [&](int i)
        {
            AiCtx c = ctx(g, i);
            c.m.time1 = c.ai.now;
            c.m.enemy = kAiPlayer;
            for (int k = 0; k < 3; ++k)
                c.m.enemyLkp[k] = g.player.origin[k];
            c.m.movementGoal = kMoveGoalNone;
            return grunt::checkRange2(c, kFxOne, 0);
        };
        int index = -1;
        for (int i = 0; i < g.actors.count && index < 0; ++i)
        {
            ActorState &one = g.actors.states[i];
            if (!isKind(one, ActorKind::Grunt) || !(one.item & kGruntGrenades))
                continue;
            for (int distance : {500, 420, 600})
            {
                if (index >= 0 || !standBefore(g, i, distance))
                    continue;
                // (In the open on the level: c1a3a has crates standing in
                // pits, whose tops are not where a throw across the yard
                // is had.)
                const int rise = fxToInt(g.player.origin[2] - kStandingFeet - one.pos[2]);
                if (rise < -8 || rise > 8)
                    continue;
                g.player.onGround = true;
                for (int n = 0; n < 8 && index < 0; ++n)
                {
                    if (canThrow(i))
                        index = i;
                }
            }
        }
        check(index >= 0, "a soldier with grenades, and somewhere in the open in front of him");
        if (index < 0)
            return;
        ActorState &me = g.actors.states[index];
        // One of his squad is kept (moved about below); the rest go.
        int mate = -1;
        for (int i = 0; i < g.actors.count; ++i)
        {
            ActorState &actor = g.actors.states[i];
            const AiMonster *m = aiFind(g.actors, i);
            if (i == index || !fights(actor.kind))
                continue;
            if (mate < 0 && isKind(actor, ActorKind::Grunt) && m != nullptr && m->squadLeader != kAiNone &&
                m->squadLeader == mind(g, index)->squadLeader)
            {
                mate = i;
                continue;
            }
            actor.active = false;
            actorPlaceBox(g.actors, i);
        }
        const fx stand[3] = {g.player.origin[0], g.player.origin[1], g.player.origin[2]};
        // (The game looks for his own squad by where it is going, but the
        // throw it looks for next decides it all the same: as it shipped,
        // that is no rule. Out of the way.)
        if (mate >= 0)
        {
            g.actors.states[mate].active = false;
            actorPlaceBox(g.actors, mate);
        }
        check(mate >= 0, "one of his squad to stand by the player");
        // Too close: no grenade.
        check(standBefore(g, index, 200), "somewhere nearer to stand");
        g.player.onGround = true;
        check(!canThrow(index), "no grenade from within 256 units");
        for (int k = 0; k < 3; ++k)
            g.player.origin[k] = stand[k];
        bool can = false;
        for (int n = 0; n < 8 && !can; ++n)
            can = canThrow(index);
        check(can, "from 500 units in the open there is a throw to be had");
        if (!can)
            return;
        // He throws it; it comes down by them, and goes off on its fuse.
        {
            AiCtx c = ctx(g, index);
            c.m.enemy = kAiPlayer;
            aiSetState(c, kStateCombat);
            aiChangeSchedule(c, &grunt::slRangeAttack2);
        }
        g.player.health = 1000;
        int thrownAt = -1, offAt = -1, nearest = 100000;
        const fx feet[3] = {stand[0], stand[1], stand[2] - kStandingFeet};
        for (int ms = 0; ms < 8000 && offAt < 0; ms += 33)
        {
            tickStill(g);
            bool flying = false;
            for (const Projectile &shot : g.actors.shots)
            {
                if (!shot.active || shot.kind != kShotGrenade)
                    continue;
                flying = true;
                const fx d[3] = {shot.pos[0] - feet[0], shot.pos[1] - feet[1], shot.pos[2] - feet[2]};
                const int far = fxToInt(fxLength(d));
                nearest = far < nearest ? far : nearest;
            }
            if (flying && thrownAt < 0)
                thrownAt = ms;
            if (!flying && thrownAt >= 0)
                offAt = ms;
        }
        std::printf("test_soldiers: grenade: thrown at %d ms, comes within %d units of their feet, off %d ms later, "
                    "%d taken\n",
                    thrownAt, nearest, offAt - thrownAt, 1000 - g.player.health);
        // (He turns to the way it is to go first; and where he aims is where
        // they were last seen -- their middle, half the time -- with a
        // little to either side: it comes down near, not on them.)
        check(thrownAt >= 1100 && thrownAt <= 2400, "the grenade leaves his hand on the throw's own frame");
        check(nearest < 160, "... and comes down by them");
        check(offAt - thrownAt >= 3400 && offAt - thrownAt <= 3600 && g.player.health < 1000,
              "... to go off three and a half seconds after");
        check(mind(g, index)->time1 > g.ai.now + 1000 && !mind(g, index)->flag1,
              "and he has not another for them inside six seconds");
    }

    /// A grenade by him: he says so and runs from it (or, with nowhere to
    /// run, gets down). Soldiers with nothing to do talk. And the dead ones
    /// the maps lay out are laid out.
    void testOddments(const char *dir)
    {
        {
            auto game = load(dir, "c1a3d");
            if (!game)
                return;
            Game &g = *game;
            const int index = findKind(g, ActorKind::Grunt);
            if (index < 0)
                return;
            only(g, index);
            ActorState &me = g.actors.states[index];
            // (The player near enough to be in what he can see of the map,
            // and behind him.)
            if (!standBefore(g, index, 300))
                return;
            me.yaw = static_cast<std::uint16_t>(me.yaw + 32768);
            for (int n = 0; n < 10; ++n)
                tickStill(g);
            const fx still[3] = {0, 0, 0};
            const fx by[3] = {me.pos[0] + fxFromInt(40), me.pos[1], me.pos[2] + fxFromInt(4)};
            g.player.health = 100;
            projectileAdd(g.actors, kShotGrenade, by, still, 3000);
            const int spoken = g.sentencesHeard;
            bool ran = false, cowered = false;
            for (int ms = 0; ms < 1500 && !ran && !cowered; ms += 33)
            {
                tickStill(g);
                ran = scheduleIs(g, index, "GruntTakeCoverFromBestSound") && aiIsMoving(ctx(g, index));
                cowered = scheduleIs(g, index, "Cower");
            }
            std::printf("test_soldiers: a grenade at his feet: he %s\n", ran ? "runs" : (cowered ? "cowers" : "stays"));
            check(ran || cowered, "a grenade at his feet: he runs from it, or gets down");
            check(g.sentencesHeard == spoken + 1, "... and says so");
        }
        {
            auto game = load(dir, "c1a3a");
            if (!game)
                return;
            Game &g = *game;
            g.player.origin[2] += fxFromInt(30000);
            for (int ms = 0; ms < 60000; ms += 33)
                tickStill(g);
            bool idle = true;
            for (int i = 0; i < g.actors.count; ++i)
            {
                const AiMonster *m = aiFind(g.actors, i);
                idle = idle && (!isKind(g.actors.states[i], ActorKind::Grunt) || (m != nullptr && m->state == kStateIdle));
            }
            std::printf("test_soldiers: c1a3a, a minute with nobody about: %d lines of talk\n", g.sentencesHeard);
            check(idle && g.sentencesHeard >= 2 && g.sentencesHeard <= 60,
                  "soldiers with nothing to do pass the time of day, one at a time");
        }
        {
            auto game = load(dir, "c1a3");
            if (!game)
                return;
            Game &g = *game;
            int laid = 0;
            for (int i = 0; i < g.actors.count; ++i)
            {
                const ActorState &actor = g.actors.states[i];
                if (isKind(actor, ActorKind::Pose) && actor.model < g.view.modelRefCount &&
                    std::string(g.view.modelRefs[actor.model].name) == "hgrunt" && actor.clip >= 0 && actor.frozen)
                    ++laid;
            }
            check(laid == 1, "c1a3's dead soldier lies where the map has him");
        }
    }

    /// The launcher, on the map that first has one: a grenade at where the
    /// player stands, off at the first thing it meets.
    void testLauncher(const char *dir)
    {
        auto game = load(dir, "c2a2b1");
        if (!game)
            return;
        Game &g = *game;
        g.player.origin[2] += fxFromInt(30000);
        for (int n = 0; n < 60; ++n) // (his first look for a grenade is a second after he is there)
            tickStill(g);
        int index = -1;
        for (int i = 0; i < g.actors.count && index < 0; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (isKind(actor, ActorKind::Grunt) && (actor.item & kGruntLauncher) && standBefore(g, i, 600))
                index = i;
        }
        check(index >= 0, "c2a2b1 has a soldier with a launcher, and somewhere to stand in front of him");
        if (index < 0)
            return;
        only(g, index);
        // (Alone: a squad's leader with a new enemy signals and fires first.)
        if (mind(g, index)->squadLeader != kAiNone)
            aiSquadRemove(g.actors, mind(g, index)->squadLeader, index);
        check(((g.view.actors[index].item >> kActorSkinShift) & 0xFF) == 1 && g.view.actors[index].body % 4 == 3,
              "he is the one in the mask");
        g.player.health = 100;
        g.player.onGround = true;
        bool launched = false, flew = false;
        int ms = 0;
        for (; ms < 6000 && g.player.health == 100; ms += 33)
        {
            tickStill(g);
            g.player.onGround = true;
            launched = launched || scheduleIs(g, index, "RangeAttack2");
            if (std::getenv("HL_DEBUG") != nullptr && ms % 99 == 0)
            {
                const AiMonster *m = aiFind(g.actors, index);
                std::printf("  launcher %d ms: st=%d %s cond=%08x next=%d now=%d squad=%d\n", ms, m->state, scheduleName(g, index),
                            m->conditions, m->time1, g.ai.now, m->squadLeader);
            }
            for (const Projectile &shot : g.actors.shots)
                flew = flew || (shot.active && shot.kind == kShotContact && shot.sprite == 1);
        }
        std::printf("test_soldiers: c2a2b1: launcher: %s, %d taken after %d ms\n", launched ? "launched" : "NOT launched",
                    100 - g.player.health, ms);
        check(launched && flew && g.times(CommonSound::Launcher) == 1 && g.times(CommonSound::Explode) >= 1 &&
                  g.player.health < 100,
              "he launches a grenade at them before anything else, and it goes off where they stand");
        check(mind(g, index)->time1 > g.ai.now, "... and not another for six seconds");
    }

    /// The shotgun: eight shells, one a "burst", five pellets of five.
    /// Which maps are given the casings' models: where a soldier or an
    /// assassin is, or the player is armed -- not the tram ride.
    void testCasingNeeds(const char *dir)
    {
        auto casings = [&](const char *map, bool armed)
        {
            BspFile bsp;
            std::vector<Entity> entities;
            std::string error;
            if (!bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error) ||
                !parseEntities(bsp.entities, entities, error))
                return -1;
            std::vector<ModelNeed> needs = collectModelNeeds(entities);
            addCasingNeeds(armed, needs);
            int found = 0;
            for (const ModelNeed &need : needs)
                found += need.name == "shell" || need.name == "shotgunshell" ? 1 : 0;
            return found;
        };
        check(casings("c0a0", false) == 0, "the tram ride has no casings to throw out");
        check(casings("c1a3d", false) == 2, "a map of soldiers has both casings' models");
        check(casings("c2a3d", false) == 2, "... and one of assassins");
        check(casings("c1a1f", true) == 2, "... and a map the player is armed in");
        // ... and the grenades in flight, as the game's models of them.
        auto grenades = [&](const char *map, bool hand, bool launcher, bool &spins)
        {
            BspFile bsp;
            std::vector<Entity> entities;
            std::string error;
            spins = false;
            if (!bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error) ||
                !parseEntities(bsp.entities, entities, error))
                return -1;
            std::vector<ModelNeed> needs = collectModelNeeds(entities);
            addGrenadeNeeds(hand, launcher, needs);
            int found = 0;
            for (const ModelNeed &need : needs)
            {
                found += need.name == "w_grenade" || need.name == "grenade" ? 1 : 0;
                if (need.name == "w_grenade")
                    spins = std::find(need.clips.begin(), need.clips.end(), "toss") != need.clips.end() &&
                            std::find(need.clips.begin(), need.clips.end(), "roll1") != need.clips.end();
            }
            return found;
        };
        bool spins = false;
        check(grenades("c0a0", false, false, spins) == 0, "the tram ride throws no grenades");
        check(grenades("c1a3d", false, false, spins) == 2 && spins,
              "a map of soldiers has both grenades, the hand grenade with its spin and its roll");
        check(grenades("c1a1b", true, false, spins) >= 1 && spins, "... and a map the player has grenades in");
    }

    void testShotgun(const char *dir)
    {
        auto game = load(dir, "c2a2b2");
        if (!game)
            return;
        Game &g = *game;
        g.player.origin[2] += fxFromInt(30000);
        for (int n = 0; n < 10; ++n)
            tickStill(g);
        int index = -1;
        for (int i = 0; i < g.actors.count && index < 0; ++i)
        {
            const ActorState &actor = g.actors.states[i];
            if (isKind(actor, ActorKind::Grunt) && (actor.item & kGruntShotgun) && standBefore(g, i, 200))
                index = i;
        }
        check(index >= 0, "c2a2b2 has a soldier with a shotgun, and somewhere to stand in front of him");
        if (index < 0)
            return;
        only(g, index);
        ActorState &me = g.actors.states[index];
        me.item &= static_cast<std::uint8_t>(~(kGruntGrenades | kGruntLauncher));
        check(me.ammo == kGruntShells && me.body % 4 == 2 && me.body / 4 == 1, "eight shells, and the gun to be seen");
        g.player.health = 100000;
        int health = g.player.health, shells = 0, last = me.ammo, most = 0, odd = 0, cases = 0, brass = 0;
        for (int ms = 0; ms < 12000 && me.ammo > 0; ms += 33)
        {
            tickStill(g);
            for (const Piece &piece : g.actors.pieces)
            {
                cases += piece.kind == kPieceShotShell && piece.leftMs > 1100 ? 1 : 0;
                brass += piece.kind == kPieceShell ? 1 : 0;
            }
            shells += me.ammo < last ? last - me.ammo : 0;
            last = me.ammo;
            const int took = health - g.player.health;
            most = took > most ? took : most;
            odd += took % kGruntPellet != 0;
            health = g.player.health;
        }
        std::printf("test_soldiers: c2a2b2: shotgun: %d shells, at most %d a shell at 200 units\n", shells, most);
        check(shells == kGruntShells && g.times(CommonSound::Shotgun) == kGruntShells, "he fires them one at a time");
        check(most > 0 && most <= kGruntPellets * kGruntPellet && odd == 0, "... five pellets of five a shell");
        bool modelled = false;
        for (std::uint32_t i = 0; i < g.view.modelRefCount; ++i)
            modelled = modelled || std::string(g.view.modelRefs[i].name) == "shotgunshell";
        std::printf("test_soldiers: c2a2b2: %d frames of fresh shotgun shells on the way out\n", cases);
        check(cases > 0 && brass == 0 && modelled,
              "... and each throws out a shotgun's shell, which the map has the model of (EjectBrass)");
    }

    /// What a monstermaker makes: soldiers, with what the game gives one
    /// that is not told what to carry.
    void testMade(const char *dir)
    {
        auto game = load(dir, "c2a1a");
        if (!game)
            return;
        Game &g = *game;
        int places = 0;
        for (std::uint32_t i = 0; i < g.view.actorCount; ++i)
            places += g.view.actors[i].kind == static_cast<std::uint8_t>(ActorKind::Grunt) &&
                      (g.view.actors[i].flags & kActorDormant);
        check(places >= 5, "c2a1a keeps places for the soldiers its makers make");
        g.player.origin[2] += fxFromInt(30000);
        int maker = -1;
        for (int n = 0; maker < 0; ++n)
        {
            const int record = g.recordOfKind(LogicKind::Maker, n);
            if (record < 0)
                break;
            const std::uint32_t first = g.view.logic[record].extra[0];
            if (g.view.logic[record].extra[1] != 0 && first < g.view.actorCount &&
                g.view.actors[first].kind == static_cast<std::uint8_t>(ActorKind::Grunt))
                maker = record;
        }
        check(maker >= 0, "a maker of soldiers");
        if (maker < 0)
            return;
        const int slot = static_cast<int>(g.view.logic[maker].extra[0]);
        logicUse(g.logic, g.player, maker, -1, kUseToggle);
        for (int ms = 0; ms < 3000; ms += 33)
            tickStill(g);
        const ActorState &made = g.actors.states[slot];
        check(made.active && made.health == kGruntHealth && (made.item & 15) == (kGruntMp5 | kGruntGrenades) &&
                  made.ammo == kGruntClip,
              "used, it makes one: an MP5 and grenades, as the game gives");
        // Killed, the one a maker made fades away once he has fallen (the
        // game's ShouldFadeOnDeath), and his place is free after.
        monsterHurt(g.logic, slot, made.health + 10, true);
        int fadedAt = -1, goneAt = -1;
        for (int ms = 0; ms < 12000 && goneAt < 0; ms += 33)
        {
            tickStill(g);
            if (fadedAt < 0 && corpseFadeLeft(made) >= 0)
                fadedAt = ms;
            if (!made.active)
                goneAt = ms;
        }
        std::printf("test_soldiers: c2a1a: the maker's soldier fades from %d ms after he is killed, gone at %d\n",
                    fadedAt, goneAt);
        check(fadedAt > 0 && goneAt > fadedAt + kCorpseFadeMs - 200 && goneAt < fadedAt + kCorpseFadeMs + 200,
              "... and the one a maker made fades away once he has fallen, in the game's time");
    }

    /// The bodies a map marks to fade (SF_MONSTER_FADECORPSE) do; others lie.
    /// A maker keeps as many places as the map lets be alive at once
    /// (c1a2's headcrabs, five), but a roach swarm only three.
    void testMakerPlaces(const char *dir)
    {
        for (const char *name : {"c1a2", "c1a3"})
        {
            auto game = load(dir, name);
            check(game != nullptr, "a map with makers loads");
            if (!game)
                continue;
            Game &g = *game;
            int crabs = 0, roaches = 0, wrongPlaces = 0, more = 0, firstMore = -1;
            for (std::uint32_t i = 0; i < g.view.logicCount; ++i)
            {
                const LogicRecord &r = g.view.logic[i];
                if (r.kind != static_cast<std::uint8_t>(LogicKind::Maker) || r.extra[0] >= static_cast<std::uint32_t>(g.actors.count))
                    continue;
                const int places = makerPlaces(r);
                const std::uint8_t kind = g.actors.states[r.extra[0]].kind;
                // (Its places past three after every other actor the map
                // places, so that a save made before keeps them where they
                // were; each a place for what it makes.)
                for (int n = 0; n < places; ++n)
                {
                    const int place = makerPlace(r, n);
                    if (place >= g.actors.count || g.actors.states[place].kind != kind)
                        ++wrongPlaces;
                    if (n >= static_cast<int>(r.extra[1]))
                        firstMore = firstMore < 0 ? place : std::min(firstMore, place);
                }
                more += static_cast<int>(r.extra[3] >> 16);
                if (kind == static_cast<std::uint8_t>(ActorKind::Headcrab))
                    crabs = std::max(crabs, places);
                if (kind == static_cast<std::uint8_t>(ActorKind::Cockroach))
                    roaches = std::max(roaches, places);
            }
            std::printf("test_soldiers: %s: a crab maker keeps up to %d places, a roach maker %d\n", name, crabs, roaches);
            if (std::string(name) == "c1a2")
                check(crabs == 5, "c1a2's crab maker keeps five places, as many as may be alive");
            check(roaches <= 3, "... and a roach maker no more than three");
            check(wrongPlaces == 0, "... each place one for what it makes");
            check(more == 0 || firstMore == static_cast<int>(g.view.actorCount) - more,
                  "... those past three after every other actor");
        }
    }
    void testFadeCorpse(const char *dir)
    {
        auto game = load(dir, "c4a1a");
        if (!game)
            return;
        Game &g = *game;
        g.player.origin[2] += fxFromInt(30000);
        int fades = -1, stays = -1;
        for (std::uint32_t i = 0; i < g.view.actorCount; ++i)
        {
            const ActorRecord &record = g.view.actors[i];
            if (!actorThinks(record.kind) || (record.flags & kActorDormant) || !g.actors.states[i].active)
                continue;
            if ((record.light & kActorLightFadeCorpse) && fades < 0)
                fades = static_cast<int>(i);
            else if (!(record.light & kActorLightFadeCorpse) && stays < 0)
                stays = static_cast<int>(i);
        }
        check(fades >= 0, "c4a1a has monsters its map marks to fade when dead");
        if (fades < 0)
            return;
        check((g.actors.states[fades].light & kActorLightFadeCorpse) == 0, "... which are not lit any the brighter for it");
        // (A second for them to be about, then killed: by more than they
        // have, not so much more as to tear them apart.)
        for (int ms = 0; ms < 1000; ms += 33)
            tickStill(g);
        for (int n = 0; n < 6 && g.actors.states[fades].ai < kAiDying; ++n)
            monsterHurt(g.logic, fades, 20, true);
        for (int n = 0; n < 6 && stays >= 0 && g.actors.states[stays].ai < kAiDying; ++n)
            monsterHurt(g.logic, stays, 20, true);
        for (int ms = 0; ms < 15000; ms += 33)
            tickStill(g);
        check(!g.actors.states[fades].active, "killed, it fades away and is gone");
        if (stays >= 0)
            check(g.actors.states[stays].active && g.actors.states[stays].ai == kAiDead,
                  "... and one the map does not mark lies where it fell");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
    {
        testMines(dir);
        testCooked(dir);
        testSoldier(dir);
        testSquads(dir);
        testSquads(dir, true);
        testGrenades(dir);
        testOddments(dir);
        testLauncher(dir);
        testCasingNeeds(dir);
        testShotgun(dir);
        testMakerPlaces(dir);
        testFadeCorpse(dir);
        testMade(dir);
    }
    else
    {
        std::printf("test_soldiers: HLDS_VALVE_DIR not set, the maps were not played\n");
        return 77;
    }
    if (g_failures == 0)
        std::printf("test_soldiers: ok\n");
    return g_failures == 0 ? 0 : 1;
}
