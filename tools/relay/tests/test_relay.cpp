// The relay (core/Relay.hpp) and what consoles say to it
// (shared/HlRelay.hpp): rooms opened, listed, joined, left and let go; and a
// lobby of the game's own (shared/HlNetGame.hpp) played through it over
// links that lose and delay datagrams, as it is through the air.

#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include "HlIni.hpp"
#include "HlNetGame.hpp"
#include "HlRelay.hpp"
#include "Relay.hpp"

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
        relay.receive(at(n), bytes, relayOpenWrite(bytes, 100u + n, o), now, out);
        const auto opened = sentTo(out, at(n), RelayKind::Opened);
        std::uint32_t id = 0;
        return opened.size() == 1 && relayRoomIdRead(opened[0].data(), static_cast<int>(opened[0].size()), id) ? id : 0;
    }

    /// Joins `n` to room `id`: its slot, or -(the reason) if refused.
    int join(Relay &relay, int n, std::uint32_t id, std::uint32_t now, std::vector<Out> &out)
    {
        std::uint8_t bytes[kRelayMostBytes];
        out.clear();
        relay.receive(at(n), bytes, relayRoomIdWrite(bytes, RelayKind::Join, 100u + n, id), now, out);
        const auto joined = sentTo(out, at(n), RelayKind::Joined);
        if (joined.size() == 1)
            return relaySmallRead(joined[0].data(), static_cast<int>(joined[0].size()));
        const auto refused = sentTo(out, at(n), RelayKind::Refused);
        return refused.size() == 1 ? -relaySmallRead(refused[0].data(), static_cast<int>(refused[0].size())) : -99;
    }

    int list(Relay &relay, int n, RelayRoom *rooms, std::uint32_t now, std::vector<Out> &out)
    {
        std::uint8_t bytes[kRelayMostBytes];
        out.clear();
        relay.receive(at(n), bytes, relaySmallWrite(bytes, RelayKind::List, 100u + n), now, out);
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
        relay.receive(at(0), bytes, relayDataWrite(bytes, 100, 0, frame, 5), now, out);
        const auto to1 = sentTo(out, at(1), RelayKind::Data);
        const auto to2 = sentTo(out, at(2), RelayKind::Data);
        int who = -1;
        const std::uint8_t *in = nullptr;
        check(out.size() == 2 && to1.size() == 1 && to2.size() == 1 &&
                  relayDataRead(to2[0].data(), static_cast<int>(to2[0].size()), who, in) == 5 && who == 0 &&
                  std::memcmp(in, frame, 5) == 0,
              "a host frame goes to each client, from the host");
        out.clear();
        relay.receive(at(2), bytes, relayDataWrite(bytes, 102, 2, frame, 5), now, out);
        const auto toHost = sentTo(out, at(0), RelayKind::Data);
        check(out.size() == 1 && toHost.size() == 1 &&
                  relayDataRead(toHost[0].data(), static_cast<int>(toHost[0].size()), who, in) == 5 && who == 2,
              "a client's goes to the host alone, saying whose");
        out.clear();
        relay.receive(at(2), bytes, relayDataWrite(bytes, 102, 7, frame, 5), now, out);
        check(sentTo(out, at(0), RelayKind::Data).size() == 1 &&
                  relayDataRead(sentTo(out, at(0), RelayKind::Data)[0].data(),
                                static_cast<int>(sentTo(out, at(0), RelayKind::Data)[0].size()), who, in) == 5 &&
                  who == 2,
              "... whoever it says it is from: the relay says");
        out.clear();
        relay.receive(at(3), bytes, relayDataWrite(bytes, 103, 0, frame, 5), now, out);
        check(out.empty(), "a console in no room sends nothing on");

        // No more let in while a match is on.
        out.clear();
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Admit, 100, 0), now, out);
        out.clear();
        relay.receive(at(1), bytes, relaySmallWrite(bytes, RelayKind::Bye, 101), now, out);
        check(lastStatus(out, 0) == 0x4u && relay.peers() == 3, "a client says goodbye: the host is told, it is forgotten");
        check(join(relay, 3, id, now, out) == -static_cast<int>(RelayRefuse::Closed),
              "a room that lets nobody in refuses, though there is room");
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Admit, 100, 1), now, out);
        check(join(relay, 3, id, now, out) == 1, "... and once it lets them in again, the first free place is given");

        // The host sends one away.
        out.clear();
        relay.receive(at(0), bytes, relaySmallWrite(bytes, RelayKind::Kick, 100, 3 - 2), now, out);
        check(sentTo(out, at(3), RelayKind::Bye).size() == 1 && lastStatus(out, 0) == 0x4u,
              "the host sends client 1 away: it is told, and the host told who is left");

        // A console started again at the same address is a new one.
        out.clear();
        relay.receive(at(2), bytes, relaySmallWrite(bytes, RelayKind::Ping, 999), now, out);
        check(lastStatus(out, 0) == 0u && sentTo(out, at(2), RelayKind::Pong).size() == 1,
              "the same address with another session: the one before is out of the room");

        // Silence.
        out.clear();
        now += kRelaySilentMs - 10;
        relay.receive(at(2), bytes, relaySmallWrite(bytes, RelayKind::Ping, 999), now, out);
        now += 20;
        relay.tick(now, out);
        check(relay.rooms() == 0 && relay.peers() == 1, "a host that says nothing for too long: gone, and its room");

        // A host that goes takes its room with it, its clients told.
        const std::uint32_t second = open(relay, 4, "Barney", 8, now, out);
        join(relay, 5, second, now, out);
        out.clear();
        relay.receive(at(4), bytes, relaySmallWrite(bytes, RelayKind::Bye, 104), now, out);
        check(relay.rooms() == 0 && sentTo(out, at(5), RelayKind::Bye).size() == 1,
              "a host says goodbye: the room is closed, its client told");
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
        std::uint32_t session = 0;
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
            c->session = 0x1000u + static_cast<std::uint32_t>(c->n);
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
                            me.up.send(now, bytes, relayRoomIdWrite(bytes, RelayKind::Join, me.session, id));
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
                        if (me.out || !relayHeadRead(datagram.data(), size, head) || head.session != me.session)
                            continue;
                        if (head.kind == RelayKind::Opened)
                            relayRoomIdRead(datagram.data(), size, me.room);
                        else if (head.kind == RelayKind::Joined && me.slot < 0)
                            me.slot = relaySmallRead(datagram.data(), size);
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
} // namespace

int main()
{
    testDatagrams();
    testRooms();
    testLobbyThroughRelay();
    testIni();
    std::printf("%s (%d failed)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
    return g_failures == 0 ? 0 : 1;
}
