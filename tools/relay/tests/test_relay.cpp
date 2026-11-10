// The relay (core/Relay.hpp) and what consoles say to it
// (shared/HlRelay.hpp): rooms opened, listed, joined, left and let go; and a
// lobby of the game's own (shared/HlNetGame.hpp) played through it over
// links that lose and delay datagrams, as it is through the air.

#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <vector>

#include "HlIni.hpp"
#include "HlNetGame.hpp"
#include "HlRelay.hpp"
#include "Relay.hpp"
#include "Serve.hpp"
#include "SipHash.hpp"

using namespace hldata;
using hlrelay::Address;
using hlrelay::Out;
using hlrelay::Relay;

namespace
{
    int g_failures = 0;
    void check(bool ok, const char *what)
    {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok)
            ++g_failures;
    }

    std::uint32_t g_seed = 4242;
    std::uint32_t rnd()
    {
        g_seed = g_seed * 1664525u + 1013904223u;
        return g_seed >> 8;
    }

    Address at(int n) { return {0x0A000002u + static_cast<std::uint32_t>(n), static_cast<std::uint16_t>(5000 + n)}; }

    /// What the relay sent `to` among `out`, of `kind`.
    std::vector<std::vector<std::uint8_t>> sentTo(const std::vector<Out> &out, const Address &to, RelayKind kind);

    /// Whether all the relay said among `out` is Unknown (its head alone), and
    /// there is `count` of it.
    bool onlyUnknown(const std::vector<Out> &out, std::size_t count)
    {
        for (const Out &one : out)
            if (one.bytes.size() != static_cast<std::size_t>(kRelayHeadBytes) ||
                one.bytes[3] != static_cast<std::uint8_t>(RelayKind::Unknown))
                return false;
        return out.size() == count;
    }

    /// Each test console's session, by relay.
    std::map<std::pair<const Relay *, int>, std::uint32_t> g_sessions;
    std::uint32_t g_nonce = 0x5000;

    /// Console `n` comes in (Hello, Enter), unless it has already -- or
    /// `again`, as a console started again: its session. (What the relay
    /// said goes in `said`, if given.)
    std::uint32_t comeIn(Relay &relay, int n, std::uint32_t now, bool again = false, std::vector<Out> *said = nullptr)
    {
        const auto key = std::make_pair(static_cast<const Relay *>(&relay), n);
        auto known = g_sessions.find(key);
        if (!again && known != g_sessions.end())
            return known->second;
        std::vector<Out> out;
        std::uint8_t bytes[64], cookie[kRelayCookieBytes] = {}, token[kRelayTokenBytes];
        const std::uint32_t nonce = ++g_nonce;
        relay.receive(at(n), bytes, relayHelloWrite(bytes, nonce), now, out);
        const auto challenge = sentTo(out, at(n), RelayKind::Challenge);
        if (challenge.size() == 1)
            relayChallengeRead(challenge[0].data(), static_cast<int>(challenge[0].size()), cookie);
        out.clear();
        relay.receive(at(n), bytes, relayEnterWrite(bytes, nonce, cookie, nullptr), now, out);
        std::uint32_t session = 0;
        const auto welcome = sentTo(out, at(n), RelayKind::Welcome);
        if (welcome.size() == 1)
            relayWelcomeRead(welcome[0].data(), static_cast<int>(welcome[0].size()), session, token);
        if (said != nullptr)
            said->insert(said->end(), out.begin(), out.end());
        g_sessions[key] = session;
        return session;
    }

    /// What the relay sent `to` among `out`, of `kind`.
    std::vector<std::vector<std::uint8_t>> sentTo(const std::vector<Out> &out, const Address &to, RelayKind kind)
    {
        std::vector<std::vector<std::uint8_t>> found;
        for (const Out &one : out)
        {
            RelayHead head;
            if (one.to == to && relayHeadRead(one.bytes.data(), static_cast<int>(one.bytes.size()), head) &&
                head.kind == kind)
                found.push_back(one.bytes);
        }
        return found;
    }

    std::uint32_t open(Relay &relay, int n, const char *name, int most, std::uint32_t now, std::vector<Out> &out)
    {
        RelayOpen o;
        std::snprintf(o.name, sizeof(o.name), "%s", name);
        o.most = static_cast<std::uint8_t>(most);
        o.room[0] = kNetVersion;
        std::uint8_t bytes[kRelayMostBytes];
        out.clear();
        relay.receive(at(n), bytes, relayOpenWrite(bytes, comeIn(relay, n, now), o), now, out);
        const auto opened = sentTo(out, at(n), RelayKind::Opened);
        std::uint32_t id = 0;
        return opened.size() == 1 && relayRoomIdRead(opened[0].data(), static_cast<int>(opened[0].size()), id) ? id : 0;
    }

    /// `n` asks to join room `id`, whose host is console `host`; the host
    /// answers `answer` (yes, no; or not at all: -1). `n`'s slot, or -(the
    /// reason) if refused; -100 if it is left waiting, -99 if nothing came.
    int join(Relay &relay, int n, std::uint32_t id, std::uint32_t now, std::vector<Out> &out, int host = 0,
             int answer = 1)
    {
        std::uint8_t bytes[kRelayMostBytes];
        char name[kNetTextBytes];
        std::snprintf(name, sizeof(name), "P%d", n);
        out.clear();
        relay.receive(at(n), bytes, relayJoinWrite(bytes, comeIn(relay, n, now), id, name), now, out);
        auto result = [&]() -> int
        {
            const auto joined = sentTo(out, at(n), RelayKind::Joined);
            if (joined.size() == 1)
                return relaySmallRead(joined[0].data(), static_cast<int>(joined[0].size()));
            const auto refused = sentTo(out, at(n), RelayKind::Refused);
            return refused.size() == 1 ? -relaySmallRead(refused[0].data(), static_cast<int>(refused[0].size()))
                                       : -99;
        };
        const int first = result();
        if (first != -99)
            return first;
        const auto knocks = sentTo(out, at(host), RelayKind::Knock);
        int ticket = 0;
        char asked[kNetTextBytes];
        if (knocks.size() != 1 || !relayKnockRead(knocks[0].data(), static_cast<int>(knocks[0].size()), ticket, asked) ||
            std::strcmp(asked, name) != 0)
            return -99;
        if (answer < 0)
            return -100;
        out.clear();
        relay.receive(at(host), bytes, relayAnswerWrite(bytes, comeIn(relay, host, now), ticket, answer != 0), now, out);
        return result();
    }

    int list(Relay &relay, int n, RelayRoom *rooms, std::uint32_t now, std::vector<Out> &out)
    {
        std::uint8_t bytes[kRelayMostBytes];
        out.clear();
        relay.receive(at(n), bytes, relaySmallWrite(bytes, RelayKind::List, comeIn(relay, n, now)), now, out);
        const auto got = sentTo(out, at(n), RelayKind::Rooms);
        return got.size() == 1 ? relayRoomsRead(got[0].data(), static_cast<int>(got[0].size()), rooms, kRelayListMost)
                               : -1;
    }

    unsigned lastStatus(const std::vector<Out> &out, int host)
    {
        const auto got = sentTo(out, at(host), RelayKind::Status);
        return got.empty() ? 0xFFFFu
                           : static_cast<unsigned>(relaySmallRead(got.back().data(), static_cast<int>(got.back().size())));
    }

    void testDatagrams()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        std::uint8_t bytes[kRelayMostBytes];
        RelayHead head;
        const int size = relaySmallWrite(bytes, RelayKind::Kick, 0xDEADBEEFu, 5);
        check(relayHeadRead(bytes, size, head) && head.kind == RelayKind::Kick && head.session == 0xDEADBEEFu &&
                  relaySmallRead(bytes, size) == 5,
              "a datagram's head and its one byte come back as they were written");
        bytes[2] = kRelayVersion + 1;
        check(!relayHeadRead(bytes, size, head), "... and one of another relay version is not read");
        bytes[2] = kRelayVersion;
        bytes[3] = 99;
        check(!relayHeadRead(bytes, size, head), "... nor one of a kind there is not");

        RelayRoom rooms[kRelayListMost];
        for (int i = 0; i < kRelayListMost; ++i)
        {
            rooms[i].id = 1000u + i;
            std::snprintf(rooms[i].name, sizeof(rooms[i].name), "Room %d", i);
            rooms[i].players = static_cast<std::uint8_t>(1 + i % 7);
            rooms[i].most = 8;
            rooms[i].open = i % 2 == 0;
            rooms[i].room[7] = static_cast<std::uint8_t>(i);
        }
        const int listed = relayRoomsWrite(bytes, 1, rooms, kRelayListMost);
        RelayRoom back[kRelayListMost];
        bool same = relayRoomsRead(bytes, listed, back, kRelayListMost) == kRelayListMost;
        for (int i = 0; i < kRelayListMost && same; ++i)
            same = back[i].id == rooms[i].id && std::strcmp(back[i].name, rooms[i].name) == 0 &&
                   back[i].players == rooms[i].players && back[i].open == rooms[i].open && back[i].room[7] == i;
        check(same, "a full list of rooms fits one datagram and reads back as it was");
        check(relayRoomsRead(bytes, listed - 1, back, kRelayListMost) == -1, "... and one cut short is not read");

        std::uint8_t frame[kNetCmdBytes];
        for (int i = 0; i < kNetCmdBytes; ++i)
            frame[i] = static_cast<std::uint8_t>(i * 7);
        const int data = relayDataWrite(bytes, 3, 0, frame, kNetCmdBytes);
        int who = -1;
        const std::uint8_t *in = nullptr;
        check(data <= kRelayMostBytes && relayDataRead(bytes, data, who, in) == kNetCmdBytes && who == 0 &&
                  std::memcmp(in, frame, kNetCmdBytes) == 0,
              "the biggest host frame goes in one datagram, whole");
    }

    void testRooms()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        Relay relay;
        std::vector<Out> out;
        std::uint32_t now = 1000;
        const std::uint32_t id = open(relay, 0, "Gordon", 3, now, out);
        check(id != 0 && relay.rooms() == 1, "a host opens a room and is told its number");
        check(lastStatus(out, 0) == 0, "... and that nobody is in it yet");
        check(open(relay, 0, "Gordon", 3, now, out) == id && relay.rooms() == 1,
              "saying again what its room is changes it, and opens no other");

        RelayRoom rooms[kRelayListMost];
        check(list(relay, 1, rooms, now, out) == 1 && rooms[0].id == id && std::strcmp(rooms[0].name, "Gordon") == 0 &&
                  rooms[0].players == 1 && rooms[0].most == 3 && rooms[0].room[0] == kNetVersion,
              "a console looking is given the room: its name, who is in it, its card");

        check(join(relay, 1, id, now, out) == 1 && lastStatus(out, 0) == 0x2u, "one joins: client 1, and the host is told");
        check(join(relay, 1, id, now, out) == 1, "... asking again (the answer lost) is given the same");
        check(join(relay, 2, id, now, out) == 2 && lastStatus(out, 0) == 0x6u, "another: client 2");
        check(join(relay, 3, id, now, out) == -static_cast<int>(RelayRefuse::Full), "a fourth of three is refused: full");
        check(list(relay, 3, rooms, now, out) == 0, "... and a full room is not listed");
        check(join(relay, 3, 777, now, out) == -static_cast<int>(RelayRefuse::NoRoom), "no such room: refused");

        // The game's frames: the host's to all, a client's to the host only,
        // saying whose.
        std::uint8_t bytes[kRelayMostBytes];
        const std::uint8_t frame[5] = {1, 2, 3, 4, 5};
        out.clear();
        relay.receive(at(0), bytes, relayDataWrite(bytes, comeIn(relay, 0, now), 0, frame, 5), now, out);
        const auto to1 = sentTo(out, at(1), RelayKind::Data);
        const auto to2 = sentTo(out, at(2), RelayKind::Data);
        int who = -1;
        const std::uint8_t *in = nullptr;
        check(out.size() == 2 && to1.size() == 1 && to2.size() == 1 &&
                  relayDataRead(to2[0].data(), static_cast<int>(to2[0].size()), who, in) == 5 && who == 0 &&
                  std::memcmp(in, frame, 5) == 0,
              "a host frame goes to each client, from the host");
        out.clear();
        relay.receive(at(2), bytes, relayDataWrite(bytes, comeIn(relay, 2, now), 2, frame, 5), now, out);
        const auto toHost = sentTo(out, at(0), RelayKind::Data);
        check(out.size() == 1 && toHost.size() == 1 &&
                  relayDataRead(toHost[0].data(), static_cast<int>(toHost[0].size()), who, in) == 5 && who == 2,
              "a client's goes to the host alone, saying whose");
        out.clear();
        relay.receive(at(2), bytes, relayDataWrite(bytes, comeIn(relay, 2, now), 7, frame, 5), now, out);
        check(sentTo(out, at(0), RelayKind::Data).size() == 1 &&
                  relayDataRead(sentTo(out, at(0), RelayKind::Data)[0].data(),
                                static_cast<int>(sentTo(out, at(0), RelayKind::Data)[0].size()), who, in) == 5 &&
                  who == 2,
              "... whoever it says it is from: the relay says");
        out.clear();
        relay.receive(at(3), bytes, relayDataWrite(bytes, comeIn(relay, 3, now), 0, frame, 5), now, out);
        check(out.empty(), "a console in no room sends nothing on");

        // No more let in while a match is on.
        out.clear();
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Admit, comeIn(relay, 0, now), 0), now, out);
        out.clear();
        relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Bye, comeIn(relay, 1, now)), now, out);
        check(lastStatus(out, 0) == 0x4u && relay.peers() == 3, "a client says goodbye: the host is told, it is forgotten");
        check(join(relay, 3, id, now, out) == -static_cast<int>(RelayRefuse::Closed),
              "a room that lets nobody in refuses, though there is room");
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Admit, comeIn(relay, 0, now), 1), now, out);
        check(join(relay, 3, id, now, out) == 1, "... and once it lets them in again, the first free place is given");

        // The host sends one away.
        out.clear();
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Kick, comeIn(relay, 0, now), 3 - 2), now, out);
        check(sentTo(out, at(3), RelayKind::Bye).size() == 1 && lastStatus(out, 0) == 0x4u,
              "the host sends client 1 away: it is told, and the host told who is left");

        // Another's session at a known address (made up, by one who is not
        // there): not answered, and the console there keeps its place.
        out.clear();
        relay.receive(at(2), bytes, relaySmallWrite(bytes, RelayKind::Ping, 999), now, out);
        relay.receive(at(2), bytes, relaySmallWrite(bytes, RelayKind::Bye, 999), now, out);
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Bye, 998), now, out);
        check(onlyUnknown(out, 1) && out[0].to == at(2) && relay.peers() == 2 && relay.rooms() == 1,
              "a datagram in another session than the console's there: nothing done; told it is not known, once");

        // A console started again at the same address comes in again: it is
        // a new one.
        out.clear();
        comeIn(relay, 2, now, true, &out);
        check(lastStatus(out, 0) == 0u && sentTo(out, at(2), RelayKind::Welcome).size() == 1,
              "the same address coming in again: the one before is out of the room");

        // Silence.
        out.clear();
        now += kRelaySilentMs - 10;
        relay.receive(at(2), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(relay, 2, now)), now, out);
        now += 20;
        relay.tick(now, out);
        check(relay.rooms() == 0 && relay.peers() == 1, "a host that says nothing for too long: gone, and its room");

        // A host that goes takes its room with it, its clients told.
        const std::uint32_t second = open(relay, 4, "Barney", 8, now, out);
        join(relay, 5, second, now, out, 4);
        out.clear();
        relay.receive(at(4), bytes, relaySmallWrite(bytes, RelayKind::Bye, comeIn(relay, 4, now)), now, out);
        check(relay.rooms() == 0 && sentTo(out, at(5), RelayKind::Bye).size() == 1,
              "a host says goodbye: the room is closed, its client told");
    }

    /// What one says aloud: to everyone else in its room, if its host lets
    /// them talk, and no more than a voice's worth a second.
    void testVoice()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        Relay relay;
        std::vector<Out> out;
        std::uint32_t now = 1000;
        const std::uint32_t id = open(relay, 0, "Gordon", 8, now, out);
        check(join(relay, 1, id, now, out) == 1 && join(relay, 2, id, now, out) == 2,
              "voice: a room with a host and two clients");
        const std::uint32_t other = open(relay, 3, "Barney", 8, now, out);
        join(relay, 4, other, now, out, 3);

        std::uint8_t packet[kVoiceAdpcmBytes];
        for (int i = 0; i < kVoiceAdpcmBytes; ++i)
            packet[i] = static_cast<std::uint8_t>(i * 7);
        std::uint8_t bytes[kRelayMostBytes];
        auto say = [&](int n, int number)
        {
            out.clear();
            relay.receive(at(n), bytes, relayVoiceWrite(bytes, comeIn(relay, n, now), n, number, 8182, packet), now, out);
        };

        say(1, 5);
        check(out.empty(), "a host that has not let them talk: nothing passed on");
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Admit, comeIn(relay, 0, now), kRelayAdmitIn | kRelayAdmitVoice),
                      now, out);
        say(1, 6);
        const auto toHost = sentTo(out, at(0), RelayKind::Voice);
        const auto toTwo = sentTo(out, at(2), RelayKind::Voice);
        RelayVoice heard;
        RelayHead head;
        check(out.size() == 2 && toHost.size() == 1 && toTwo.size() == 1 && sentTo(out, at(1), RelayKind::Voice).empty(),
              "it lets them: a client's goes to the host and the other client, not back to it");
        check(relayVoiceRead(toTwo[0].data(), static_cast<int>(toTwo[0].size()), heard) && heard.player == 1 &&
                  heard.number == 6 && heard.rate == 8182 && std::memcmp(heard.packet, packet, kVoiceAdpcmBytes) == 0 &&
                  relayHeadRead(toTwo[0].data(), static_cast<int>(toTwo[0].size()), head) &&
                      head.session == comeIn(relay, 2, now),
              "... as it was said, in the hearer's own session");
        check(sentTo(out, at(3), RelayKind::Voice).empty() && sentTo(out, at(4), RelayKind::Voice).empty(),
              "... and nothing to another room");
        say(0, 1);
        check(out.size() == 2 && sentTo(out, at(1), RelayKind::Voice).size() == 1 &&
                  sentTo(out, at(2), RelayKind::Voice).size() == 1,
              "the host's goes to its clients");
        say(5, 1);
        check(out.empty(), "a console in no room: nothing");
        out.clear();
        relayVoiceWrite(bytes, comeIn(relay, 1, now), 1, 8, 8182, packet);
        bytes[kRelayHeadBytes + 2] = 0; // (a rate of nothing)
        bytes[kRelayHeadBytes + 3] = 0;
        relay.receive(at(1), bytes, kRelayVoiceBytes, now, out);
        check(out.empty(), "one that is not a voice: nothing");

        // No more than a voice's worth a second.
        now += 1000;
        int passed = 0;
        for (int n = 0; n < kRelayVoicePerSecond + 20; ++n)
        {
            say(2, n);
            passed += sentTo(out, at(0), RelayKind::Voice).size() == 1 ? 1 : 0;
        }
        check(passed == kRelayVoicePerSecond, "more than a voice's worth in a second: the rest let fall");
        now += 1000;
        say(2, 0);
        check(sentTo(out, at(0), RelayKind::Voice).size() == 1, "... and the next second, passed on again");

        // Talk taken back.
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Admit, comeIn(relay, 0, now), kRelayAdmitIn), now, out);
        say(1, 7);
        check(out.empty(), "the host no longer lets them: nothing");
    }

    void testKnocks()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        Relay relay;
        std::vector<Out> out;
        std::uint32_t now = 5000;
        const std::uint32_t id = open(relay, 0, "Gordon", 8, now, out);
        check(join(relay, 1, id, now, out, 0, 0) == -static_cast<int>(RelayRefuse::Declined),
              "the host says no: told so, and not in the room");
        check(join(relay, 1, id, now, out, 0, 1) == 1, "... asking again, and the host says yes: client 1");

        // Left waiting: asking again is not a second knock; the host is asked
        // again now and then; then it is too late.
        check(join(relay, 2, id, now, out, 0, -1) == -100, "one at the door, the host not answering yet");
        std::uint8_t bytes[kRelayMostBytes];
        out.clear();
        relay.receive(at(2), bytes, relayJoinWrite(bytes, comeIn(relay, 2, now), id, "P2"), now + 100, out);
        check(out.empty(), "... it asks again: the host is not asked twice for it");
        out.clear();
        relay.tick(now + 1200, out);
        check(sentTo(out, at(0), RelayKind::Knock).size() == 1, "... a second later the host is asked again");
        out.clear();
        relay.receive(at(2), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(relay, 2, now)), now + kRelayKnockMs, out);
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(relay, 0, now)), now + kRelayKnockMs, out);
        out.clear();
        relay.tick(now + kRelayKnockMs + 100, out);
        const auto late = sentTo(out, at(2), RelayKind::Refused);
        check(late.size() == 1 &&
                  relaySmallRead(late[0].data(), static_cast<int>(late[0].size())) ==
                      static_cast<int>(RelayRefuse::NoAnswer),
              "... and after half a minute it is told the host did not answer");

        // One at the door who goes: the host's late yes lets nobody in.
        now += kRelayKnockMs + 200;
        check(join(relay, 3, id, now, out, 0, -1) == -100, "another at the door");
        const auto knocked = sentTo(out, at(0), RelayKind::Knock);
        int ticket = 0;
        char name[kNetTextBytes];
        relayKnockRead(knocked[0].data(), static_cast<int>(knocked[0].size()), ticket, name);
        out.clear();
        relay.receive(at(3), bytes, relaySmallWrite(bytes, RelayKind::Bye, comeIn(relay, 3, now)), now, out);
        out.clear();
        relay.receive(at(0), bytes, relayAnswerWrite(bytes, comeIn(relay, 0, now), ticket, true), now, out);
        check(sentTo(out, at(3), RelayKind::Joined).empty() && lastStatus(out, 0) == 0xFFFFu,
              "... it goes before the host says yes: nobody let in");

        // A host that goes: those at its door are told there is no room.
        check(join(relay, 4, id, now, out, 0, -1) == -100, "one more at the door");
        out.clear();
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Bye, comeIn(relay, 0, now)), now, out);
        const auto gone = sentTo(out, at(4), RelayKind::Refused);
        check(gone.size() == 1 &&
                  relaySmallRead(gone[0].data(), static_cast<int>(gone[0].size())) ==
                      static_cast<int>(RelayRefuse::NoRoom),
              "... the host goes: it is told the room is gone");
    }

    /// A link that loses some datagrams and delays the rest (and lets one
    /// overtake another).
    struct Link
    {
        int lossPercent = 15;
        int mostDelay = 6;
        struct Datagram
        {
            int due;
            std::vector<std::uint8_t> bytes;
        };
        std::deque<Datagram> flying;
        void send(int now, const std::uint8_t *bytes, int size)
        {
            if (size <= 0 || static_cast<int>(rnd() % 100) < lossPercent)
                return;
            flying.push_back({now + 1 + static_cast<int>(rnd() % (mostDelay + 1)),
                              std::vector<std::uint8_t>(bytes, bytes + size)});
        }
        std::vector<std::vector<std::uint8_t>> land(int now)
        {
            std::vector<std::vector<std::uint8_t>> out;
            for (auto it = flying.begin(); it != flying.end();)
            {
                if (it->due <= now)
                {
                    out.push_back(std::move(it->bytes));
                    it = flying.erase(it);
                }
                else
                    ++it;
            }
            return out;
        }
    };

    /// A console online, as source/net/NetRelay.cpp has it: it opens or
    /// joins (asking again until it is answered), then sends the game's
    /// frames through the relay; a host takes its clients from the relay's
    /// Status.
    struct Console
    {
        int n = 0;
        std::uint32_t nonce = 0, session = 0;
        std::uint8_t cookie[kRelayCookieBytes] = {};
        bool haveCookie = false, entered = false;
        Link up, down;
        std::unique_ptr<NetMatch> match;
        std::uint32_t room = 0;
        int slot = -1;
        unsigned clients = 0;
        bool out = false;
        std::uint8_t spray[kNetSprayBytes] = {};
    };

    struct Online
    {
        Relay relay;
        std::vector<std::unique_ptr<Console>> consoles;
        LogicWorld none;
        PlayerState nobody;
        int now = 0;

        Console &add(bool host)
        {
            auto c = std::make_unique<Console>();
            c->n = static_cast<int>(consoles.size());
            c->nonce = 0x1000u + static_cast<std::uint32_t>(c->n);
            c->match = std::make_unique<NetMatch>();
            char name[kNetTextBytes];
            std::snprintf(name, sizeof(name), "P%d", c->n);
            netBegin(*c->match, host ? NetMatch::Role::Host : NetMatch::Role::Client,
                     static_cast<std::uint16_t>(70 + c->n), name);
            for (int i = 0; i < kNetSprayBytes; ++i)
                c->spray[i] = static_cast<std::uint8_t>(rnd());
            NetProfile profile;
            profile.top = static_cast<std::uint8_t>(10 + c->n * 20);
            profile.bottom = static_cast<std::uint8_t>(200 - c->n * 10);
            profile.sprayColour = static_cast<std::uint8_t>(c->n % 9);
            netSetProfile(*c->match, profile, c->spray);
            consoles.push_back(std::move(c));
            return *consoles.back();
        }
        bool isHost(const Console &c) const { return c.match->role == NetMatch::Role::Host; }

        void run(int ticks)
        {
            std::vector<Out> out;
            std::uint8_t bytes[kRelayMostBytes];
            for (int t = 0; t < ticks; ++t)
            {
                ++now;
                // What each console says.
                for (auto &c : consoles)
                {
                    if (c->out)
                        continue;
                    Console &me = *c;
                    // (Coming in first, said again until it is answered.)
                    if (!me.entered)
                    {
                        me.up.send(now, bytes,
                                   me.haveCookie ? relayEnterWrite(bytes, me.nonce, me.cookie, nullptr)
                                                 : relayHelloWrite(bytes, me.nonce));
                        continue;
                    }
                    if (isHost(me) && me.room == 0)
                    {
                        RelayOpen open;
                        std::snprintf(open.name, sizeof(open.name), "P%d", me.n);
                        me.up.send(now, bytes, relayOpenWrite(bytes, me.session, open));
                        continue;
                    }
                    if (!isHost(me) && me.slot < 0)
                    {
                        // (The host's room, once it has one: as from a list.)
                        const std::uint32_t id = consoles[0]->room;
                        if (id != 0)
                        {
                            char name[kNetTextBytes];
                            std::snprintf(name, sizeof(name), "P%d", me.n);
                            me.up.send(now, bytes, relayJoinWrite(bytes, me.session, id, name));
                        }
                        continue;
                    }
                    netTick(*me.match, none, nobody, 0, 0, 0, false, 17);
                    std::uint8_t frame[kNetCmdBytes];
                    const int size = netWrite(*me.match, frame);
                    if (size > 0)
                        me.up.send(now, bytes, relayDataWrite(bytes, me.session, isHost(me) ? 0 : me.slot, frame, size));
                }
                // The relay.
                for (auto &c : consoles)
                    for (const auto &datagram : c->up.land(now))
                        relay.receive(at(c->n), datagram.data(), static_cast<int>(datagram.size()),
                                      static_cast<std::uint32_t>(now * 16), out);
                relay.tick(static_cast<std::uint32_t>(now * 16), out);
                for (const Out &one : out)
                    for (auto &c : consoles)
                        if (at(c->n) == one.to)
                            c->down.send(now, one.bytes.data(), static_cast<int>(one.bytes.size()));
                out.clear();
                // What each hears.
                for (auto &c : consoles)
                {
                    Console &me = *c;
                    for (const auto &datagram : me.down.land(now))
                    {
                        RelayHead head;
                        const int size = static_cast<int>(datagram.size());
                        if (me.out || !relayHeadRead(datagram.data(), size, head))
                            continue;
                        if (!me.entered)
                        {
                            if (head.session != me.nonce)
                                continue;
                            if (head.kind == RelayKind::Challenge)
                                me.haveCookie = relayChallengeRead(datagram.data(), size, me.cookie);
                            std::uint8_t token[kRelayTokenBytes];
                            if (head.kind == RelayKind::Welcome)
                                me.entered = relayWelcomeRead(datagram.data(), size, me.session, token);
                            continue;
                        }
                        if (head.session != me.session)
                            continue;
                        if (head.kind == RelayKind::Opened)
                            relayRoomIdRead(datagram.data(), size, me.room);
                        else if (head.kind == RelayKind::Joined && me.slot < 0)
                            me.slot = relaySmallRead(datagram.data(), size);
                        else if (head.kind == RelayKind::Knock && isHost(me))
                        {
                            // (The host lets everyone in; a knock said again is
                            // answered again.)
                            int ticket = 0;
                            char name[kNetTextBytes];
                            if (relayKnockRead(datagram.data(), size, ticket, name))
                                me.up.send(now, bytes, relayAnswerWrite(bytes, me.session, ticket, true));
                        }
                        else if (head.kind == RelayKind::Status && isHost(me))
                        {
                            // (As NetGame::frame takes the radio's client mask.)
                            const unsigned now = static_cast<unsigned>(relaySmallRead(datagram.data(), size));
                            for (int k = 1; k < kNetPlayers; ++k)
                                if ((now & (1u << k)) && !(me.clients & (1u << k)))
                                    netHostJoin(me.match->host, k);
                            me.clients = now;
                        }
                        else if (head.kind == RelayKind::Data)
                        {
                            int who = 0;
                            const std::uint8_t *frame = nullptr;
                            const int length = relayDataRead(datagram.data(), size, who, frame);
                            if (length > 0)
                                netRead(*me.match, frame, length, who);
                        }
                    }
                }
            }
        }

        /// Whether console `n` knows the player of console `p` as they made
        /// themself (by the place each was given).
        bool knows(int n, int p) const
        {
            const Console &them = *consoles[p];
            const int place = isHost(them) ? 0 : them.slot;
            if (place < 0)
                return false;
            const NetProfile &profile = consoles[n]->match->profiles[place];
            return profile.known && profile.top == 10 + p * 20 && profile.bottom == 200 - p * 10 &&
                   profile.sprayColour == p % 9 && profile.sprayWhole() &&
                   std::memcmp(profile.spray, them.spray, kNetSprayBytes) == 0;
        }
    };

    void testLobbyThroughRelay()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        Online online;
        Console &host = online.add(true);
        NetRules rules;
        rules.mode = kNetModeTeamplay;
        rules.most = kNetPlayers;
        rules.frags = 25;
        netSetRules(*host.match, rules);
        for (int i = 0; i < 4; ++i)
            online.add(false);
        online.run(600);
        bool placed = true;
        for (int n = 1; n < 5; ++n)
            placed = placed && online.consoles[n]->slot >= 1;
        check(host.room != 0 && placed && host.clients == 0x1Eu,
              "a host and four through a relay that loses and delays: a room, four places, the host told");
        bool all = true;
        for (int n = 0; n < 5; ++n)
            for (int p = 0; p < 5; ++p)
                all = all && online.knows(n, p);
        check(all, "... the same lobby as through the air: each has everyone's profile and spray, byte for byte");
        bool sameRules = true;
        for (int n = 1; n < 5; ++n)
            sameRules = sameRules && online.consoles[n]->match->rules.mode == kNetModeTeamplay &&
                        online.consoles[n]->match->rules.frags == 25;
        check(sameRules, "... and the host's rules");

        // One more, late.
        online.add(false);
        online.run(400);
        bool late = true;
        for (int n = 0; n < 6; ++n)
            for (int p = 0; p < 6; ++p)
                late = late && online.knows(n, p);
        check(late, "one comes late: it has everyone's, and everyone its");
    }

    void testIni()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        const char text[] = "# the relay\r\n"
                            "[relay]\r\n"
                            "  Host =  relay.example.org  \r\n"
                            "port=27600\n"
                            "; port = 1\n"
                            "hostname = other\n"
                            "empty =\n";
        char value[32];
        check(iniValue(text, sizeof(text) - 1, "host", value, sizeof(value)) && std::strcmp(value, "relay.example.org") == 0,
              "server.ini: a key whatever its case, spaces and the line's end left out");
        check(iniNumber(text, sizeof(text) - 1, "port", kRelayPort, 1, 65535) == 27600, "... a number");
        check(!iniValue(text, sizeof(text) - 1, "missing", value, sizeof(value)) &&
                  iniNumber(text, sizeof(text) - 1, "missing", kRelayPort, 1, 65535) == kRelayPort,
              "... a key not there: the default");
        check(iniValue(text, sizeof(text) - 1, "empty", value, sizeof(value)) && value[0] == 0, "... an empty value");
        char small[5];
        check(iniValue(text, sizeof(text) - 1, "host", small, sizeof(small)) && std::strcmp(small, "rela") == 0,
              "... cut to the room it is given");
        const char bad[] = "port = 99999\nport2 = 12x\n";
        check(iniNumber(bad, sizeof(bad) - 1, "port", 7, 1, 65535) == 7 &&
                  iniNumber(bad, sizeof(bad) - 1, "port2", 7, 1, 65535) == 7,
              "... a number out of range, or not one: the default");
    }

    /// A socket that holds what has come, and says sending failed when told to.
    struct PretendSocket : hlrelay::Socket
    {
        std::deque<std::pair<Address, std::vector<std::uint8_t>>> waiting;
        std::vector<Out> sent;
        bool failing = false;
        int receive(Address &from, std::uint8_t *bytes, int room) override
        {
            if (waiting.empty())
                return 0;
            const auto &one = waiting.front();
            const int size = static_cast<int>(one.second.size()) < room ? static_cast<int>(one.second.size()) : room;
            from = one.first;
            std::memcpy(bytes, one.second.data(), static_cast<std::size_t>(size));
            waiting.pop_front();
            return size;
        }
        bool send(const Address &to, const std::uint8_t *bytes, int size) override
        {
            if (failing)
                return false;
            sent.push_back({to, std::vector<std::uint8_t>(bytes, bytes + size)});
            return true;
        }
    };

    void testServeRound()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        using hlrelay::Serve;
        Relay relay;
        PretendSocket socket;
        std::uint32_t now = 1000;
        Serve serve(relay, socket, now);
        std::vector<Out> out;
        const std::uint32_t id = open(relay, 0, "Gordon", 8, now, out);
        out.clear();

        // A flood: more than a round reads.
        std::uint8_t ping[kRelayHeadBytes];
        const int pingSize = relaySmallWrite(ping, RelayKind::Ping, comeIn(relay, 1, now));
        for (int i = 0; i < Serve::kMostPerRound * 3 + 10; ++i)
            socket.waiting.push_back({at(1), std::vector<std::uint8_t>(ping, ping + pingSize)});
        now += Relay::kStatusMs; // (the host is due to be told who is there)
        const bool more = serve.round(now);
        check(more && serve.counts.received == static_cast<std::uint64_t>(Serve::kMostPerRound) &&
                  socket.waiting.size() == static_cast<std::size_t>(Serve::kMostPerRound * 2 + 10),
              "a flood: one round reads no more than its share, and says more may be waiting");
        check(!sentTo(socket.sent, at(0), RelayKind::Status).empty(),
              "... and the relay's clock still ran in it (the host told who is there)");
        check(serve.counts.sent == socket.sent.size() && serve.counts.sendFailed == 0, "... what it had to say, sent and counted");

        // Rounds until it has read everything: each one ends.
        int rounds = 1;
        while (serve.round(now) && rounds < 100)
            ++rounds;
        check(socket.waiting.empty() && rounds == 3 && serve.counts.fullRounds == 3,
              "... the rest read in the rounds after, the last not full");

        // The clock: not before its time.
        socket.sent.clear();
        serve.round(now + Serve::kTickMs - 1);
        check(socket.sent.empty(), "nothing come and the clock not due: nothing said");
        serve.round(now + Serve::kTickMs + 1000);
        check(!sentTo(socket.sent, at(0), RelayKind::Status).empty(), "... once due, the clock runs");

        // A send that fails is counted, not retried.
        socket.failing = true;
        socket.waiting.push_back({at(1), std::vector<std::uint8_t>(ping, ping + pingSize)});
        const std::uint64_t failedBefore = serve.counts.sendFailed;
        serve.round(now + 2 * Serve::kTickMs + 1000);
        check(serve.counts.sendFailed > failedBefore && relay.rooms() == 1 && id != 0,
              "a send that fails is counted, and the relay goes on");
    }

    void testSipHash()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        // The paper's own (appendix A), and the reference code's first vector:
        // key 00..0f, and a message of 00..0e, or none.
        std::uint8_t key[16], message[15];
        for (int i = 0; i < 16; ++i)
            key[i] = static_cast<std::uint8_t>(i);
        for (int i = 0; i < 15; ++i)
            message[i] = static_cast<std::uint8_t>(i);
        check(hlrelay::sipHash24(key, message, 15) == 0xa129ca6149be45e5ull, "SipHash-2-4: the paper's vector");
        check(hlrelay::sipHash24(key, message, 0) == 0x726fdb47dd0e0e31ull, "... and the empty message's");
    }

    void testLog()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        using hlrelay::IpLog;
        Relay relay;
        std::vector<std::string> lines;
        relay.log = [&](const std::string &line) { lines.push_back(line); };
        std::vector<Out> out;
        std::uint32_t now = 1000;
        const std::uint32_t id = open(relay, 0, "Gordon", 8, now, out);
        join(relay, 1, id, now, out);
        relay.tick(now + kRelaySilentMs + 1, out);
        bool names = false, whole = false, cut = false;
        for (const std::string &line : lines)
        {
            names = names || line.find("Gordon") != std::string::npos || line.find("P1") != std::string::npos;
            whole = whole || line.find("10.0.0.") != std::string::npos && line.find(".x") == std::string::npos;
            whole = whole || line.find(":500") != std::string::npos;
            cut = cut || line.find("#1 (10.0.0.x) came") != std::string::npos;
        }
        check(!lines.empty() && !names, "the log names no room and no player");
        check(cut && !whole, "... and gives a console's id and the first three numbers of its address, no more");

        const Address one = {0xC0000201u, 1234}, sameOther = {0xC0000201u, 4321}, two = {0xC0000202u, 1234};
        relay.ipLog = IpLog::Full;
        check(relay.shown(one) == "192.0.2.1:1234", "--log-ips full: address and port");
        relay.ipLog = IpLog::Trunc;
        check(relay.shown(one) == "192.0.2.x", "--log-ips trunc: the first three numbers");
        relay.ipLog = IpLog::Hash;
        std::uint8_t salt[16] = {1, 2, 3};
        relay.setLogSalt(salt);
        const std::string a = relay.shown(one);
        check(a.size() == 8 && a.rfind("h:", 0) == 0 && a.find("192") == std::string::npos &&
                  relay.shown(sameOther) == a && relay.shown(two) != a,
              "--log-ips hash: the same address the same whatever its port, another another");
        salt[0] = 9;
        relay.setLogSalt(salt);
        check(relay.shown(one) != a, "... and not the same under the next day's secret");
    }

    void testStatsLine()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        using hlrelay::Serve;
        Relay relay;
        std::vector<std::string> lines;
        relay.log = [&](const std::string &line) { lines.push_back(line); };
        PretendSocket socket;
        std::uint32_t now = 1000;
        Serve serve(relay, socket, now);
        std::uint8_t ping[kRelayHeadBytes];
        const int pingSize = relaySmallWrite(ping, RelayKind::Ping, comeIn(relay, 1, now));
        for (int i = 0; i < 3; ++i)
            socket.waiting.push_back({at(1), std::vector<std::uint8_t>(ping, ping + pingSize)});
        socket.waiting.push_back({at(2), {1, 2, 3}});
        serve.round(now);
        serve.round(now + Serve::kStatsMs - 1);
        bool stats = false;
        for (const std::string &line : lines)
            stats = stats || line.rfind("stats ", 0) == 0;
        check(!stats, "no line of counts before its minute");
        serve.round(now + Serve::kStatsMs);
        // (Three Pongs; still known a minute on: the relay waits longer.)
        const std::string &last = lines.back();
        check(last.find("stats peers=1 rooms=0 in=4 in_bytes=27 out=3 out_bytes=24 drop_not_ours=1 other_version=0 resumed=0 drop_too_many=0 "
                        "drop_unknown=0 drop_bad_cookie=0 drop_rate_hello=0 drop_rate_control=0 drop_rate_data=0 "
                        "drop_rate_voice=0 drop_cap_peers=0 drop_cap_rooms=0 drop_cap_knocks=0 drop_out_budget=0 "
                        "send_failed=0 full_rounds=0 (last 60 s)") == 0,
              "a line of counts each minute: consoles, rooms, what came, went and fell");
        serve.round(now + 2 * Serve::kStatsMs);
        check(lines.back().find("in=0 in_bytes=0 out=0") != std::string::npos, "... each minute's own");
    }

    void testComingIn()
    {
        g_sessions.clear(); // (a relay of an earlier test's may have been where this one is)
        Relay relay;
        std::uint8_t secret[Relay::kSecretBytes];
        for (int i = 0; i < Relay::kSecretBytes; ++i)
            secret[i] = static_cast<std::uint8_t>(rnd());
        relay.setSecret(secret);
        std::vector<Out> out;
        const std::uint32_t now = 5000000;
        std::uint8_t bytes[kRelayMostBytes + 16];

        // A console not known: only Hello and Enter are answered, and never
        // with more than was sent; nothing is kept.
        bool quiet = true, small = true;
        for (int kind = 0; kind <= static_cast<int>(kRelayLastKind) + 1; ++kind)
        {
            for (int size = 0; size <= kRelayMostBytes + 16; size += 1 + static_cast<int>(rnd() % 7))
            {
                for (int i = 0; i < size; ++i)
                    bytes[i] = static_cast<std::uint8_t>(rnd());
                if (size >= kRelayHeadBytes)
                    relayHeadWrite(bytes, static_cast<RelayKind>(kind), rnd());
                out.clear();
                relay.receive(at(9), bytes, size, now, out);
                int said = 0;
                for (const Out &one : out)
                    said += static_cast<int>(one.bytes.size());
                small = small && said <= size;
                if (kind != static_cast<int>(RelayKind::Hello))
                    quiet = quiet && onlyUnknown(out, out.size());
            }
        }
        check(quiet && relay.peers() == 0,
              "a console not known: nothing answered but a Hello (and Unknown), and nothing kept (an Enter needs a good cookie)");
        check(small, "... and no answer bigger than what it was sent, whatever it was");

        // The cookie.
        out.clear();
        const std::uint32_t nonce = 0xC0FFEEu;
        relay.receive(at(1), bytes, relayHelloWrite(bytes, nonce), now, out);
        std::uint8_t cookie[kRelayCookieBytes] = {}, token[kRelayTokenBytes] = {};
        const auto challenge = sentTo(out, at(1), RelayKind::Challenge);
        check(challenge.size() == 1 && challenge[0].size() <= static_cast<std::size_t>(kRelayHelloBytes) &&
                  relayChallengeRead(challenge[0].data(), static_cast<int>(challenge[0].size()), cookie) &&
                  relay.peers() == 0,
              "a Hello: a cookie, nothing kept");
        out.clear();
        relay.receive(at(1), bytes, relayHelloWrite(bytes, nonce) - 1, now, out);
        check(out.empty(), "... a Hello short of its padding: nothing");

        auto enterWith = [&](const Address &from, std::uint32_t n, const std::uint8_t *c, std::uint32_t when)
        {
            out.clear();
            relay.receive(from, bytes, relayEnterWrite(bytes, n, c, nullptr), when, out);
            return sentTo(out, from, RelayKind::Welcome);
        };
        std::uint8_t forged[kRelayCookieBytes];
        std::memcpy(forged, cookie, sizeof(forged));
        forged[7] ^= 1;
        const std::uint64_t badBefore = relay.drops().badCookie;
        check(enterWith(at(1), nonce, forged, now).empty() && relay.peers() == 0 &&
                  relay.drops().badCookie == badBefore + 1,
              "an Enter with a cookie it did not make: refused, nothing said");
        check(enterWith(at(2), nonce, cookie, now).empty() && relay.peers() == 0,
              "... with one made for another address");
        check(enterWith({at(1).ip, static_cast<std::uint16_t>(at(1).port + 1)}, nonce, cookie, now).empty() &&
                  relay.peers() == 0,
              "... or another port of the same address");
        check(enterWith(at(1), nonce + 1, cookie, now).empty() && relay.peers() == 0, "... or another nonce");
        const std::uint32_t window = 1u << Relay::kCookieShift;
        check(enterWith(at(1), nonce, cookie, now + 2 * window).empty() && relay.peers() == 0,
              "... or too old (two windows on)");

        const auto welcome = enterWith(at(1), nonce, cookie, now + window);
        std::uint32_t session = 0;
        check(welcome.size() == 1 && welcome[0].size() <= static_cast<std::size_t>(kRelayEnterBytes) &&
                  relayWelcomeRead(welcome[0].data(), static_cast<int>(welcome[0].size()), session, token) &&
                  session != 0 && session != nonce && relay.peers() == 1,
              "the cookie sent back, in the next window still: known, given a session of the relay's");
        std::uint8_t sameToken[kRelayTokenBytes];
        std::uint32_t same = 0;
        const auto again = enterWith(at(1), nonce, cookie, now + window);
        check(again.size() == 1 && relayWelcomeRead(again[0].data(), static_cast<int>(again[0].size()), same, sameToken) &&
                  same == session && std::memcmp(sameToken, token, kRelayTokenBytes) == 0 && relay.peers() == 1,
              "... the Enter said again (the Welcome lost): the same answer, nothing more kept");

        out.clear();
        relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, session), now + window, out);
        check(sentTo(out, at(1), RelayKind::Pong).size() == 1, "... and from then on, in its session, it is answered");
        out.clear();
        relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, nonce), now + window + 2000, out);
        check(onlyUnknown(out, 1), "... not in another (told it is not known)");

        // Another relay (or this one started again): another secret, other
        // cookies and sessions.
        Relay other;
        secret[0] ^= 0xFF;
        other.setSecret(secret);
        out.clear();
        other.receive(at(1), bytes, relayEnterWrite(bytes, nonce, cookie, nullptr), now, out);
        check(out.empty() && other.peers() == 0, "a cookie of another relay's (or of this one before it started again): no good");

        // As many as it may know.
        Relay full;
        for (int n = 0; n < static_cast<int>(Relay::kMostPeers); ++n)
            comeIn(full, 100 + n, now);
        check(full.peers() == Relay::kMostPeers && comeIn(full, 99, now) == 0 && full.drops().tooMany == 1,
              "one more than it may know: not let in, and counted");
    }

    void testOtherVersion()
    {
        g_sessions.clear();
        Relay relay;
        std::vector<Out> out;
        std::uint32_t now = 70000;
        std::uint8_t bytes[kRelayMostBytes];

        auto older = [&](const Address &from, std::uint32_t when)
        {
            out.clear();
            const int size = relaySmallWrite(bytes, RelayKind::Ping, 0xABCD1234u);
            bytes[2] = kRelayVersion - 1;
            relay.receive(from, bytes, size, when, out);
        };
        older(at(1), now);
        int version = 0;
        std::uint32_t echoed = 0;
        check(out.size() == 1 && out[0].to == at(1) && out[0].bytes.size() == static_cast<std::size_t>(kRelayHeadBytes) &&
                  relayVersionRead(out[0].bytes.data(), kRelayHeadBytes, version, echoed) &&
                  version == kRelayVersion && echoed == 0xABCD1234u && relay.peers() == 0,
              "a console of an older version: told the relay's, in its own session, and nothing kept");
        RelayHead head;
        check(!relayHeadRead(out[0].bytes.data(), kRelayHeadBytes, head),
              "... an answer no console of any version takes for anything else");
        older(at(1), now + 500);
        check(out.empty(), "... said again within the second: not answered again");
        older({at(1).ip, 9999}, now + 600);
        check(out.empty(), "... nor from another port of that address");
        older(at(1), now + 1000);
        check(out.size() == 1, "... a second later, answered again");

        out.clear();
        relayHelloWrite(bytes, 77);
        bytes[2] = kRelayVersion + 1;
        relay.receive(at(2), bytes, kRelayHelloBytes, now, out);
        check(out.size() == 1 && out[0].bytes.size() <= static_cast<std::size_t>(kRelayHelloBytes) &&
                  relayVersionRead(out[0].bytes.data(), kRelayHeadBytes, version, echoed) && echoed == 77,
              "a newer console's Hello: told the relay's version, nothing bigger than it sent");

        out.clear();
        relayVersionWrite(bytes, 5);
        bytes[2] = kRelayVersion + 1;
        relay.receive(at(3), bytes, kRelayHeadBytes, now, out);
        check(out.empty(), "another relay's answer of its version: not answered (no two relays answering for ever)");
        out.clear();
        relay.receive(at(3), bytes, kRelayHeadBytes - 1, now, out);
        bytes[0] ^= 0xFF;
        relay.receive(at(3), bytes, kRelayHeadBytes, now, out);
        check(out.empty(), "... nor a head cut short, nor one not of this protocol");

        // From very many addresses at once: not more than so many answers a second.
        int answered = 0;
        for (int n = 0; n < 3 * Relay::kVersionPerSecond; ++n)
        {
            older({0x0B000000u + static_cast<std::uint32_t>(n) * 7919u, 1}, now + 5000);
            answered += static_cast<int>(out.size());
        }
        check(answered <= Relay::kVersionPerSecond && answered > Relay::kVersionPerSecond / 2,
              "very many of another version at once: no more than so many answered a second");
        check(relay.drops().otherVersion >= static_cast<std::uint64_t>(3 * Relay::kVersionPerSecond),
              "... and every one counted");
    }

    /// A console at `from` comes in: its session (0 if not let in).
    std::uint32_t comeInAt(Relay &relay, const Address &from, std::uint32_t now)
    {
        std::vector<Out> out;
        std::uint8_t bytes[64], cookie[kRelayCookieBytes] = {}, token[kRelayTokenBytes];
        const std::uint32_t nonce = ++g_nonce;
        relay.receive(from, bytes, relayHelloWrite(bytes, nonce), now, out);
        const auto challenge = sentTo(out, from, RelayKind::Challenge);
        if (challenge.size() != 1)
            return 0;
        relayChallengeRead(challenge[0].data(), static_cast<int>(challenge[0].size()), cookie);
        out.clear();
        relay.receive(from, bytes, relayEnterWrite(bytes, nonce, cookie, nullptr), now, out);
        std::uint32_t session = 0;
        const auto welcome = sentTo(out, from, RelayKind::Welcome);
        if (welcome.size() == 1)
            relayWelcomeRead(welcome[0].data(), static_cast<int>(welcome[0].size()), session, token);
        return session;
    }

    void testLimits()
    {
        g_sessions.clear();
        using hlrelay::Bucket;
        using hlrelay::Limits;

        // The bucket itself.
        Bucket b;
        int taken = 0;
        while (b.take(1, 10, 20, 1000) && taken < 100)
            ++taken;
        check(taken == 20, "a bucket: as much as its burst at once");
        check(!b.take(1, 10, 20, 1099) && b.take(1, 10, 20, 1100) && !b.take(1, 10, 20, 1100),
              "... then its rate: one a tenth of a second at ten a second");
        check(b.take(20, 10, 20, 1100 + 3600000u * 3) && !b.take(1, 10, 20, 1100 + 3600000u * 3),
              "... a long silence fills it, no more than its burst");
        Bucket wrap;
        wrap.take(5, 10, 10, 0xFFFFFF00u);
        check(wrap.take(5, 10, 10, 0xFFFFFF00u) && wrap.take(1, 10, 10, 0x00000100u),
              "... and the clock going round is no trouble");

        Limits named;
        check(named.set("data-per-s", 5) && named.dataPerSecond == 5 && !named.set("nope", 5) &&
                  !named.set("data-per-s", 0) && named.dataPerSecond == 5,
              "limits set by name; not one that is not there, nor to 0");
        check(named.set("silent-seconds", 60) && named.silentSeconds == 60 && !named.set("silent-seconds", 0) &&
                  !named.set("silent-seconds", Limits::kMostSilentSeconds + 1),
              "... the silence a console is let go after: in seconds, not 0, an hour at most");
        {
            Relay quiet;
            quiet.limits.silentSeconds = 60;
            std::vector<Out> said;
            comeIn(quiet, 1, 1000);
            quiet.tick(1000 + 60000, said);
            check(quiet.peers() == 1, "... a console quiet a minute, the relay set to a minute: still there");
            quiet.tick(1000 + 60001, said);
            check(quiet.peers() == 0, "... a moment after: let go");
            g_sessions.erase({&quiet, 1}); // (another relay may be where this one was)
            check(Limits().silentSeconds * 1000u == kRelaySilentMs && kRelaySilentMs == 210000u,
                  "... by default three and a half minutes");
        }

        Relay relay;
        std::vector<std::string> lines;
        relay.log = [&](const std::string &line) { lines.push_back(line); };
        std::vector<Out> out;
        std::uint32_t now = 100000;
        std::uint8_t bytes[kRelayMostBytes];

        // Coming in, from one address: its burst, then its rate.
        int answered = 0;
        for (int i = 0; i < 50; ++i)
        {
            out.clear();
            relay.receive(at(1), bytes, relayHelloWrite(bytes, 1), now, out);
            answered += static_cast<int>(out.size());
        }
        const Limits &l = relay.limits;
        check(answered == static_cast<int>(2 * l.helloPerSecond) &&
                  relay.drops().rateHello == 50 - 2 * l.helloPerSecond,
              "Hellos from one address at once: twice its rate answered, the rest let fall");
        out.clear();
        relay.receive(at(2), bytes, relayHelloWrite(bytes, 1), now, out);
        check(out.size() == 1, "... another address is not held up by it");
        out.clear();
        relay.receive(at(1), bytes, relayHelloWrite(bytes, 1), now + 1000 / l.helloPerSecond, out);
        check(out.size() == 1, "... and the first is answered again in its time");
        int overLines = 0;
        for (const std::string &line : lines)
            overLines += line.find("over its limit (coming in)") != std::string::npos;
        check(overLines == 1, "... logged once, not for each one let fall");
        for (int i = 0; i < 50; ++i)
            relay.receive(at(1), bytes, relayHelloWrite(bytes, 1), now + Relay::kOverLogMs + 200, out);
        overLines = 0;
        for (const std::string &line : lines)
            overLines += line.find("over its limit (coming in)") != std::string::npos;
        check(overLines == 2, "... and again a minute later, if it is still over");

        // Consoles from one address at once.
        now += 10 * Relay::kOverLogMs;
        const std::uint32_t lan = 0xC6336401u; // (198.51.100.1: behind one router)
        std::vector<std::uint32_t> sessions;
        for (std::uint32_t n = 0; n <= l.peersPerIp; ++n)
        {
            // (Each a second apart: not held up by coming in too often.)
            now += 1000;
            sessions.push_back(comeInAt(relay, {lan, static_cast<std::uint16_t>(6000 + n)}, now));
        }
        check(sessions[l.peersPerIp - 1] != 0 && sessions[l.peersPerIp] == 0 && relay.drops().capPeers == 1,
              "as many consoles from one address as it may have: one more is not let in");
        out.clear();
        relay.receive({lan, 6000}, bytes, relaySmallWrite(bytes, RelayKind::Bye, sessions[0]), now, out);
        now += 1000;
        check(comeInAt(relay, {lan, 7000}, now) != 0, "... one goes: another may come");

        // Rooms from one address at once.
        RelayOpen o;
        int opened = 0;
        for (std::uint32_t n = 1; n <= l.roomsPerIp + 1; ++n)
        {
            out.clear();
            relay.receive({lan, static_cast<std::uint16_t>(6000 + n)}, bytes, relayOpenWrite(bytes, sessions[n], o),
                          now, out);
            opened += static_cast<int>(sentTo(out, {lan, static_cast<std::uint16_t>(6000 + n)}, RelayKind::Opened).size());
        }
        check(opened == static_cast<int>(l.roomsPerIp) && relay.rooms() == l.roomsPerIp && relay.drops().capRooms == 1,
              "as many rooms from one address as it may have: one more is not opened");
        out.clear();
        relay.receive({lan, 6001}, bytes, relaySmallWrite(bytes, RelayKind::Bye, sessions[1]), now, out);
        relay.receive({lan, static_cast<std::uint16_t>(6000 + l.roomsPerIp + 1)}, bytes,
                      relayOpenWrite(bytes, sessions[l.roomsPerIp + 1], o), now, out);
        check(relay.rooms() == l.roomsPerIp, "... one closes: another may open");

        // Consoles of one address at doors at once (rooms elsewhere, their
        // hosts not answering).
        const std::uint32_t idA = open(relay, 20, "A", 8, now, out);
        const std::uint32_t idB = open(relay, 21, "B", 8, now, out);
        const std::uint32_t knockers = 0xCB007101u; // (203.0.113.1)
        int busy = 0, waiting = 0;
        std::uint32_t last = 0;
        for (std::uint32_t n = 0; n <= l.knocksPerIp; ++n)
        {
            now += 1000;
            const Address from = {knockers, static_cast<std::uint16_t>(8000 + n)};
            last = comeInAt(relay, from, now);
            out.clear();
            relay.receive(from, bytes, relayJoinWrite(bytes, last, n % 2 ? idA : idB, "K"), now, out);
            const auto refused = sentTo(out, from, RelayKind::Refused);
            busy += refused.size() == 1 && relaySmallRead(refused[0].data(), static_cast<int>(refused[0].size())) ==
                                               static_cast<int>(RelayRefuse::Busy);
            waiting += refused.empty();
        }
        check(waiting == static_cast<int>(l.knocksPerIp) && busy == 1 && relay.drops().capKnocks == 1,
              "as many of one address at doors as it may have: one more is told so (Busy)");
        const std::uint32_t firstKnocker = comeInAt(relay, {knockers, 9000}, now + 1000);
        relay.tick(now + kRelayKnockMs + 2000, out); // (the hosts never answer: all turned away)
        out.clear();
        relay.receive({knockers, 9000}, bytes, relayJoinWrite(bytes, firstKnocker, idA, "K"), now + kRelayKnockMs + 2000,
                      out);
        check(sentTo(out, {knockers, 9000}, RelayKind::Refused).empty(),
              "... once those at the doors are turned away (not answered in time), others may knock");

        // A console's own: control, and frames.
        Relay r2;
        out.clear();
        const std::uint32_t t = 200000;
        const std::uint32_t room = open(r2, 0, "H", 8, t, out);
        join(r2, 1, room, t, out);
        int pongs = 0;
        for (int i = 0; i < 100; ++i)
        {
            out.clear();
            r2.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(r2, 1, t)), t + 1, out);
            pongs += static_cast<int>(sentTo(out, at(1), RelayKind::Pong).size());
        }
        check(pongs > 0 && pongs < 100 && r2.drops().rateControl == static_cast<std::uint64_t>(100 - pongs),
              "a console saying a hundred things at once: what is over its rate let fall");

        // A host's frames as a game sends them (60 a second, the biggest):
        // every one passed on, for ten seconds.
        std::uint8_t frame[kNetCmdBytes] = {};
        int passed = 0, sent = 0;
        for (int f = 0; f < 600; ++f)
        {
            out.clear();
            const std::uint32_t when = t + 2000 + static_cast<std::uint32_t>(f * 1000 / 60);
            r2.receive(at(0), bytes, relayDataWrite(bytes, comeIn(r2, 0, t), 0, frame, kNetCmdBytes), when, out);
            // (and the client's, and a ping each second, as a game does)
            r2.receive(at(1), bytes, relayDataWrite(bytes, comeIn(r2, 1, t), 1, frame, kNetReplyBytes), when, out);
            passed += static_cast<int>(sentTo(out, at(1), RelayKind::Data).size() + sentTo(out, at(0), RelayKind::Data).size());
            sent += 2;
            if (f % 60 == 0)
            {
                r2.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(r2, 0, t)), when, out);
                r2.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(r2, 1, t)), when, out);
            }
        }
        check(passed == sent && r2.drops().rateData == 0, "a game's own frames, host's and client's: every one passed on");
        // A host sending a map as a game does online (its frame and four
        // pieces of 600, with their heads, each 60th of a second): every one
        // passed on, for ten seconds.
        const int pieceBytes = 620; // (a piece and its head, as NetGame's fetchWrite makes it)
        int sentNow = 0;
        passed = 0;
        for (int f = 0; f < 600; ++f)
        {
            out.clear();
            const std::uint32_t when = t + 20000 + static_cast<std::uint32_t>(f * 1000 / 60);
            r2.receive(at(0), bytes, relayDataWrite(bytes, comeIn(r2, 0, t), 0, frame, kNetCmdBytes), when, out);
            for (int k = 0; k < 4; ++k)
                r2.receive(at(0), bytes, relayDataWrite(bytes, comeIn(r2, 0, t), 0, frame, pieceBytes), when, out);
            r2.receive(at(1), bytes, relayDataWrite(bytes, comeIn(r2, 1, t), 1, frame, kNetReplyBytes), when, out);
            if (f % 60 == 0)
            {
                r2.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(r2, 0, t)), when, out);
                r2.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(r2, 1, t)), when, out);
            }
            passed += static_cast<int>(sentTo(out, at(1), RelayKind::Data).size() + sentTo(out, at(0), RelayKind::Data).size());
            sentNow += 6;
        }
        check(passed == sentNow && r2.drops().rateData == 0,
              "a host sending a map online (its frame and four pieces each 60th): every one passed on");
        // ... twice that: held to its rate.
        passed = 0;
        int tried = 0;
        for (int f = 0; f < 600; ++f)
        {
            out.clear();
            const std::uint32_t when = t + 40000 + static_cast<std::uint32_t>(f * 1000 / 60);
            for (int k = 0; k < 10; ++k)
                r2.receive(at(0), bytes, relayDataWrite(bytes, comeIn(r2, 0, t), 0, frame, kNetCmdBytes), when, out);
            tried += 10;
            passed += static_cast<int>(sentTo(out, at(1), RelayKind::Data).size());
        }
        const std::uint32_t mostBytes = 10 * r2.limits.hostBytesPerSecond + 2 * r2.limits.hostBytesPerSecond;
        check(passed < tried && static_cast<std::uint32_t>(passed) * (kRelayHeadBytes + 1 + kNetCmdBytes) <= mostBytes &&
                  r2.drops().rateData == static_cast<std::uint64_t>(tried - passed),
              "a host sending twice that: held to its rate, the rest let fall");
    }

    void testOutBudget()
    {
        g_sessions.clear();
        using hlrelay::Serve;
        Relay relay;
        relay.limits.outBytesPerSecond = 10 * kRelayHeadBytes; // (ten Pongs a second)
        PretendSocket socket;
        std::uint32_t now = 1000;
        Serve serve(relay, socket, now);
        std::uint8_t ping[kRelayHeadBytes];
        const int pingSize = relaySmallWrite(ping, RelayKind::Ping, comeIn(relay, 1, now));
        relay.limits.controlPerSecond = 1000;
        for (int i = 0; i < 25; ++i)
            socket.waiting.push_back({at(1), std::vector<std::uint8_t>(ping, ping + pingSize)});
        serve.round(now);
        check(socket.sent.size() == 10 && serve.counts.outBudget == 15,
              "the relay sending more than it may in all: the rest not sent, and counted");
        socket.waiting.push_back({at(1), std::vector<std::uint8_t>(ping, ping + pingSize)});
        serve.round(now + 100);
        check(socket.sent.size() == 11, "... and sent again in its time");
    }

    /// The reason of the one Bye `to` was sent among `out`; -1 if none, or not one.
    int byeTo(const std::vector<Out> &out, const Address &to)
    {
        const auto byes = sentTo(out, to, RelayKind::Bye);
        return byes.size() == 1 ? relaySmallRead(byes[0].data(), static_cast<int>(byes[0].size())) : -1;
    }

    void testUnknown()
    {
        g_sessions.clear();
        Relay relay;
        std::vector<Out> out;
        std::uint8_t bytes[kRelayMostBytes];
        std::uint32_t now = 400000;
        relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, 0x77u), now, out);
        RelayHead head;
        check(out.size() == 1 && out[0].to == at(1) && out[0].bytes.size() == static_cast<std::size_t>(kRelayHeadBytes) &&
                  relayHeadRead(out[0].bytes.data(), kRelayHeadBytes, head) && head.kind == RelayKind::Unknown &&
                  head.session == 0x77u && relay.peers() == 0,
              "a console's Ping from where none is known: told so, in its own session, its head alone; nothing kept");
        out.clear();
        relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, 0x77u), now + 999, out);
        check(out.empty(), "... said again within the second: not told again");
        relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, 0x77u), now + 1000, out);
        check(out.size() == 1, "... a second later, told again");
        bool none = true;
        const RelayKind relayOnly[] = {RelayKind::Pong, RelayKind::Opened, RelayKind::Rooms, RelayKind::Joined,
                                       RelayKind::Refused, RelayKind::Status, RelayKind::Knock, RelayKind::Bye,
                                       RelayKind::Challenge, RelayKind::Welcome, RelayKind::Unknown};
        int n = 10;
        for (RelayKind kind : relayOnly)
        {
            out.clear();
            relay.receive(at(n++), bytes, relaySmallWrite(bytes, kind, 1), now, out);
            none = none && out.empty();
        }
        check(none, "... but what only a relay says (or a Bye): never answered (no two relays answering for ever)");
    }

    /// A console at `from` comes in with `token` (null: none): the Welcome's
    /// session, and its token into `got` (0 if not let in).
    std::uint32_t enterWithToken(Relay &relay, const Address &from, const std::uint8_t *token, std::uint32_t now,
                                 std::uint8_t *got, std::vector<Out> *said = nullptr)
    {
        std::vector<Out> out;
        std::uint8_t bytes[64], cookie[kRelayCookieBytes] = {};
        const std::uint32_t nonce = ++g_nonce;
        relay.receive(from, bytes, relayHelloWrite(bytes, nonce), now, out);
        const auto challenge = sentTo(out, from, RelayKind::Challenge);
        if (challenge.size() != 1)
            return 0;
        relayChallengeRead(challenge[0].data(), static_cast<int>(challenge[0].size()), cookie);
        out.clear();
        relay.receive(from, bytes, relayEnterWrite(bytes, nonce, cookie, token), now, out);
        std::uint32_t session = 0;
        const auto welcome = sentTo(out, from, RelayKind::Welcome);
        if (welcome.size() == 1)
            relayWelcomeRead(welcome[0].data(), static_cast<int>(welcome[0].size()), session, got);
        if (said != nullptr)
            said->insert(said->end(), out.begin(), out.end());
        return session;
    }

    void testResume()
    {
        g_sessions.clear();
        Relay relay;
        std::vector<Out> out;
        std::uint8_t bytes[kRelayMostBytes];
        std::uint32_t now = 500000;
        const Address host = at(0), client = at(1);
        const Address moved = {client.ip, static_cast<std::uint16_t>(client.port + 100)};
        std::uint8_t hostToken[kRelayTokenBytes], token[kRelayTokenBytes], again[kRelayTokenBytes];
        const std::uint32_t hostSession = enterWithToken(relay, host, nullptr, now, hostToken);
        const std::uint32_t session = enterWithToken(relay, client, nullptr, now, token);
        RelayOpen o;
        o.most = 8;
        relay.receive(host, bytes, relayOpenWrite(bytes, hostSession, o), now, out);
        const auto opened = sentTo(out, host, RelayKind::Opened);
        std::uint32_t room = 0;
        relayRoomIdRead(opened[0].data(), static_cast<int>(opened[0].size()), room);
        out.clear();
        relay.receive(client, bytes, relayJoinWrite(bytes, session, room, "C"), now, out);
        int ticket = 0;
        char name[kNetTextBytes];
        const auto knocked = sentTo(out, host, RelayKind::Knock);
        relayKnockRead(knocked[0].data(), static_cast<int>(knocked[0].size()), ticket, name);
        relay.receive(host, bytes, relayAnswerWrite(bytes, hostSession, ticket, true), now, out);

        // Its router gives it another port: what it says is not known there...
        now += 2000;
        out.clear();
        relay.receive(moved, bytes, relaySmallWrite(bytes, RelayKind::Ping, session), now, out);
        check(onlyUnknown(out, 1) && out[0].to == moved, "a client's router gives it another port: told it is not known there");
        // ... so it takes its session up again, with its token.
        out.clear();
        const std::uint32_t back = enterWithToken(relay, moved, token, now, again, &out);
        check(back == session && std::memcmp(again, token, kRelayTokenBytes) == 0 && relay.peers() == 2 &&
                  relay.drops().resumed == 1 && lastStatus(out, 0) == 0xFFFFu,
              "... it comes in with its token: the same session, nothing new kept, the host not told of any change");
        const std::uint8_t frame[3] = {7, 8, 9};
        out.clear();
        relay.receive(host, bytes, relayDataWrite(bytes, hostSession, 0, frame, 3), now, out);
        check(sentTo(out, moved, RelayKind::Data).size() == 1 && sentTo(out, client, RelayKind::Data).empty(),
              "... the host's frames go to where it is now, not where it was");
        out.clear();
        relay.receive(moved, bytes, relayDataWrite(bytes, session, 1, frame, 3), now, out);
        check(sentTo(out, host, RelayKind::Data).size() == 1, "... its own still reach the host, as client 1");
        out.clear();
        relay.receive(client, bytes, relayDataWrite(bytes, session, 1, frame, 3), now, out);
        check(sentTo(out, host, RelayKind::Data).empty(), "... and what comes from its old address is not passed on");

        // Its Welcome lost: the Enter said again, from where it is now.
        // (Covered by the nonce: a new one each time; here the same token.)
        out.clear();
        check(enterWithToken(relay, moved, token, now + 100, again) == session && relay.peers() == 2,
              "... taking it up again from the same address (nothing heard for a while): the same session");

        // Not with another's token, nor one made up.
        std::uint8_t wrong[kRelayTokenBytes];
        std::memcpy(wrong, token, sizeof(wrong));
        wrong[0] ^= 1;
        const Address stranger = at(7);
        const std::uint32_t other = enterWithToken(relay, stranger, wrong, now, again);
        check(other != 0 && other != session && std::memcmp(again, token, kRelayTokenBytes) != 0 && relay.peers() == 3,
              "a token that is no one's: a console of its own, nobody's place taken");
        // Nor a good token without a cookie for where it is.
        out.clear();
        std::uint8_t noCookie[kRelayCookieBytes] = {};
        relay.receive(at(8), bytes, relayEnterWrite(bytes, 5, noCookie, token), now, out);
        check(out.empty() && relay.drops().resumed == 2,
              "a client's token, with no cookie for where it is sent from: nothing (its place not taken)");

        // A host takes its up again: its room with it.
        const Address hostMoved = {host.ip, static_cast<std::uint16_t>(host.port + 7)};
        check(enterWithToken(relay, hostMoved, hostToken, now, again) == hostSession && relay.rooms() == 1,
              "a host takes its session up again from another port: its room still there");
        out.clear();
        relay.receive(hostMoved, bytes, relayDataWrite(bytes, hostSession, 0, frame, 3), now, out);
        check(sentTo(out, moved, RelayKind::Data).size() == 1, "... and its frames still reach its client");

        // A console let go is not taken up again: a new one instead.
        now += kRelaySilentMs + 1000;
        relay.receive(moved, bytes, relaySmallWrite(bytes, RelayKind::Ping, session), now - 10, out);
        out.clear();
        relay.tick(now, out);
        const std::uint32_t late = enterWithToken(relay, {client.ip, 9999}, token, now, again);
        check(late != 0 && late != session, "a console let go (quiet too long): its token takes nothing up, it is new");

        // From another address that has as many as it may: refused.
        Relay full;
        full.limits.peersPerIp = 1;
        std::uint8_t t1[kRelayTokenBytes], t2[kRelayTokenBytes];
        const Address a = {0x0A0A0001u, 1}, b = {0x0A0A0002u, 1};
        enterWithToken(full, a, nullptr, now, t1);
        enterWithToken(full, b, nullptr, now, t2);
        check(enterWithToken(full, {b.ip, 2}, t1, now + 1000, again) == 0 && full.peers() == 2,
              "taking a session up from an address that has as many consoles as it may: refused");
    }

    void testRoomTimes()
    {
        g_sessions.clear();
        std::vector<Out> out;
        std::uint8_t bytes[kRelayMostBytes];
        const std::uint32_t t = 600000;
        hlrelay::Limits named;
        check(named.set("empty-room-minutes", 0) && named.emptyRoomMinutes == 0 && named.set("room-max-minutes", 90) &&
                  named.roomMaxMinutes == 90 && !named.set("room-max-minutes", hlrelay::Limits::kMostMinutes + 1),
              "the rooms' times set by name: 0 is never, and not more than the clock can tell");

        // (A host keeps pinging, so that it is not let go for its silence.)
        auto keepAlive = [&](Relay &relay, std::uint32_t from, std::uint32_t to, int n)
        {
            for (std::uint32_t when = from; when <= to; when += 20000)
            {
                for (int k = 0; k < n; ++k)
                    relay.receive(at(k), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(relay, k, t)), when, out);
                relay.tick(when, out);
            }
        };
        const std::uint32_t empty = 30 * 60000u;
        {
            Relay relay;
            open(relay, 0, "H", 8, t, out);
            keepAlive(relay, t, t + empty - 20000, 1);
            out.clear();
            relay.tick(t + empty - 1, out);
            check(relay.rooms() == 1, "a room nobody joins: still there just short of half an hour");
            relay.tick(t + empty, out);
            check(relay.rooms() == 0 && relay.peers() == 0 &&
                      byeTo(out, at(0)) == static_cast<int>(RelayBye::RoomExpired),
                  "... at half an hour, closed: its host told why, and let go");
        }
        g_sessions.clear();
        {
            Relay relay;
            const std::uint32_t id = open(relay, 0, "H", 8, t, out);
            join(relay, 1, id, t, out);
            keepAlive(relay, t, t + 2 * empty, 2);
            check(relay.rooms() == 1, "a room with someone in it: open as long as they play");
            // The client goes: the half hour counts from then.
            relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Bye, comeIn(relay, 1, t)), t + 2 * empty, out);
            relay.tick(t + 2 * empty + 100, out);
            g_sessions.erase({&relay, 1});
            keepAlive(relay, t + 2 * empty + 20000, t + 3 * empty - 20000, 1);
            check(relay.rooms() == 1, "... the last goes: its half hour from then, not from when it was opened");
            out.clear();
            relay.tick(t + 3 * empty + 200, out);
            check(relay.rooms() == 0, "... and then closed");
        }
        g_sessions.clear();
        {
            // One at the door, the host not answering yet: not empty.
            Relay relay;
            const std::uint32_t id = open(relay, 0, "H", 8, t, out);
            keepAlive(relay, t, t + empty - 20000, 1);
            join(relay, 1, id, t + empty - 10000, out, 0, -1);
            relay.tick(t + empty + 5000, out);
            check(relay.rooms() == 1, "a room someone is asking to join: not empty");
        }
        g_sessions.clear();
        {
            // Never closed for being empty, if so set; at most so long, if so set.
            Relay relay;
            relay.limits.emptyRoomMinutes = 0;
            relay.limits.roomMaxMinutes = 90;
            const std::uint32_t id = open(relay, 0, "H", 8, t, out);
            join(relay, 1, id, t, out);
            keepAlive(relay, t, t + 90 * 60000u - 20000, 2);
            check(relay.rooms() == 1, "rooms never closed for being empty, at most an hour and a half: still there");
            out.clear();
            relay.tick(t + 90 * 60000u, out);
            check(relay.rooms() == 0 && byeTo(out, at(0)) == static_cast<int>(RelayBye::RoomTimeUp) &&
                      byeTo(out, at(1)) == static_cast<int>(RelayBye::HostLeft),
                  "... at an hour and a half, closed though they play: the host told why, its client that it went");
        }
    }

    void testByeReasons()
    {
        g_sessions.clear();
        std::vector<Out> out;
        std::uint8_t bytes[kRelayMostBytes];
        const std::uint32_t now = 300000;
        {
            Relay relay;
            const std::uint32_t id = open(relay, 0, "H", 8, now, out);
            join(relay, 1, id, now, out);
            join(relay, 2, id, now, out);
            join(relay, 3, id, now, out, 0, -1); // (at the door)
            out.clear();
            relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Bye, comeIn(relay, 0, now)), now, out);
            const auto refused = sentTo(out, at(3), RelayKind::Refused);
            check(byeTo(out, at(1)) == static_cast<int>(RelayBye::HostLeft) &&
                      byeTo(out, at(2)) == static_cast<int>(RelayBye::HostLeft) && refused.size() == 1 &&
                      relaySmallRead(refused[0].data(), static_cast<int>(refused[0].size())) ==
                          static_cast<int>(RelayRefuse::NoRoom),
                  "the host says goodbye: each client told the host left, the one at the door that there is no room");
            check(relay.peers() == 1 && relay.rooms() == 0,
                  "... and its clients let go with it (they come in again for whatever they do next)");
            out.clear();
            relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(relay, 1, now)), now, out);
            check(onlyUnknown(out, 1), "... one of them still in its old session: told it is not known");
        }
        g_sessions.clear();
        {
            Relay relay;
            const std::uint32_t id = open(relay, 0, "H", 8, now, out);
            join(relay, 1, id, now, out);
            join(relay, 2, id, now, out);
            out.clear();
            relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Kick, comeIn(relay, 0, now), 1), now, out);
            check(byeTo(out, at(1)) == static_cast<int>(RelayBye::Kicked) && relay.peers() == 2 && lastStatus(out, 0) == 0x4u,
                  "the host sends one away: it is told so, and let go; the host told who is left");
            // The client keeps talking; the host goes quiet.
            out.clear();
            relay.receive(at(2), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(relay, 2, now)),
                          now + kRelaySilentMs - 100, out);
            out.clear();
            relay.tick(now + kRelaySilentMs + 1, out);
            check(byeTo(out, at(0)) == static_cast<int>(RelayBye::Silent) &&
                      byeTo(out, at(2)) == static_cast<int>(RelayBye::HostSilent) && relay.peers() == 0,
                  "a host that says nothing for too long: told so (if it hears), its client told the host stopped answering");
        }
        g_sessions.clear();
        {
            Relay relay;
            const std::uint32_t id = open(relay, 0, "H", 8, now, out);
            join(relay, 1, id, now, out);
            out.clear();
            relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Ping, comeIn(relay, 0, now)),
                          now + kRelaySilentMs - 100, out);
            out.clear();
            relay.tick(now + kRelaySilentMs + 1, out);
            check(byeTo(out, at(1)) == static_cast<int>(RelayBye::Silent) && lastStatus(out, 0) == 0u &&
                      relay.peers() == 1,
                  "a client that says nothing for too long: told so, the host told it is gone");
        }
        g_sessions.clear();
        {
            // A whole room quiet at once: the host's clients go with it, and
            // none is counted twice.
            Relay relay;
            const std::uint32_t id = open(relay, 0, "H", 8, now, out);
            for (int n = 1; n < 5; ++n)
                join(relay, n, id, now, out);
            out.clear();
            relay.tick(now + kRelaySilentMs + 1, out);
            check(relay.peers() == 0 && relay.rooms() == 0, "a whole room quiet at once: all let go, nothing left over");
        }
        g_sessions.clear();
        {
            Relay relay;
            const std::uint32_t id = open(relay, 0, "H", 8, now, out);
            join(relay, 1, id, now, out);
            comeIn(relay, 2, now);
            out.clear();
            relay.stop(out);
            bool all = true;
            for (int n = 0; n < 3; ++n)
            {
                const auto byes = sentTo(out, at(n), RelayKind::Bye);
                all = all && byes.size() == 2 &&
                      relaySmallRead(byes[0].data(), static_cast<int>(byes[0].size())) ==
                          static_cast<int>(RelayBye::RelayStopping);
            }
            check(all && relay.peers() == 0 && relay.rooms() == 0,
                  "the relay stopping: every console told so, twice, and nothing kept");
        }
    }
} // namespace

int main()
{
    testDatagrams();
    testRooms();
    testKnocks();
    testVoice();
    testLobbyThroughRelay();
    testIni();
    testServeRound();
    testSipHash();
    testLog();
    testStatsLine();
    testComingIn();
    testOtherVersion();
    testLimits();
    testOutBudget();
    testByeReasons();
    testUnknown();
    testResume();
    testRoomTimes();
    std::printf("%s (%d failed)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
