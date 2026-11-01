// A demo sent map by map (shared/HlDemo.hpp): real bytes, over a radio that
// loses frames, to consoles going at their own pace.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "HlDemo.hpp"
#include "HlLevel.hpp"

using namespace hldata;

namespace
{
    int g_failures = 0;
    void check(bool ok, const char *what)
    {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    std::uint32_t g_seed = 77;
    int roll()
    {
        g_seed = g_seed * 1664525u + 1013904223u;
        return static_cast<int>((g_seed >> 8) % 100);
    }

    using Bytes = std::vector<std::uint8_t>;
    Bytes made(std::size_t size, unsigned seed)
    {
        Bytes out(size);
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<std::uint8_t>((i * 31 + seed * 17 + (i >> 7)) & 0xFF);
        return out;
    }

    struct File
    {
        const char *name;
        std::uint32_t map;
        Bytes bytes;
        std::uint32_t crc;
        std::uint16_t serve;
    };

    File file(const char *name, std::size_t size, unsigned seed)
    {
        File f{name, nameHash(name), made(size, seed), 0, static_cast<std::uint16_t>(seed)};
        f.crc = crc32(f.bytes.data(), static_cast<std::uint32_t>(f.bytes.size()));
        return f;
    }

    /// A console taking one stream straight into its memory.
    struct Console
    {
        std::uint16_t token;
        int seat;
        AirReceiver taking;
        Bytes arena;
        int loss = 0;
        bool away = false;
        void want(const File &f, std::uint32_t from = 0)
        {
            if (arena.size() < f.bytes.size())
                arena.resize(f.bytes.size());
            taking.wantDirect(AirStream::MapFull, f.name, arena.data(), from);
        }
        void resumeFrom(const File &f)
        {
            const std::uint32_t from = taking.base;
            want(f, from);
        }
    };

    /// `frames` of the host's: up to `perFrame` pieces, each heard or lost by
    /// each console, and each console's answer heard or lost by the host. How
    /// many pieces were sent, of each stream, are added up in `sent`.
    void air(DemoServer &server, const std::vector<const File *> &files, std::vector<Console *> &consoles, int frames,
             int perFrame = 3, int *sent = nullptr)
    {
        std::uint8_t frame[kNetCmdBytes];
        std::uint8_t reply[kNetReplyBytes];
        for (int f = 0; f < frames; ++f)
        {
            server.frame();
            for (int p = 0; p < perFrame; ++p)
            {
                int stream;
                std::uint32_t index;
                if (!server.next(stream, index))
                    break;
                const AirSender &sender = server.streams[stream].air;
                const File *of = nullptr;
                for (const File *one : files)
                    of = one->map == server.streams[stream].map ? one : of;
                const int count = sender.pieceBytes(index);
                const int size = sender.piece(index, of->bytes.data() + index * kAirPiece, count, frame);
                if (sent != nullptr)
                    ++*sent;
                AirHead head;
                check(airRead(frame, size, head), "a piece reads back");
                for (Console *c : consoles)
                {
                    if (!c->away && roll() >= c->loss)
                        c->taking.frame(head);
                }
            }
            for (Console *c : consoles)
            {
                if (!c->away && c->taking.stream != AirStream::None && roll() >= c->loss)
                    server.heard(c->seat, reply, c->taking.ask(reply));
            }
        }
    }

    bool same(const Console &c, const File &f)
    {
        return c.taking.done() && std::memcmp(c.arena.data(), f.bytes.data(), f.bytes.size()) == 0 &&
               c.taking.crc == crc32(c.arena.data(), static_cast<std::uint32_t>(f.bytes.size()));
    }

    void testRefusal()
    {
        check(netDemoRefusal(true) == nullptr, "a console in DSi mode may be sent it");
        check(std::strcmp(netDemoRefusal(false), "Needs a DSi or 3DS") == 0, "a console in DS mode is told why not");
    }

    void testArena()
    {
        DemoArena arena;
        check(arena.plan(8000, 1000, 2000), "planned");
        check(arena.count == 3 && arena.placeBytes == 2000 && arena.offsetOf(1) == 3000, "three places, past what is kept");
        DemoArena none;
        check(!none.plan(1500, 1000, 2000), "no room for one: says so");
        DemoArena two;
        check(two.plan(5000, 500, 2000) && two.count == 2, "room for two: two");

        const std::uint32_t a = 1, b = 2, c = 3, d = 4;
        using Role = DemoArena::Role;
        check(arena.take(a, 1500, Role::Current) == 0, "the map played");
        check(arena.take(b, 1500, Role::Next) == 1, "the next");
        check(arena.take(c, 3000, Role::Next) == -1, "too long for a place: no room");
        check(arena.take(b, 1500, Role::Next) == 1, "asked again: where it is");
        arena.arrived(a);
        arena.arrived(b);
        check(arena.isWhole(a) && arena.find(c) < 0, "what has come");
        // On to the next: what was played is kept to go back to.
        check(arena.changeTo(b, 1500) == 1, "on to the next: it is there");
        check(arena.places[1].role == Role::Current && arena.places[0].role == Role::Previous && arena.isWhole(b),
              "... the map played, and the last the one come from");
        // Back: at once, both still whole.
        check(arena.changeTo(a, 1500) == 0 && arena.isWhole(a) && arena.places[1].role == Role::Previous, "and back");
        check(arena.changeTo(b, 1500) == 1, "forward again");
        // What is given up: free first, then the one come from; never the map played.
        check(arena.take(c, 1500, Role::Next) == 2, "the next goes in what is free");
        arena.arrived(c);
        check(arena.take(d, 1500, Role::Next) == 0 && arena.find(a) < 0, "room for another next: the one come from gives way");
        const std::uint32_t e = 5;
        check(arena.take(e, 1500, Role::Next) == -1, "... but no next puts out a next, nor the map played");
        check(arena.changeTo(c, 1500) == 2 && arena.places[2].role == Role::Current, "on to it");
        check(arena.places[1].role == Role::Previous && arena.places[0].role == Role::Spare, "... the next not taken is spare");
        check(arena.take(e, 1500, Role::Next) == 0 && arena.find(b) == 1, "... and gives way before the one come from");
        check(arena.take(d, 1500, Role::Next) == 1 && arena.find(c) == 2 && arena.take(7, 1500, Role::Next) == -1,
              "the map played is never given up");
        arena.spoiled(c);
        check(!arena.isWhole(c), "a stream that came wrong is to be had again");

        // With two places: going on gives up the map come from; back, it is asked for again.
        two.take(a, 1500, Role::Current);
        two.arrived(a);
        two.take(b, 1500, Role::Next);
        two.arrived(b);
        two.changeTo(b, 1500);
        check(two.find(a) == 0, "two places: it stays until wanted");
        check(two.take(c, 1500, Role::Next) == 0 && two.find(a) < 0, "... and gives way to the next");
        check(two.changeTo(a, 1500) == 0 && !two.isWhole(a) && two.find(c) < 0, "going back, it is to be had again");
    }

    void testPrefetch()
    {
        DemoArena arena;
        arena.plan(8000, 0, 2000);
        const std::uint32_t links[3] = {nameHash("next"), nameHash("other"), nameHash("back")};
        check(netDemoPrefetch(arena, links, 3) == links[0], "the first way out first");
        arena.take(links[0], 100, DemoArena::Role::Next);
        check(netDemoPrefetch(arena, links, 3) == links[1], "what is coming is not asked for again");
        arena.arrived(links[0]);
        arena.take(links[1], 100, DemoArena::Role::Spare);
        check(netDemoPrefetch(arena, links, 3) == links[2], "... nor what is held");
        arena.take(links[2], 100, DemoArena::Role::Spare);
        check(netDemoPrefetch(arena, links, 3) == 0, "all held: nothing");
        check(netDemoPrefetch(arena, links, 0) == 0, "the last map: nothing");
    }

    void testSaidAgain()
    {
        // A console that repeats what it wants keeps its place in the stream.
        DemoServer server;
        server.want(7, 1, 111, false, 0, "a", 60000, 1, 1);
        AirSender &air = server.streams[0].air;
        air.wants[1].base = 40;
        air.wants[1].have = 5;
        server.want(7, 1, 111, true, 40, "a", 60000, 1, 1);
        check(air.wants[1].base == 40 && air.wants[1].have == 5, "said again: what it holds is still known");
        check(server.streamIsLive(0), "... and it may now be wanted now");
    }

    void testOnePlace()
    {
        // Room for the map played alone: nothing got ready, and the next
        // takes its place when it is gone to.
        DemoArena arena;
        check(arena.plan(2500, 200, 2000), "one place fits");
        check(arena.count == 1, "only one");
        const std::uint32_t here = nameHash("here"), next = nameHash("next");
        check(arena.take(here, 0, DemoArena::Role::Current) == 0, "the map played has it");
        check(arena.take(next, 0, DemoArena::Role::Next) < 0, "the next has no room while it is played");
        arena.arrived(here);
        check(arena.changeTo(next, 0) == 0, "gone to: it takes the place");
        check(arena.find(here) < 0 && !arena.isWhole(next), "the old map is gone, the new one to come");
    }

    void testOneStream()
    {
        const File f = file("hldemo1", 40 * kAirPiece + 123, 1);
        DemoServer server;
        Console c{100, 1};
        c.want(f);
        server.want(c.token, c.seat, f.map, true, 0, f.name, static_cast<std::uint32_t>(f.bytes.size()), f.crc, f.serve);
        std::vector<Console *> consoles{&c};
        c.loss = 20;
        air(server, {&f}, consoles, 300);
        check(same(c, f), "a stream, straight into memory, whole and right through 20% loss");
        // (A piece come wrong is what the checksum is for.)
        c.arena[17] ^= 1;
        check(c.taking.crc != crc32(c.arena.data(), static_cast<std::uint32_t>(f.bytes.size())), "a bad stream is known");
        server.frame();
        check(!server.streams[0].on, "all had: the stream stops");
        c.arena[17] ^= 1;
        // Had again from the start.
        c.want(f);
        check(c.taking.base == 0 && !c.taking.done(), "... to be had again from the start");
    }

    void testResume()
    {
        const File f = file("hldemo2", 50 * kAirPiece, 2);
        DemoServer server;
        Console c{100, 1};
        c.want(f);
        server.want(c.token, c.seat, f.map, true, 0, f.name, static_cast<std::uint32_t>(f.bytes.size()), f.crc, f.serve);
        std::vector<Console *> consoles{&c};
        int sent = 0;
        air(server, {&f}, consoles, 8, 3, &sent);
        check(c.taking.base > 5 && !c.taking.done(), "part of it has come");
        // The link goes; it comes back on another seat, and takes it up where it stopped.
        c.away = true;
        server.lost(c.token);
        air(server, {&f}, consoles, 20);
        const std::uint32_t had = c.taking.base;
        check(had > 0, "what has come is kept");
        c.away = false;
        c.seat = 3;
        c.resumeFrom(f);
        server.want(c.token, c.seat, f.map, true, had, f.name, static_cast<std::uint32_t>(f.bytes.size()), f.crc, f.serve);
        check(server.receiverOf(c.token)->seat == 3, "who they are goes by their token, not their seat");
        int more = 0;
        air(server, {&f}, consoles, 40, 3, &more);
        check(same(c, f), "taken up where it stopped, on another seat: whole and right");
        check(sent + more <= 50 + 12, "and what had come was not sent again");
    }

    void testLiveFirst()
    {
        const File a = file("a", 30 * kAirPiece, 3), b = file("b", 30 * kAirPiece, 4), c = file("c", 30 * kAirPiece, 5),
                   d = file("d", 20 * kAirPiece, 6);
        DemoServer server;
        const auto ask = [&](std::uint16_t token, int seat, const File &f, bool live, std::uint32_t from = 0)
        { server.want(token, seat, f.map, live, from, f.name, static_cast<std::uint32_t>(f.bytes.size()), f.crc, f.serve); };
        Console ca{1, 1}, cb{2, 2}, cc{3, 3}, cd{4, 4};
        ca.want(a);
        cb.want(b);
        cc.want(c);
        cd.want(d);
        ask(1, 1, a, false);
        ask(2, 2, b, false);
        ask(3, 3, c, true);
        std::vector<Console *> consoles{&ca, &cb, &cc};
        // What is wanted now has the air until it can take no more.
        int stream;
        std::uint32_t index;
        check(server.next(stream, index) && server.streams[stream].map == c.map, "what is wanted now goes first");
        server.streams[stream].air.cursor = 0; // (only looked at: not sent)
        air(server, {&a, &b, &c, &d}, consoles, 10, 1);
        check(cc.taking.base >= 8 && ca.taking.base == 0 && cb.taking.base == 0, "... before what is wanted for later");
        // All three streams busy: a live request puts out one for later, which goes on after.
        ask(4, 4, d, true);
        check(server.receiverOf(4)->stream >= 0, "a live request takes the place of one for later");
        int waiting = 0;
        for (const std::uint16_t token : {1, 2})
            waiting += server.receiverOf(token)->stream < 0 ? 1 : 0;
        check(waiting == 1, "... which waits");
        consoles.push_back(&cd);
        air(server, {&a, &b, &c, &d}, consoles, 300);
        check(same(ca, a) && same(cb, b) && same(cc, c) && same(cd, d), "and everyone has all of theirs in the end");
        // All live and full: the fourth live request waits its turn.
        DemoServer full;
        const auto fill = [&](std::uint16_t token, const File &f)
        { full.want(token, token, f.map, true, 0, f.name, static_cast<std::uint32_t>(f.bytes.size()), f.crc, f.serve); };
        fill(1, a);
        fill(2, b);
        fill(3, c);
        fill(4, d);
        check(full.receiverOf(4)->stream < 0, "no stream to spare: it waits");
    }

    void testSharing()
    {
        const File f = file("shared", 60 * kAirPiece, 7);
        DemoServer server;
        Console slow{1, 1}, quick{2, 2};
        slow.want(f);
        quick.want(f);
        slow.loss = 50;
        quick.loss = 5;
        const auto ask = [&](const Console &c)
        { server.want(c.token, c.seat, f.map, true, 0, f.name, static_cast<std::uint32_t>(f.bytes.size()), f.crc, f.serve); };
        ask(slow);
        ask(quick);
        int streams = 0;
        for (const DemoServer::Stream &s : server.streams)
            streams += s.on ? 1 : 0;
        check(streams == 1, "two who want one map share one stream");
        std::vector<Console *> consoles{&slow, &quick};
        air(server, {&f}, consoles, 600);
        check(same(slow, f) && same(quick, f), "... each at its own pace, through its own losses");
        // One who leaves is nothing to the other.
        DemoServer other;
        Console stays{1, 1}, goes{2, 2};
        stays.want(f);
        goes.want(f);
        for (const Console *c : {&stays, &goes})
            other.want(c->token, c->seat, f.map, true, 0, f.name, static_cast<std::uint32_t>(f.bytes.size()), f.crc, f.serve);
        std::vector<Console *> both{&stays, &goes};
        air(other, {&f}, both, 5);
        other.leave(goes.token);
        goes.away = true;
        air(other, {&f}, both, 200);
        check(same(stays, f), "who leaves costs the other nothing");
        check(other.receiverOf(goes.token) == nullptr, "... and nothing is kept of them");
    }
} // namespace

int main()
{
    // The demos' maps, the first where each begins.
    check(std::strcmp(netDemoMaps(kDemoUplink)[0], "hldemo1") == 0, "Uplink begins on hldemo1");
    check(netDemoMaps(kDemoUplink)[3] == nullptr, "Uplink has three maps");
    check(std::strcmp(netDemoMaps(kDemoHazard)[0], "t0a0") == 0, "the Hazard Course begins on t0a0");
    check(std::strcmp(netDemoMaps(kDemoHazard)[kDemoMapsMost - 1], "t0a0d") == 0, "the Hazard Course ends on t0a0d");
    testRefusal();
    testArena();
    testPrefetch();
    testOnePlace();
    testSaidAgain();
    testOneStream();
    testResume();
    testLiveFirst();
    testSharing();
    if (g_failures == 0)
        std::printf("test_demo: ok\n");
    return g_failures == 0 ? 0 : 1;
}
