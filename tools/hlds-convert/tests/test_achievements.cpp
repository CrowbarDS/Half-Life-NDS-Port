// The achievements (shared/HlAchievements.hpp): the table, the file they are
// kept in, and every rule that earns one -- from the events the game reports
// (shared/HlAchEvents.hpp), made up here. With HLDS_VALVE_DIR, against the
// game's maps besides: every name and place the rules go by is in the map
// they name, and the game reports what it should (a button's target used, a
// kill by the player) and nothing it should not (a name not listened for,
// one whose entities are gone, a cheat's kills).

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

#include "HlAchievements.hpp"
#include "TestGame.hpp"

using namespace hlds;
using namespace hldata;
using namespace hldstest;

namespace
{
    AchEvents eventOf(AchEventType type, std::uint32_t hash, std::uint8_t kind = 0, std::uint8_t flags = 0)
    {
        AchEvents events;
        events.push(type, hash, kind, flags);
        return events;
    }

    void testTable()
    {
        std::set<std::string> apis, titles;
        int hidden = 0, counted = 0;
        for (const AchievementInfo &info : kAchievements)
        {
            apis.insert(info.api);
            titles.insert(info.title);
            hidden += info.hidden ? 1 : 0;
            counted += info.goal != 0 ? 1 : 0;
            check(info.text[0] != '\0', "every achievement says what to do");
        }
        check(kAchievementCount == 68, "the mod's 68");
        check(apis.size() == 68 && titles.size() == 68, "each named once");
        check(hidden == 4, "four hidden (both ends of the game, Cheater, Signature move)");
        check(counted == 5, "five that count");
        check(std::string(kAchievements[kAchInit].api) == "INIT", "the ids are the table's rows");
        check(std::string(kAchievements[kAch5Tur].api) == "5TUR" && kAchievements[kAch5Tur].goal == 5,
              "the turrets: five");
        for (int c = 0; c < kAchCounterCount; ++c)
            check(kAchievements[kCounterAchievement[c]].goal != 0, "a counter counts towards one with a goal");

        // By chapter: each once, no chapter empty, the general ones last.
        std::set<int> listed;
        for (int id : kAchByChapter)
            listed.insert(id);
        check(listed.size() == kAchievementCount && *listed.rbegin() == kAchievementCount - 1,
              "every achievement is in one chapter");
        check(kAchChapterStart[0] == 0 && kAchChapterStart[kAchChapterCount] == kAchievementCount,
              "the chapters cover the list");
        for (int c = 0; c < kAchChapterCount; ++c)
            check(kAchChapterStart[c + 1] > kAchChapterStart[c], "no chapter is empty");
        check(std::string(kAchChapterNames[kAchChapterCount - 1]) == "GENERAL" &&
                  kAchByChapter[kAchChapterStart[kAchChapterCount - 1]] == kAchInit,
              "the general ones are last");
    }

    void testFile()
    {
        AchievementTracker t;
        t.unlock(kAchInit);
        t.unlock(kAchC4a1eSpiral);
        t.data.counts[kCountHeadcrabs] = 321;
        const std::vector<std::uint8_t> bytes = writeAchievements(t.data);
        check(bytes.size() <= kAchievementsMostBytes, "the file fits what the console reads");
        AchievementData back;
        check(readAchievements(bytes.data(), bytes.size(), back), "read back");
        // (Field by field: the struct's padding is not part of what is written.)
        check(std::memcmp(back.unlocked, t.data.unlocked, sizeof(back.unlocked)) == 0 &&
                  std::memcmp(back.counts, t.data.counts, sizeof(back.counts)) == 0,
              "as written");

        std::vector<std::uint8_t> bad = bytes;
        bad.back() ^= 1;
        AchievementData kept;
        kept.counts[0] = 7;
        check(!readAchievements(bad.data(), bad.size(), kept) && kept.counts[0] == 7,
              "a damaged file is refused, and nothing changed");
        check(!readAchievements(bytes.data(), bytes.size() - 1, kept), "a short one too");
        check(!readAchievements(bytes.data(), 4, kept), "and a stub");
        bad = bytes;
        bad[0] = 'X';
        check(!readAchievements(bad.data(), bad.size(), kept), "and another file");
    }

