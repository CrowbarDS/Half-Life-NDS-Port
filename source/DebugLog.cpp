#include "DebugLog.hpp"
#include "game/SaveGame.hpp"

#ifdef HLDS_PROFILE

#include <cstdio>

#include <nds.h>

#include "Global.hpp"
#include "Guard.hpp"
#include "HlProfileParts.hpp"
#include "HlTraceCount.hpp"
#include "game/Think.hpp"

// (The linker's: where the stack starts, under DTCM's own data, which sits
// at the top of DTCM. It grows down to the bottom of DTCM.)
extern "C" char __sp_usr[];
// The interrupts' stack (ds_arm9.ld): from __dtcm_data_top up to __sp_irq_user.
extern "C" char __dtcm_data_top[], __sp_irq_user[];
// (HwMath.s: the divides and the roots the hardware has been asked for.)
// (divides, roots, divides left to the library, the library's 32-bit divides: HwMath.s)
extern "C" std::uint32_t hlHwCount[4];
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
        // notePace: this frame's; each section summed by the blanks it was
        // given (up to 2, 3, 4 or more) and whether it was slow, since the
        // last line; and over the onsets: a slow frame after at least
        // kOnsetRun quick ones, all given no more than the pace.
        constexpr int kPaceBuckets = 3, kOnsetRun = 8;
        bool paceSlow = false;
        int paceIn = 0;
        std::uint64_t paceSum[kPaceBuckets][2][kCount];
        std::uint32_t paceFrames[kPaceBuckets][2];
        std::uint64_t onsetSum[kCount];
        std::uint32_t onsets = 0, quickRun = 0;
        std::uint64_t speechSamples = 0; // noteSpeechSamples, since the last line
        // noteKinds: this frame's, by kind of record; summed over all frames,
        // and over the slow and the quick given no more than the pace; and
        // the longest one record since the last line.
        constexpr int kKinds = hldata::kRecordKinds;
        std::uint32_t kindFrame[kKinds];
        std::uint64_t kindAll[kKinds], kindPace[2][kKinds];
        std::uint32_t recordLongest = 0;
        int recordLongestIndex = -1, recordLongestKind = 0;
        constexpr const char *kKindNames[] = {
                "static", "door", "button", "breakable", "wall_toggle", "trigger_once", "trigger_multiple", "trigger_auto",
                "trigger_relay", "multi_manager", "multi_source", "train", "ladder", "door_rotating", "change_level", "env_global",
                "rotating", "track_train", "track_change", "pushable", "trigger_hurt", "trigger_push", "trigger_teleport", "auto_save",
                "script", "maker", "health_charger", "suit_charger", "ambient", "music", "sprite", "beam",
                "explosion", "shake", "spark", "shooter", "render", "fade", "message", "conveyor",
                "plat_rot", "light", "mark", "pendulum", "beverage", "funnel", "sentence", "tank",
                "tank_controls", "mortar_field", "momentary_button", "momentary_door", "momma_node", "target", "repel", "trigger_gravity",
                "transition", "counter", "change_target", "weapon_strip", "load_saved", "end_section", "monster_jump", "camera",
                "speaker", "gun_target", "button_target", "blood", "friction", "game_counter", "game_counter_set", "player_freeze",
                "warp_ball"};
        static_assert(sizeof(kKindNames) / sizeof(kKindNames[0]) == static_cast<int>(hldata::LogicKind::Count),
                      "a name for each kind of record");
        constexpr std::uint32_t kStackPaint = 0x5AC3E5A1u;

        constexpr const char *kNames[kCount] = {"input", "logic", "move", "walk",
                                                "emit",  "pose",  "models", "frame", "monsters", "look", "hear", "going",
                                                "keys", "sound", "stream", "vblank", "hblank", "radio", "card", "lamp", "tree",
                                                "fires", "marks", "links", "clips", "movers", "lights", "hazards", "aiworld",
                                                "pieces", "makers", "actors", "shots", "weapons", "sndstart", "speech",
                                                "sndroom", "sndloops"};
        // Key reads since the last line: how many, the longest one, and what
        // of the worst frame was reading keys (does a read make that frame?).
        std::uint32_t keyReads = 0, keyLongest = 0, keysInWorstFrame = 0;
        // Every read of the card's sectors (libnds's disk_read, wrapped: the
        // card's own driver, DLDI or the DSi's SD), whoever asked: FatFs's own
        // ones (its tables: a seek's walk down a file's chain, a folder) told
        // apart from a file's. In the window, and the most in one pass.
        // (Not the profiler's own: its log is written with cardAside set.)
        struct CardReads
        {
            std::uint32_t reads = 0, sectors = 0, ticks = 0;
            std::uint32_t fatReads = 0, fatTicks = 0;
            std::uint32_t longest = 0; ///< one read
        };
        CardReads cardWindow;
        std::uint32_t cardFrame = 0, cardFrameMost = 0, cardFrameReads = 0, cardFrameReadsMost = 0;
        bool cardAside = false;
        // ... its writes (the saves'), in the window, and passes since the
        // last: a card may be slow to read for a while after it is written.
        std::uint32_t cardWrites = 0, cardWriteSectors = 0, cardWriteTicks = 0, cardWriteLongest = 0;
        std::uint32_t passesSinceWrite = 0xFFFF;
        // ... and the window's slowest reads, each with who asked (CardWho).
        struct SlowRead
        {
            std::uint32_t ticks = 0, sector = 0, count = 0, sinceWrite = 0;
            bool fat = false;
            const char *who = nullptr;
        };
        constexpr int kSlowReads = 8;
        SlowRead slowReads[kSlowReads];
        // ... and its slowest writes, each with who wrote (CardWho; none: the
        // profiler's own log lines, the main one apart).
        struct SlowWrite
        {
            std::uint32_t ticks = 0, sector = 0, count = 0;
            const char *who = nullptr;
        };
        constexpr int kSlowWrites = 6;
        SlowWrite slowWrites[kSlowWrites];
        // ... and all of them by who made them (CardWho; the last for the rest).
        struct WhoWrote
        {
            const char *who = nullptr;
            std::uint32_t writes = 0, sectors = 0, ticks = 0;
        };
        constexpr int kWhoWrote = 6;
        WhoWrote whoWrote[kWhoWrote];
        // The lamp's counts since the last line (noteLamp): summed and most,
        // and in how many passes; the divides it made.
        constexpr int kLampCounts = 6;
        std::uint32_t lampSum[kLampCounts], lampMost[kLampCounts], lampPasses = 0;
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
        // The worst frame of the monsters' since the last line: where each of
        // its samples was (the sampler fills framePcs as the frame goes;
        // endFrame keeps it, if it was the worst), for "worstpcs:" -- what a
        // spike is, which the whole period's pieces cannot say. (A frame of
        // 64 ms at most: past that its samples are not kept.)
        constexpr std::uint32_t kFramePcs = 256;
        std::uint32_t framePcs[kFramePcs];
        volatile std::uint32_t frameSampled = 0;
        std::uint32_t worstPcs[kFramePcs];
        std::uint32_t worstCount = 0, worstMonsters = 0, worstFrameTicks = 0;

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
            {
                const std::uint32_t n = frameSampled;
                if (n < kFramePcs)
                {
                    framePcs[n] = pc;
                    frameSampled = n + 1;
                }
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

        // Where `pc` is, as the pieces say it: "itcm", "prog" or "dsl" and
        // its offset; false if in none of them.
        bool placeOf(std::uint32_t pc, const char *&where, std::uint32_t &offset)
        {
            if (pc - 0x01000000u < 0x8000u || pc - 0x01FF8000u < 0x8000u)
            {
                where = "itcm";
                offset = (pc - 0x01000000u < 0x8000u) ? pc - 0x01000000u : pc - 0x01FF8000u;
            }
            else if (pc - textStart < textBytes)
            {
                where = "prog";
                offset = pc - textStart;
            }
            else if (pc - libraryStart < libraryBytes)
            {
                where = "dsl";
                offset = pc - libraryStart;
            }
            else
            {
                return false;
            }
            return true;
        }

        std::uint32_t toMicros(std::uint64_t ticks);

        // The worst frame of the monsters': its time and the frame's, and
        // where each of its samples was.
        void writeWorst(std::FILE *log)
        {
            std::fprintf(log, "worstpcs: monsters %lu, frame %lu, %lu samples",
                         static_cast<unsigned long>(toMicros(worstMonsters)),
                         static_cast<unsigned long>(toMicros(worstFrameTicks)), static_cast<unsigned long>(worstCount));
            for (std::uint32_t i = 0; i < worstCount; ++i)
            {
                const char *where;
                std::uint32_t offset;
                if (placeOf(worstPcs[i], where, offset))
                    std::fprintf(log, ", %s+%lx", where, static_cast<unsigned long>(offset));
            }
            std::fprintf(log, "\n");
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

        // Where the divides and the roots come from (HwMath.s calls
        // hlProfileSite, interrupts held off): the return address of each
        // call, and how many. A table each, looked up by a hash of the
        // address; one that finds no room in eight tries is counted lost.
        // (div32Sites: the library's 32-bit divides, which keep to it.)
        struct Site
        {
            std::uint32_t at, count;
        };
        constexpr int kDivSites = 256, kRootSites = 64, kDiv32Sites = 512;
        Site divSites[kDivSites], rootSites[kRootSites], div32Sites[kDiv32Sites];
        // Who traces, and how long it takes (shared/HlTraceCount.hpp): the
        // line that called, which trace (kTrace*, kTraceOnWorker on the think
        // worker's thread), how many times and the time in all.
        struct TraceSite
        {
            std::uint32_t at, kind, count, ticks;
        };
        constexpr int kTraceSites = 512;
        TraceSite traceSites[kTraceSites];
        std::uint32_t traceSitesLost = 0;
        std::uint32_t divFits = 0, sitesLost = 0, div32Lost = 0;

        void writeSites(std::FILE *log, const char *what, Site *sites, int count)
        {
            constexpr int kShown = 40;
            int top[kShown];
            int shown = 0;
            for (int i = 0; i < count; ++i)
            {
                const std::uint32_t n = sites[i].count;
                if (n == 0 || (shown == kShown && n <= sites[top[shown - 1]].count))
                    continue;
                int at = shown < kShown ? shown++ : kShown - 1;
                while (at > 0 && sites[top[at - 1]].count < n)
                {
                    top[at] = top[at - 1];
                    --at;
                }
                top[at] = i;
            }
            std::fprintf(log, "%s: %d frames", what, framesIn);
            if (sites == divSites)
                std::fprintf(log, ", %lu fit 32 bits, %lu lost", static_cast<unsigned long>(divFits),
                             static_cast<unsigned long>(sitesLost));
            if (sites == div32Sites)
                std::fprintf(log, ", %lu divides, %lu lost", static_cast<unsigned long>(hlHwCount[3]),
                             static_cast<unsigned long>(div32Lost));
            for (int k = 0; k < shown; ++k)
                std::fprintf(log, ", %lx %lu", static_cast<unsigned long>(sites[top[k]].at),
                             static_cast<unsigned long>(sites[top[k]].count));
            std::fprintf(log, "\n");
        }

        // The costliest callers of the traces, by their time.
        void writeTraceSites(std::FILE *log)
        {
            constexpr int kShown = 48;
            int top[kShown];
            int shown = 0;
            for (int i = 0; i < kTraceSites; ++i)
            {
                const std::uint32_t t = traceSites[i].ticks;
                if (traceSites[i].count == 0 || (shown == kShown && t <= traceSites[top[shown - 1]].ticks))
                    continue;
                int at = shown < kShown ? shown++ : kShown - 1;
                while (at > 0 && traceSites[top[at - 1]].ticks < t)
                {
                    top[at] = top[at - 1];
                    --at;
                }
                top[at] = i;
            }
            std::fprintf(log, "tracesites: %d frames, %lu lost", framesIn, static_cast<unsigned long>(traceSitesLost));
            for (int k = 0; k < shown; ++k)
            {
                const TraceSite &site = traceSites[top[k]];
                std::fprintf(log, ", %lx %lu %lu %lu", static_cast<unsigned long>(site.at),
                             static_cast<unsigned long>(site.kind), static_cast<unsigned long>(site.count),
                             static_cast<unsigned long>(toMicros(site.ticks)));
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
        // And the interrupts' (none is in it: the interrupts held off).
        const int saved = enterCriticalSection();
        for (auto *w = reinterpret_cast<std::uint32_t *>(__dtcm_data_top);
             w < reinterpret_cast<std::uint32_t *>(__sp_irq_user); ++w)
            *w = kStackPaint;
        leaveCriticalSection(saved);
    }

    void logEvery(const char *map, int frames)
    {
        if (framesIn < frames)
            return;
        // (What the log itself reads of the card -- its folder, the end of
        // the file -- is not the game's.)
        cardAside = true;
        struct Back
        {
            ~Back() { cardAside = false; }
        } back;
        // The lowest word the stack has written over.
        const std::uint32_t *low = guard::stackLimit();
        const std::uint32_t *top = reinterpret_cast<const std::uint32_t *>(__sp_usr);
        while (low < top && *low == kStackPaint)
            ++low;
        const std::uint32_t *irqLow = reinterpret_cast<const std::uint32_t *>(__dtcm_data_top);
        const std::uint32_t *irqTop = reinterpret_cast<const std::uint32_t *>(__sp_irq_user);
        while (irqLow < irqTop && *irqLow == kStackPaint)
            ++irqLow;
        if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
        {
            std::fprintf(log,
                         "time: %s %s %d frames, stack %d, irq stack %d, waiter stack %lu, divides %lu (library %lu), "
                         "roots %lu",
                         map, global.isDSi ? "DSi" : "DS", framesIn, static_cast<int>((top - low) * 4),
                         static_cast<int>((irqTop - irqLow) * 4),
                         static_cast<unsigned long>(Global::vblankWaiterPeak()),
                         static_cast<unsigned long>(hlHwCount[0] / static_cast<unsigned>(framesIn)),
                         static_cast<unsigned long>(hlHwCount[2] / static_cast<unsigned>(framesIn)),
                         static_cast<unsigned long>(hlHwCount[1] / static_cast<unsigned>(framesIn)));
            for (int i = 0; i < kCount; ++i)
                std::fprintf(log, ", %s %lu/%lu", kNames[i],
                             static_cast<unsigned long>(toMicros(summed[i] / static_cast<unsigned>(framesIn))),
                             static_cast<unsigned long>(toMicros(worst[i])));
            // (How long the worker's answers took, asked to taken up, in
            // frames: whether asking sooner would be worth it.)
            const Think::Waits waits = think.takeWaits();
            std::fprintf(log, ", answers taken %lu, waited %lu frames each, longest %lu, dropped %lu, gave up %lu",
                         static_cast<unsigned long>(waits.taken),
                         static_cast<unsigned long>(waits.taken != 0 ? waits.frames / waits.taken : 0),
                         static_cast<unsigned long>(waits.longest), static_cast<unsigned long>(waits.dropped),
                         static_cast<unsigned long>(waits.gaveUp));
            // ... by kind: answers, mean and longest wait, and the oldest
            // still waiting, in frames.
            static const char *const kQuestionNames[hldata::kThinkKinds] = {"none", "plan", "detour", "look", "cover", "route"};
            for (int k = 1; k < hldata::kThinkKinds; ++k)
            {
                if (waits.takenOf[k] == 0 && waits.waitingOf[k] == 0)
                    continue;
                std::fprintf(log, ", %s %lu waited %lu longest %lu waiting %lu", kQuestionNames[k],
                             static_cast<unsigned long>(waits.takenOf[k]),
                             static_cast<unsigned long>(waits.takenOf[k] != 0 ? waits.framesOf[k] / waits.takenOf[k] : 0),
                             static_cast<unsigned long>(waits.longestOf[k]), static_cast<unsigned long>(waits.waitingOf[k]));
            }
            // (The card: reads a pass, mean; its sectors as KB a pass; time a
            // pass, mean and most; reads in the pass with most; the longest
            // read; and of the reads, FatFs's own -- a seek's walk, a folder --
            // in the window, and their time a pass.)
            std::fprintf(log, ", card reads %lu, %lu KB, %lu/%lu us, most reads %lu, longest read %lu us, fat reads %lu (%lu us)",
                         static_cast<unsigned long>(cardWindow.reads / static_cast<unsigned>(framesIn)),
                         static_cast<unsigned long>(cardWindow.sectors / 2u / static_cast<unsigned>(framesIn)),
                         static_cast<unsigned long>(toMicros(cardWindow.ticks / static_cast<unsigned>(framesIn))),
                         static_cast<unsigned long>(toMicros(cardFrameMost)),
                         static_cast<unsigned long>(cardFrameReadsMost),
                         static_cast<unsigned long>(toMicros(cardWindow.longest)),
                         static_cast<unsigned long>(cardWindow.fatReads),
                         static_cast<unsigned long>(toMicros(cardWindow.fatTicks / static_cast<unsigned>(framesIn))));
            std::fprintf(log, ", card writes %lu (%lu KB, %lu us, longest %lu us)", static_cast<unsigned long>(cardWrites),
                         static_cast<unsigned long>(cardWriteSectors / 2u),
                         static_cast<unsigned long>(toMicros(cardWriteTicks)),
                         static_cast<unsigned long>(toMicros(cardWriteLongest)));
            std::fprintf(log, ", answered %lu, woken for none %lu, think stack %lu, save stack %lu, key reads %lu, "
                         "longest read %lu, keys in worst frame %lu\n",
                         static_cast<unsigned long>(think.answered), static_cast<unsigned long>(think.idleWakes),
                         static_cast<unsigned long>(think.stackPeak()),
                         static_cast<unsigned long>(SaveGame::stackPeak()),
                         static_cast<unsigned long>(keyReads), static_cast<unsigned long>(toMicros(keyLongest)),
                         static_cast<unsigned long>(toMicros(keysInWorstFrame)));
            // The window's slowest reads of the card: how long, where (sector
            // and how many), FatFs's own or a file's, who asked, and how many
            // passes after the card was last written.
            if (slowReads[0].ticks != 0)
            {
                std::fprintf(log, "cardslow: %s", map);
                for (const SlowRead &slow : slowReads)
                {
                    if (slow.ticks == 0)
                        continue;
                    std::fprintf(log, "; %lu us at %lu x%lu %s %s, %s", static_cast<unsigned long>(toMicros(slow.ticks)),
                                 static_cast<unsigned long>(slow.sector), static_cast<unsigned long>(slow.count),
                                 slow.fat ? "fat" : "data", slow.who != nullptr ? slow.who : "?",
                                 slow.sinceWrite >= 0xFFFF ? "no write yet" : "");
                    if (slow.sinceWrite < 0xFFFF)
                        std::fprintf(log, "%lu passes after a write", static_cast<unsigned long>(slow.sinceWrite));
                }
                std::fprintf(log, "\n");
            }
            if (slowWrites[0].ticks != 0 || slowWrites[1].ticks != 0)
            {
                std::fprintf(log, "cardwrites: %s", map);
                for (const SlowWrite &slow : slowWrites)
                {
                    if (slow.ticks != 0)
                        std::fprintf(log, "; %lu us at %lu x%lu %s", static_cast<unsigned long>(toMicros(slow.ticks)),
                                     static_cast<unsigned long>(slow.sector), static_cast<unsigned long>(slow.count),
                                     slow.who != nullptr ? slow.who : "?");
                }
                std::fprintf(log, "\n");
                // (By who: "?" is mostly this file's own lines.)
                std::fprintf(log, "cardwho: %s", map);
                for (const WhoWrote &w : whoWrote)
                {
                    if (w.writes != 0)
                        std::fprintf(log, "; %s %lu writes, %lu KB, %lu ms", w.who != nullptr ? w.who : "?",
                                     static_cast<unsigned long>(w.writes), static_cast<unsigned long>(w.sectors / 2),
                                     static_cast<unsigned long>(toMicros(w.ticks) / 1000));
                }
                std::fprintf(log, "\n");
            }
            // Where a pass that missed the game's pace spent its time, beside
            // one that did not, at the same time given to simulate (notePace):
            // each section's mean over each, in microseconds.
            static const char *const kBuckets[kPaceBuckets] = {"2", "3", "4+"};
            for (int b = 0; b < kPaceBuckets; ++b)
            {
                const std::uint32_t slow = paceFrames[b][1], quick = paceFrames[b][0];
                if (slow + quick == 0)
                    continue;
                std::fprintf(log, "pace in %s: %s %s slow %lu quick %lu", kBuckets[b], map,
                             global.isDSi ? "DSi" : "DS", static_cast<unsigned long>(slow),
                             static_cast<unsigned long>(quick));
                for (int i = 0; i < kCount; ++i)
                    std::fprintf(log, ", %s %lu/%lu", kNames[i],
                                 static_cast<unsigned long>(slow != 0 ? toMicros(paceSum[b][1][i] / slow) : 0),
                                 static_cast<unsigned long>(quick != 0 ? toMicros(paceSum[b][0][i] / quick) : 0));
                std::fprintf(log, "\n");
            }
            // ... and where the first slow pass after quick ones spent it
            // (beside "pace in 2"'s quick ones), and what speech costs.
            if (onsets != 0)
            {
                std::fprintf(log, "onset: %s %s %lu", map, global.isDSi ? "DSi" : "DS", static_cast<unsigned long>(onsets));
                for (int i = 0; i < kCount; ++i)
                    std::fprintf(log, ", %s %lu", kNames[i], static_cast<unsigned long>(toMicros(onsetSum[i] / onsets)));
                std::fprintf(log, "\n");
            }
            // The movers' part by the kind of record: the costliest kinds,
            // each one's mean over every frame, and over the slow and the
            // quick given no more than the pace; and the longest one record.
            {
                const std::uint32_t slow = paceFrames[0][1], quick = paceFrames[0][0];
                bool shown[kKinds] = {};
                std::fprintf(log, "movers by kind: %s %s, slow %lu quick %lu", map, global.isDSi ? "DSi" : "DS",
                             static_cast<unsigned long>(slow), static_cast<unsigned long>(quick));
                for (int n = 0; n < 10; ++n)
                {
                    int best = -1;
                    for (int k = 0; k < kKinds; ++k)
                    {
                        if (!shown[k] && kindAll[k] != 0 && (best < 0 || kindAll[k] > kindAll[best]))
                            best = k;
                    }
                    if (best < 0 || toMicros(kindAll[best] / static_cast<unsigned>(framesIn)) < 5)
                        break;
                    shown[best] = true;
                    const int named = static_cast<int>(sizeof(kKindNames) / sizeof(kKindNames[0]));
                    std::fprintf(log, ", %s %lu (%lu/%lu)", best < named ? kKindNames[best] : "?",
                                 static_cast<unsigned long>(toMicros(kindAll[best] / static_cast<unsigned>(framesIn))),
                                 static_cast<unsigned long>(slow != 0 ? toMicros(kindPace[1][best] / slow) : 0),
                                 static_cast<unsigned long>(quick != 0 ? toMicros(kindPace[0][best] / quick) : 0));
                }
                const int named = static_cast<int>(sizeof(kKindNames) / sizeof(kKindNames[0]));
                if (recordLongestIndex >= 0)
                    std::fprintf(log, "; longest record #%d %s %lu us", recordLongestIndex,
                                 recordLongestKind < named ? kKindNames[recordLongestKind] : "?",
                                 static_cast<unsigned long>(toMicros(recordLongest)));
                std::fprintf(log, "\n");
            }
            if (speechSamples != 0)
                std::fprintf(log, "speech: %s %s %lu samples a pass, %lu us a thousand\n", map,
                             global.isDSi ? "DSi" : "DS", static_cast<unsigned long>(speechSamples / framesIn),
                             static_cast<unsigned long>(toMicros(summed[static_cast<int>(Section::Speech)] * 1000 /
                                                                 speechSamples)));
            if (lampPasses != 0)
            {
                static const char *const kLampNames[kLampCounts] = {"polygons", "vertices", "surfaces",
                                                                    "triangles", "pieces", "divides"};
                std::fprintf(log, "lamp: %lu passes", static_cast<unsigned long>(lampPasses));
                for (int i = 0; i < kLampCounts; ++i)
                    std::fprintf(log, ", %s %lu/%lu", kLampNames[i],
                                 static_cast<unsigned long>(lampSum[i] / lampPasses),
                                 static_cast<unsigned long>(lampMost[i]));
                std::fprintf(log, "\n");
            }
            writePieces(log);
            writeWorst(log);
            writeSites(log, "divsites", divSites, kDivSites);
            writeTraceSites(log);
            writeSites(log, "rootsites", rootSites, kRootSites);
            writeSites(log, "div32sites", div32Sites, kDiv32Sites);
            std::fclose(log);
        }
        {
            const int saved = enterCriticalSection();
            for (std::uint16_t &n : pieces)
                n = 0;
            outside = 0;
            idle = 0;
            for (Site &site : divSites)
                site = Site{};
            for (Site &site : rootSites)
                site = Site{};
            for (Site &site : div32Sites)
                site = Site{};
            div32Lost = 0;
            for (TraceSite &site : traceSites)
                site = TraceSite{};
            traceSitesLost = 0;
            divFits = sitesLost = 0;
            leaveCriticalSection(saved);
        }
        think.answered = 0;
        think.idleWakes = 0;
        worstCount = worstMonsters = worstFrameTicks = 0;
        keyReads = keyLongest = keysInWorstFrame = 0;
        cardWindow = CardReads();
        cardFrameMost = cardFrameReadsMost = 0;
        cardWrites = cardWriteSectors = cardWriteTicks = cardWriteLongest = 0;
        for (SlowRead &slow : slowReads)
            slow = SlowRead();
        for (SlowWrite &slow : slowWrites)
            slow = SlowWrite();
        for (WhoWrote &w : whoWrote)
            w = WhoWrote();
        for (int i = 0; i < kLampCounts; ++i)
            lampSum[i] = lampMost[i] = 0;
        lampPasses = 0;
        for (int i = 0; i < kCount; ++i)
        {
            summed[i] = 0;
            worst[i] = 0;
            onsetSum[i] = 0;
            for (int b = 0; b < kPaceBuckets; ++b)
                paceSum[b][0][i] = paceSum[b][1][i] = 0;
        }
        for (int b = 0; b < kPaceBuckets; ++b)
            paceFrames[b][0] = paceFrames[b][1] = 0;
        onsets = 0;
        speechSamples = 0;
        for (int k = 0; k < kKinds; ++k)
            kindAll[k] = kindPace[0][k] = kindPace[1][k] = 0;
        recordLongest = 0;
        recordLongestIndex = -1;
        framesIn = 0;
        hlHwCount[0] = hlHwCount[1] = hlHwCount[2] = hlHwCount[3] = 0;
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

    void noteLamp(const LampCounts &counts)
    {
        const std::uint32_t values[kLampCounts] = {
            static_cast<std::uint32_t>(counts.polygons), static_cast<std::uint32_t>(counts.vertices),
            static_cast<std::uint32_t>(counts.surfaces), static_cast<std::uint32_t>(counts.triangles),
            static_cast<std::uint32_t>(counts.pieces), counts.divides};
        for (int i = 0; i < kLampCounts; ++i)
        {
            lampSum[i] += values[i];
            lampMost[i] = values[i] > lampMost[i] ? values[i] : lampMost[i];
        }
        ++lampPasses;
    }

    std::uint32_t dividesSoFar()
    {
        return hlHwCount[0] + hlHwCount[1];
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

    void worstSections(char *out, std::size_t size)
    {
        // (The last pass's three longest, but the frame, which holds them.)
        int top[3] = {-1, -1, -1};
        for (int i = 0; i < kCount; ++i)
        {
            if (i == static_cast<int>(Section::Frame))
                continue;
            for (int k = 0; k < 3; ++k)
            {
                if (top[k] < 0 || latched[i] > latched[top[k]])
                {
                    for (int m = 2; m > k; --m)
                        top[m] = top[m - 1];
                    top[k] = i;
                    break;
                }
            }
        }
        std::snprintf(out, size, "frame %lu ms: %s %lu, %s %lu, %s %lu",
                      static_cast<unsigned long>(micros(Section::Frame) / 1000), kNames[top[0]],
                      static_cast<unsigned long>(latched[top[0]] * 2u / 67u / 1000), kNames[top[1]],
                      static_cast<unsigned long>(latched[top[1]] * 2u / 67u / 1000), kNames[top[2]],
                      static_cast<unsigned long>(latched[top[2]] * 2u / 67u / 1000));
    }

    void noteKeyRead(std::uint32_t ticks)
    {
        running[static_cast<int>(Section::Keys)] += ticks;
        ++keyReads;
        if (ticks > keyLongest)
            keyLongest = ticks;
    }

    void notePace(int incoming, bool slow)
    {
        paceIn = incoming;
        paceSlow = slow;
    }

    void noteSpeechSamples(std::uint32_t samples) { speechSamples += samples; }

    void noteKinds(const std::uint32_t *spent, int kinds, int worstRecord, int worstKind, std::uint32_t worstTicks)
    {
        for (int k = 0; k < kinds && k < kKinds; ++k)
            kindFrame[k] += spent[k];
        if (worstRecord >= 0 && worstTicks > recordLongest)
        {
            recordLongest = worstTicks;
            recordLongestIndex = worstRecord;
            recordLongestKind = worstKind;
        }
    }

    void endFrame()
    {
        // (A section the profiler took its own time out of (Perf::Aside) but
        // did not time this frame -- its log written outside it -- would be
        // below nothing: it is nothing.)
        for (std::uint32_t &r : running)
        {
            if (r >= 0x80000000u)
                r = 0;
        }
        // (The monsters' worst frame yet: where its samples were, kept.)
        {
            const int saved = enterCriticalSection();
            const std::uint32_t monsters = running[static_cast<int>(Section::Monsters)];
            if (monsters > worstMonsters)
            {
                worstMonsters = monsters;
                worstFrameTicks = running[static_cast<int>(Section::Frame)];
                worstCount = frameSampled;
                for (std::uint32_t i = 0; i < worstCount; ++i)
                    worstPcs[i] = framePcs[i];
            }
            frameSampled = 0;
            leaveCriticalSection(saved);
        }
        // (The worst frame yet: what of it was reading keys.)
        if (running[static_cast<int>(Section::Frame)] > worst[static_cast<int>(Section::Frame)])
            keysInWorstFrame = running[static_cast<int>(Section::Keys)];
        const int bucket = paceIn <= 2 ? 0 : (paceIn == 3 ? 1 : 2);
        std::uint64_t *pace = paceSum[bucket][paceSlow ? 1 : 0];
        ++paceFrames[bucket][paceSlow ? 1 : 0];
        const bool onset = paceSlow && paceIn <= 2 && quickRun >= kOnsetRun;
        onsets += onset;
        quickRun = !paceSlow && paceIn <= 2 ? quickRun + 1 : 0;
        for (int k = 0; k < kKinds; ++k)
        {
            kindAll[k] += kindFrame[k];
            if (bucket == 0)
                kindPace[paceSlow ? 1 : 0][k] += kindFrame[k];
            kindFrame[k] = 0;
        }
        paceSlow = false;
        paceIn = 0;
        // (The card's reads this pass.)
        if (cardFrame > cardFrameMost)
            cardFrameMost = cardFrame;
        if (cardFrameReads > cardFrameReadsMost)
            cardFrameReadsMost = cardFrameReads;
        cardFrame = cardFrameReads = 0;
        if (passesSinceWrite < 0xFFFF)
            ++passesSinceWrite;
        for (int i = 0; i < kCount; ++i)
        {
            latched[i] = running[i];
            summed[i] += running[i];
            pace[i] += running[i];
            if (onset)
                onsetSum[i] += running[i];
            if (running[i] > worst[i])
                worst[i] = running[i];
            running[i] = 0;
        }
        ++framesIn;
    }
} // namespace hlrt::DebugLog::Perf

// HwMath.s, with interrupts held off, for each divide (kind 0; `wide` 0 if
// both its numbers fit 32 bits), each root (kind 1) and each of the
// library's 32-bit divides (kind 2): `at`, the return address of the call.
// (No divide here, nor anything that waits.)
extern "C" void hlProfileSite(std::uint32_t at, int kind, std::uint32_t wide)
{
    using namespace hlrt::DebugLog::Perf;
    if (kind == 0 && wide == 0)
        ++divFits;
    Site *const sites = kind == 0 ? divSites : (kind == 1 ? rootSites : div32Sites);
    const std::uint32_t mask =
        static_cast<std::uint32_t>(kind == 0 ? kDivSites : (kind == 1 ? kRootSites : kDiv32Sites)) - 1;
    const std::uint32_t hash = (at >> 1) * 2654435761u;
    for (std::uint32_t k = 0; k < 8; ++k)
    {
        Site &site = sites[((hash >> 23) + k) & mask];
        if (site.at == at)
        {
            ++site.count;
            return;
        }
        if (site.at == 0)
        {
            site.at = at;
            site.count = 1;
            return;
        }
    }
    ++(kind == 2 ? div32Lost : sitesLost);
}

// libnds's disk_read (FatFs's read of the card's sectors), wrapped in a
// profiling build (build.py): each read timed. `drive`'s top bit is FatFs's
// mark for what it reads for itself (its tables, a folder): cached, and
// what a seek's walk down a file's chain costs. (From whichever thread reads;
// never an interrupt.)
extern "C" int __real_libnds_disk_read(unsigned char drive, unsigned char *to, unsigned long sector, unsigned int count);
extern "C" int __wrap_libnds_disk_read(unsigned char drive, unsigned char *to, unsigned long sector, unsigned int count)
{
    using namespace hlrt::DebugLog::Perf;
    const std::uint32_t from = now();
    const int result = __real_libnds_disk_read(drive, to, sector, count);
    if (cardAside)
        return result;
    const std::uint32_t ticks = now() - from;
    ++cardWindow.reads;
    cardWindow.sectors += count;
    cardWindow.ticks += ticks;
    if (ticks > cardWindow.longest)
        cardWindow.longest = ticks;
    if (drive & 0x80)
    {
        ++cardWindow.fatReads;
        cardWindow.fatTicks += ticks;
    }
    cardFrame += ticks;
    ++cardFrameReads;
    // (Among the window's slowest: in place of the quickest of them.)
    SlowRead *least = &slowReads[0];
    for (SlowRead &slow : slowReads)
        least = slow.ticks < least->ticks ? &slow : least;
    if (ticks > least->ticks)
        *least = SlowRead{ticks, static_cast<std::uint32_t>(sector), count, passesSinceWrite, (drive & 0x80) != 0, cardWho};
    return result;
}

// ... and its writes (build.py wraps this one too).
extern "C" int __real_libnds_disk_write(unsigned char drive, const unsigned char *from, unsigned long sector,
                                        unsigned int count);
extern "C" int __wrap_libnds_disk_write(unsigned char drive, const unsigned char *from, unsigned long sector,
                                        unsigned int count)
{
    using namespace hlrt::DebugLog::Perf;
    const std::uint32_t start = now();
    const int result = __real_libnds_disk_write(drive, from, sector, count);
    if (cardAside)
        return result;
    const std::uint32_t ticks = now() - start;
    ++cardWrites;
    cardWriteSectors += count;
    cardWriteTicks += ticks;
    if (ticks > cardWriteLongest)
        cardWriteLongest = ticks;
    passesSinceWrite = 0;
    SlowWrite *least = &slowWrites[0];
    for (SlowWrite &slow : slowWrites)
        least = slow.ticks < least->ticks ? &slow : least;
    if (ticks > least->ticks)
        *least = SlowWrite{ticks, static_cast<std::uint32_t>(sector), count, cardWho};
    // (Its own row; else the first free one; else the last, "others".)
    WhoWrote *tally = nullptr;
    for (WhoWrote &w : whoWrote)
    {
        if (w.writes != 0 && w.who == cardWho)
            tally = &w;
    }
    for (int k = 0; tally == nullptr && k < kWhoWrote - 1; ++k)
    {
        if (whoWrote[k].writes == 0)
        {
            tally = &whoWrote[k];
            tally->who = cardWho;
        }
    }
    if (tally == nullptr)
    {
        tally = &whoWrote[kWhoWrote - 1];
        if (tally->writes == 0)
            tally->who = cardWho;
        else if (tally->who != cardWho)
            tally->who = "others";
    }
    ++tally->writes;
    tally->sectors += count;
    tally->ticks += ticks;
    return result;
}

// shared/HlTraceCount.hpp: the profiler's clock, and a trace counted to the
// line that called it. (From the frame's thread or the think worker's, never
// an interrupt; they take turns only at their yields, so no lock.)
extern "C" std::uint32_t hlProfileNow() { return hlrt::DebugLog::Perf::now(); }
extern "C"
{
    std::uint32_t hlProfileAway = 0;
}

extern "C" void hlProfileTrace(std::uint32_t at, std::uint32_t kind, std::uint32_t ticks)
{
    using namespace hlrt::DebugLog::Perf;
    if (hlrt::think.onWorker())
        kind |= hldata::kTraceOnWorker;
    const std::uint32_t hash = ((at >> 1) ^ (kind << 28)) * 2654435761u;
    for (std::uint32_t k = 0; k < 8; ++k)
    {
        TraceSite &site = traceSites[((hash >> 23) + k) & (kTraceSites - 1)];
        if (site.count != 0 && site.at == at && site.kind == kind)
        {
            ++site.count;
            site.ticks += ticks;
            return;
        }
        if (site.count == 0)
        {
            site = TraceSite{at, kind, 1, ticks};
            return;
        }
    }
    ++traceSitesLost;
}

#endif
