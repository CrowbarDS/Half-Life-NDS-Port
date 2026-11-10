#pragma once

// A game between consoles over the Internet: what a console and the relay
// (tools/relay) say to each other, in UDP datagrams.
//
// THE RELAY stands in for the radio. A host opens a room on it; consoles
// that look are given the list of rooms, and ask to join one -- the host is
// asked whether to let each in (Knock, Answer); from then on the
// relay passes the host's frames to every client of its room, and each
// client's to the host, saying whose it is -- and what one says aloud (Voice,
// if the host lets them) to all the others. It never reads a game frame:
// what is in them is HlNet.hpp's, the same as through the air, so the lobby
// and the match are the very ones played over the local wireless.
//
// Every datagram begins with a head of eight bytes: the magic, the relay
// protocol's version, the kind, and the sender's session.
//
// COMING IN. The relay keeps nothing for an address until the console there
// has shown it gets what is sent to it (so that nobody can make the relay
// keep, or send, anything in another's name): it says Hello, with a number
// of its own (its nonce) in the session's place; the relay answers with a
// cookie, made from that address, that nonce and the time, under a secret of
// its own -- and keeps nothing; the console sends the cookie back (Enter),
// and only then is it known: Welcome gives it its session, picked by the
// relay at random, which everything it says from then on carries. Before
// that, nothing the relay sends is bigger than what it was sent (Hello and
// Enter are padded so), and nothing else is answered at all.
//
// std-only, no heap: the console and the relay both compile it, and the
// relay's tests run it.

#include <cstdint>
#include <cstring>

#include "HlNet.hpp"
#include "HlVoice.hpp"

namespace hldata
{
    inline constexpr std::uint16_t kRelayMagic = 0x5248; // "HR"
    /// (2: the host lets each in, or not. 3: Voice. 4: Hello, Enter -- the
    /// console proves its address before the relay keeps anything.)
    inline constexpr std::uint8_t kRelayVersion = 4;
    /// The port a relay listens on unless server.ini says another.
    inline constexpr int kRelayPort = 27500;
    inline constexpr int kRelayHeadBytes = 8;
    /// The most a datagram is: a host frame with its head and whose it is.
    inline constexpr int kRelayMostBytes = kRelayHeadBytes + 1 + kNetCmdBytes;
    /// Rooms in one list.
    inline constexpr int kRelayListMost = 8;
    /// A console that says nothing for this long is taken for gone by the
    /// relay (milliseconds): longer than a map takes to load, which says
    /// nothing all the while.
    inline constexpr std::uint32_t kRelaySilentMs = 45000;
    /// How long a console asking to join waits for the host's answer before
    /// it is told the host did not answer (milliseconds).
    inline constexpr std::uint32_t kRelayKnockMs = 30000;
    /// Consoles waiting at a room's door at once.
    inline constexpr int kRelayKnocksMost = 8;

    enum class RelayKind : std::uint8_t
    {
        Ping = 1,    ///< console: I am here (answered by Pong)
        Pong = 2,    ///< relay
        Open = 3,    ///< console: be a host -- or what my room says has changed (RelayOpen)
        Opened = 4,  ///< relay: your room's number (u32)
        List = 5,    ///< console: which rooms are there?
        Rooms = 6,   ///< relay: RelayRoom entries, a count first
        Join = 7,    ///< console: let me into room (u32), I am (name) -- the host is asked
        Joined = 8,  ///< relay: you are client (u8, 1..7) of it
        Refused = 9, ///< relay: you are not (u8, RelayRefuse)
        Admit = 10,  ///< host: let more in, or not (u8)
        Kick = 11,   ///< host: send client (u8) away
        Status = 12, ///< relay to a host: which clients are there (u8, bit n for client n)
        Data = 13,   ///< a game frame: whose (u8: 0 the host, else the client), then the frame
        Bye = 14,    ///< console: I am going; relay: you are out, and no longer known (u8, RelayBye)
        Knock = 15,  ///< relay to a host: (ticket u8, name) asks to come in; said again until answered
        Answer = 16, ///< host: (ticket u8, yes u8)
        Voice = 17,  ///< what one says aloud: (player u8, number u8, rate u16, a packet: HlVoice.hpp), to all the others
        Hello = 18,     ///< console, its nonce for session: let me in (padded: kRelayHelloBytes)
        Challenge = 19, ///< relay: the cookie (kRelayCookieBytes) to send back
        Enter = 20,     ///< console, its nonce for session: the cookie, and a token (0: none) (kRelayEnterBytes)
        Welcome = 21,   ///< relay, to the nonce: your session (u32), and your token (u64)
    };
    inline constexpr RelayKind kRelayLastKind = RelayKind::Welcome;

