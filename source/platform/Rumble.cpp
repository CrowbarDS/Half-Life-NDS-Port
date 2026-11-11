#include "platform/Rumble.hpp"

#ifndef HLDS_CHILD

#include <nds.h>

#include "HlRumble.hpp"
#include "Settings.hpp"
#include "platform/Slot2.hpp"

namespace hlrt::rumble
{
    namespace
    {
        hldata::RumbleState state;
        bool motorOn = false;

        void set(bool on)
        {
            if (on == motorOn)
                return;
            motorOn = on;
            setRumble(on ? slot2::rumbleMost() : 0);
        }
    } // namespace

    void kick(int strength, int blanks)
    {
        if (slot2::hasRumble())
            state.kick(strength, blanks);
    }

    void hold(int strength)
    {
        if (slot2::hasRumble())
            state.hold(strength);
    }

    void frame(int blanks)
    {
        if (!slot2::hasRumble())
            return;
        const auto setting = static_cast<hldata::RumbleSetting>(settings.vibration);
        set(state.tick(blanks, slot2::rumbleEdge(), setting));
    }

    void stop()
    {
        if (!slot2::hasRumble())
            return;
        state.stop();
        set(false);
    }
} // namespace hlrt::rumble

#endif
