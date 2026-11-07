#include "global_header.hpp"

#include "Achievements.hpp"

#include <cstdio>

#include "Global.hpp"
#include "HlUi.hpp"
#include "Settings.hpp"
#include "audio/Sound.hpp"
#include "game/Game.hpp"
#include "game/Player.hpp"
#include "net/NetGame.hpp"
#include "render/TopText.hpp"

Achievements achievements;

#ifdef HLDS_CHILD
// A console that was sent the game plays matches between consoles only, where
// there are none to earn, and has no card to keep them on: nothing is
// tracked, nothing shown, and the game is given nowhere to report.
void Achievements::load() {}
bool Achievements::save() { return false; }
void Achievements::flush() {}
void Achievements::attach(const std::string &) { hlrt::game.logic.achievements = nullptr; }
void Achievements::travelled(const std::string &, const std::string &) {}
void Achievements::frame() {}
void Achievements::cheated() {}
void Achievements::cheatsSwitched(bool) {}
bool Achievements::held() const { return true; }
void Achievements::started() {}
bool Achievements::off() const { return true; }
#else
namespace
{
    /// How long one earned is shown: in, held, out (TopText::announce).
    constexpr int kShownMs = 3000;
    constexpr std::uint32_t kShownVblanks = (250 + kShownMs + 600) * 60 / 1000 + 12;

    /// Every tripmine c2a5d laid is still on its wall (one the player has
    /// laid is not the map's: it comes after the map's own actors).
    bool minesIntact()
    {
        const hldata::ActorWorld &actors = hlrt::game.actors;
        for (int i = 0; i < actors.recordCount && i < actors.count; ++i)
        {
            const hldata::ActorState &actor = actors.states[i];
            if (actor.kind == static_cast<std::uint8_t>(hldata::ActorKind::Tripmine) &&
                (!actor.active || actor.ai >= hldata::kAiDying))
                return false;
        }
        return true;
    }
} // namespace

void Achievements::load()
{
    if (!global.hasFat)
        return;
    std::FILE *file = std::fopen(global.dataPath("achievements.dat").c_str(), "rb");
    if (file == nullptr)
        return;
    std::uint8_t bytes[hldata::kAchievementsMostBytes];
    const std::size_t size = std::fread(bytes, 1, sizeof(bytes), file);
    std::fclose(file);
    hldata::readAchievements(bytes, size, tracker.data);
}

bool Achievements::save()
{
    tracker.unsaved = tracker.counted = false;
    if (!global.hasFat)
        return false;
    const std::vector<std::uint8_t> bytes = hldata::writeAchievements(tracker.data);
    std::FILE *file = std::fopen(global.dataPath("achievements.dat").c_str(), "wb");
    if (file == nullptr)
        return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    return std::fclose(file) == 0 && ok;
}

void Achievements::flush()
{
    if (tracker.unsaved || tracker.counted)
        save();
}

bool Achievements::off() const
{
    // (Not in a game between consoles, from its lobby to its end: none are
    // earned there, and none are shown over it.)
    return hlrt::netGame.on();
}

void Achievements::attach(const std::string &map)
{
    if (off())
    {
        hlrt::game.logic.achievements = nullptr;
        return;
    }
    map_ = hldata::nameHash(map.c_str());
    hldata::AchEvents &events = hlrt::game.achievements;
    events.count = 0;
    events.muted = 0;
    events.watchedCount = hldata::achievementsWatched(map_, events.watched, hldata::AchEvents::kWatchMost);
    hlrt::game.logic.achievements = &events;
}

bool Achievements::held() const { return settings.cheats; }

void Achievements::cheatsSwitched(bool on)
{
    if (on)
    {
        cheated();
        flush();
        return;
    }
    // (Off: what the cheats had on goes off with it. What they gave stays:
    // it cannot be told from what was found.)
    hlrt::game.cheats = hldata::Cheats();
    hlrt::player.noclip = false;
}

void Achievements::travelled(const std::string &left, const std::string &entered)
{
    if (off() || held())
        return;
    tracker.travelled(hldata::nameHash(left.c_str()), hldata::nameHash(entered.c_str()));
}

void Achievements::frame()
{
    hldata::AchEvents &events = hlrt::game.achievements;
    if (off())
    {
        events.count = 0;
        return;
    }
    // (With cheats on, what the game reports counts for nothing; Cheater,
    // earned as they were turned on, is still shown.)
    if (!held())
    {
        hldata::AchContext context;
        if (map_ == hldata::ach::kLiftMap && events.count > 0)
            context.minesIntact = minesIntact();
        tracker.apply(map_, events, context);
        tracker.standing(map_, hlrt::player.state.origin);
    }
    events.count = 0;
    if (tracker.unsaved)
        save();

    // One at a time, each when the last has gone.
    if (Global::vblanks() < shownUntil_)
        return;
    const int id = tracker.next();
    if (id < 0)
        return;
    // (Turned off: earned, listed, and not shown -- now or later.)
    if (!settings.achievementPopups)
    {
        while (tracker.next() >= 0)
        {
        }
        return;
    }
    std::snprintf(shown_, sizeof(shown_), "ACHIEVEMENT UNLOCKED\n%s", hldata::kAchievements[id].title);
    if (hlrt::topText.announce(shown_, kShownMs))
    {
        hlrt::sound.ui(hldata::kUiSoundPress);
        shownUntil_ = Global::vblanks() + kShownVblanks;
    }
    else
    {
        // (Both of the top screen's places are taken by what the map
        // writes: it waits its turn, a second from now.)
        tracker.showLater(id);
        shownUntil_ = Global::vblanks() + 60;
    }
}

void Achievements::cheated()
{
    if (off())
        return;
    tracker.unlock(hldata::kAchCheater);
}

void Achievements::started()
{
    if (held())
        return;
    tracker.unlock(hldata::kAchInit);
    flush();
}
#endif
