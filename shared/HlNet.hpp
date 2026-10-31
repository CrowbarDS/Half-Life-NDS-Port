#pragma once

// A game between consoles, over the DS's local wireless: what goes through
// the air and how each end keeps its account of it.
//
// THE RADIO has one console the host and up to three its clients. Every
// frame the host sends one frame that all the clients hear (its CMD), and
// each client answers with one of its own (its REPLY), which only the host
// hears. Nothing else is used while a game is played: that pair is the one
// thing the hardware does on time, and it does it every frame.
//
// WHAT IS SAID. Each console runs its own player and says, every frame,
// where that player is and what they are doing: a STATE, sixteen bytes,
// whole each time -- one lost is replaced by the next. What cannot be lost
// (a shot that hit, a death, a pickup taken, a door set off) is an EVENT:
// numbered, sent again in every frame until the other end says it has it,
// and taken in order, once.
//
// A client's events go to the host; what the others need of them the host
// passes on in its own. So the host's events are one numbered stream that
// every client follows, each from the number it was given when it joined,
// and the host sends from the oldest that any of them still lacks.
//
// FILES. A console started by Download Play has no card: it asks the host
// for each file it opens, and the host sends it in pieces (FileSender,
// FileReceiver, at the bottom). That is done before a match, in frames of
// its own kind.
//
// std-only, no heap, fixed buffers: the console runs part of this in an
// interrupt, and the host tests run all of it (test_net) through a link
// that loses frames.

#include <cstdint>
#include <cstring>

namespace hldata
{
    inline constexpr int kNetPlayers = 8; ///< the host is player 0
    /// The most a frame may be: what the radio is set up for.
    inline constexpr int kNetCmdBytes = 720;   ///< the host's
    inline constexpr int kNetReplyBytes = 220; ///< a client's
    /// ... and the most the radio can carry (dswifi: a host's frame in its
    /// 768-byte command buffer with its headers; a client's in 256).
    static_assert(kNetCmdBytes <= 724 && kNetReplyBytes <= 227, "a frame the radio cannot carry");
    /// The first two bytes of every frame, and of the beacon's game id: a
    /// frame of another game, or of another build of this one, is not read.
    /// (Version 2: eight players, profiles, the rules, teams, sprays.)
    inline constexpr std::uint16_t kNetMagic = 0x4C48; // "HL"
    inline constexpr std::uint8_t kNetVersion = 6;

    // ------------------------------------------------------------ the state

    inline constexpr std::uint8_t kNetHere = 1;      ///< there is such a player
    inline constexpr std::uint8_t kNetAlive = 2;
    inline constexpr std::uint8_t kNetCrouched = 4;
    inline constexpr std::uint8_t kNetOnGround = 8;
    inline constexpr std::uint8_t kNetFiring = 16;
    inline constexpr std::uint8_t kNetLamp = 32;
    inline constexpr std::uint8_t kNetInGame = 64;   ///< its map is loaded: it is playing

    /// Where a player is and what they are doing.
    struct NetPlayer
    {
        std::int16_t pos[3] = {0, 0, 0}; ///< quarter units
        std::uint16_t yaw = 0;
        std::int8_t pitch = 0;           ///< 256 to the circle
        std::int8_t velocity[3] = {0, 0, 0}; ///< eight units a second
        std::uint8_t weapon = 0;         ///< kWeapon*
        std::uint8_t flags = 0;          ///< kNet*
        std::uint8_t health = 0;
        /// Counts the times the player has been put somewhere (a spawn): a
        /// change says "do not glide there".
        std::uint8_t spawns = 0;
    };
    inline constexpr int kNetPlayerBytes = 16;

    // ----------------------------------------------------------- the events