    void testUnlock()
    {
        AchievementTracker t;
        check(t.unlock(kAchInit) && !t.unlock(kAchInit), "once only");
        check(t.unsaved && t.has(kAchInit) && t.earned() == 1, "earned, and to be written");
        check(t.next() == kAchInit && t.next() == -1, "shown once");
        t.unlock(kAchBeverages);
        t.unlock(kAchFemdead);
        const int first = t.next();
        t.showLater(first);
        check(t.next() == kAchBeverages && t.next() == kAchFemdead, "one put back is shown first");
        check(!t.unlock(-1) && !t.unlock(kAchievementCount), "no such achievement");
    }

    void testChapters()
    {
        for (const ach::ChapterEnd &end : ach::kChapterEnds)
        {
            AchievementTracker t;
            t.travelled(end.entered, end.left);
            check(!t.has(end.id), "going back does not finish a chapter");
            t.travelled(end.left, end.entered);
            check(t.has(end.id), "the way into the next chapter finishes one");
        }
        AchievementTracker t;
        t.travelled(nameHash("c1a0"), nameHash("c1a0d"));
        check(t.earned() == 0, "a way within a chapter finishes nothing");
        std::set<int> ids;
        for (const ach::ChapterEnd &end : ach::kChapterEnds)
            ids.insert(end.id);
        check(ids.size() == 18, "eighteen chapters end by a way out");
    }

    void testFired()
    {
        for (const ach::FireRule &rule : ach::kFireRules)
        {
            AchievementTracker t;
            AchContext context;
            context.minesIntact = true;
            t.apply(rule.map, eventOf(AchEventType::Fired, rule.name), context);
            check(t.has(rule.id), "a name used in its map");
            AchievementTracker other;
            other.apply(nameHash("nowhere"), eventOf(AchEventType::Fired, rule.name), context);
            check(other.earned() == 0, "... not in another");
        }
        // The tripmines: only past every one.
        AchievementTracker t;
        AchContext context;
        t.apply(nameHash("c2a5d"), eventOf(AchEventType::Fired, nameHash("lift_1")), context);
        check(!t.has(kAchC2a5dLift), "not with a tripmine set off");
        context.minesIntact = true;
        t.apply(nameHash("c2a5d"), eventOf(AchEventType::Fired, nameHash("lift_1")), context);
        check(t.has(kAchC2a5dLift), "with none");

        std::uint32_t watched[AchEvents::kWatchMost];
        int most = 0;
        for (const ach::FireRule &rule : ach::kFireRules)
        {
            const int n = achievementsWatched(rule.map, watched, AchEvents::kWatchMost);
            most = n > most ? n : most;
            bool has = false;
            for (int i = 0; i < n; ++i)
                has = has || watched[i] == rule.name;
            check(has, "its map listens for its name");
        }
        check(most < AchEvents::kWatchMost, "no map listens for more than there is room for");
    }

