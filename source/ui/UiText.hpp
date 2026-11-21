#pragma once

// The game's UI text: compiled from text/ui/<group>.txt (tools/uitext.py,
// run by make.sh) into tables, and read through uiText, by reference
// (txt::<group>::<Name>).
//
// Its rules (the owner's, 2026-11-21: a group may come to live in an overlay,
// where what was read is gone once another group takes its place):
//   - What uiText gives is for the draw that asked for it: never kept past
//     it. What is to be shown again (a row, a message that lasts) keeps the
//     TextRef and asks again each draw. tools/uitext_check.py fails the
//     build on a uiText result put in a member, a static or a field.
//   - From the main thread only: never the thinking worker, the network's
//     callbacks or an interrupt.
// A debug build (HLDS_DEBUG) holds them: it hands out each line in a ring
// that is spoiled at the end of every frame, so text kept to the next frame
// reads as garbage at once; and it halts on a call off the main thread.

#include "ui/UiText.gen.hpp"

namespace hlrt::ui
{
    /// The line `ref`: for this draw.
    const char *uiText(TextRef ref);

    /// The main thread's, called once as the program starts (main.cpp): the
    /// thread uiText may be called from.
    void textInit();

    /// The end of a frame (main.cpp): a debug build spoils what was handed
    /// out during it.
    void textFrameEnd();

    /// A row's label, a message's text: a line of the tables, or a line the
    /// program made itself (a game found, a name) and keeps where it is --
    /// never something uiText gave.
    struct Label
    {
        /// A line of the tables' group; none: a line made here.
        const TextGroupInfo *group = nullptr;
        union
        {
            std::uint16_t id; ///< (group) which line
            const char *made = nullptr;
        };
        constexpr Label() = default;
        constexpr Label(TextRef r) : group(r.group), id(r.id) {}
        constexpr Label(const char *line) : made(line) {}
        /// For this draw.
        const char *text() const;
    };
} // namespace hlrt::ui