    enum class NetEventType : std::uint8_t
    {
        None,
        Hello,    ///< client: text = its name
        Roster,   ///< host: player `a` is called text ("" = gone)
        Start,    ///< host: a match is to begin -- every console loads its map (sent it, if it has not the
                  ///< game) and says Ready; v[0..1] the map's name hash, v[2] frag limit, v[3] minutes,
                  ///< v[4] how hard
        End,      ///< host: the match is over
        Shot,     ///< `from` fired weapon `a`: v[0] yaw, v[1] pitch (65536 to the circle), v[2] 1 = second trigger
        Hit,      ///< `from` hit player `a`: v[0] damage, v[1] which of a's lives (their spawn count)
        Throw,    ///< `from` threw a grenade: v[0..2] from where (quarter units), v[3..5] how fast (units a second)
        Burst,    ///< `from`'s grenade went off at v[0..2] (quarter units)
        Died,     ///< `from` died; `a` = who did it (kNetPlayers: nobody else)
        Spawned,  ///< `from` is back
        Score,    ///< host: player `a` has v[0] frags and v[1] deaths
        Take,     ///< client: I want the item that is actor v[0]
        Taken,    ///< host: player `a` has the item that is actor v[0]
        Back,     ///< host: the item that is actor v[0] is back
        Fire,     ///< `from` set off the map's target v[0], used as v[1], by record v[2]
        Brushes,  ///< host: brush v[0] is at v[1..3] (quarter units), in state v[4], v[5] ms into it
        Bye,      ///< leaving: `from` (the host: everybody out; to a client in `a`: not let in)
        Rules,    ///< host: v[0] the mode (kMode*), v[1] kRule* flags, v[2] how many may play, v[3..4]
                  ///< the map's name hash (0: none chosen)
        Profile,  ///< `from` looks like this: v[0] model, v[1] top | bottom << 8, v[2] team asked for
                  ///< (0: any), v[3] spray colour, v[4..5] its spray's netSprayHash
        SprayPiece, ///< `from`'s spray: piece `a` (kNetSprayPieces of them), twelve bytes in v
        Team,     ///< host: player `a` plays for team v[0] (1 or 2; 0: none, a free-for-all)
        Spray,    ///< `from` sprayed: at v[0..2] (quarter units), on a wall facing v[3..4]
                  ///< (netPackNormal: ten bits of each axis)
        Ready,    ///< client: the match's map is loaded here
        Go,       ///< host: everyone is ready (or long enough has gone by): the match begins, on all at once
        Cancel,   ///< host: the match about to begin is not to; back to the lobby
        Room,     ///< a console sent the game: v[0..1] how far up its memory its heap reaches, v[2..3] what of it
                  ///< is free in its lobby (KB)
        Fetch,    ///< client: I want this (HlAirBank.hpp): `a` FetchFrom, v[0..1] what
        FetchHave,///< client: of sending `a`, v[0] the first piece I lack, v[1..4] the bits of those past it
        FetchAgain,///< client: sending `a` came wrong: all of it, again
        FetchNone,///< host: there is no such thing: `a` FetchFrom, v[0..1] what
        RulesMore,///< host: what follows a match: v[0] seconds of its scores, v[1] how the next map is chosen,
                  ///< v[2] 1 = a lobby's matches added up, v[3] seconds of a vote
        NextDecided,///< host: the map that comes next, for the scores to say: text = its name, cut to fit;
                  ///< `a` = which of the vote's, and 1 (0: not by a vote)
        NextMap,  ///< host: the vote offers this as its map `a`: text = its name, or the rest of it
                  ///< (HlNetVote.hpp's kNetVoteNameRest, kNetVoteCancel)
        NextPick, ///< client: I vote for its map `a`
        NextTally,///< host: v[0] have chosen the vote's map `a`
        NextOpen, ///< host: the vote is open
        NextTurn, ///< host: player `a` alone chooses the next map, from the vote's list
    };

    /// The rules' flags (Rules' v[1]): the game's mp_weaponstay,
    /// mp_falldamage 1, mp_friendlyfire, mp_flashlight, its autoaim; and
    /// whether consoles without the game are let in.
    inline constexpr std::uint16_t kRuleWeaponsStay = 1;
    inline constexpr std::uint16_t kRuleRealisticFall = 2;
    inline constexpr std::uint16_t kRuleFriendlyFire = 4;
    inline constexpr std::uint16_t kRuleFlashlight = 8;
    inline constexpr std::uint16_t kRuleAutoaim = 16;
    inline constexpr std::uint16_t kRuleGuests = 32;

    /// The kinds of match (Rules' v[0]): every one for themselves, or two
    /// teams (HlSettings.hpp's kModeDeathmatch, kModeTeamplay).
    inline constexpr std::uint8_t kNetModeDeathmatch = 0;
    inline constexpr std::uint8_t kNetModeTeamplay = 1;

    /// A spray: 32 by 32, two bits a pixel (HlUi.hpp's logos), 256 bytes, in
    /// pieces of twelve.
    inline constexpr int kNetSprayBytes = 256;
    inline constexpr int kNetSprayPieces = (kNetSprayBytes + 11) / 12;
    inline constexpr std::uint32_t kNetSprayAll = (1u << kNetSprayPieces) - 1;
    /// What a spray's bytes come to (FNV-1a): a Profile says it, so that a
    /// spray is known to be whole. Never 0.
    inline std::uint32_t netSprayHash(const std::uint8_t *bytes)
    {
        std::uint32_t hash = 2166136261u;
        for (int i = 0; i < kNetSprayBytes; ++i)
            hash = (hash ^ bytes[i]) * 16777619u;
        return hash != 0 ? hash : 1;
    }

    inline constexpr int kNetTextBytes = 12;

