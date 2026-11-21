#pragma once

// Timing the frame instead of reasoning about it.
//
// Perf is the port's profiler, and it exists because the first guess about
// where a frame goes is usually wrong: the first time this was measured, the
// renderer took 6 ms and the four lines of text reporting on it took 13.
//
// It is OPT-IN. Add 'HLDS_PROFILE=1' to the defines in build.py to build it
// in; without that every entry point below is an empty inline and the shipped
// ROM carries none of it. Sections are a fixed enum rather than strings so
// that a scope costs two timer reads and an add.

#include <cstdint>

namespace hlrt::DebugLog
{
    enum class Section
    {
        Input,
        Logic, ///< doors, triggers and the rest of the entity logic
        Move,  ///< the player's movement and its hull traces
        Walk,  ///< choosing the visible surfaces
        Emit,  ///< sending them to the geometry FIFO
        Pose,  ///< working out where the models' bones are
        Models, ///< the models' matrices and their commands (Pose and this are inside Emit)
        Frame, ///< everything between two vblank waits, overlay excluded
        Monsters, ///< what fights and thinks (monstersTick: inside Logic)
        AiLook,   ///< ... their looking and listening (HlAi.hpp's aiSpent)
        AiSched,  ///< ... their hearing, and their enemy looked to
        AiMove,   ///< ... their going about
        Keys,     ///< animation keys read off the card (data/KeyCache.cpp: a DS's key paging)
        // What else the ARM9 does, for what the ARM7 could take (Perf::Timed:
        // no phase of the I-cache profiler's).
        Sound,  ///< Sound::frame, in the game's frame
        Stream, ///< TextureStream::update: what is in view read off the card
        Vblank, ///< the VBlank interrupt (video memory written, sprites)
        Hblank, ///< the HBlank interrupt, every line (Nitro Engine's)
        Radio,  ///< the wireless's word from the ARM7, in the FIFO interrupt (local play)
        Count
    };

#ifdef HLDS_PROFILE
    namespace Perf
    {
        /// Start the clock. Timers 0 and 1 belong to the profiler from here on.
        void begin();
        std::uint32_t now();
        void add(Section section, std::uint32_t ticks);
        /// One read of a clip's keys off the card, `ticks` long: added to
        /// Section::Keys and counted, for the line's reads, its longest
        /// read, and what of the window's worst frame was reading keys.
        void noteKeyRead(std::uint32_t ticks);
        /// Microseconds spent in a section during the last reported frame.
        std::uint32_t micros(Section section);
        /// Latch this frame's totals for micros() and clear the accumulators.
        void endFrame();
        /// A line in perf.txt of what the sections took since the last one
        /// (each one's mean and its worst frame, in microseconds, and how
        /// deep the stack has been), once every so many frames. The emulator
        /// and the console both leave it, to be compared build with build.
        void logEvery(const char *map, int frames);
        /// Paint the unused stack, for logEvery to see how far it was used.
        void markStack();
        /// Where the game library's code is (GameDll), for the sampler: where
        /// the ARM9 is, several thousand times a second, counted by 256-byte
        /// piece of the program, of the library and of ITCM -- written with
        /// the rest (perf.txt "pcs:"; tools/pcsample.py names them).
        void noteLibrary(const void *base, std::uint32_t codeBytes);

#ifdef HLDS_ICACHE_PHASES
        /// (HLDS_ICACHE_PHASES: which part of the frame is running, for the
        /// I-cache profiler -- its melonDS counts misses by it: the section
        /// plus 1, 0 outside any. Its register is one the console does not
        /// have, tools/icache/melonds-icache.patch's; a write there is
        /// nothing on the console.)
        inline std::uint32_t phaseNow = 0;
        inline void setPhase(std::uint32_t phase)
        {
            phaseNow = phase;
            *reinterpret_cast<volatile std::uint32_t *>(0x04FFFA2C) = phase;
        }
#endif

        struct Scope
        {
#ifdef HLDS_ICACHE_PHASES
            // (The innermost section is the phase; the one around it again
            // once it ends.)
            explicit Scope(Section s) : section(s), start(now()), outer(phaseNow)
            {
                setPhase(static_cast<std::uint32_t>(s) + 1);
            }
            ~Scope()
            {
                add(section, now() - start);
                setPhase(outer);
            }
#else
            explicit Scope(Section s) : section(s), start(now()) {}
            ~Scope() { add(section, now() - start); }
#endif
            Scope(const Scope &) = delete;
            Scope &operator=(const Scope &) = delete;
            Section section;
            std::uint32_t start;
#ifdef HLDS_ICACHE_PHASES
            std::uint32_t outer;
#endif
        };
        /// A section's time and nothing else -- no phase: one an interrupt
        /// may time.
        struct Timed
        {
            explicit Timed(Section s) : section(s), start(now()) {}
            ~Timed() { add(section, now() - start); }
            Timed(const Timed &) = delete;
            Timed &operator=(const Timed &) = delete;
            Section section;
            std::uint32_t start;
        };
    } // namespace Perf
#else
    namespace Perf
    {
        inline void begin() {}
        inline void endFrame() {}
        inline void logEvery(const char *, int) {}
        inline void markStack() {}
        inline void noteLibrary(const void *, std::uint32_t) {}
        inline void noteKeyRead(std::uint32_t) {}
        inline std::uint32_t micros(Section) { return 0; }
        struct Scope
        {
            explicit Scope(Section) {}
        };
        struct Timed
        {
            explicit Timed(Section) {}
        };
    } // namespace Perf
#endif
} // namespace hlrt::DebugLog
