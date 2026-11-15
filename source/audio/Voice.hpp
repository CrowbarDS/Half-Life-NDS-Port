#pragma once

// Voice chat (a DSi's, online, the host letting it): while TALK is held (or,
// the host choosing it, all the time: the open microphone), what the
// microphone hears -- the ARM7 drains it into a ring each 2 ms block
// (HlAudioLink.hpp's MicOn) -- is cut into packets of 40 ms and encoded as the
// DS's sound hardware plays them (shared/HlVoice.hpp), and sent through the
// relay to the others in the room (netrelay::sendVoice). What they say comes
// the same way, is put back in order, and is played on the game's sound
// channels, three talkers at once at most. All of it in the DSi's memory: a DS
// has no room for it, and is never online.
//
// Not in a console sent the game.

#include <cstdint>

#include "HlVoice.hpp"

namespace hlrt::voice
{
#ifndef HLDS_CHILD
    /// Once a frame. `hearing`: online, in a room whose host lets them talk
    /// (its lobby, its votes, its matches; a host says so to the relay):
    /// what others say is heard; and the microphone is on -- `talkHeld`
    /// (TALK), or with `openMic` always, unless muted -- and what it hears
    /// is sent as player `me` (with the microphone open, only what its gate
    /// lets through). Not hearing: all of it off and let go of.
    void frame(bool talkHeld, bool openMic, bool hearing, int me);
    /// Out of the room: the microphone off, what others said let go of.
    void stop();
    /// A new game: the player's mute taken off (it is this game's alone).
    void reset();
    /// The player's own microphone muted (open microphone only).
    void setMuted(bool muted);
    bool muted();
    /// The player heard speaking now (the first, if two are); -1 if none.
    int speaking();
#endif
} // namespace hlrt::voice
