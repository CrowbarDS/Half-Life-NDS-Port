#pragma once

// The relay's round, apart from the socket itself: what has come is read (no
// more than so many at a time), the relay's clock let run, and what it has to
// say sent. main.cpp gives it the real socket; the tests a pretend one.

#include <cstdint>
#include <string>

#include "Relay.hpp"

namespace hlrelay
{
    /// What the round reads from and sends through.
    class Socket
    {
    public:
        virtual ~Socket() = default;
        /// One datagram that has come, into `bytes` (`room` of them), and
        /// where from; its length, or 0 or less if none is waiting.
        virtual int receive(Address &from, std::uint8_t *bytes, int room) = 0;
        /// False if it could not be sent.
        virtual bool send(const Address &to, const std::uint8_t *bytes, int size) = 0;
    };

    class Serve
    {
    public:
        /// The most datagrams read in one round: under a flood, the rest wait
        /// for the next, and the relay's clock still runs between.
        static constexpr int kMostPerRound = 256;
        /// How often the relay's clock runs (Relay::tick).
        static constexpr std::uint32_t kTickMs = 100;
        /// How often a line of counts goes to the relay's log.
        static constexpr std::uint32_t kStatsMs = 60000;

        Serve(Relay &relay, Socket &socket, std::uint32_t nowMs)
            : relay_(relay), socket_(socket), lastTick_(nowMs), lastStats_(nowMs)
        {
        }

        /// One round, at `nowMs`. True if it stopped at kMostPerRound with
        /// more perhaps waiting (the next round should not wait for more).
        bool round(std::uint32_t nowMs);
        /// The relay stopping: every console told so (Relay::stop), sent now.
        void stop();

        /// Counts since start, for the operator.
        struct Counts
        {
            std::uint64_t received = 0, receivedBytes = 0;
            std::uint64_t sent = 0, sentBytes = 0;
            std::uint64_t sendFailed = 0;
            std::uint64_t outBudget = 0;  ///< not sent: the relay had sent as much as it may (Limits::outBytesPerSecond)
            std::uint64_t fullRounds = 0; ///< rounds stopped at kMostPerRound
            Drops drops;                  ///< (the relay's)
        };
        Counts counts;

        /// The line of counts: how many consoles and rooms now, and what came,
        /// went and fell since `before`, over `seconds`.
        std::string statsLine(const Counts &before, unsigned seconds) const;

    private:
        Relay &relay_;
        Socket &socket_;
        std::uint32_t lastTick_;
        std::uint32_t lastStats_;
        Counts atLastStats_;
        std::vector<Out> out_;
        Bucket outBytes_;
        std::uint8_t bytes_[2048];
    };
} // namespace hlrelay
