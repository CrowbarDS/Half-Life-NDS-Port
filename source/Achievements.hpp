#pragma once

// The achievements on the console: loaded once at boot, written when one is
// earned (and what is counted towards the others, at the quiet moments), and
// each shown as it is earned. What earns them is shared/HlAchievements.hpp;
// what the game tells them, shared/HlAchEvents.hpp.

#include <cstdint>
#include <string>

#include "HlAchievements.hpp"

class Achievements
{
public:
    hldata::AchievementTracker tracker;

    /// Read achievements.dat. Absent or unreadable: none earned yet.
    void load();
    /// Write achievements.dat. False if the card cannot be written.
    bool save();
    /// Write it if anything has been counted since it was last written.
    void flush();

    /// `map` has been loaded: the game is told where to report, and what
    /// this map's achievements listen for.
    void attach(const std::string &map);
    /// The player went from `left` to `entered` by a way between them.
    void travelled(const std::string &left, const std::string &entered);
    /// After each frame of the game: what it reported, and where the player
    /// is; and one earned, shown, if nothing else is being.
    void frame();
    /// The COMMAND line has been used to cheat.
    void cheated();
    /// The Cheats setting has just been turned on (Cheater is earned) or
    /// off (what the cheats had on is turned off).
    void cheatsSwitched(bool on);
    /// None but Cheater is earned now: the Cheats setting is on.
    bool held() const;
    /// The game has been started (the title).
    void started();
    /// None are earned or shown now: in a game between consoles, and ever
    /// on a console that was sent the game.
    bool off() const;

private:
    std::uint32_t map_ = 0;
    std::uint32_t shownUntil_ = 0; ///< a blank count: until the last one shown has gone
    char shown_[96] = {};          ///< what it says (TopText keeps a pointer to it)
};

extern Achievements achievements;
