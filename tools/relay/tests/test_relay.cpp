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
        check(out.empty() && relay.peers() == 3 && relay.rooms() == 1,
              "a datagram in another session than the console's there: nothing done, nothing said");

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
        // (Three Pongs, and a Bye: by then it had said nothing for longer
        // than the relay waits.)
        const std::string &last = lines.back();
        check(last.find("stats peers=0 rooms=0 in=4 in_bytes=27 out=4 out_bytes=32 drop_not_ours=1 drop_too_many=0 "
                        "drop_unknown=0 drop_bad_cookie=0 send_failed=0 full_rounds=0 (last 60 s)") == 0,
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
                relay.receive(at(1), bytes, size, now, out);
                int said = 0;
                for (const Out &one : out)
                    said += static_cast<int>(one.bytes.size());
                small = small && said <= size;
                if (kind != static_cast<int>(RelayKind::Hello))
                    quiet = quiet && out.empty();
            }
        }
        check(quiet && relay.peers() == 0,
              "a console not known: nothing answered but a Hello, and nothing kept (an Enter needs a good cookie)");
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
        relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Ping, nonce), now + window, out);
        check(out.empty(), "... not in another");

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
    std::printf("%s (%d failed)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
