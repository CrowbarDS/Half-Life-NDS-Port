#pragma once

// The relay's round, apart from the socket itself: what has come is read (no
// more than so many at a time), the relay's clock let run, and what it has to
// say sent. main.cpp gives it the real socket; the tests a pretend one.

#include <cstdint>

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

        Serve(Relay &relay, Socket &socket, std::uint32_t nowMs) : relay_(relay), socket_(socket), lastTick_(nowMs) {}

        /// One round, at `nowMs`. True if it stopped at kMostPerRound with
        /// more perhaps waiting (the next round should not wait for more).
        bool round(std::uint32_t nowMs);

        // (Counts since start, for the operator.)
        std::uint64_t received = 0;
        std::uint64_t sent = 0;
        std::uint64_t sendFailed = 0;
        std::uint64_t fullRounds = 0; ///< rounds stopped at kMostPerRound

    private:
        Relay &relay_;
        Socket &socket_;
        std::uint32_t lastTick_;
        std::vector<Out> out_;
        std::uint8_t bytes_[2048];
    };
} // namespace hlrelay