    /// `Admit`'s byte: bit 0, more are let in; bit 1, they may talk (Voice is
    /// passed on). A host says it again with its room, now and then.
    inline constexpr int kRelayAdmitIn = 1;
    inline constexpr int kRelayAdmitVoice = 2;
    /// The most Voice datagrams one console may send in a second: the relay
    /// lets the rest fall. (A packet each 27 ms at the most, at the DSi's
    /// faster rate: 37 a second.)
    inline constexpr int kRelayVoicePerSecond = 45;

    enum class RelayRefuse : std::uint8_t
    {
        NoRoom = 1, ///< there is no such room (any more)
        Full = 2,
        Closed = 3, ///< its host lets nobody in (a match is on)
        Declined = 4, ///< its host said no
        NoAnswer = 5, ///< its host said nothing in time
        Busy = 6,     ///< as many from its address wait at doors as may
    };

    /// Why the relay let a console go (its `Bye`'s byte).
    enum class RelayBye : std::uint8_t
    {
        HostLeft = 1,      ///< its room's host went (said goodbye, or left the room)
        HostSilent = 2,    ///< its room's host said nothing for too long
        Kicked = 3,        ///< its host sent it away
        Silent = 4,        ///< it said nothing for too long
        RoomExpired = 5,   ///< its room stayed empty too long
        RelayStopping = 6, ///< the relay is stopping (started again, or put away)
    };

    struct RelayHead
    {
        RelayKind kind = RelayKind::Ping;
        std::uint32_t session = 0;
    };