    struct NetEvent
    {
        NetEventType type = NetEventType::None;
        std::uint8_t from = 0; ///< whose it is (a player)
        std::uint8_t a = 0;
        union
        {
            std::int16_t v[6];
            char text[kNetTextBytes];
        };
        NetEvent() { std::memset(text, 0, sizeof(text)); }
    };
    inline constexpr int kNetEventBytes = 15;
    inline constexpr int kNetClientEvents = 6; ///< in a frame of a client's, at most
    /// A host's frame before its events (a header, each player's place and
    /// stream, the events' count): with eight, still room for thirty.
    inline constexpr int kNetHostFixedBytes = 8 + kNetPlayers * (kNetPlayerBytes + 6) + 3;
    static_assert(kNetHostFixedBytes + 30 * kNetEventBytes <= kNetCmdBytes, "a host's frame with too few events");

    // -------------------------------------------------------------- packing

    namespace detail
    {
        struct NetOut
        {
            std::uint8_t *at;
            std::uint8_t *end;
            bool ok = true;
            void u8(std::uint32_t v)
            {
                if (at >= end)
                {
                    ok = false;
                    return;
                }
                *at++ = static_cast<std::uint8_t>(v);
            }
            void u16(std::uint32_t v)
            {
                u8(v);
                u8(v >> 8);
            }
            void u32(std::uint32_t v)
            {
                u16(v);
                u16(v >> 16);
            }
            void bytes(const void *from, int count)
            {
                if (end - at < count)
                {
                    ok = false;
                    return;
                }
                std::memcpy(at, from, static_cast<std::size_t>(count));
                at += count;
            }
        };
        struct NetIn
        {
            const std::uint8_t *at;
            const std::uint8_t *end;
            bool ok = true;
            std::uint32_t u8()
            {
                if (at >= end)
                {
                    ok = false;
                    return 0;
                }
                return *at++;
            }
            std::uint32_t u16()
            {
                const std::uint32_t low = u8();
                return low | (u8() << 8);
            }
            std::uint32_t u32()
            {
                const std::uint32_t low = u16();
                return low | (u16() << 16);
            }
            void bytes(void *to, int count)
            {
                if (end - at < count)
                {
                    ok = false;
                    std::memset(to, 0, static_cast<std::size_t>(count));
                    return;
                }
                std::memcpy(to, at, static_cast<std::size_t>(count));
                at += count;
            }
        };

        inline void put(NetOut &out, const NetPlayer &p)
        {
            for (int i = 0; i < 3; ++i)
                out.u16(static_cast<std::uint16_t>(p.pos[i]));
            out.u16(p.yaw);
            out.u8(static_cast<std::uint8_t>(p.pitch));
            for (int i = 0; i < 3; ++i)
                out.u8(static_cast<std::uint8_t>(p.velocity[i]));
            out.u8(p.weapon);
            out.u8(p.flags);
            out.u8(p.health);
            out.u8(p.spawns);
        }
        inline void get(NetIn &in, NetPlayer &p)
        {
            for (int i = 0; i < 3; ++i)
                p.pos[i] = static_cast<std::int16_t>(in.u16());
            p.yaw = static_cast<std::uint16_t>(in.u16());
            p.pitch = static_cast<std::int8_t>(in.u8());
            for (int i = 0; i < 3; ++i)
                p.velocity[i] = static_cast<std::int8_t>(in.u8());
            p.weapon = static_cast<std::uint8_t>(in.u8());
            p.flags = static_cast<std::uint8_t>(in.u8());
            p.health = static_cast<std::uint8_t>(in.u8());
            p.spawns = static_cast<std::uint8_t>(in.u8());
        }
        inline bool netHasText(NetEventType type)
        {
            return type == NetEventType::Hello || type == NetEventType::Roster;
        }
        inline void put(NetOut &out, const NetEvent &e)
        {
            out.u8(static_cast<std::uint8_t>(e.type));
            out.u8(e.from);
            out.u8(e.a);
            if (netHasText(e.type))
                out.bytes(e.text, kNetTextBytes);
            else
                for (int i = 0; i < 6; ++i)
                    out.u16(static_cast<std::uint16_t>(e.v[i]));
        }
        inline void get(NetIn &in, NetEvent &e)
        {
            e.type = static_cast<NetEventType>(in.u8());
            e.from = static_cast<std::uint8_t>(in.u8());
            e.a = static_cast<std::uint8_t>(in.u8());
            if (netHasText(e.type))
            {
                in.bytes(e.text, kNetTextBytes);
                e.text[kNetTextBytes - 1] = 0;
            }
            else
                for (int i = 0; i < 6; ++i)
                    e.v[i] = static_cast<std::int16_t>(in.u16());
        }
    } // namespace detail

