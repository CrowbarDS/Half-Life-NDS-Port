#include "DebugLog.hpp"

#ifdef HLDS_PROFILE

#include <cstdio>

#include <nds.h>

#include "Global.hpp"
#include "Guard.hpp"

// (The linker's: where the 16 KB of fast data memory starts. The stack is
// the top of it and grows down.)
extern "C" char __dtcm_start[];
// (HwMath.s: the divides and the roots the hardware has been asked for.)
extern "C" std::uint32_t hlHwCount[2];

namespace hlrt::DebugLog::Perf
{
    namespace
    {
        constexpr int kCount = static_cast<int>(Section::Count);
        std::uint32_t running[kCount];
        std::uint32_t latched[kCount];
        std::uint64_t summed[kCount];
        std::uint32_t worst[kCount];
        int framesIn = 0;
        constexpr std::uint32_t kStackPaint = 0x5AC3E5A1u;

        constexpr const char *kNames[kCount] = {"input", "logic", "move", "walk",
                                                "emit",  "pose",  "models", "frame", "monsters", "look", "hear", "going"};
        std::uint32_t toMicros(std::uint64_t ticks)
        {
            // The timer counts the 33.5 MHz bus clock.
            return static_cast<std::uint32_t>((ticks * 2u) / 67u);
        }
    } // namespace

    void markStack()
    {
        // From the bottom of the stack's room -- above the guard's pattern,
        // which is the fast memory's first words -- up to a little under
        // where the stack is now.
        volatile std::uint32_t here = 0;
        std::uint32_t *from = guard::stackLimit();
        std::uint32_t *to = const_cast<std::uint32_t *>(&here) - 64;
        for (; from < to; ++from)
            *from = kStackPaint;
    }

    void logEvery(const char *map, int frames)
    {
        if (framesIn < frames)
            return;
        // The lowest word the stack has written over.
        const std::uint32_t *low = guard::stackLimit();
        const std::uint32_t *top = reinterpret_cast<const std::uint32_t *>(__dtcm_start) + 16384 / 4;
        while (low < top && *low == kStackPaint)
            ++low;
        if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
        {
            std::fprintf(log, "time: %s %s %d frames, stack %d, divides %lu, roots %lu", map,
                         global.isDSi ? "DSi" : "DS", framesIn, static_cast<int>((top - low) * 4),
                         static_cast<unsigned long>(hlHwCount[0] / static_cast<unsigned>(framesIn)),
                         static_cast<unsigned long>(hlHwCount[1] / static_cast<unsigned>(framesIn)));
            for (int i = 0; i < kCount; ++i)
                std::fprintf(log, ", %s %lu/%lu", kNames[i],
                             static_cast<unsigned long>(toMicros(summed[i] / static_cast<unsigned>(framesIn))),
                             static_cast<unsigned long>(toMicros(worst[i])));
            std::fprintf(log, "\n");
            std::fclose(log);
        }
        for (int i = 0; i < kCount; ++i)
        {
            summed[i] = 0;
            worst[i] = 0;
        }
        framesIn = 0;
        hlHwCount[0] = hlHwCount[1] = 0;
        // (The frame the writing is in is not one of the game's.)
        for (int i = 0; i < kCount; ++i)
            running[i] = 0;
    }

    void begin()
    {
        cpuStartTiming(0);
    }

    std::uint32_t now()
    {
        return cpuGetTiming();
    }

    void add(Section section, std::uint32_t ticks)
    {
        running[static_cast<int>(section)] += ticks;
    }

    std::uint32_t micros(Section section)
    {
        // The timer counts the 33.5 MHz bus clock.
        return (latched[static_cast<int>(section)] * 2u) / 67u;
    }

    void endFrame()
    {
        for (int i = 0; i < kCount; ++i)
        {
            latched[i] = running[i];
            summed[i] += running[i];
            if (running[i] > worst[i])
                worst[i] = running[i];
            running[i] = 0;
        }
        ++framesIn;
    }
} // namespace hlrt::DebugLog::Perf

#endif
