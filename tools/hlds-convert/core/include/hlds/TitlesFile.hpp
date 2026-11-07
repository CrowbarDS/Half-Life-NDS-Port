#pragma once

// The game's titles.txt: what its messages say, and how each is shown -- the
// chapters' names, the names of the introduction, the hazard course's
// lessons. An env_message names one of them.
//
// The file is a run of settings ("$position -1 0.65", "$effect 2", ...) that
// stand until they are set again, and messages: a name on a line of its own,
// then its text between braces. A message is shown the way the settings
// stood where it is.

#include <map>
#include <string>

namespace hlds
{
    struct Title
    {
        std::string text;      ///< its lines, '\n' between them
        double x = -1, y = -1; ///< 0..1 of the screen; -1: in the middle
        int effect = 0;        ///< 0 fades in and out, 1 flickers, 2 is written out letter by letter
        int colour[3] = {100, 100, 100};
        int colour2[3] = {240, 110, 0}; ///< effect 2: what a letter is while it is new
        double fadeIn = 1.5;   ///< seconds (effect 2: for each letter)
        double fadeOut = 0.5;
        double hold = 1.2;
        double fxTime = 0.25;  ///< effect 2: how long a letter stays new
    };

    struct TitlesBook
    {
        std::map<std::string, Title> titles; ///< by name, upper-case
        const Title *find(const std::string &name) const;
    };

    TitlesBook parseTitles(const std::string &text);

    /// A text file's bytes as the game keeps its text, in Windows-1252 (as a
    /// translated game's titles.txt is): UTF-8 -- a file saved by a modern
    /// editor -- turned into it letter by letter, '?' for one it has not
    /// got; anything that is not UTF-8 taken as Windows-1252 already. A
    /// UTF-8 mark at the start is dropped.
    std::string toCp1252(const std::string &bytes);
} // namespace hlds
