#pragma once

// How much the relay takes from each address and each console, and sends in
// all: what is over is let fall (and counted), so that no one can make it
// work, or send, for others more than a game needs.
//
// A game's needs, with room to spare. A host sends a frame every 60th of a
// second, of 729 bytes at most with its head -- and, while it sends a map to
// a console that has not got it, up to four pieces of 600 more with each
// (NetGame's kOnlineFetchPieces): up to 300 datagrams and about 190 KB a
// second. A client sends a frame of 229 bytes at most each 60th, and a little
// more while it is sent a map; a voice packet each 27 ms at most; and a ping,
// a room or a list each second. (The first limits, a match's needs alone,
// stopped a map being sent: seen 2026-11-10.)

#include <cstdint>
#include <string>

#include "HlRelay.hpp"

namespace hlrelay
{
    /// A token bucket: `rate` a second, up to `burst` at once. Whole numbers
    /// only: thousandths of a token.
    struct Bucket
    {
        std::uint64_t milli = 0; ///< tokens in it, in thousandths
        std::uint32_t lastMs = 0;
        bool started = false;

        /// Whether `cost` tokens are there at `nowMs` (taken if so).
        bool take(std::uint32_t cost, std::uint32_t rate, std::uint32_t burst, std::uint32_t nowMs)
        {
            const std::uint64_t most = static_cast<std::uint64_t>(burst) * 1000;
            if (!started)
            {
                started = true;
                milli = most;
            }
            else
            {
                // (A long silence fills it, no more: and no product too big.)
                std::uint32_t elapsed = nowMs - lastMs;
                if (elapsed > 3600000u)
                    elapsed = 3600000u;
                milli += static_cast<std::uint64_t>(elapsed) * rate;
                if (milli > most)
                    milli = most;
            }
            lastMs = nowMs;
            const std::uint64_t need = static_cast<std::uint64_t>(cost) * 1000;
            if (milli < need)
                return false;
            milli -= need;
            return true;
        }
    };

    struct Limits
    {
        // Each address, before its console is known (Hello, Enter).
        std::uint32_t helloPerSecond = 10;
        // Each console known.
        std::uint32_t controlPerSecond = 30; ///< all but Data and Voice
        std::uint32_t dataPerSecond = 400;
        std::uint32_t hostBytesPerSecond = 256 * 1024; ///< a host's Data, which goes to each of its clients
        std::uint32_t clientBytesPerSecond = 48 * 1024;
        std::uint32_t voicePerSecond = 45;
        // Each address, at once.
        std::uint32_t peersPerIp = 16; ///< (consoles behind one router: a LAN party)
        std::uint32_t roomsPerIp = 4;
        std::uint32_t knocksPerIp = 8;
        // The relay, in all.
        std::uint32_t outBytesPerSecond = 12 * 1024 * 1024;
        /// A console that says nothing this long is let go (seconds).
        std::uint32_t silentSeconds = hldata::kRelaySilentMs / 1000;
        static constexpr std::uint32_t kMostSilentSeconds = 3600;
        // Rooms (minutes; 0: never).
        std::uint32_t emptyRoomMinutes = 30; ///< a room nobody is in or asking to join, closed after this
        std::uint32_t roomMaxMinutes = 0;    ///< any room, closed after this
        /// The most minutes either may be (the relay's clock goes round in 49 days).
        static constexpr std::uint32_t kMostMinutes = 20000;

        /// One set by its name (as `--limit NAME=VALUE` gives it); false if
        /// there is no such name, or the value is 0 (but for the rooms' times,
        /// where 0 is never) or too big.
        bool set(const std::string &name, std::uint32_t value);
        /// The names and what each is, a line each, for --help.
        static const char *help();
    };
} // namespace hlrelay
