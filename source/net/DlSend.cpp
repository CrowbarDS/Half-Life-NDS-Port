// (The host's only: a console that was sent the game sends it to nobody.)
// What it knows, and the little that reads it; its work is in the front
// end's overlay (DlSendBody.cpp), reached through the gates below.
#ifndef HLDS_CHILD
#include "net/DlSend.hpp"
#include "net/DlSendState.hpp"

#include <cstdio>
#include <cstdlib>

#include "Global.hpp"
#include "Overlay.hpp"
#include "net/NetLink.hpp"

namespace hlrt::dlsend
{
    namespace detail
    {
        State now = State::Off;
        void *rom = nullptr;
        std::size_t romSize = 0;
        int wanted = 2;
        unsigned seen = 0;
        std::uint32_t firstAt = 0, newestAt = 0;
        std::uint32_t movedAt = 0, startedAt = 0;
        int lastSum = -1;
        netlink::DlCounts countsWere;
        std::uint32_t countedAt = 0;
        char rateLine[48] = "";
    } // namespace detail
    using namespace detail;

    bool available()
    {
        static int known = -1;
        if (known < 0)
        {
            std::FILE *file = std::fopen(kChildPath, "rb");
            known = file != nullptr ? 1 : 0;
            if (file != nullptr)
                std::fclose(file);
        }
        return known == 1;
    }

    // (The gates: the overlay read in -- a halt naming it if a map holds
    // its memory -- then its work.)
    HL_OVL_GATED bool begin(const char *description, int most, std::string &why)
    {
#if HL_OVL_COUNT > 0
        ovl::load(ovl::Id::Front);
#endif
        HL_OVL_GATE(Front);
        return beginBody(description, most, why);
    }

    HL_OVL_GATED void frame()
    {
        if (now == State::Off || now == State::Done || now == State::Failed)
            return;
#if HL_OVL_COUNT > 0
        ovl::load(ovl::Id::Front);
#endif
        HL_OVL_GATE(Front);
        frameBody();
    }

    void go()
    {
        // All who are there at once; nobody else let in meanwhile.
        if (now != State::Gathering)
            return;
        netlink::dlLock(true);
        netlink::dlGather(false);
        movedAt = Global::vblanks();
        now = State::Sending;
    }

    bool full()
    {
        int count = 0;
        for (int n = 1; n < 16; ++n)
            count += (netlink::dlGuests() >> n) & 1;
        return now == State::Gathering && count >= wanted;
    }

    const char *rate() { return rateLine; }

    State state() { return now; }

    unsigned consoles() { return now == State::Off ? 0 : seen & 0xFFFEu; }

    HL_OVL_GATED void describe(int n, char *status, int room, const char *&name)
    {
#if HL_OVL_COUNT > 0
        ovl::load(ovl::Id::Front);
#endif
        HL_OVL_GATE(Front);
        describeBody(n, status, room, name);
    }

    int percent()
    {
        int sum = 0, count = 0;
        for (int n = 1; n < 16; ++n)
        {
            const int progress = netlink::dlProgress(n);
            if (progress >= 0 && !netlink::dlFailed(n))
            {
                sum += progress;
                ++count;
            }
        }
        return count == 0 ? 0 : sum / count;
    }

    void end()
    {
        // (The radio set as it was before: the host's game, opened next,
        // needs it so -- NetLink.cpp.)
        netlink::dlStop();
        netlink::shutdown();
        std::free(rom);
        rom = nullptr;
        romSize = 0;
        if (now != State::Done)
            now = State::Off;
    }
} // namespace hlrt::dlsend
#endif
