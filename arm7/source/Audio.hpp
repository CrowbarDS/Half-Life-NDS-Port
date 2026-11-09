#pragma once

// The game's sound on the ARM7 (Audio.cpp).

namespace hlaudio
{
    /// The block timer and the handler that is told where the shared block
    /// is (the ARM9 sends its address once: FIFO_USER_01).
    void install();
    /// Waits for the next block (2 ms) and does it: the ARM9's commands, the
    /// sounds that ended, the music, the counters.
    void waitAndRun();
    /// (Diagnostic) what the ARM7 is about to do, for the ARM9 to see if it
    /// never comes back (HlAudioLink.hpp, where).
    void mark(unsigned what);
    unsigned marked();
} // namespace hlaudio
