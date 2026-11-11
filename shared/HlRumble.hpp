#pragma once

// A Rumble Pak's motor (a DS's GBA slot): what it does, frame by frame, from
// what the game felt -- a hit, a shot, a landing, the ground shaking.
//
// The motor is on or off, nothing between. A continuous one (Nintendo's
// Rumble Pak) runs while it is held on: how hard it feels is how many frames
// in four it is on. An edge one (the GBA carts' motors) gives one knock each
// time it goes from off to on: how hard it feels is how often it is knocked.
//
// Kept here, apart from the hardware, so the host tests run it.

#include <cstdint>

namespace hldata
{
    /// The setting: how hard the motor may go.
    enum class RumbleSetting : std::uint8_t
    {
        Off = 0,
        Light = 1,
        Strong = 2,
    };
    constexpr int kRumbleSettings = 3;

    struct RumbleState
    {
        std::uint8_t level = 0;   ///< 0..255: the last kick's strength while it lasts
        std::uint16_t left = 0;   ///< vertical blanks the kick still lasts
        std::uint8_t held = 0;    ///< 0..255: a strength for this frame only (a shake)
        std::uint8_t phase = 0;   ///< counts frames, for the pattern
        bool on = false;          ///< the motor as last set

        /// A jolt, `strength` 0..255 for `blanks` vertical blanks: the
        /// stronger of it and the one going on is felt.
        void kick(int strength, int blanks)
        {
            if (strength <= 0 || blanks <= 0)
                return;
            if (strength > 255)
                strength = 255;
            if (blanks > 0xFFFF)
                blanks = 0xFFFF;
            // (A weaker one while a jolt goes on is not felt; as strong, it
            // lasts the longer of the two.)
            if (left == 0 || strength > level)
            {
                level = static_cast<std::uint8_t>(strength);
                left = static_cast<std::uint16_t>(blanks);
            }
            else if (strength == level && static_cast<int>(left) < blanks)
                left = static_cast<std::uint16_t>(blanks);
        }

        /// A strength felt this frame only (the ground shaking).
        void hold(int strength)
        {
            if (strength > 255)
                strength = 255;
            if (strength > held)
                held = static_cast<std::uint8_t>(strength);
        }

        /// What it is felt as now, 0..255, before the setting.
        int strength() const
        {
            const int kicked = left != 0 ? level : 0;
            return kicked > held ? kicked : held;
        }

        /// Stopped at once (a menu, a map let go).
        void stop()
        {
            level = 0;
            left = 0;
            held = 0;
            phase = 0;
            on = false;
        }

        /// One step of the game, `blanks` vertical blanks long: whether the
        /// motor is on until the next. `edge`: an edge motor.
        bool tick(int blanks, bool edge, RumbleSetting setting)
        {
            int felt = strength();
            if (setting == RumbleSetting::Off)
                felt = 0;
            else if (setting == RumbleSetting::Light)
                felt >>= 1;
            held = 0;
            if (blanks > 0)
                left = static_cast<int>(left) > blanks ? static_cast<std::uint16_t>(left - blanks) : 0;
            if (left == 0)
                level = 0;
            phase = static_cast<std::uint8_t>(phase + 1);
            if (felt == 0)
            {
                on = false;
                return on;
            }
            if (edge)
            {
                // A knock every second frame when strong, every fourth when
                // not: off between, or the motor is not knocked again.
                const int every = felt >= 128 ? 2 : 4;
                on = (phase & (every - 1)) == 0;
            }
            else
            {
                // On for 1 to 4 frames in 4.
                const int frames = (felt + 63) >> 6;
                on = (phase & 3) < frames;
            }
            return on;
        }
    };
} // namespace hldata
