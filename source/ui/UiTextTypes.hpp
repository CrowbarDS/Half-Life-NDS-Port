#pragma once

// The types of the UI text tables (tools/uitext.py, text/ui/), apart from the
// tables themselves (UiText.gen.hpp), so that the generator's header and the
// accessor (UiText.hpp) can share them.

#include <cstdint>

namespace hlrt::ui
{
    /// One group of text, text/ui/<name>.txt: its lines one after another,
    /// each ending in a NUL, and where each begins.
    struct TextGroupInfo
    {
        const char *name;
        const char *chars;
        const std::uint16_t *at;
        std::uint16_t count;
    };

    /// One line of it: what is kept where text is to be shown later (a
    /// menu's rows, a message that lasts), never the text itself -- which
    /// uiText gives each time it is drawn.
    struct TextRef
    {
        const TextGroupInfo *group;
        std::uint16_t id;
    };
} // namespace hlrt::ui
