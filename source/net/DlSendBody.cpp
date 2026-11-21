// SEND THE GAME's work: the program read in and offered, the sending
// followed, a console's state put in words. In the front end's overlay
// (source/Overlay.hpp, build.py's OVERLAYS), read in with it; reached only
// through DlSend.cpp's gates, which keep what it knows.
#ifndef HLDS_CHILD
#include "net/DlSendState.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <malloc.h>
#include <nds.h>

#include "Global.hpp"
#include "HlNetAir.hpp"
#include "net/NetLink.hpp"

namespace hlrt::dlsend::detail
{
    namespace
    {
        /// A sending that has not moved for this long is given up on: those
        /// that have it all are started, the rest are not.
        constexpr std::uint32_t kStallVblanks = 15 * 60;
        /// After the word to start: time for it to be heard.
        constexpr std::uint32_t kStartVblanks = 60;
        // How long more consoles are waited for, once one has come: four
        // seconds with nobody new, twelve at most (START: at once).
        constexpr std::uint32_t kQuietVblanks = 4 * 60, kMostVblanks = 12 * 60;
    } // namespace

    bool beginBody(const char *description, int most, std::string &why)
    {
        end();
        // (All of it read before the radio is: the card read while the
        // program is being sent disturbs the sending.)
        std::FILE *file = std::fopen(kChildPath, "rb");
        if (file == nullptr)
        {
            why = "This copy of the game has nothing to send.";
            return false;
        }
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        // (The memory made first, the file shut meanwhile: open, a file of
        // the ROM's holds what a megabyte more would need.)
        std::fclose(file);
        rom = size > 0 ? std::malloc(static_cast<std::size_t>(size)) : nullptr;
        file = rom != nullptr ? std::fopen(kChildPath, "rb") : nullptr;
        // (A piece at a time: the ROM's files are read off the card, which
        // does not give a megabyte in one go.)
        bool read = rom != nullptr;
        for (long at = 0; read && at < size;)
        {
            const std::size_t piece = size - at < 16 * 1024 ? static_cast<std::size_t>(size - at) : 16 * 1024;
            read = std::fread(static_cast<std::uint8_t *>(rom) + at, 1, piece, file) == piece;
            at += static_cast<long>(piece);
        }
        if (file != nullptr)
            std::fclose(file);
        else
            read = false;
        if (rom != nullptr && !read)
        {
            end();
            why = "The program to send could not be read.";
            return false;
        }
        if (rom == nullptr)
        {
            end();
            // (How short: what is free, at the heap's end and inside it,
            // against the program.)
            const struct mallinfo info = mallinfo();
            char text[72];
            std::snprintf(text, sizeof(text), "Not enough memory to send the game (%u+%u KB free, %u needed).",
                          static_cast<unsigned>(global.heapHeadroom() / 1024),
                          static_cast<unsigned>(info.fordblks / 1024), static_cast<unsigned>(size / 1024));
            why = text;
            return false;
        }
        romSize = static_cast<std::size_t>(size);
        // Where the consoles it starts are to find this one.
        hldata::NetDlParam param;
        param.channel = static_cast<std::uint8_t>(netlink::kChannel);
        netlink::address(param.host);
        std::uint8_t bytes[hldata::kNetDlParamBytes];
        hldata::netDlWrite(param, bytes);
        if (!netlink::dlStart(rom, romSize, description, bytes))
        {
            end();
            why = "The radio would not start.";
            return false;
        }
        netlink::dlFast(true); // (far ahead of what the consoles have said they have)
        wanted = most < 2 ? 1 : most - 1;
        seen = 0;
        lastSum = -1;
        countsWere = netlink::DlCounts();
        countedAt = Global::vblanks();
        rateLine[0] = 0;
        now = State::Waiting;
        return true;
    }

    void frameBody()
    {
        if (now == State::Off || now == State::Done || now == State::Failed)
            return;
        netlink::dlUpdate();
        const std::uint32_t t = Global::vblanks();
        const unsigned here = netlink::dlGuests();
        int count = 0;
        for (int n = 1; n < 16; ++n)
            count += (here >> n) & 1;
        if ((here & ~seen) != 0)
        {
            if (seen == 0)
                firstAt = t;
            newestAt = t;
        }
        seen = here;
        // (What the radio did in the last second, for the page to say.)
        if (t - countedAt >= 60)
        {
            const netlink::DlCounts counts = netlink::dlCounts();
            const unsigned blocks = counts.blocks >= countsWere.blocks ? counts.blocks - countsWere.blocks : 0;
            std::snprintf(rateLine, sizeof(rateLine), "%u KB/s  sent %u/s  done %u  again %u  full %u",
                          blocks * 504 / 1024, static_cast<unsigned>(static_cast<std::uint16_t>(counts.armed - countsWere.armed)),
                          static_cast<unsigned>(static_cast<std::uint16_t>(counts.done - countsWere.done)),
                          static_cast<unsigned>(static_cast<std::uint16_t>(counts.retried - countsWere.retried)),
                          static_cast<unsigned>(static_cast<std::uint16_t>(counts.refused - countsWere.refused)));
            countsWere = counts;
            countedAt = t;
        }
        switch (now)
        {
        // (Those who come are held, then sent it all at once: go().)
        case State::Waiting:
            if (count > 0)
                now = State::Gathering;
            break;
        case State::Gathering:
            if (count == 0)
                now = State::Waiting;
            else if (t - newestAt >= kQuietVblanks || t - firstAt >= kMostVblanks || count >= wanted)
                go();
            break;
        case State::Sending:
        {
            int sum = 0;
            for (int n = 1; n < 16; ++n)
                sum += netlink::dlProgress(n) + 1;
            if (sum != lastSum)
            {
                lastSum = sum;
                movedAt = t;
            }
            const bool stalled = t - movedAt >= kStallVblanks;
            if (netlink::dlAllReady() || stalled)
            {
                if (!netlink::dlBootAll())
                {
                    now = State::Failed;
                    break;
                }
                startedAt = t;
                now = State::Starting;
            }
            else if (count == 0)
                now = State::Failed;
            break;
        }
        case State::Starting:
            if (t - startedAt >= kStartVblanks)
            {
                end();
                now = State::Done;
            }
            break;
        default:
            break;
        }
    }

    void describeBody(int n, char *status, int room, const char *&name)
    {
        name = netlink::dlName(n);
        const int progress = netlink::dlProgress(n);
        if (netlink::dlFailed(n))
            std::snprintf(status, static_cast<std::size_t>(room), "failed");
        else if (netlink::dlChecking(n))
            std::snprintf(status, static_cast<std::size_t>(room), "checking");
        else if (progress >= 100)
            std::snprintf(status, static_cast<std::size_t>(room), "ready");
        else if (now == State::Waiting || now == State::Gathering)
            std::snprintf(status, static_cast<std::size_t>(room), "here");
        else
            std::snprintf(status, static_cast<std::size_t>(room), "%d%%", progress < 0 ? 0 : progress);
    }
} // namespace hlrt::dlsend::detail
#endif