    void testKills()
    {
        const AchContext context;
        for (const ach::KillRule &rule : ach::kKillRules)
        {
            const std::uint32_t map = rule.map != 0 ? rule.map : nameHash("c1a1");
            AchievementTracker t;
            t.apply(map, eventOf(AchEventType::Killed, rule.name, static_cast<std::uint8_t>(rule.kind), kAchByPlayer),
                    context);
            check(t.has(rule.id), "killed by the player");
            AchievementTracker other;
            other.apply(map, eventOf(AchEventType::Killed, rule.name, static_cast<std::uint8_t>(rule.kind), 0), context);
            check(other.has(rule.id) == !rule.byPlayer, "killed by someone else: only if anyone may");
        }
        AchievementTracker t;
        t.apply(nameHash("c1a1"),
                eventOf(AchEventType::Killed, nameHash("someone"), static_cast<std::uint8_t>(ActorKind::Guard), kAchByPlayer),
                context);
        check(!t.has(kAchC1a1BarnDie), "not any guard");

        // What counts, and when it is enough.
        AchievementTracker c;
        for (int i = 0; i < 14; ++i)
            c.apply(1, eventOf(AchEventType::Killed, 0, static_cast<std::uint8_t>(ActorKind::Barnacle), kAchByPlayer),
                    context);
        check(!c.has(kAch5Barn) && c.progress(kAch5Barn) == 14 && c.counted, "fourteen barnacles");
        c.apply(1, eventOf(AchEventType::Killed, 0, static_cast<std::uint8_t>(ActorKind::Barnacle), 0), context);
        check(!c.has(kAch5Barn), "one the player did not kill is not counted");
        c.apply(1, eventOf(AchEventType::Killed, 0, static_cast<std::uint8_t>(ActorKind::Barnacle), kAchByPlayer),
                context);
        check(c.has(kAch5Barn), "fifteen");
        for (int i = 0; i < 5; ++i)
            c.apply(1, eventOf(AchEventType::Killed, 0, static_cast<std::uint8_t>(ActorKind::Turret), kAchByPlayer),
                    context);
        check(!c.has(kAch5Tur), "a ceiling's turrets are not counted");
        for (int i = 0; i < 5; ++i)
            c.apply(1, eventOf(AchEventType::Killed, 0, static_cast<std::uint8_t>(ActorKind::Sentry), kAchByPlayer),
                    context);
        check(c.has(kAch5Tur), "the floor's are");
        for (int i = 0; i < 499; ++i)
            c.apply(1,
                    eventOf(AchEventType::Killed, 0,
                            static_cast<std::uint8_t>(i % 2 ? ActorKind::Headcrab : ActorKind::BabyCrab), kAchByPlayer),
                    context);
        check(!c.has(kAchHeadhumpers), "499 headcrabs");
        c.apply(1, eventOf(AchEventType::Killed, 0, static_cast<std::uint8_t>(ActorKind::Headcrab), kAchByPlayer),
                context);
        check(c.has(kAchHeadhumpers), "500");
        for (int i = 0; i < 250; ++i)
            c.apply(1, eventOf(AchEventType::Broke, 0), context);
        check(c.has(kAchLoperEggz) && c.progress(kAchLoperEggz) == 250, "250 broken");
    }

    void testOthers()
    {
        const AchContext context;
        AchievementTracker t;
        t.apply(1, eventOf(AchEventType::Soda, 0), context);
        check(t.has(kAchBeverages), "a soda");
        t.apply(nameHash("c2a4e"), eventOf(AchEventType::Picked, 0, static_cast<std::uint8_t>(ItemKind::Gauss)), context);
        check(!t.has(kAchC2a4fGauss), "the tau cannon, not there");
        t.apply(nameHash("c2a4f"), eventOf(AchEventType::Picked, 0, static_cast<std::uint8_t>(ItemKind::Glock)), context);
        check(!t.has(kAchC2a4fGauss), "nor another gun");
        t.apply(nameHash("c2a4f"), eventOf(AchEventType::Picked, 0, static_cast<std::uint8_t>(ItemKind::Gauss)), context);
        check(t.has(kAchC2a4fGauss), "the tau cannon, there");

        for (const ach::BoxRule &rule : ach::kBoxRules)
        {
            AchievementTracker in;
            fx middle[3];
            for (int k = 0; k < 3; ++k)
                middle[k] = fxFromInt((rule.mins[k] + rule.maxs[k]) / 2);
            in.standing(nameHash("nowhere"), middle);
            check(in.earned() == 0, "a place in another map");
            fx outside[3] = {middle[0], middle[1], fxFromInt(rule.maxs[2] + 37)};
            in.standing(rule.map, outside);
            check(in.earned() == 0, "above it");
            fx touching[3] = {middle[0], middle[1], fxFromInt(rule.maxs[2] + 35)};
            in.standing(rule.map, touching);
            check(in.has(rule.id), "a place, touched");
        }

        AchievementTracker m;
        m.unlock(kAchTrainingComplete);
        m.unlock(kAchC5a1Failed);
        check(!m.has(kAchMarathon), "the course and the game: not yet");
        m.unlock(kAchUplinkComplete);
        check(m.has(kAchMarathon), "and Uplink: the marathon");
        int shown = 0;
        while (m.next() >= 0)
            ++shown;
        check(shown == 4, "the marathon is shown as well");
    }

