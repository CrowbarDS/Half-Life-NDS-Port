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
        Card,   ///< reads off the card (data/CardRead.hpp), wherever they are made
        Lamp,   ///< the lamp's light on the walls (WorldDraw::lampPass: inside Emit)
        Tree,   ///< WorldDraw::walk alone: the tree and its culling (inside Walk)
        // Logic's parts (hldata::LogicPart, HlProfileParts.hpp), in its order:
        Fires, Marks, Links, Clips, Movers, Lights, Hazards, AiWorld, Pieces, Makers, Actors, Shots,
        Weapons,  ///< the weapon in hand (inside Logic)
        SndStart, ///< the pass's sounds started (inside Sound)
        Speech,   ///< what is said, and the mouth's meter decoding it (inside Sound)
        SndRoom,  ///< the room the player is in, four times a second (inside Sound)
        SndLoops, ///< the loops and what has a place, ten times a second (inside Sound)
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
        /// What one pass of the lamp's light drew and read (WorldDraw::
        /// lampPass), for perf.txt's "lamp" counts: each one's mean over the
        /// frames it was lit, and its most.
        struct LampCounts
        {
            int polygons = 0;  ///< sent to the hardware
            int vertices = 0;
            int surfaces = 0;  ///< lit (the light near enough, facing the eye)
            int triangles = 0; ///< of theirs read
            int pieces = 0;    ///< made (a cell, a strip's quad, a piece cut to a triangle)
            std::uint32_t divides = 0; ///< hardware divides and roots (dividesSoFar's difference)
        };
        void noteLamp(const LampCounts &counts);
        /// The hardware divides and roots so far (source/HwMath.s's count):
        /// one part's, by the difference.
        std::uint32_t dividesSoFar();
        /// Microseconds spent in a section during the last reported frame.
        std::uint32_t micros(Section section);
        /// Latch this frame's totals for micros() and clear the accumulators.
        void endFrame();
        /// How many blanks this frame (a pass) was given to simulate
        /// (`incoming`: the last pass's length), and whether it missed the
        /// game's pace itself (took more than GpuStats::kTargetBlanks): its
        /// sections are summed by the two, for perf.txt's "pace in" lines --
        /// at the same time simulated, what a slow pass spends more on is a
        /// cause, not what a slow pass before it brought -- and its "onset"
        /// line: the first slow pass after a run of quick ones given no
        /// more than the pace. Before endFrame.
        void notePace(int incoming, bool slow);
        /// The last pass: its frame's length, and its three longest sections
        /// (ms), into `out` -- for the save line (main.cpp's stepSave).
        void worstSections(char *out, std::size_t size);
        /// Samples the mouth's meter decoded this frame (inside Speech): what
        /// the speech costs a sample, in perf.txt.
        void noteSpeechSamples(std::uint32_t samples);
        /// What the movers' part took this frame by the kind of each record
        /// (hldata::LogicKind, `kinds` of them), and its longest record:
        /// perf.txt's "movers by kind". Before endFrame.
        void noteKinds(const std::uint32_t *spent, int kinds, int worstRecord, int worstKind, std::uint32_t worstTicks);
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
        /// The profiler's own work in the middle of a section -- its log
        /// written to the card, milliseconds on a DSi's SD -- taken back out
        /// of that section and of the frame's: what they say is the game's.
        /// (Within the frame that is timed: the sums are taken at its end.)
        struct Aside
        {
            explicit Aside(Section s) : section(s), start(now()) {}
            ~Aside()
            {
                const std::uint32_t spent = now() - start;
                add(section, 0u - spent);
                if (section != Section::Frame)
                    add(Section::Frame, 0u - spent);
            }
            Aside(const Aside &) = delete;
            Aside &operator=(const Aside &) = delete;
            Section section;
            std::uint32_t start;
        };
        /// Who is reading or writing the card now (the slowest reads say so:
        /// perf.txt's "cardslow:"); null, none that said.
        inline const char *cardWho = nullptr;
        struct CardWho
        {
            explicit CardWho(const char *who) : was(cardWho) { cardWho = who; }
            ~CardWho() { cardWho = was; }
            CardWho(const CardWho &) = delete;
            CardWho &operator=(const CardWho &) = delete;
            const char *was;
        };
    } // namespace Perf
#else
    namespace Perf
    {
        inline void begin() {}
        inline void endFrame() {}
        inline void notePace(int, bool) {}
        inline void noteSpeechSamples(std::uint32_t) {}
        inline void noteKinds(const std::uint32_t *, int, int, int, std::uint32_t) {}
        inline void logEvery(const char *, int) {}
        inline void markStack() {}
        inline void noteLibrary(const void *, std::uint32_t) {}
        inline void noteKeyRead(std::uint32_t) {}
        struct LampCounts
        {
            int polygons = 0, vertices = 0, surfaces = 0, triangles = 0, pieces = 0;
            std::uint32_t divides = 0;
        };
        inline void noteLamp(const LampCounts &) {}
        inline std::uint32_t dividesSoFar() { return 0; }
        inline std::uint32_t micros(Section) { return 0; }
        struct Scope
        {
            explicit Scope(Section) {}
        };
        struct Timed
        {
            explicit Timed(Section) {}
        };
        struct Aside
        {
            explicit Aside(Section) {}
        };
        struct CardWho
        {
            explicit CardWho(const char *) {}
        };
    } // namespace Perf
#endif
} // namespace hlrt::DebugLog
