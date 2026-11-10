#pragma once

// How much the relay takes from each address and each console, and sends in
// all: what is over is let fall (and counted), so that no one can make it
// work, or send, for others more than a game needs.
//
// A game's needs (a host sends a frame every 60th of a second, of 729 bytes
// at most with its head; a client one of 229 at most; a voice packet each
// 27 ms at most; and a ping, a room or a list each second), with room to
// spare: the defaults below are about one and a half times that.

#include <cstdint>
#include <string>

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
        std::uint32_t dataPerSecond = 90;
        std::uint32_t hostBytesPerSecond = 64 * 1024; ///< a host's Data, which goes to each of its clients
        std::uint32_t clientBytesPerSecond = 24 * 1024;
        std::uint32_t voicePerSecond = 45;
        // Each address, at once.
        std::uint32_t peersPerIp = 16; ///< (consoles behind one router: a LAN party)
        std::uint32_t roomsPerIp = 4;
        std::uint32_t knocksPerIp = 8;
        // The relay, in all.
        std::uint32_t outBytesPerSecond = 12 * 1024 * 1024;
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
