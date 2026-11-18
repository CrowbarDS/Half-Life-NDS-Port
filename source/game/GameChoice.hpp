#pragma once

// Which of the card's games is played: Half-Life, or an expansion beside it
// in a folder of its own (shared/HlGame.hpp). A DSi's only, all of it (its
// code is in the DSi's own memory: HL_TWL_CODE). A DS plays Half-Life alone,
// as it always did, and none of this is asked of it.

#include <string>
#include <vector>

#include "HlGame.hpp"
#include "HlNetMaps.hpp"

namespace hlrt::choice
{
    /// The screen that says the game cannot go on (main.cpp's).
    using Halt = void (*)(const char *, const char *, const char *);

    /// The games on the card (the menu told), and the one to start with:
    /// the one played last, if it is there, else the first (activeGame and
    /// its folder set). Before the manifest is read.
    void pickStart();

    /// `game` played from now on, from the title (no map loaded): its
    /// common archive, its manifest -- its start and its matches' maps, in
    /// `startMap` and `multiplayer` -- its saves, and what was read of the
    /// last one's read again. Its data not readable: `halt` says why.
    void switchTo(hldata::GameId game, std::string &startMap, std::vector<hldata::NetMapInfo> &multiplayer,
                  Halt halt);

    /// CHOOSE GAME, until one is chosen; it is the one played from now on
    /// (switchTo), and offered first next time. True if it is another than
    /// the one there was.
    bool choose(std::string &startMap, std::vector<hldata::NetMapInfo> &multiplayer, Halt halt);

    /// At boot, a card of several games: CHOOSE GAME, and the chosen one's
    /// data read again if it is the one started with and did not read
    /// (`readOk`).
    void atBoot(bool readOk, std::string &startMap, std::vector<hldata::NetMapInfo> &multiplayer, Halt halt);

    /// The title's CHANGE GAME: CHOOSE GAME, and another's intro if another
    /// was chosen.
    void change(std::string &startMap, std::vector<hldata::NetMapInfo> &multiplayer, Halt halt);
} // namespace hlrt::choice