    // ------------------------------------------------- events, kept in order

    /// Events waiting to be sent, each with its number: kept until whoever
    /// they are for has said it has them.
    struct NetOutbox
    {
        static constexpr int kMost = 96;
        NetEvent events[kMost];
        std::uint16_t first = 0; ///< the number of events[head]
        int head = 0, count = 0;
        /// Events dropped because the box was full: a link that has stopped
        /// taking them. (Whoever owns the link gives it up.)
        int lost = 0;

        std::uint16_t next() const { return static_cast<std::uint16_t>(first + count); }
        bool add(const NetEvent &event)
        {
            if (count >= kMost)
            {
                ++lost;
                return false;
            }
            events[(head + count++) % kMost] = event;
            return true;
        }
        const NetEvent &at(std::uint16_t number) const
        {
            return events[(head + static_cast<std::uint16_t>(number - first)) % kMost];
        }
        /// Everything before `number` has arrived: forget it.
        void acked(std::uint16_t number)
        {
            const std::uint16_t done = static_cast<std::uint16_t>(number - first);
            if (done == 0 || done > count)
                return; // (nothing new; or a number from another time)
            head = (head + done) % kMost;
            count -= done;
            first = number;
        }
    };

    /// Events arrived and not yet read, in order: `Most` of them at most (one
    /// that does not fit is sent again).
    template <int Most>
    struct NetInboxOf
    {
        static constexpr int kMost = Most;
        NetEvent events[kMost];
        int head = 0, count = 0;
        std::uint16_t expected = 0; ///< the number of the next one wanted
        bool synced = false;        ///< `expected` has been set

        bool full() const { return count >= kMost; }
        bool take(NetEvent &event)
        {
            if (count == 0)
                return false;
            event = events[head];
            head = (head + 1) % kMost;
            --count;
            return true;
        }
        /// An event numbered `number` has come. Kept if it is the one
        /// wanted (and there is room: if not, it will be sent again).
        void arrive(std::uint16_t number, const NetEvent &event)
        {
            if (!synced || number != expected || full())
                return;
            events[(head + count++) % kMost] = event;
            ++expected;
        }
    };
    /// A client's of the host's stream (as much as a frame of the host's
    /// brings, and more) ...
    using NetInbox = NetInboxOf<64>;
    /// ... and the host's of each client's (whose frames bring twelve).
    using NetClientInbox = NetInboxOf<32>;

    // -------------------------------------------------------- the room's card

    /// What a host's beacon says of its match, for the consoles looking for
    /// one (16 bytes, after its name: the radio's game data): the protocol's
    /// version, the mode, how many may play, whether consoles without the
    /// game are let in, the map's name hash.
    struct NetRoomInfo
    {
        std::uint8_t version = kNetVersion;
        std::uint8_t mode = kNetModeDeathmatch;
        std::uint8_t most = 4;
        bool guests = false;
        std::uint32_t mapHash = 0;
    };
    inline constexpr int kNetRoomBytes = 8;
    inline void netRoomWrite(const NetRoomInfo &room, std::uint8_t *out)
    {
        out[0] = room.version;
        out[1] = room.mode;
        out[2] = room.most;
        out[3] = room.guests ? 1 : 0;
        for (int i = 0; i < 4; ++i)
            out[4 + i] = static_cast<std::uint8_t>(room.mapHash >> (i * 8));
    }
    /// False for a host that said nothing, or not enough (one of an older
    /// build: `room` says what is known, its version 0).
    inline bool netRoomRead(const std::uint8_t *bytes, int size, NetRoomInfo &room)
    {
        room = NetRoomInfo();
        room.version = 0;
        if (size < kNetRoomBytes)
            return false;
        room.version = bytes[0];
        room.mode = bytes[1];
        room.most = bytes[2];
        room.guests = bytes[3] != 0;
        room.mapHash = 0;
        for (int i = 0; i < 4; ++i)
            room.mapHash |= static_cast<std::uint32_t>(bytes[4 + i]) << (i * 8);
        return true;
    }

    /// How strong a signal is, in bars (0..4): from the radio's own reading
    /// (the DS's, 0..255; in dBm, below 0, from a DSi's own radio) ...
    inline int netBars(int rssi)
    {
        if (rssi < 0)
            return rssi >= -55 ? 4 : (rssi >= -65 ? 3 : (rssi >= -75 ? 2 : (rssi >= -85 ? 1 : 0)));
        return rssi >= 40 ? 4 : (rssi >= 28 ? 3 : (rssi >= 18 ? 2 : (rssi >= 8 ? 1 : 0)));
    }
    /// ... or, on a host, which has none of its clients', from how many of
    /// its last 60 frames were answered (netLinkShare).
    inline int netShareBars(int answered)
    {
        return answered >= 54 ? 4 : (answered >= 42 ? 3 : (answered >= 24 ? 2 : (answered >= 1 ? 1 : 0)));
    }