    // ------------------------------------------------- the game's own maps

    std::string mapOf(const std::filesystem::path &maps, std::uint32_t hash)
    {
        for (const auto &file : std::filesystem::directory_iterator(maps))
        {
            const std::string stem = file.path().stem().string();
            if (file.path().extension() == ".bsp" && nameHash(stem.c_str()) == hash)
                return stem;
        }
        return {};
    }

    bool entitiesOf(const std::filesystem::path &maps, const std::string &map, std::vector<Entity> &out)
    {
        BspFile bsp;
        std::string error;
        out.clear();
        return bsp.load(maps / (map + ".bsp"), error) && parseEntities(bsp.entities, out, error);
    }

    bool named(const std::vector<Entity> &entities, std::uint32_t hash, const char *classPrefix = "")
    {
        for (const Entity &e : entities)
        {
            const std::string name(e.value("targetname"));
            if (!name.empty() && nameHash(name.c_str()) == hash && e.className().starts_with(classPrefix))
                return true;
        }
        return false;
    }

    void testMaps(const char *dir)
    {
        const std::filesystem::path maps = std::filesystem::path(dir) / "maps";
        std::vector<Entity> entities;
        for (const ach::ChapterEnd &end : ach::kChapterEnds)
        {
            const std::string left = mapOf(maps, end.left), entered = mapOf(maps, end.entered);
            check(!left.empty() && !entered.empty() && entitiesOf(maps, left, entities), "a chapter's maps");
            bool way = false;
            for (const Entity &e : entities)
                way = way || (e.className() == "trigger_changelevel" && e.value("map") == entered);
            check(way, ("a way from " + left + " to " + entered).c_str());
        }
        for (const ach::FireRule &rule : ach::kFireRules)
        {
            const std::string map = mapOf(maps, rule.map);
            check(!map.empty() && entitiesOf(maps, map, entities) && named(entities, rule.name),
                  ("a name listened for is in " + map + ": " + kAchievements[rule.id].api).c_str());
        }
        for (const ach::KillRule &rule : ach::kKillRules)
        {
            if (rule.map == 0)
                continue;
            const std::string map = mapOf(maps, rule.map);
            check(!map.empty() && entitiesOf(maps, map, entities), "a monster's map");
            if (rule.name != 0)
                check(named(entities, rule.name, "monster_"),
                      ("the monster is in " + map + ": " + kAchievements[rule.id].api).c_str());
        }
        for (const ach::BoxRule &rule : ach::kBoxRules)
        {
            // Something the place is known by (what lies in it) is in it.
            const std::string map = mapOf(maps, rule.map);
            check(!map.empty() && entitiesOf(maps, map, entities), "a place's map");
            bool found = false;
            for (const Entity &e : entities)
            {
                int o[3];
                const std::string origin(e.value("origin"));
                if (origin.empty() || std::sscanf(origin.c_str(), "%d %d %d", &o[0], &o[1], &o[2]) != 3)
                    continue;
                bool in = true;
                for (int k = 0; k < 3; ++k)
                    in = in && o[k] >= rule.mins[k] && o[k] <= rule.maxs[k];
                found = found || in;
            }
            check(found, ("something lies in the place in " + map + ": " + kAchievements[rule.id].api).c_str());
        }
    }

