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
#include "Limits.hpp"

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

    /// How a console's address is put in the log. (An address says who
    /// someone is, near enough: it is kept no more than the operator needs.)
    enum class IpLog : std::uint8_t
    {
        Trunc, ///< its first three numbers: 192.0.2.x
        Hash,  ///< a short hash, under a secret the operator changes daily: the same address the same that day
        Full,  ///< all of it, and its port (to find a fault; not on a public relay)
    };

    /// Datagrams the relay let fall, by why.
    struct Drops
    {
        std::uint64_t notOurs = 0;   ///< not this relay protocol's
        std::uint64_t otherVersion = 0; ///< of another version (answered, now and then, with this one's)
        std::uint64_t tooMany = 0;   ///< a console coming in, while it knows as many as it may
        std::uint64_t unknown = 0;   ///< from no console it knows (or not in its session), and not coming in (answered Unknown, now and then)
        std::uint64_t badCookie = 0; ///< coming in with a cookie it did not make for that address, or too old
        std::uint64_t resumed = 0;   ///< consoles taken up again from another address (not a drop: counted for the log)
        // Over a limit (Limits.hpp).
        std::uint64_t rateHello = 0;   ///< an address coming in too often
        std::uint64_t rateControl = 0; ///< a console saying too much (but Data and Voice)
        std::uint64_t rateData = 0;    ///< a console sending frames too often, or too many bytes of them
        std::uint64_t rateVoice = 0;   ///< a console talking more than a voice's worth
        std::uint64_t capPeers = 0;    ///< a console coming in from an address with as many as it may have
        std::uint64_t capRooms = 0;    ///< a room opened from an address with as many as it may have
        std::uint64_t capKnocks = 0;   ///< a console asking to join from an address with as many waiting as it may
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
        /// A cookie is good in the window of time it was made in and the
        /// next: windows of 2^14 ms (16 s).
        static constexpr int kCookieShift = 14;
        static constexpr int kSecretBytes = 32;
        /// A datagram of another version, or from a console not known, is
        /// answered (Version, Unknown) at most once a second to an address (as
        /// near as a table of kVersionSlots says), and to kVersionPerSecond
        /// addresses a second in all.
        static constexpr int kVersionSlots = 1024;
        static constexpr int kVersionPerSecond = 200;
        /// Addresses kept account of before their console is known (each one
        /// in a slot by a hash of it, under the secret: another in the same
        /// slot takes its place).
        static constexpr int kIpSlots = 8192;
        /// A line in the log for an address over a limit: once a minute.
        static constexpr std::uint32_t kOverLogMs = 60000;

        /// The relay's own secret (kSecretBytes, random, made at start-up and
        /// never shown): what cookies are made under, and sessions and tokens
        /// picked from. (Without it, all zeros: for the tests.)
        void setSecret(const std::uint8_t *secret);

        /// What it does, a line at a time (rooms opened and closed, who came
        /// and went), for the operator.
        std::function<void(const std::string &)> log;
        /// How much each address and console may do (and the relay send).
        Limits limits;
        /// How addresses are put in it.
        IpLog ipLog = IpLog::Trunc;
        /// The secret IpLog::Hash hashes under (16 bytes): a new one each day.
        void setLogSalt(const std::uint8_t *salt);
        /// An address as the log shows it.
        std::string shown(const Address &at) const;

        /// A datagram from `from` at `nowMs`: what it says is done, and what
        /// is to be sent because of it is added to `out`.
        void receive(const Address &from, const std::uint8_t *bytes, int size, std::uint32_t nowMs,
                     std::vector<Out> &out);
        /// Now and then (every tenth of a second or so): who said nothing for
        /// too long is let go, and the hosts are told who is there.
        void tick(std::uint32_t nowMs, std::vector<Out> &out);
        /// The relay stopping: every console told so (twice: there is no
        /// waiting to hear it arrived), and forgotten.
        void stop(std::vector<Out> &out);

        std::size_t peers() const { return peers_.size(); }
        std::size_t rooms() const { return rooms_.size(); }
        const Drops &drops() const { return drops_; }

    private:
        struct Peer
        {
            std::uint32_t id = 0; ///< its number here, for as long as it is known (never 0)
            Address at;
            std::uint32_t session = 0; ///< the relay's pick
            std::uint32_t nonce = 0;   ///< what it came in with (an Enter said again is welcomed again)
            std::uint8_t token[hldata::kRelayTokenBytes] = {};
            std::uint32_t heardMs = 0;
            std::uint32_t room = 0; ///< the room it hosts or is in; 0: none
            int slot = -1;          ///< 0: its host; 1..7: a client of it
            std::uint32_t knocking = 0; ///< the room whose host it waits to be let in by; 0: none
            Bucket control, data, dataBytes, voice; ///< Limits.hpp
        };
        /// A console at a room's door, waiting for its host's answer.
        struct Knock
        {
            std::uint32_t peer = 0; ///< its console's id; 0: none
            std::uint32_t ip = 0;   ///< (its console's address, as counted)
            char name[hldata::kNetTextBytes] = {};
            std::uint8_t ticket = 0;
            std::uint32_t sinceMs = 0, saidMs = 0;
        };
        struct Room
        {
            std::uint32_t id = 0;
            std::uint32_t host = 0; ///< its host's id
            hldata::RelayOpen open;
            bool admit = true;
            bool voice = false; ///< its host lets them talk
            std::uint32_t clients[hldata::kNetPlayers] = {}; ///< ids by slot (0 unused); 0: nobody
            std::uint32_t statusMs = 0;
            Knock knocks[hldata::kRelayKnocksMost];
            std::uint8_t nextTicket = 1;
            int count() const;
            unsigned mask() const;
        };

        /// The console at `at`, if the relay knows one there.
        Peer *peer(const Address &at);
        /// A console by its number; null if there is none.
        Peer *byId(std::uint32_t id);
        /// `peer` out of its room (as a host: the room closed, its clients
        /// told); `why`: it is told so itself, and why (0: not told).
        void leave(Peer &peer, std::vector<Out> &out, int why);
        /// `peer` no longer at a door: its knock forgotten (and, `refused`,
        /// it is told why).
        void unknock(Peer &peer, std::vector<Out> &out, int refused);
        void knock(const Room &room, const Knock &knock, std::vector<Out> &out);
        /// `peer` let into `room`, or told why not.
        void admitInto(Room &room, Peer &peer, std::uint32_t nowMs, std::vector<Out> &out);
        /// `id` forgotten, out of its room first (`why`: as leave).
        void drop(std::uint32_t id, std::vector<Out> &out, int why);
        void status(Room &room, std::uint32_t nowMs, std::vector<Out> &out);
        void say(std::vector<Out> &out, const Peer &to, const std::uint8_t *bytes, int size);
        void say(std::vector<Out> &out, const Address &to, const std::uint8_t *bytes, int size);
        /// The cookie for `at` coming in with `nonce`, made in `window`.
        void cookie(const Address &at, std::uint32_t nonce, std::uint32_t window, std::uint8_t *out) const;
        bool cookieGood(const Address &at, std::uint32_t nonce, const std::uint8_t *cookie, std::uint32_t nowMs) const;
        /// An Enter: a console known from now on (or again), if its cookie is good.
        void enter(const Address &from, std::uint32_t nonce, const std::uint8_t *bytes, int size, std::uint32_t nowMs,
                   std::vector<Out> &out);
        /// Eight bytes no one can guess (keyed by the secret).
        std::uint64_t random();
        /// The console whose token `token` is (kRelayTokenBytes); null if
        /// none (or it is all zeros: none given).
        Peer *byToken(const std::uint8_t *token);
        /// `peer` taken up again from `from` (its address changed: a router
        /// gave it another port, or it came back after a drop), its place
        /// kept; whoever was known at `from` is gone. False if `from`'s
        /// address may have no more consoles.
        bool rebind(Peer &peer, const Address &from, std::uint32_t nowMs, std::vector<Out> &out);
        /// What `from` had counted for `peer` (itself, its room, its knock)
        /// now counted for `to`.
        void moveCounts(const Peer &peer, std::uint32_t from, std::uint32_t to);

        /// An address before its console is known: how often it comes in,
        /// and when it was last logged as over a limit.
        struct IpSlot
        {
            std::uint32_t ip = 0;
            bool used = false;
            Bucket hello;
            bool logged = false;
            std::uint32_t loggedMs = 0;
        };
        IpSlot &ipSlot(std::uint32_t ip);
        /// What one address has at once: counted as it changes.
        struct IpCount
        {
            std::uint32_t peers = 0, rooms = 0, knocks = 0;
        };
        IpCount &counted(std::uint32_t ip) { return ipCounts_[ip]; }
        void uncount(std::uint32_t ip, std::uint32_t IpCount::*what);
        /// A knock forgotten, and its address's count with it.
        void clearKnock(Knock &k);
        /// A datagram from `at` over a limit: counted in `count`, and logged
        /// (once a minute for an address).
        void over(const Address &at, std::uint64_t &count, const char *why, std::uint32_t nowMs);
        void note(const std::string &line);
        /// A console as the log names it: its id here, and its address as shown.
        std::string named(const Peer &peer) const;

        // The consoles by id, and which is at each address. (Rooms and knocks
        // name a console by its id, so that it may come from another address
        // and keep its place.)
        std::map<std::uint32_t, Peer> peers_;
        std::map<std::uint64_t, std::uint32_t> at_;
        std::map<std::uint32_t, Room> rooms_;
        std::uint32_t nextRoom_ = 1;
        std::uint32_t nextPeer_ = 1;
        Drops drops_;
        std::uint8_t salt_[16] = {};
        std::uint8_t cookieKey_[16] = {};
        std::uint8_t randomKey_[16] = {};
        std::uint64_t randomCount_ = 0;
        std::vector<IpSlot> ipSlots_ = std::vector<IpSlot>(kIpSlots);
        std::map<std::uint32_t, IpCount> ipCounts_;
        /// When each slot (an address's, hashed) was last answered Version or
        /// Unknown (+1: 0 never).
        std::uint32_t versionSaidMs_[kVersionSlots] = {};
        std::uint32_t versionSecondMs_ = 0;
        int versionCount_ = 0;
        /// Whether a datagram of another version, or from one not known, from
        /// `from` is answered now.
        bool mayAnswer(const Address &from, std::uint32_t nowMs);
    };
} // namespace hlrelay
