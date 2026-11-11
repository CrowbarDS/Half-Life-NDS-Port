// A Rumble Pak's motor (shared/HlRumble.hpp): how often it is on for a
// strength, Light at half of Strong, an edge motor knocked rather than held,
// the strongest jolt felt, a shake felt only while it lasts, and stopped.

#include <cstdio>

#include "HlRumble.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    /// Frames on in `frames` steps of one blank, from a fresh `kick`.
    int onFor(int strength, int frames, bool edge, RumbleSetting setting)
    {
        RumbleState state;
        state.kick(strength, frames);
        int on = 0;
        for (int n = 0; n < frames; ++n)
            on += state.tick(1, edge, setting) ? 1 : 0;
        return on;
    }

    void testDuty()
    {
        check(onFor(255, 16, false, RumbleSetting::Strong) == 16, "the strongest jolt holds the motor on");
        check(onFor(255, 16, false, RumbleSetting::Light) == 8, "... Light, half the time");
        check(onFor(64, 16, false, RumbleSetting::Strong) == 4, "a quarter strength: one frame in four");
        check(onFor(255, 16, false, RumbleSetting::Off) == 0, "Off: never on");
        check(onFor(1, 16, false, RumbleSetting::Light) == 0, "a jolt too weak to be felt at Light is not");
    }

    void testEdge()
    {
        // An edge motor must be let go between knocks, or it is not knocked
        // again: never on two frames running.
        RumbleState state;
        state.kick(255, 32);
        bool last = false, twice = false;
        int knocks = 0;
        for (int n = 0; n < 32; ++n)
        {
            const bool on = state.tick(1, true, RumbleSetting::Strong);
            twice = twice || (on && last);
            knocks += on && !last ? 1 : 0;
            last = on;
        }
        check(!twice && knocks == 16, "an edge motor, strong: knocked every second frame, let go between");
        check(onFor(255, 32, true, RumbleSetting::Light) == 8, "... at Light (half): every fourth");
    }

    void testKicks()
    {
        RumbleState state;
        state.kick(200, 4);
        state.kick(50, 30);
        check(state.strength() == 200 && state.left == 4, "a weaker jolt while a stronger goes on is not felt");
        state.kick(250, 2);
        check(state.strength() == 250 && state.left == 2, "a stronger one takes its place");
        state.kick(250, 10);
        check(state.left == 10, "as strong and longer: it lasts the longer");
        for (int n = 0; n < 10; ++n)
            state.tick(1, false, RumbleSetting::Strong);
        check(state.strength() == 0 && !state.tick(1, false, RumbleSetting::Strong), "it ends when it has lasted");
        state.kick(255, 6);
        state.tick(4, false, RumbleSetting::Strong);
        check(state.left == 2, "a step of four blanks takes four off");
        state.tick(5, false, RumbleSetting::Strong);
        check(state.left == 0 && state.strength() == 0, "... and never goes below none");
        state.kick(0, 10);
        state.kick(-5, 10);
        state.kick(100, 0);
        check(state.strength() == 0, "a jolt of nothing, or of no time, is none");
        state.kick(1000, 3);
        check(state.strength() == 255, "a jolt beyond the strongest is the strongest");
    }

    void testHold()
    {
        RumbleState state;
        state.hold(255);
        check(state.tick(1, false, RumbleSetting::Strong), "a shake is felt the frame it is held");
        check(!state.tick(1, false, RumbleSetting::Strong), "... and no longer once it is not");
        state.kick(64, 10);
        state.hold(255);
        check(state.strength() == 255, "a shake stronger than the jolt is what is felt");
        state.stop();
        check(state.strength() == 0 && !state.on && !state.tick(1, false, RumbleSetting::Strong),
              "stopped: nothing left to feel");
    }
} // namespace

int main()
{
    testDuty();
    testEdge();
    testKicks();
    testHold();
    if (g_failures == 0)
        std::printf("test_rumble: ok\n");
    return g_failures == 0 ? 0 : 1;
}