    // ------------------------------------------------------------ the frames

    inline constexpr std::uint8_t kNetFrameGame = 1;     ///< states and events
    inline constexpr std::uint8_t kNetFrameFileAsk = 2;  ///< client: send me this file
    inline constexpr std::uint8_t kNetFrameFileData = 3; ///< host: a piece of one

    /// A client's account of the game.
    struct NetClient
    {
        /// Which player this console is (1..3): its number with the radio,
        /// which only the host is told. 0 until the host has said (it
        /// knows this console by `token`, which goes in every frame).
        int me = 0;
        std::uint16_t token = 0;
        NetOutbox out;
        NetInbox in;
        NetPlayer players[kNetPlayers]; ///< the others, as last heard
        std::uint32_t heard = 0;        ///< frames since the host was last heard
        std::uint32_t hostFrame = 0;    ///< the host's count of its own frames, as last heard
    };

    /// The host's.
    struct NetHost
    {
        NetOutbox out; ///< one stream, which every client follows
        struct Client
        {
            bool here = false;
            NetClientInbox in;
            std::uint16_t acked = 0;    ///< what it has of the host's stream
            std::uint16_t joinedAt = 0; ///< ... and where it started
            std::uint16_t token = 0;    ///< what it calls itself (0: not heard yet)
            std::uint32_t heard = 0;    ///< frames since it was last heard
            /// A bit a frame of the host's, the newest lowest: whether it
            /// was answered (how good the link is: netLinkShare).
            std::uint64_t answered = 0;
        } clients[kNetPlayers];         ///< [0] is never used: that is the host
        NetPlayer players[kNetPlayers]; ///< [0] is the host's own, the rest as last heard
        std::uint32_t frame = 0;
    };

    /// A client comes (the radio says so): it follows the stream from here.
    inline void netHostJoin(NetHost &host, int who)
    {
        NetHost::Client &client = host.clients[who];
        client = NetHost::Client();
        client.here = true;
        client.acked = client.joinedAt = host.out.next();
        client.in.synced = true; // (a client's own stream starts at nought)
        host.players[who] = NetPlayer();
    }
    inline void netHostLeave(NetHost &host, int who)
    {
        host.clients[who].here = false;
        host.players[who] = NetPlayer();
    }

    /// The host's frame: who is where, and the events from the oldest any
    /// client still lacks. Returns its length.
    inline int netHostWrite(NetHost &host, const NetPlayer &mine, std::uint8_t *bytes, int size = kNetCmdBytes)
    {
        host.players[0] = mine;
        // What the slowest client has: nothing older need be kept.
        bool any = false;
        std::uint16_t oldest = host.out.next();
        for (int c = 1; c < kNetPlayers; ++c)
        {
            if (!host.clients[c].here)
                continue;
            const std::uint16_t behind = static_cast<std::uint16_t>(host.out.next() - host.clients[c].acked);
            if (!any || behind > static_cast<std::uint16_t>(host.out.next() - oldest))
                oldest = host.clients[c].acked;
            any = true;
        }
        host.out.acked(oldest);

        detail::NetOut out{bytes, bytes + size};
        out.u16(kNetMagic);
        out.u8(kNetVersion);
        out.u8(kNetFrameGame);
        out.u32(host.frame++);
        for (int p = 0; p < kNetPlayers; ++p)
        {
            detail::put(out, host.players[p]);
            // What the host has of this client's events, and where this
            // client's following of the host's began.
            out.u16(host.clients[p].in.expected);
            out.u16(host.clients[p].joinedAt);
            out.u16(host.clients[p].here ? host.clients[p].token : 0);
        }
        const int room = static_cast<int>((out.end - out.at - 3) / kNetEventBytes);
        const int count = !any ? 0 : (host.out.count < room ? host.out.count : room);
        out.u16(host.out.first);
        out.u8(static_cast<std::uint8_t>(count));
        for (int n = 0; n < count; ++n)
            detail::put(out, host.out.at(static_cast<std::uint16_t>(host.out.first + n)));
        for (int c = 1; c < kNetPlayers; ++c)
            if (host.clients[c].here)
            {
                ++host.clients[c].heard;
                host.clients[c].answered <<= 1;
            }
        return out.ok ? static_cast<int>(out.at - bytes) : 0;
    }

