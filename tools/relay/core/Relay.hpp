#pragma once

// The relay: the rooms, who is in which, and where each datagram goes on
// (shared/HlRelay.hpp says what is in them). No sockets here -- a datagram
// comes in with where it came from and the time, and what is to be sent is
// handed back -- so that the tests run it as main.cpp does.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "HlRelay.hpp"

namespace hlrelay
{
    /// Where a datagram came from or goes to (IPv4, host byte order).
    struct Address
    {
        std::uint32_t ip = 0;
        std::uint16_t port = 0;
        bool operator==(const Address &other) const { return ip == other.ip && port == other.port; }
        std::uint64_t key() const { return static_cast<std::uint64_t>(ip) << 16 | port; }
        std::string text() const;
    };

    struct Out
    {
        Address to;
        std::vector<std::uint8_t> bytes;
    };

    class Relay
    {
    public:
        /// The most consoles it keeps account of at once, and rooms.
        static constexpr std::size_t kMostPeers = 1024;
        static constexpr std::size_t kMostRooms = 256;
        /// How often a host is told who is in its room, whether it changed
        /// or not (a Status lost on the way is not waited on).
        static constexpr std::uint32_t kStatusMs = 1000;

        /// What it does, a line at a time (rooms opened and closed, who came
        /// and went), for the operator.
        std::function<void(const std::string &)> log;

        /// A datagram from `from` at `nowMs`: what it says is done, and what
        /// is to be sent because of it is added to `out`.
        void receive(const Address &from, const std::uint8_t *bytes, int size, std::uint32_t nowMs,
                     std::vector<Out> &out);
        /// Now and then (every tenth of a second or so): who said nothing for
        /// too long is let go, and the hosts are told who is there.
        void tick(std::uint32_t nowMs, std::vector<Out> &out);

        std::size_t peers() const { return peers_.size(); }
        std::size_t rooms() const { return rooms_.size(); }

    private:
        struct Peer
        {
            Address at;
            std::uint32_t session = 0;
            std::uint32_t heardMs = 0;
            std::uint32_t room = 0; ///< the room it hosts or is in; 0: none
            int slot = -1;          ///< 0: its host; 1..7: a client of it
            std::uint32_t knocking = 0; ///< the room whose host it waits to be let in by; 0: none
            std::uint32_t voiceSinceMs = 0; ///< the second its Voice is counted in
            int voiceCount = 0;
        };
        /// A console at a room's door, waiting for its host's answer.
        struct Knock
        {
            std::uint64_t peer = 0; ///< 0: none
            char name[hldata::kNetTextBytes] = {};
            std::uint8_t ticket = 0;
            std::uint32_t sinceMs = 0, saidMs = 0;
        };
        struct Room
        {
            std::uint32_t id = 0;
            std::uint64_t host = 0; ///< its host's key
            hldata::RelayOpen open;
            bool admit = true;
            bool voice = false; ///< its host lets them talk
            std::uint64_t clients[hldata::kNetPlayers] = {}; ///< by slot (0 unused); 0: nobody
            std::uint32_t statusMs = 0;
            Knock knocks[hldata::kRelayKnocksMost];
            std::uint8_t nextTicket = 1;
            int count() const;
            unsigned mask() const;
        };

        Peer *peer(const Address &at);
        void leave(Peer &peer, std::vector<Out> &out, bool told);
        /// `peer` no longer at a door: its knock forgotten (and, `refused`,
        /// it is told why).
        void unknock(Peer &peer, std::vector<Out> &out, int refused);
        void knock(const Room &room, const Knock &knock, std::vector<Out> &out);
        /// `peer` let into `room`, or told why not.
        void admitInto(Room &room, Peer &peer, std::uint32_t nowMs, std::vector<Out> &out);
        void drop(std::uint64_t key, std::vector<Out> &out, bool told);
        void status(Room &room, std::uint32_t nowMs, std::vector<Out> &out);
        void say(std::vector<Out> &out, const Peer &to, const std::uint8_t *bytes, int size);
        void note(const std::string &line);

        std::map<std::uint64_t, Peer> peers_;
        std::map<std::uint32_t, Room> rooms_;
        std::uint32_t nextRoom_ = 1;
    };
} // namespace hlrelay
