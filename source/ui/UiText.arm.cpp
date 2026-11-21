// (ARM: it asks the processor's mode, to refuse an interrupt's call.)
#include "ui/UiText.hpp"

#include <cstring>

#include <nds.h>

// The crash screen's halt (main.cpp; the overlay loader's own).
extern "C" [[noreturn]] void hlOvlHalt(const char *what, const char *detail);

namespace hlrt::ui
{
    namespace
    {
#ifdef HLDS_DEBUG_BUILD
        cothread_t mainThread = 0;
        bool started = false;
        // What was handed out this frame, a copy each: spoiled at its end.
        constexpr std::uint32_t kRing = 8192;
        char ring[kRing];
        std::uint32_t ringAt = 0;

        bool inInterrupt()
        {
            std::uint32_t cpsr;
            asm volatile("mrs %0, cpsr" : "=r"(cpsr));
            return (cpsr & 0x1F) == 0x12;
        }
#endif
    } // namespace

    void textInit()
    {
#ifdef HLDS_DEBUG_BUILD
        mainThread = cothread_get_current();
        started = true;
#endif
    }

    void textFrameEnd()
    {
#ifdef HLDS_DEBUG_BUILD
        // ('#' all through, a NUL at the end: kept text shows as #####.)
        std::memset(ring, '#', kRing - 1);
        ring[kRing - 1] = '\0';
        ringAt = 0;
#endif
    }

    const char *Label::text() const
    {
        if (group != nullptr)
            return uiText(TextRef{group, at});
        return made != nullptr ? made : "";
    }

    void putText(char *out, std::size_t size, TextRef ref)
    {
        if (size == 0)
            return;
        const char *text = uiText(ref);
        std::size_t length = std::strlen(text);
        if (length >= size)
            length = size - 1;
        std::memcpy(out, text, length);
        out[length] = '\0';
    }

    const char *uiText(TextRef ref)
    {
        const TextGroupInfo &group = *ref.group;
        const char *text = ref.at < group.size ? group.chars + ref.at : "";
#ifdef HLDS_DEBUG_BUILD
        if (inInterrupt())
            hlOvlHalt("UI text asked for from an interrupt:", group.name);
        if (started && cothread_get_current() != mainThread)
            hlOvlHalt("UI text asked for off the main thread:", group.name);
        if (ref.at >= group.size || (ref.at > 0 && group.chars[ref.at - 1] != '\0'))
            hlOvlHalt("UI text asked for where no line of it begins:", group.name);
        const std::uint32_t length = static_cast<std::uint32_t>(std::strlen(text)) + 1;
        if (length > kRing)
            return text;
        if (ringAt + length > kRing)
            ringAt = 0;
        char *copy = ring + ringAt;
        std::memcpy(copy, text, length);
        ringAt += (length + 3) & ~3u;
        return copy;
#else
        return text;
#endif
    }
} // namespace hlrt::ui