    /// Of the host's last 60 frames, how many client `who` answered (a
    /// host has no strength of a client's signal: this is what it has).
    inline int netLinkShare(const NetHost &host, int who)
    {
        if (who < 1 || who >= kNetPlayers || !host.clients[who].here)
            return 0;
        std::uint64_t bits = host.clients[who].answered & ((std::uint64_t{1} << 60) - 1);
        int n = 0;
        for (; bits != 0; bits &= bits - 1)
            ++n;
        return n;
    }

    /// A client hears the host's frame. False if it is not one.
    inline bool netClientRead(NetClient &client, const std::uint8_t *bytes, int size)
    {
        detail::NetIn in{bytes, bytes + size};
        if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameGame)
            return false;
        const std::uint32_t frame = in.u32();
        NetPlayer players[kNetPlayers];
        std::uint16_t has[kNetPlayers], joined[kNetPlayers], tokens[kNetPlayers];
        for (int p = 0; p < kNetPlayers; ++p)
        {
            detail::get(in, players[p]);
            has[p] = static_cast<std::uint16_t>(in.u16());
            joined[p] = static_cast<std::uint16_t>(in.u16());
            tokens[p] = static_cast<std::uint16_t>(in.u16());
        }
        const std::uint16_t first = static_cast<std::uint16_t>(in.u16());
        const int count = static_cast<int>(in.u8());
        if (!in.ok)
            return false;
        client.hostFrame = frame;
        client.heard = 0;
        for (int p = 0; p < kNetPlayers; ++p)
            client.players[p] = players[p];
        // Which of them this console is: the one the host has its token for.
        if (client.me == 0)
        {
            for (int p = 1; p < kNetPlayers; ++p)
                if (client.token != 0 && tokens[p] == client.token)
                    client.me = p;
            if (client.me == 0)
                return true; // (the host has not heard this console yet)
        }
        else if (tokens[client.me] != client.token)
            return true; // (a frame from before this console had its place)
        client.out.acked(has[client.me]);
        if (!client.in.synced)
        {
            client.in.expected = joined[client.me];
            client.in.synced = true;
        }
        for (int n = 0; n < count; ++n)
        {
            NetEvent event;
            detail::get(in, event);
            if (!in.ok)
                break;
            client.in.arrive(static_cast<std::uint16_t>(first + n), event);
        }
        return true;
    }

    /// A client's frame: where it is, what it has of the host's events, and
    /// its own from the oldest the host lacks.
    inline int netClientWrite(NetClient &client, const NetPlayer &mine, std::uint8_t *bytes,
                              int size = kNetReplyBytes)
    {
        detail::NetOut out{bytes, bytes + size};
        out.u16(kNetMagic);
        out.u8(kNetVersion);
        out.u8(kNetFrameGame);
        out.u16(client.token);
        detail::put(out, mine);
        out.u16(client.in.expected);
        out.u8(client.in.synced ? 1 : 0);
        // (No more than the radio carries: a frame near kNetReplyBytes is lost
        // whole, and sent again, and lost: the rest wait for the next.)
        const int room = static_cast<int>((out.end - out.at - 3) / kNetEventBytes);
        const int most = room < kNetClientEvents ? room : kNetClientEvents;
        const int count = client.out.count < most ? client.out.count : most;
        out.u16(client.out.first);
        out.u8(static_cast<std::uint8_t>(count));
        for (int n = 0; n < count; ++n)
            detail::put(out, client.out.at(static_cast<std::uint16_t>(client.out.first + n)));
        ++client.heard;
        return out.ok ? static_cast<int>(out.at - bytes) : 0;
    }

    /// The host hears a frame from client `who` (the radio says which).
    /// False if it is not a game's frame, or not from anyone the host has.
    inline bool netHostRead(NetHost &host, int who, const std::uint8_t *bytes, int size)
    {
        detail::NetIn in{bytes, bytes + size};
        if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameGame)
            return false;
        const std::uint16_t token = static_cast<std::uint16_t>(in.u16());
        if (who < 1 || who >= kNetPlayers || !host.clients[who].here)
            return false;
        NetPlayer player;
        detail::get(in, player);
        const std::uint16_t has = static_cast<std::uint16_t>(in.u16());
        const bool synced = in.u8() != 0;
        const std::uint16_t first = static_cast<std::uint16_t>(in.u16());
        const int count = static_cast<int>(in.u8());
        if (!in.ok)
            return false;
        NetHost::Client &client = host.clients[who];
        // (Someone else in that place now: a frame of whoever was there
        // before is not theirs.)
        if (client.token != 0 && client.token != token)
            return false;
        client.token = token;
        client.heard = 0;
        client.answered |= 1;
        host.players[who] = player;
        host.players[who].flags |= kNetHere;
        // (Only a number that is between where it began and where the
        // stream now is means anything.)
        if (synced && static_cast<std::uint16_t>(has - client.acked) <=
                          static_cast<std::uint16_t>(host.out.next() - client.acked))
            client.acked = has;
        for (int n = 0; n < count; ++n)
        {
            NetEvent event;
            detail::get(in, event);
            if (!in.ok)
                break;
            event.from = static_cast<std::uint8_t>(who); // (whatever it says)
            client.in.arrive(static_cast<std::uint16_t>(first + n), event);
        }
        return true;
    }

    // ----------------------------------------------------------------- files

    /// A file's name on the wire: "map:/lvl/crossfire.lvl", cut to this.
    inline constexpr int kNetNameBytes = 48;
    /// A file goes in pieces of this many bytes (the last, what is left).
    inline constexpr int kNetFilePiece = 600;
    /// The client keeps account of this many pieces past the first it
    /// lacks; the host sends within that, and only those it lacks.
    inline constexpr int kNetFileWindow = 64;

    /// The client's side of one file. It says, every frame, which file and
    /// which pieces of it have come; the pieces may come in any order, and
    /// each is put where it belongs in a buffer that holds the whole file.
    struct FileReceiver
    {
        char name[kNetNameBytes] = {};
        std::uint16_t id = 0;     ///< this asking's number: a piece of an older one is not taken
        std::uint32_t total = 0;  ///< the file's length (once the host has said)
        bool known = false;       ///< the host has said how long it is
        bool missing = false;     ///< ... or that there is no such file
        std::uint32_t base = 0;   ///< the first piece not yet come
        std::uint64_t have = 0;   ///< bit n: piece base + 1 + n has

        void open(const char *file)
        {
            const std::uint16_t was = id;
            *this = FileReceiver();
            id = static_cast<std::uint16_t>(was + 1);
            std::strncpy(name, file, kNetNameBytes - 1);
        }
        std::uint32_t pieces() const { return (total + kNetFilePiece - 1) / kNetFilePiece; }
        bool done() const { return missing || (known && base >= pieces()); }
        /// How much has come, in bytes, near enough: for whoever shows it.
        std::uint32_t got() const
        {
            const std::uint32_t bytes = base * kNetFilePiece;
            return known && bytes > total ? total : bytes;
        }

        /// Once a frame while it is wanted: an asking frame to send, or 0.
        int ask(std::uint8_t *bytes, int size = kNetReplyBytes)
        {
            if (done())
                return 0;
            detail::NetOut out{bytes, bytes + size};
            out.u16(kNetMagic);
            out.u8(kNetVersion);
            out.u8(kNetFrameFileAsk);
            out.u16(id);
            out.u32(base);
            out.u32(static_cast<std::uint32_t>(have));
            out.u32(static_cast<std::uint32_t>(have >> 32));
            out.bytes(name, kNetNameBytes);
            return out.ok ? static_cast<int>(out.at - bytes) : 0;
        }

        /// A frame from the host. Says how long the file is (`length()`
        /// after it: the caller's buffer must hold that before the next
        /// call); and if it is a piece of this file not yet come, copies it
        /// into `buffer` (the whole file's, `room` long) where it belongs.
        /// True if the frame was about this file.
        bool piece(int me, const std::uint8_t *bytes, int size, std::uint8_t *buffer, std::uint32_t room)
        {
            detail::NetIn in{bytes, bytes + size};
            if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameFileData)
                return false;
            const int who = static_cast<int>(in.u8());
            const std::uint16_t which = static_cast<std::uint16_t>(in.u16());
            const std::uint32_t index = in.u32();
            const std::uint32_t length = in.u32();
            const int count = static_cast<int>(in.u16());
            if (!in.ok || who != me || which != id || done())
                return false;
            if (length == 0xFFFFFFFFu)
            {
                missing = true;
                return true;
            }
            total = length;
            known = true;
            // (Not yet a buffer for it, or one too small: the length is
            // what this frame was good for.)
            if (buffer == nullptr || room < total || count <= 0 || in.end - in.at < count)
                return true;
            const std::uint32_t at = index * kNetFilePiece;
            if (index < base || index > base + kNetFileWindow || at + static_cast<std::uint32_t>(count) > total)
                return true;
            if (index > base && ((have >> (index - base - 1)) & 1))
                return true; // (come already)
            in.bytes(buffer + at, count);
            if (index > base)
            {
                have |= std::uint64_t(1) << (index - base - 1);
                return true;
            }
            // The first lacking has come: on to the next that has not.
            ++base;
            while (have & 1)
            {
                have >>= 1;
                ++base;
            }
            have >>= 1;
            return true;
        }
    };

    /// The host's side: what one client has asked for, which pieces it
    /// has, and which is sent next.
    struct FileSender
    {
        char name[kNetNameBytes] = {};
        int to = 0;                ///< which client (0: nobody is asking)
        std::uint16_t id = 0;
        std::uint32_t base = 0;    ///< the first piece the client lacks
        std::uint64_t have = 0;    ///< ... and which it has past that
        std::uint32_t cursor = 0;  ///< the next piece to send
        bool fresh = false;        ///< a new asking: whoever reads the file looks at `name` again

        /// A frame from client `who`. True if it is an asking (now in here).
        bool asked(int who, const std::uint8_t *bytes, int size)
        {
            detail::NetIn in{bytes, bytes + size};
            if (in.u16() != kNetMagic || in.u8() != kNetVersion || in.u8() != kNetFrameFileAsk)
                return false;
            const std::uint16_t which = static_cast<std::uint16_t>(in.u16());
            const std::uint32_t first = in.u32();
            const std::uint64_t low = in.u32();
            const std::uint64_t mask = low | (static_cast<std::uint64_t>(in.u32()) << 32);
            char wanted[kNetNameBytes];
            in.bytes(wanted, kNetNameBytes);
            wanted[kNetNameBytes - 1] = 0;
            if (!in.ok || who < 1 || who >= kNetPlayers)
                return false;
            fresh = to != who || which != id || std::strcmp(wanted, name) != 0;
            if (fresh)
            {
                std::memcpy(name, wanted, kNetNameBytes);
                to = who;
                id = which;
                cursor = first;
            }
            else if (first < base)
                return true; // (an asking from before the last: older news)
            base = first;
            have = mask;
            if (cursor < base)
                cursor = base;
            return true;
        }

        /// Which piece to send now, of a file `length` long: the next the
        /// client lacks, going round within what it keeps account of.
        /// False if it has them all (or nobody is asking).
        bool next(std::uint32_t length, std::uint32_t &index)
        {
            const std::uint32_t pieces = (length + kNetFilePiece - 1) / kNetFilePiece;
            if (to == 0 || base >= pieces)
                return false;
            for (int tries = 0; tries <= kNetFileWindow + 1; ++tries)
            {
                if (cursor >= pieces || cursor > base + kNetFileWindow)
                    cursor = base;
                const bool has = cursor > base && ((have >> (cursor - base - 1)) & 1);
                index = cursor++;
                if (!has)
                    return true;
            }
            return false;
        }

        /// The frame for piece `index`: `length` is the file's (0xFFFFFFFF:
        /// there is no such file), `data` the piece's bytes, `count` of
        /// them. Returns the frame's length.
        int piece(std::uint32_t length, std::uint32_t index, const std::uint8_t *data, int count,
                  std::uint8_t *bytes, int size = kNetCmdBytes) const
        {
            detail::NetOut out{bytes, bytes + size};
            out.u16(kNetMagic);
            out.u8(kNetVersion);
            out.u8(kNetFrameFileData);
            out.u8(static_cast<std::uint8_t>(to));
            out.u16(id);
            out.u32(index);
            out.u32(length);
            out.u16(static_cast<std::uint16_t>(count));
            if (count > 0)
                out.bytes(data, count);
            return out.ok ? static_cast<int>(out.at - bytes) : 0;
        }
    };

    // ------------------------------------------------------------- smoothing

    /// Another player as shown: gliding to where they were last heard to
    /// be, in Q12 units.
    struct NetGlide
    {
        std::int32_t pos[3] = {0, 0, 0};
        std::uint8_t spawns = 0;
        bool placed = false;
    };

    /// A frame's glide of `glide` towards `heard`, `ms` long: half of what
    /// is left every thirtieth of a second, and a little of where they were
    /// going; straight there if it is far (a spawn, a teleport, a lost
    /// second).
    inline void netGlide(NetGlide &glide, const NetPlayer &heard, int ms)
    {
        std::int32_t to[3];
        bool far = !glide.placed || glide.spawns != heard.spawns;
        for (int i = 0; i < 3; ++i)
        {
            // (Quarter units to Q12; and two frames on at the speed heard.)
            to[i] = heard.pos[i] * 1024 + heard.velocity[i] * 8 * 4096 / 30;
            const std::int32_t off = to[i] - glide.pos[i];
            far = far || off > 96 * 4096 || off < -96 * 4096;
        }
        for (int i = 0; i < 3; ++i)
        {
            if (far)
                glide.pos[i] = heard.pos[i] * 1024;
            else
            {
                // 1 - 2^(-ms/33), near enough: ms/48 up to the whole of it.
                const std::int32_t part = ms >= 48 ? 48 : (ms < 1 ? 1 : ms);
                glide.pos[i] += (to[i] - glide.pos[i]) * part / 48;
            }
        }
        glide.spawns = heard.spawns;
        glide.placed = true;
    }
} // namespace hldata