    std::unique_ptr<Game> play(const char *dir, const char *map, AchEvents &events)
    {
        auto game = std::make_unique<Game>();
        std::string error;
        if (!game->bsp.load(std::string(dir) + "/maps/" + map + ".bsp", error))
        {
            check(false, (std::string(dir) + "/maps/" + map + ".bsp loads").c_str());
            return nullptr;
        }
        static Globals globals;
        finish(*game, &globals, dir);
        if (!game->ok)
            return nullptr;
        events = AchEvents();
        events.watchedCount = achievementsWatched(nameHash(map), events.watched, AchEvents::kWatchMost);
        game->logic.achievements = &events;
        return game;
    }

    std::uint16_t nameId(const Game &g, const char *name)
    {
        for (int n = 1; n < g.logic.nameCount; ++n)
        {
            if (g.logic.nameHashes[n] == nameHash(name))
                return static_cast<std::uint16_t>(n);
        }
        return 0;
    }

    void testReports(const char *dir)
    {
        AchEvents events;
        // A button's target: told; another name: not.
        auto g = play(dir, "c1a0d", events);
        check(g != nullptr, "c1a0d");
        if (g != nullptr)
        {
            logicFire(g->logic, g->player, nameId(*g, "hand_dryermm"), -1, kUseToggle, 0);
            check(events.count == 1 && events.list[0].type == AchEventType::Fired &&
                      events.list[0].hash == nameHash("hand_dryermm"),
                  "the hand dryer is told of");
            events.count = 0;
            logicFire(g->logic, g->player, nameId(*g, "scientist1"), -1, kUseToggle, 0);
            check(events.count == 0, "a name not listened for is not");
        }
        // The G-Man's offer taken: what would have told of the other end is
        // gone, and firing its name tells of nothing.
        g = play(dir, "c5a1", events);
        check(g != nullptr, "c5a1");
        if (g != nullptr)
        {
            logicKill(g->logic, nameId(*g, "loser_mm"));
            logicFire(g->logic, g->player, nameId(*g, "loser_mm"), -1, kUseToggle, 0);
            check(events.count == 0, "a name whose entities are gone is not told of");
        }
        // The guard fighting the zombie, killed by the player; and the cheat
        // that kills every zombie, not told of.
        g = play(dir, "c1a1", events);
        check(g != nullptr, "c1a1");
        if (g != nullptr)
        {
            int guard = -1;
            for (int i = 0; i < g->actors.count && guard < 0; ++i)
            {
                if (g->actors.states[i].active && g->actors.states[i].name == nameId(*g, "fighting_barney"))
                    guard = i;
            }
            check(guard >= 0, "the guard is there");
            if (guard >= 0)
            {
                for (int i = 0; i < 4 && events.count == 0; ++i)
                    monsterHurt(g->logic, guard, 200, false);
                check(events.count == 1 && events.list[0].type == AchEventType::Killed &&
                          events.list[0].kind == static_cast<std::uint8_t>(ActorKind::Guard) &&
                          events.list[0].hash == nameHash("fighting_barney") && events.list[0].flags == kAchByPlayer,
                      "the guard's death is told of, as the player's doing");
                AchievementTracker t;
                t.apply(nameHash("c1a1"), events, AchContext());
                check(t.has(kAchC1a1BarnDie), "and earns it");
            }
            events.count = 0;
            check(killAllOfKind(g->logic, ActorKind::Zombie) > 0 && events.count == 0 && events.muted == 0,
                  "a cheat's kills are not told of");
        }
    }
} // namespace

int main()
{
    testTable();
    testFile();
    testUnlock();
    testChapters();
    testFired();
    testKills();
    testOthers();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || dir[0] == '\0')
    {
        std::printf("test_achievements: HLDS_VALVE_DIR not set, the maps were not looked at\n");
    }
    else
    {
        testMaps(dir);
        testReports(dir);
    }
    if (g_failures == 0)
        std::printf("test_achievements: ok\n");
    return g_failures == 0 ? 0 : 1;
}