    inline void relayPut32(std::uint8_t *out, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out[i] = static_cast<std::uint8_t>(v >> (i * 8));
    }
    inline std::uint32_t relayGet32(const std::uint8_t *in)
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(in[i]) << (i * 8);
        return v;
    }

    /// The head, into `out` (which has room for kRelayHeadBytes); returns
    /// where what follows goes.
    inline int relayHeadWrite(std::uint8_t *out, RelayKind kind, std::uint32_t session)
    {
        out[0] = static_cast<std::uint8_t>(kRelayMagic & 0xFF);
        out[1] = static_cast<std::uint8_t>(kRelayMagic >> 8);
        out[2] = kRelayVersion;
        out[3] = static_cast<std::uint8_t>(kind);
        relayPut32(out + 4, session);
        return kRelayHeadBytes;
    }
    /// False for what is not a datagram of this relay protocol's version.
    inline bool relayHeadRead(const std::uint8_t *bytes, int size, RelayHead &head)
    {
        if (size < kRelayHeadBytes || bytes[0] != (kRelayMagic & 0xFF) || bytes[1] != (kRelayMagic >> 8) ||
            bytes[2] != kRelayVersion || bytes[3] < static_cast<std::uint8_t>(RelayKind::Ping) ||
            bytes[3] > static_cast<std::uint8_t>(kRelayLastKind))
            return false;
        head.kind = static_cast<RelayKind>(bytes[3]);
        head.session = relayGet32(bytes + 4);
        return true;
    }

    // ------------------------------------------------- another version
    //
    // A datagram of another version than the relay's is answered (now and
    // then: not more than once a second to an address) with its head alone:
    // the magic, the RELAY's version, kind 0, and the session it was sent
    // with -- so that a console says which of the two is to be updated rather
    // than that the relay is not there. THIS NEVER CHANGES, in any version
    // to come: an old console and a new relay (or a new console and an old
    // relay) must still understand it.
    inline constexpr std::uint8_t kRelayVersionKind = 0;

    inline int relayVersionWrite(std::uint8_t *out, std::uint32_t session)
    {
        out[0] = static_cast<std::uint8_t>(kRelayMagic & 0xFF);
        out[1] = static_cast<std::uint8_t>(kRelayMagic >> 8);
        out[2] = kRelayVersion;
        out[3] = kRelayVersionKind;
        relayPut32(out + 4, session);
        return kRelayHeadBytes;
    }
    /// The version a relay answered with, and the session it echoed; false
    /// if it is not such an answer.
    inline bool relayVersionRead(const std::uint8_t *bytes, int size, int &version, std::uint32_t &session)
    {
        if (size < kRelayHeadBytes || bytes[0] != (kRelayMagic & 0xFF) || bytes[1] != (kRelayMagic >> 8) ||
            bytes[3] != kRelayVersionKind)
            return false;
        version = bytes[2];
        session = relayGet32(bytes + 4);
        return true;
    }
    /// Whether `bytes` is of this protocol (its magic) but of another version
    /// -- and not itself such an answer (two relays never answer each other
    /// for ever).
    inline bool relayOtherVersion(const std::uint8_t *bytes, int size)
    {
        return size >= kRelayHeadBytes && bytes[0] == (kRelayMagic & 0xFF) && bytes[1] == (kRelayMagic >> 8) &&
               bytes[2] != kRelayVersion && bytes[3] != kRelayVersionKind;
    }

    /// A datagram with nothing after its head but one byte (`Joined`,
    /// `Refused`, `Admit`, `Kick`, `Status`), or none (`value` < 0).
    inline int relaySmallWrite(std::uint8_t *out, RelayKind kind, std::uint32_t session, int value = -1)
    {
        int at = relayHeadWrite(out, kind, session);
        if (value >= 0)
            out[at++] = static_cast<std::uint8_t>(value);
        return at;
    }
    /// The byte after the head; -1 if there is none.
    inline int relaySmallRead(const std::uint8_t *bytes, int size)
    {
        return size > kRelayHeadBytes ? bytes[kRelayHeadBytes] : -1;
    }

    /// `Opened` and `Join`: a room's number.
    inline int relayRoomIdWrite(std::uint8_t *out, RelayKind kind, std::uint32_t session, std::uint32_t room)
    {
        const int at = relayHeadWrite(out, kind, session);
        relayPut32(out + at, room);
        return at + 4;
    }
    inline bool relayRoomIdRead(const std::uint8_t *bytes, int size, std::uint32_t &room)
    {
        if (size < kRelayHeadBytes + 4)
            return false;
        room = relayGet32(bytes + kRelayHeadBytes);
        return true;
    }

    /// `Join`: which room, and who asks (the name the host is shown).
    inline int relayJoinWrite(std::uint8_t *out, std::uint32_t session, std::uint32_t room, const char *name)
    {
        int at = relayRoomIdWrite(out, RelayKind::Join, session, room);
        std::memset(out + at, 0, kNetTextBytes);
        for (int i = 0; i < kNetTextBytes - 1 && name != nullptr && name[i] != 0; ++i)
            out[at + i] = static_cast<std::uint8_t>(name[i]);
        return at + kNetTextBytes;
    }
    /// The name of a `Join` (into `name`, kNetTextBytes); "" from one without.
    inline void relayJoinName(const std::uint8_t *bytes, int size, char *name)
    {
        std::memset(name, 0, kNetTextBytes);
        if (size >= kRelayHeadBytes + 4 + kNetTextBytes)
            std::memcpy(name, bytes + kRelayHeadBytes + 4, kNetTextBytes - 1);
    }

    /// `Knock`: a ticket for the host to answer, and who it is.
    inline int relayKnockWrite(std::uint8_t *out, std::uint32_t session, int ticket, const char *name)
    {
        int at = relayHeadWrite(out, RelayKind::Knock, session);
        out[at++] = static_cast<std::uint8_t>(ticket);
        std::memset(out + at, 0, kNetTextBytes);
        for (int i = 0; i < kNetTextBytes - 1 && name[i] != 0; ++i)
            out[at + i] = static_cast<std::uint8_t>(name[i]);
        return at + kNetTextBytes;
    }
    inline bool relayKnockRead(const std::uint8_t *bytes, int size, int &ticket, char *name)
    {
        if (size < kRelayHeadBytes + 1 + kNetTextBytes)
            return false;
        ticket = bytes[kRelayHeadBytes];
        std::memcpy(name, bytes + kRelayHeadBytes + 1, kNetTextBytes);
        name[kNetTextBytes - 1] = 0;
        return true;
    }

    /// `Answer`: the host's to a ticket.
    inline int relayAnswerWrite(std::uint8_t *out, std::uint32_t session, int ticket, bool yes)
    {
        int at = relayHeadWrite(out, RelayKind::Answer, session);
        out[at++] = static_cast<std::uint8_t>(ticket);
        out[at++] = yes ? 1 : 0;
        return at;
    }
    inline bool relayAnswerRead(const std::uint8_t *bytes, int size, int &ticket, bool &yes)
    {
        if (size < kRelayHeadBytes + 2)
            return false;
        ticket = bytes[kRelayHeadBytes];
        yes = bytes[kRelayHeadBytes + 1] != 0;
        return true;
    }

    /// What a host says of its room: what a console looking is shown, as the
    /// radio's beacon says it (its name, how many may be in it, NetRoomInfo's
    /// bytes).
    struct RelayOpen
    {
        char name[kNetTextBytes] = {};
        std::uint8_t most = kNetPlayers;
        std::uint8_t room[kNetRoomBytes] = {};
    };
    inline constexpr int kRelayOpenBytes = kNetTextBytes + 1 + kNetRoomBytes;
    inline int relayOpenWrite(std::uint8_t *out, std::uint32_t session, const RelayOpen &open)
    {
        int at = relayHeadWrite(out, RelayKind::Open, session);
        std::memcpy(out + at, open.name, kNetTextBytes);
        out[at + kNetTextBytes - 1] = 0;
        at += kNetTextBytes;
        out[at++] = open.most;
        std::memcpy(out + at, open.room, kNetRoomBytes);
        return at + kNetRoomBytes;
    }
    inline bool relayOpenRead(const std::uint8_t *bytes, int size, RelayOpen &open)
    {
        if (size < kRelayHeadBytes + kRelayOpenBytes)
            return false;
        const std::uint8_t *in = bytes + kRelayHeadBytes;
        std::memcpy(open.name, in, kNetTextBytes);
        open.name[kNetTextBytes - 1] = 0;
        open.most = in[kNetTextBytes];
        if (open.most < 2 || open.most > kNetPlayers)
            open.most = kNetPlayers;
        std::memcpy(open.room, in + kNetTextBytes + 1, kNetRoomBytes);
        return true;
    }

    /// A room as a list gives it.
    struct RelayRoom
    {
        std::uint32_t id = 0;
        char name[kNetTextBytes] = {};
        std::uint8_t players = 0; ///< the host among them
        std::uint8_t most = 0;
        bool open = false;        ///< it lets people in
        std::uint8_t room[kNetRoomBytes] = {};
    };
    inline constexpr int kRelayRoomBytes = 4 + kNetTextBytes + 3 + kNetRoomBytes;
    inline constexpr int kRelayRoomsMostBytes = kRelayHeadBytes + 1 + kRelayListMost * kRelayRoomBytes;
    static_assert(kRelayRoomsMostBytes <= kRelayMostBytes, "a list in one datagram");

    inline int relayRoomsWrite(std::uint8_t *out, std::uint32_t session, const RelayRoom *rooms, int count)
    {
        count = count < 0 ? 0 : (count > kRelayListMost ? kRelayListMost : count);
        int at = relayHeadWrite(out, RelayKind::Rooms, session);
        out[at++] = static_cast<std::uint8_t>(count);
        for (int i = 0; i < count; ++i)
        {
            const RelayRoom &room = rooms[i];
            relayPut32(out + at, room.id);
            at += 4;
            std::memcpy(out + at, room.name, kNetTextBytes);
            out[at + kNetTextBytes - 1] = 0;
            at += kNetTextBytes;
            out[at++] = room.players;
            out[at++] = room.most;
            out[at++] = room.open ? 1 : 0;
            std::memcpy(out + at, room.room, kNetRoomBytes);
            at += kNetRoomBytes;
        }
        return at;
    }
    /// How many rooms were read into `rooms` (up to `most`); -1 if it is not
    /// a list.
    inline int relayRoomsRead(const std::uint8_t *bytes, int size, RelayRoom *rooms, int most)
    {
        if (size < kRelayHeadBytes + 1)
            return -1;
        int count = bytes[kRelayHeadBytes];
        if (size < kRelayHeadBytes + 1 + count * kRelayRoomBytes)
            return -1;
        count = count > most ? most : count;
        const std::uint8_t *in = bytes + kRelayHeadBytes + 1;
        for (int i = 0; i < count; ++i, in += kRelayRoomBytes)
        {
            RelayRoom &room = rooms[i];
            room.id = relayGet32(in);
            std::memcpy(room.name, in + 4, kNetTextBytes);
            room.name[kNetTextBytes - 1] = 0;
            room.players = in[4 + kNetTextBytes];
            room.most = in[5 + kNetTextBytes];
            room.open = in[6 + kNetTextBytes] != 0;
            std::memcpy(room.room, in + 7 + kNetTextBytes, kNetRoomBytes);
        }
        return count;
    }

    // ------------------------------------------------------- coming in

    /// A cookie: the time it was made in (the relay's), and what proves it.
    inline constexpr int kRelayCookieBytes = 12;
    /// A token: what a console shows to be the one it was (a session taken
    /// up again from another address).
    inline constexpr int kRelayTokenBytes = 8;
    inline constexpr int kRelayHelloBytes = kRelayHeadBytes + 16;
    inline constexpr int kRelayChallengeBytes = kRelayHeadBytes + kRelayCookieBytes;
    inline constexpr int kRelayEnterBytes = kRelayHeadBytes + kRelayCookieBytes + kRelayTokenBytes;
    inline constexpr int kRelayWelcomeBytes = kRelayHeadBytes + 4 + kRelayTokenBytes;
    static_assert(kRelayChallengeBytes <= kRelayHelloBytes && kRelayWelcomeBytes <= kRelayEnterBytes,
                  "an answer to one not yet known no bigger than what it said");

    inline int relayHelloWrite(std::uint8_t *out, std::uint32_t nonce)
    {
        const int at = relayHeadWrite(out, RelayKind::Hello, nonce);
        std::memset(out + at, 0, kRelayHelloBytes - at);
        return kRelayHelloBytes;
    }

    inline int relayChallengeWrite(std::uint8_t *out, std::uint32_t nonce, const std::uint8_t *cookie)
    {
        const int at = relayHeadWrite(out, RelayKind::Challenge, nonce);
        std::memcpy(out + at, cookie, kRelayCookieBytes);
        return kRelayChallengeBytes;
    }
    /// The cookie of a `Challenge` (kRelayCookieBytes into `cookie`).
    inline bool relayChallengeRead(const std::uint8_t *bytes, int size, std::uint8_t *cookie)
    {
        if (size < kRelayChallengeBytes)
            return false;
        std::memcpy(cookie, bytes + kRelayHeadBytes, kRelayCookieBytes);
        return true;
    }

    inline int relayEnterWrite(std::uint8_t *out, std::uint32_t nonce, const std::uint8_t *cookie,
                               const std::uint8_t *token)
    {
        const int at = relayHeadWrite(out, RelayKind::Enter, nonce);
        std::memcpy(out + at, cookie, kRelayCookieBytes);
        if (token != nullptr)
            std::memcpy(out + at + kRelayCookieBytes, token, kRelayTokenBytes);
        else
            std::memset(out + at + kRelayCookieBytes, 0, kRelayTokenBytes);
        return kRelayEnterBytes;
    }
    /// Where the cookie and the token of an `Enter` are in `bytes`; false if
    /// it is short.
    inline bool relayEnterRead(const std::uint8_t *bytes, int size, const std::uint8_t *&cookie,
                               const std::uint8_t *&token)
    {
        if (size < kRelayEnterBytes)
            return false;
        cookie = bytes + kRelayHeadBytes;
        token = cookie + kRelayCookieBytes;
        return true;
    }

    inline int relayWelcomeWrite(std::uint8_t *out, std::uint32_t nonce, std::uint32_t session,
                                 const std::uint8_t *token)
    {
        const int at = relayHeadWrite(out, RelayKind::Welcome, nonce);
        relayPut32(out + at, session);
        std::memcpy(out + at + 4, token, kRelayTokenBytes);
        return kRelayWelcomeBytes;
    }
    /// The session a `Welcome` gives, and its token (kRelayTokenBytes); false
    /// if it is short, or gives none.
    inline bool relayWelcomeRead(const std::uint8_t *bytes, int size, std::uint32_t &session, std::uint8_t *token)
    {
        if (size < kRelayWelcomeBytes)
            return false;
        session = relayGet32(bytes + kRelayHeadBytes);
        std::memcpy(token, bytes + kRelayHeadBytes + 4, kRelayTokenBytes);
        return session != 0;
    }

    /// A game frame, and whose it is (0: the host's; else the client's).
    inline int relayDataWrite(std::uint8_t *out, std::uint32_t session, int who, const void *frame, int size)
    {
        size = size < 0 ? 0 : (size > kNetCmdBytes ? kNetCmdBytes : size);
        int at = relayHeadWrite(out, RelayKind::Data, session);
        out[at++] = static_cast<std::uint8_t>(who);
        std::memcpy(out + at, frame, static_cast<std::size_t>(size));
        return at + size;
    }
    /// Where the frame is in `bytes`, and its length; -1 if there is none.
    inline int relayDataRead(const std::uint8_t *bytes, int size, int &who, const std::uint8_t *&frame)
    {
        if (size < kRelayHeadBytes + 1)
            return -1;
        who = bytes[kRelayHeadBytes];
        frame = bytes + kRelayHeadBytes + 1;
        return size - kRelayHeadBytes - 1;
    }

    /// `Voice`: who says it (the player), its number (one more each packet,
    /// so that the hearer puts them in order), the rate it was heard at, and
    /// the packet.
    inline constexpr int kRelayVoiceBytes = kRelayHeadBytes + 4 + kVoiceAdpcmBytes;
    static_assert(kRelayVoiceBytes <= kRelayMostBytes, "a packet in one datagram");
    struct RelayVoice
    {
        std::uint8_t player = 0;
        std::uint8_t number = 0;
        std::uint16_t rate = 0;
        const std::uint8_t *packet = nullptr; ///< (in the datagram read)
    };
    inline int relayVoiceWrite(std::uint8_t *out, std::uint32_t session, int player, int number, int rate,
                               const std::uint8_t *packet)
    {
        int at = relayHeadWrite(out, RelayKind::Voice, session);
        out[at++] = static_cast<std::uint8_t>(player);
        out[at++] = static_cast<std::uint8_t>(number);
        out[at++] = static_cast<std::uint8_t>(rate & 0xFF);
        out[at++] = static_cast<std::uint8_t>(rate >> 8);
        std::memcpy(out + at, packet, kVoiceAdpcmBytes);
        return at + kVoiceAdpcmBytes;
    }
    /// False for what is not a whole one (or a rate no voice is heard at).
    inline bool relayVoiceRead(const std::uint8_t *bytes, int size, RelayVoice &voice)
    {
        if (size != kRelayVoiceBytes)
            return false;
        const std::uint8_t *in = bytes + kRelayHeadBytes;
        voice.player = in[0];
        voice.number = in[1];
        voice.rate = static_cast<std::uint16_t>(in[2] | in[3] << 8);
        voice.packet = in + 4;
        return voice.player < kNetPlayers && voice.rate >= 4000 && voice.rate <= 16000;
    }
} // namespace hldata
