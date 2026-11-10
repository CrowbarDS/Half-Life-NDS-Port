#pragma once

// Voice chat (a DSi's, online, the host letting it): while TALK is held, what
// the microphone hears -- the ARM7 drains it into a ring each 2 ms block
// (HlAudioLink.hpp's MicOn) -- is cut into packets of 40 ms and encoded as the
// DS's sound hardware plays them (shared/HlVoice.hpp), to be sent. All of it
// in the DSi's memory: a DS has no room for it, and is never online.
//
// Not in a console sent the game.

#include <cstdint>

#include "HlVoice.hpp"

namespace hlrt::voice
{
#ifndef HLDS_CHILD
    /// Once a frame: `talking` (TALK held, talk allowed), the microphone on
    /// and what it heard made into packets; not, off (what was heard but not
    /// yet made into a packet let go of).
    void frame(bool talking);
    /// The oldest packet made and not yet taken: its bytes (kVoiceAdpcmBytes)
    /// and rate. False if there is none.
    bool take(std::uint8_t *packet, std::uint16_t &rate);
    /// Off, whatever it was doing: the microphone off, nothing waiting.
    void stop();
    /// Whether the microphone is on (TALK held).
    bool on();
#endif
} // namespace hlrt::voice
