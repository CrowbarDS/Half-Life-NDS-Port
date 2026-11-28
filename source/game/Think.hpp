#pragma once

#include "EngineObject.hpp"

// The actors' long thinking outside the frame (shared/HlThink.hpp): a thread
// of its own, under the frame's, that answers their questions while the
// frame waits for the screen, stopping now and then so as to give the CPU
// back as soon as the frame wants it. Until start() it is not there, and
// every question is answered as it is asked, as before.

#include "HlActors.hpp"
#include "HlLogic.hpp"

namespace hlrt
{
    class Think
    {
    public:
        /// Once, after the 3D engine is up: the worker.
        void start();
        /// Each time the engine is brought up (Global::InitVideo). The
        /// worker is the program's, not the engine's: made once, it stays.
        void restart() { start(); }
        /// A map is set up: its actors ask, the worker answers.
        void attach(hldata::LogicWorld &w);
        /// The map is being left: what is being worked out is finished at
        /// once and dropped, and nothing more is asked of the worker.
        void detach();
        bool running() const { return started_; }
        /// The frame's wait for the screen (`wait`), the one time the world
        /// is let go to the worker: the frame's thread holds it otherwise,
        /// and takes it back as it wakes -- at the worker's next pause, if it
        /// is in the middle of a question. (libnds cuts threads off at any
        /// interrupt: the world changes only between the worker's pauses.)
        void waitForScreen(void (*wait)());
        /// Whether the frame's thread holds the world now (the guard asks).
        bool frameHolds() const { return frameHolds_; }
        /// Whether what runs now is the worker (perf.txt's trace counts).
        bool onWorker() const;

        /// Questions the worker has answered (perf.txt counts them and
        /// sets this back to 0).
        std::uint32_t answered = 0;
        /// Times the worker woke for a question and found none to answer
        /// (perf.txt counts them and sets this back to 0).
        std::uint32_t idleWakes = 0;
        /// How long the answers took to be taken up, asked to taken (in
        /// frames), since this was last asked; and those dropped (a walk's
        /// plan from where it no longer was) or not taken up in time (the
        /// asker worked them out itself). Read and cleared by perf.txt.
        struct Waits
        {
            std::uint32_t taken = 0, frames = 0, longest = 0, dropped = 0, gaveUp = 0;
            /// By kind of question (hldata::kThink*): answers taken up, the
            /// frames they took in all, the longest; and the oldest still
            /// waiting to be taken up, in frames, as the line is written.
            std::uint32_t takenOf[hldata::kThinkKinds] = {}, framesOf[hldata::kThinkKinds] = {};
            std::uint32_t longestOf[hldata::kThinkKinds] = {}, waitingOf[hldata::kThinkKinds] = {};
        };
        Waits takeWaits();
        /// How long the frame, woken by the screen, waited for the world to
        /// be let go (the worker in the middle of a question), in the
        /// profiler's ticks (HLDS_PROFILE; all 0 without it).
        struct Holdup
        {
            std::uint32_t worst = 0;  ///< the longest wait
            std::uint32_t radio = 0;  ///< ... of which the wireless library's thread had the CPU
            std::int16_t actor = -1;  ///< ... whose question the worker was in (-1: none)
            std::uint8_t kind = 0;    ///< ... and of what kind (hldata::kThink*; 0: none)
            std::uint32_t over = 0;   ///< waits longer than a millisecond
        };
        struct Holdups
        {
            Holdup window; ///< since perf.txt last asked (cleared)
            Holdup map;    ///< since the map was set up
            Holdup match;  ///< ... the radio on (a match, Download Play, online)
            /// The worker's longest stretch between two of its pauses, by
            /// kind of question, the wireless library's time in it taken out:
            /// what the frame would wait at worst, had the screen woken it at
            /// its start. Since perf.txt last asked, and since the map was
            /// set up.
            std::uint32_t stretchOf[hldata::kThinkKinds] = {};
            std::uint32_t mapStretchOf[hldata::kThinkKinds] = {};
        };
        Holdups takeHoldups();
        /// The deepest the worker's stack has been (perf.txt; 0 unless
        /// profiling).
        std::uint32_t stackPeak() const;
        /// Where the worker's stack ends, if it has been written past;
        /// null if not (the guard asks every frame).
        const void *stackOverflowed() const;

    private:
        bool started_ = false;
        bool frameHolds_ = false;
    };

    HL_ENGINE_OBJECT(Think, think);
} // namespace hlrt
