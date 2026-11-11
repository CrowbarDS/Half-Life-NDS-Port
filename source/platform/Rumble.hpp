#pragma once

// A Rumble Pak's motor (platform/Slot2.hpp found it), driven once a step of
// the game -- never from an interrupt -- from what the game felt: a hit, a
// shot, a landing, the ground shaking (hldata::RumbleState, HlRumble.hpp).
// As hard as the Vibration setting lets it; nothing without a motor.

namespace hlrt::rumble
{
#ifndef HLDS_CHILD
    /// A jolt, `strength` 0..255, for `blanks` vertical blanks.
    void kick(int strength, int blanks);
    /// A strength felt this step only (the ground shaking).
    void hold(int strength);
    /// One step of the game, `blanks` long: the motor set for it.
    void frame(int blanks);
    /// Off at once, and what was felt forgotten (a menu, a map let go).
    void stop();
#else
    inline void kick(int, int) {}
    inline void hold(int) {}
    inline void frame(int) {}
    inline void stop() {}
#endif
} // namespace hlrt::rumble
