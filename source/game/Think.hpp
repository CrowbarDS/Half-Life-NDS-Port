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

        /// Questions the worker has answered (perf.txt counts them and
        /// sets this back to 0).
        std::uint32_t answered = 0;
        /// How long the answers took to be taken up, asked to taken (in
        /// frames), since this was last asked; and those dropped (a walk's
        /// plan from where it no longer was) or not taken up in time (the
        /// asker worked them out itself). Read and cleared by perf.txt.
        struct Waits
        {
            std::uint32_t taken = 0, frames = 0, longest = 0, dropped = 0, gaveUp = 0;
        };
        Waits takeWaits();
        /// The deepest the worker's stack has been (perf.txt; 0 unless
        /// profiling).
        std::uint32_t stackPeak() const;
        /// Where the worker's stack ends, if it has been written past;
        /// null if not (the guard asks every frame).
        const void *stackOverflowed() const;

    private:
        bool started_ = false;
    };

    HL_ENGINE_OBJECT(Think, think);
} // namespace hlrt
