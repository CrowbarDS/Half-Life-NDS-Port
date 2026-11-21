#pragma once

// The types of the UI text tables (tools/uitext.py, text/ui/), apart from the
// tables themselves (UiText.gen.hpp), so that the generator's header and the
// accessor (UiText.hpp) can share them.

#include <cstdint>

namespace hlrt::ui
{
    /// One group of text, text/ui/<name>.txt: its lines one after another,
    /// each ending in a NUL. In the program (`chars`), or in an overlay:
    /// then `bring`, its gate, loads it and says where it is.
    struct TextGroupInfo
    {
        const char *name;
        const char *chars;
        std::uint16_t size; ///< bytes, the last NUL with them
        const char *(*bring)();
    };

    /// One line of it: what is kept where text is to be shown later (a
    /// menu's rows, a message that lasts), never the text itself -- which
    /// uiText gives each time it is drawn.
    struct TextRef
    {
        const TextGroupInfo *group;
        std::uint16_t at; ///< where the line begins in the group's text (so no table of where)
    };
} // namespace hlrt::ui
