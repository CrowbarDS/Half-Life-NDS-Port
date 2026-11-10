#pragma once

// Voice chat (a DSi's, online, the host letting it): while TALK is held, what
// the microphone hears -- the ARM7 drains it into a ring each 2 ms block
// (HlAudioLink.hpp's MicOn) -- is cut into packets of 40 ms and encoded as the
// DS's sound hardware plays them (shared/HlVoice.hpp), and sent through the
// relay to the others in the room (netrelay::sendVoice). What they say comes
// the same way, is put back in order, and is played on the game's sound
// channels, two talkers at once at most. All of it in the DSi's memory: a DS
// has no room for it, and is never online.
//
// Not in a console sent the game.

#include <cstdint>

#include "HlVoice.hpp"

namespace hlrt::voice
{
#ifndef HLDS_CHILD
    /// Once a frame. `hearing`: in a match online whose host lets them talk
    /// (a host says so to the relay); what others say is heard, and
    /// `talking` (TALK held), the microphone is on and what it hears is
    /// sent as player `me`. Not hearing: all of it off and let go of.
    void frame(bool talking, bool hearing, int me);
    /// The player heard speaking now (the first, if two are); -1 if none.
    int speaking();
#endif
} // namespace hlrt::voice
