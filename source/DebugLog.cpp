#include "DebugLog.hpp"

#ifdef HLDS_PROFILE

#include <cstdio>

#include <nds.h>

#include "Global.hpp"
#include "Guard.hpp"
#include "game/Think.hpp"

// (The linker's: where the stack starts, under DTCM's own data, which sits
// at the top of DTCM. It grows down to the bottom of DTCM.)
extern "C" char __sp_usr[];
// (HwMath.s: the divides and the roots the hardware has been asked for.)
extern "C" std::uint32_t hlHwCount[2];
// (The linker's: the program's code, and the top of the interrupt stack,
// under which the BIOS keeps what an interrupt broke into.)
extern "C" char __text_start[], __text_end[], __sp_irq[];

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
                                                "emit",  "pose",  "models", "frame", "monsters", "look", "hear", "going",
                                                "keys"};
        // Key reads since the last line: how many, the longest one, and what
        // of the worst frame was reading keys (does a read make that frame?).
        std::uint32_t keyReads = 0, keyLongest = 0, keysInWorstFrame = 0;
        // The sampler: timer 2 (0 and 1 are the clock above; 3 the
        // wireless's), kSampleHz times a second, adds one to the 256-byte
        // piece of code the ARM9 was in -- ITCM first, then the program, then
        // the game library, then what is none of them.
        constexpr int kSampleHz = 4000;
        constexpr std::uint32_t kPieceShift = 8;
        constexpr std::uint32_t kItcmPieces = 0x8000 >> kPieceShift;
        constexpr std::uint32_t kPieces = 6400;
        std::uint16_t pieces[kPieces];
        std::uint32_t textStart = 0, textBytes = 0, libraryStart = 0, libraryBytes = 0;
        std::uint32_t libraryFirst = kPieces; // its first piece
        volatile std::uint32_t outside = 0;    // samples in none of them
        volatile std::uint32_t idle = 0;       // ... asleep (the threads' sleep: CP15_WaitForInterrupt)
        std::uint32_t idleAt = 0;

        // (An interrupt handler: no divider, no root -- tools/irq-divider.py.)
        void sample()
        {
            // What the BIOS saved as the interrupt came: r0-r3, r12 and the
            // return address, at the top of the interrupt stack -- the
            // place broken into, plus 4.
            const std::uint32_t pc = reinterpret_cast<const std::uint32_t *>(__sp_irq)[-1] - 4;
            if (pc - idleAt < 12u)
            {
                idle = idle + 1;
                return;
            }
            std::uint32_t piece;
            if (pc - 0x01000000u < 0x8000u)
                piece = (pc - 0x01000000u) >> kPieceShift;
            else if (pc - 0x01FF8000u < 0x8000u) // (ITCM, called by its mirror: GameDll)
                piece = (pc - 0x01FF8000u) >> kPieceShift;
            else if (pc - textStart < textBytes)
                piece = kItcmPieces + ((pc - textStart) >> kPieceShift);
            else if (pc - libraryStart < libraryBytes)
                piece = libraryFirst + ((pc - libraryStart) >> kPieceShift);
            else
                piece = kPieces;
            if (piece < kPieces)
            {
                if (pieces[piece] != 0xFFFF)
                    ++pieces[piece];
            }
            else
            {
                outside = outside + 1;
            }
        }

        // The busiest pieces since the last line, where each is ("itcm",
        // "prog" or "dsl" and its offset), and how many of the samples.
        void writePieces(std::FILE *log)
        {
            constexpr int kShown = 40;
            int top[kShown];
            int shown = 0;
            std::uint32_t total = outside + idle;
            for (std::uint32_t i = 0; i < kPieces; ++i)
                total += pieces[i];
            std::fprintf(log, "pcs: %lu samples, %lu outside, %lu idle", static_cast<unsigned long>(total),
                         static_cast<unsigned long>(outside), static_cast<unsigned long>(idle));
            // (One pass, keeping the busiest kShown in order.)
            for (std::uint32_t i = 0; i < kPieces; ++i)
            {
                const std::uint16_t n = pieces[i];
                if (n == 0 || (shown == kShown && n <= pieces[top[shown - 1]]))
                    continue;
                int at = shown < kShown ? shown++ : kShown - 1;
                while (at > 0 && pieces[top[at - 1]] < n)
                {
                    top[at] = top[at - 1];
                    --at;
                }
                top[at] = static_cast<int>(i);
            }
            for (int k = 0; k < shown; ++k)
            {
                const std::uint32_t i = static_cast<std::uint32_t>(top[k]);
                const char *where = i < kItcmPieces ? "itcm" : i < libraryFirst ? "prog" : "dsl";
                const std::uint32_t first = i < kItcmPieces ? 0 : i < libraryFirst ? kItcmPieces : libraryFirst;
                std::fprintf(log, ", %s+%lx %u", where, static_cast<unsigned long>((i - first) << kPieceShift),
                             static_cast<unsigned>(pieces[i]));
            }
            std::fprintf(log, "\n");
        }

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
        const std::uint32_t *top = reinterpret_cast<const std::uint32_t *>(__sp_usr);
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
            std::fprintf(log, ", answered %lu, think stack %lu, key reads %lu, longest read %lu, keys in worst frame %lu\n",
                         static_cast<unsigned long>(think.answered), static_cast<unsigned long>(think.stackPeak()),
                         static_cast<unsigned long>(keyReads), static_cast<unsigned long>(toMicros(keyLongest)),
                         static_cast<unsigned long>(toMicros(keysInWorstFrame)));
            writePieces(log);
            std::fclose(log);
        }
        {
            const int saved = enterCriticalSection();
            for (std::uint16_t &n : pieces)
                n = 0;
            outside = 0;
            idle = 0;
            leaveCriticalSection(saved);
        }
        think.answered = 0;
        keyReads = keyLongest = keysInWorstFrame = 0;
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

    namespace
    {
        // Where each part's pieces are (the library is loaded before the
        // clock starts: whichever comes first lays them out).
        void layOut()
        {
            if (textBytes != 0)
                return;
            idleAt = reinterpret_cast<std::uint32_t>(&CP15_WaitForInterrupt);
            textStart = reinterpret_cast<std::uint32_t>(__text_start);
            textBytes = reinterpret_cast<std::uint32_t>(__text_end) - textStart;
            const std::uint32_t textPieces = (textBytes + 255) >> kPieceShift;
            libraryFirst = kItcmPieces + textPieces < kPieces ? kItcmPieces + textPieces : kPieces;
            if (libraryFirst == kPieces)
                textBytes = (kPieces - kItcmPieces) << kPieceShift;
        }
    } // namespace

    void begin()
    {
        cpuStartTiming(0);
        layOut();
        timerStart(2, ClockDivider_1, TIMER_FREQ(kSampleHz), sample);
    }

    void noteLibrary(const void *base, std::uint32_t codeBytes)
    {
        layOut();
        const std::uint32_t room = (kPieces - libraryFirst) << kPieceShift;
        libraryStart = reinterpret_cast<std::uint32_t>(base);
        libraryBytes = codeBytes < room ? codeBytes : room;
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

    void noteKeyRead(std::uint32_t ticks)
    {
        running[static_cast<int>(Section::Keys)] += ticks;
        ++keyReads;
        if (ticks > keyLongest)
            keyLongest = ticks;
    }

    void endFrame()
    {
        // (The worst frame yet: what of it was reading keys.)
        if (running[static_cast<int>(Section::Frame)] > worst[static_cast<int>(Section::Frame)])
            keysInWorstFrame = running[static_cast<int>(Section::Keys)];
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
