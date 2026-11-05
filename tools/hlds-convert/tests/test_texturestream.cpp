// Which textures are in video memory (shared/HlTextureStream.hpp): the arena,
// the pinned, the queue, and who makes way for whom.

#include <cstdio>
#include <vector>

#include "HlTextureStream.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    using Arena = ByteArena<64>;

    StreamInfo info(std::uint32_t texel, std::uint32_t palette = 512, bool streamed = true)
    {
        StreamInfo i;
        i.texelBytes = texel;
        i.paletteBytes = palette;
        i.stageBytes = texel + palette;
        i.streamed = streamed;
        return i;
    }

    /// No two blocks of an arena overlap, and all of them are inside it.
    template <class A>
    bool sound(const A &a)
    {
        std::uint32_t end = 0, used = 0;
        for (int i = 0; i < a.n; ++i)
        {
            if (a.b[i].off < end || a.b[i].off + a.b[i].size > a.cap || (a.b[i].off & 31) || (a.b[i].size & 31))
                return false;
            end = a.b[i].off + a.b[i].size;
            used += a.b[i].size;
        }
        return used == a.used;
    }

    /// What the console does with a plan: the loads are written (a frame
    /// later in the real thing; at once here).
    int step(TextureStreamer &s, std::uint32_t budget = 1u << 20, std::uint32_t stage = 1u << 20)
    {
        StreamLoad loads[16];
        int evicted[32], nEvicted = 0;
        const int n = s.plan(budget, stage, loads, 16, evicted, 32, nEvicted);
        for (int i = 0; i < n; ++i)
            s.loaded(loads[i].id);
        return n;
    }

    void testArena()
    {
        Arena a;
        a.init(1024);
        const int x = a.alloc(100, 1); // rounds to 128
        const int y = a.alloc(200, 2); // 224
        const int z = a.alloc(300, 3); // 320
        check(x == 0 && y == 1 && z == 2 && a.used == 128 + 224 + 320 && sound(a), "blocks go one after another, rounded");
        check(a.b[1].off == 128 && a.b[2].off == 352, "at the first place there is");
        a.free(a.find(2));
        check(a.fit(200) == 128, "a freed gap is the first fit");
        check(a.alloc(200, 4) >= 0 && a.b[a.find(4)].off == 128, "and is used");
        check(a.alloc(2000, 5) == -1, "too big a block is refused");
        check(a.alloc(0, 6) == -1, "an empty one too");
        const std::uint32_t left = a.cap - a.used;
        check(a.fit(left) >= 0 && a.fit(left + 32) == -1, "the last of the room can be had, not more");

        Arena b;
        b.init(256);
        b.alloc(96, 1);
        b.alloc(96, 2);
        b.alloc(32, 3);
        bool evictable[3] = {true, false, false};
        check(!b.fitsIfEvicted(128, evictable), "a gap that takes the first only is not enough for 128 with the rest in the way");
        evictable[1] = true;
        check(b.fitsIfEvicted(192, evictable), "two neighbours gone make room for 192");
    }

    void testEverythingFits()
    {
        std::vector<StreamInfo> tex;
        for (int i = 0; i < 6; ++i)
            tex.push_back(info(1000));
        tex.push_back(info(5000, 512, false)); // not the planner's
        TextureStreamer s;
        s.init(tex.data(), static_cast<int>(tex.size()), 8 * 1024, 8 * 1024);
        check(s.allFit(), "six textures of a kilobyte fit in eight");
        std::uint32_t t = 0, p = 0;
        bool all = true;
        for (int i = 0; i < 6; ++i)
        {
            all = all && s.pin(i, t, p);
            s.loaded(i);
        }
        check(all && s.residentCount() == 6, "all of them are pinned and resident");
        s.clearScores();
        for (int i = 0; i < 6; ++i)
            s.addScore(i, 100);
        s.rebuild();
        check(step(s) == 0 && s.loads() == 0 && s.evictions() == 0, "nothing is left to load or to evict");
        check(sound(s.texelArena()) && sound(s.paletteArena()), "and the areas are sound");

        TextureStreamer big;
        tex.push_back(info(4000));
        big.init(tex.data(), static_cast<int>(tex.size()), 8 * 1024, 8 * 1024);
        check(!big.allFit(), "seven do not");
    }

    void testWantedComeInBestFirst()
    {
        std::vector<StreamInfo> tex(10, info(1024));
        TextureStreamer s;
        s.init(tex.data(), 10, 4 * 1024, 8 * 1024); // room for four
        s.clearScores();
        for (int i = 0; i < 10; ++i)
            s.addScore(i, 10 + i); // 9 is wanted most
        s.rebuild();
        const int n = step(s);
        check(n == 4, "four come in: that is all the room there is");
        check(s.resident(9) && s.resident(8) && s.resident(7) && s.resident(6) && !s.resident(5), "the four most wanted");
        check(s.evictions() == 0, "with nothing to make way for them");
    }

    void testMakingWay()
    {
        std::vector<StreamInfo> tex(8, info(1024));
        TextureStreamer s;
        s.init(tex.data(), 8, 4 * 1024, 8 * 1024);
        s.clearScores();
        for (int i = 0; i < 4; ++i)
            s.addScore(i, 100);
        s.rebuild();
        step(s);
        check(s.residentCount() == 4, "four in");

        // Wanted nearly as much as what is here: nothing makes way (or two
        // textures would swap places for ever).
        s.clearScores();
        for (int i = 0; i < 4; ++i)
            s.addScore(i, 100);
        s.addScore(4, 150);
        s.rebuild();
        step(s);
        check(!s.resident(4) && s.evictions() == 0, "a score under double the others' makes nobody leave");
        check(s.state(4) == StreamState::Failed, "and is let alone until the next demand pass");
        s.retryFailed();
        check(s.state(4) == StreamState::Absent, "which tries it again");

        // Wanted more than twice as much: the least wanted goes.
        s.clearScores();
        s.addScore(0, 100);
        s.addScore(1, 30);
        s.addScore(2, 90);
        s.addScore(3, 80);
        s.addScore(4, 500);
        s.rebuild();
        StreamLoad loads[4];
        int evicted[8], n = 0;
        const int made = s.plan(1u << 20, 1u << 20, loads, 4, evicted, 8, n);
        check(made == 1 && loads[0].id == 4, "the one asked for is loaded");
        check(n == 1 && evicted[0] == 1 && s.state(1) == StreamState::Absent, "and the least wanted made way");
        s.loaded(4);
        check(s.resident(4) && s.residentCount() == 4 && sound(s.texelArena()) && sound(s.paletteArena()), "still four, and sound");
        check(loads[0].texelOffset == 1024 && loads[0].paletteOffset == 512, "in the place the other left");
    }

    void testPinnedAreSafe()
    {
        std::vector<StreamInfo> tex(6, info(1024));
        TextureStreamer s;
        s.init(tex.data(), 6, 3 * 1024, 8 * 1024);
        std::uint32_t t = 0, p = 0;
        check(s.pin(0, t, p) && s.pin(1, t, p), "two are pinned");
        s.loaded(0);
        s.loaded(1);
        s.clearScores();
        s.addScore(2, 5);
        s.addScore(3, 100000);
        s.addScore(4, 100000);
        s.rebuild();
        step(s);
        check(s.resident(0) && s.resident(1), "the pinned are never evicted");
        check((s.resident(3) ? 1 : 0) + (s.resident(4) ? 1 : 0) + (s.resident(2) ? 1 : 0) == 1, "one place left, and one takes it");
        s.evictAll();
        check(s.resident(0) && s.resident(1) && s.residentCount() == 2, "evict-all leaves the pinned");
    }

    void testWhatCannotFit()
    {
        std::vector<StreamInfo> tex;
        tex.push_back(info(100000)); // bigger than the whole area
        tex.push_back(info(1024));
        tex.push_back(info(1024));
        TextureStreamer s;
        s.init(tex.data(), 3, 4 * 1024, 8 * 1024);
        s.clearScores();
        s.addScore(0, 1000);
        s.addScore(1, 10);
        s.addScore(2, 10);
        s.rebuild();
        step(s);
        check(!s.resident(0) && s.state(0) == StreamState::Failed, "a texture bigger than the area is never loaded");
        check(s.resident(1) || s.resident(2), "and does not stop the others (it takes one of the two tries)");
        s.rebuild();
        step(s);
        check(s.resident(1) && s.resident(2), "which come in the next time");

        // Palettes can be the short thing.
        std::vector<StreamInfo> pal(4, info(512, 4096));
        TextureStreamer q;
        q.init(pal.data(), 4, 16 * 1024, 8 * 1024); // two palettes' worth
        q.clearScores();
        for (int i = 0; i < 4; ++i)
            q.addScore(i, 10 + i);
        q.rebuild();
        step(q);
        check(q.residentCount() == 2 && q.resident(3) && q.resident(2), "short of palette, the most wanted two come in");
        q.clearScores();
        q.addScore(0, 1000);
        q.retryFailed(); // (a new demand pass: the ones that did not fit are tried again)
        q.rebuild();
        step(q);
        check(q.resident(0) && q.residentCount() == 2 && sound(q.paletteArena()), "and a palette's room is made by evicting");

        // A read that fails gives its room back.
        TextureStreamer f;
        f.init(pal.data(), 4, 16 * 1024, 8 * 1024);
        f.clearScores();
        f.addScore(1, 50);
        f.rebuild();
        StreamLoad l[2];
        int e[2], n = 0;
        check(f.plan(1u << 20, 1u << 20, l, 2, e, 2, n) == 1, "one load");
        f.failed(1);
        check(f.texelBytesUsed() == 0 && f.paletteBytesUsed() == 0 && f.state(1) == StreamState::Failed, "a failed read gives its room back");
    }

    void testBudgets()
    {
        std::vector<StreamInfo> tex(10, info(1024));
        TextureStreamer s;
        s.init(tex.data(), 10, 16 * 1024, 16 * 1024);
        s.clearScores();
        for (int i = 0; i < 10; ++i)
            s.addScore(i, 100 - i);
        s.rebuild();
        StreamLoad loads[16];
        int e[4], n = 0;
        // 1536 bytes a texture to stage: a budget of 4000 takes three (the
        // check is made before each, so it can be passed by the last).
        const int made = s.plan(4000, 1u << 20, loads, 16, e, 4, n);
        check(made == 3, "the byte budget ends the plan once it is spent");
        // Staging for only two more.
        const int staged = s.plan(1u << 20, 3100, loads, 16, e, 4, n);
        check(staged == 2, "so does what is left of the staging buffer");
        check(s.plan(1u << 20, 100, loads, 16, e, 4, n) == 0, "none that does not fit it");
        // The one that did not fit the staging stays at the head of the queue.
        const int next = s.plan(1u << 20, 1u << 20, loads, 16, e, 4, n);
        check(next >= 1 && loads[0].id == 5, "and is the next to come");
    }

    // Random demand against a small area, over and over: whatever happens,
    // the areas are sound, nothing pinned leaves, and every resident texture
    // has a block of its own.
    void testStress()
    {
        std::uint32_t seed = 12345;
        auto rnd = [&]()
        {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 8;
        };
        std::vector<StreamInfo> tex;
        for (int i = 0; i < 120; ++i)
            tex.push_back(info(512 * (1 + rnd() % 8), 128 * (1 + rnd() % 16)));
        TextureStreamer s;
        s.init(tex.data(), 120, 40 * 1024, 20 * 1024);
        std::uint32_t t = 0, p = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (s.pin(i, t, p))
                s.loaded(i);
        }
        bool ok = true, pinnedKept = true;
        int loadsSeen = 0;
        for (int round = 0; round < 3000 && ok; ++round)
        {
            if (round % 5 == 0)
            {
                s.clearScores();
                const int base = static_cast<int>(rnd() % 100);
                for (int k = 0; k < 30; ++k)
                    s.addScore(4 + (base + k) % 116, 1 + rnd() % 1000);
                s.retryFailed();
                s.rebuild();
            }
            StreamLoad loads[8];
            int evicted[16], nEvicted = 0;
            const int made = s.plan(4096, 8192, loads, 8, evicted, 16, nEvicted);
            loadsSeen += made;
            for (int i = 0; i < nEvicted; ++i)
                ok = ok && !s.pinned(evicted[i]) && !s.resident(evicted[i]);
            for (int i = 0; i < made; ++i)
                s.loaded(loads[i].id);
            ok = ok && sound(s.texelArena()) && sound(s.paletteArena());
            int resident = 0;
            for (int id = 0; id < 120; ++id)
            {
                const bool here = s.resident(id);
                resident += here ? 1 : 0;
                const bool placed = s.texelArena().find(id) >= 0 && s.paletteArena().find(id) >= 0;
                ok = ok && (here == placed);
                if (id < 4 && s.pinned(id) && !here)
                    pinnedKept = false;
            }
            ok = ok && resident == s.residentCount();
        }
        check(ok, "over three thousand rounds of changing demand the areas stay sound and in step with the states");
        check(pinnedKept, "and the pinned stay");
        check(loadsSeen > 100 && s.evictions() > 50, "with a good deal of coming and going");
    }

    // The memory says no to what the plan thought would fit: the plan is
    // corrected, and goes on within what it learned.
    void testRefusal()
    {
        std::vector<StreamInfo> tex(8, info(1024));
        TextureStreamer s;
        s.init(tex.data(), 8, 8 * 1024, 16 * 1024);
        s.clearScores();
        for (int i = 0; i < 8; ++i)
            s.addScore(i, 100 + i);
        s.rebuild();
        StreamLoad loads[8];
        int evicted[8], n = 0;
        const int made = s.plan(1u << 20, 1u << 20, loads, 8, evicted, 8, n);
        check(made == 8, "everything is planned to fit");
        for (int i = 0; i < 5; ++i)
            s.loaded(loads[i].id);
        // The last three would not go in.
        for (int i = 5; i < 8; ++i)
            s.failed(loads[i].id);
        s.shrinkCapacity();
        check(s.texelCapacity() == 5 * 1024, "the area is taken to hold what is in it");
        s.retryFailed();
        s.clearScores();
        s.addScore(loads[6].id, 50); // wanted, but less than twice what is here
        for (int i = 0; i < 5; ++i)
            s.addScore(loads[i].id, 100);
        s.rebuild();
        StreamLoad again[4];
        const int more = s.plan(1u << 20, 1u << 20, again, 4, evicted, 8, n);
        check(more == 0 && sound(s.texelArena()), "and nothing is planned beyond it");
        // Wanted much more: one makes way, and the area stays the size it was.
        s.retryFailed();
        s.clearScores();
        s.addScore(loads[6].id, 1000);
        for (int i = 0; i < 5; ++i)
            s.addScore(loads[i].id, 100);
        s.rebuild();
        const int swap = s.plan(1u << 20, 1u << 20, again, 4, evicted, 8, n);
        check(swap == 1 && n == 1 && s.texelCapacity() == 5 * 1024, "a much more wanted one takes a place, the area's size kept");
    }

    /// What makes way is always told: never more than the caller's list.
    void testEvictionsAllTold()
    {
        // Forty small residents; then one big texture wanted that needs
        // thirty of them gone.
        std::vector<StreamInfo> infos;
        for (int i = 0; i < 40; ++i)
            infos.push_back(info(1024, 32));
        infos.push_back(info(30 * 1024, 32));
        TextureStreamer s;
        s.init(infos.data(), static_cast<int>(infos.size()), 40 * 1024, 64 * 1024);
        s.clearScores();
        for (int i = 0; i < 40; ++i)
            s.addScore(i, 10);
        s.rebuild();
        while (step(s) > 0)
        {
        }
        check(s.residentCount() == 40, "forty little ones fill the area");
        s.clearScores();
        s.addScore(40, 1000000);
        s.rebuild();
        StreamLoad loads[4];
        int evicted[16], n = 0;
        const int made = s.plan(1u << 20, 1u << 20, loads, 4, evicted, 16, n);
        // Whatever the planner let go is exactly what it said it let go.
        int stillResident = 0;
        for (int i = 0; i < 40; ++i)
            stillResident += s.resident(i) ? 1 : 0;
        check(n <= 16 && stillResident + n == 40, "every texture that made way is in the list it was given, and no more");
        check(made == 0 && s.dropped() >= 1, "... and with the list full it stopped, without loading what could not yet fit");
        check(sound(s.texelArena()), "the area is as the plan says");
    }

    /// What is on the screen does not make way, however much another is
    /// wanted.
    void testRecentStay()
    {
        std::vector<StreamInfo> infos = {info(1024), info(1024), info(1024)};
        TextureStreamer s;
        s.init(infos.data(), 3, 2 * 1024, 64 * 1024);
        s.clearScores();
        s.addScore(0, 5);
        s.addScore(1, 5);
        s.rebuild();
        step(s);
        check(s.resident(0) && s.resident(1), "two fill the area");
        s.clearScores();
        s.addScore(0, 5);
        s.addScore(1, 5);
        s.setRecent(0);
        s.setRecent(1);
        s.addScore(2, 1000000);
        s.rebuild();
        StreamLoad loads[4];
        int evicted[8], n = 0;
        check(s.plan(1u << 20, 1u << 20, loads, 4, evicted, 8, n) == 0 && n == 0 && s.resident(0) && s.resident(1),
              "a texture on the screen is not let go for one wanted more");
        s.clearScores();
        s.addScore(0, 5);
        s.addScore(1, 5);
        s.setRecent(0);
        s.addScore(2, 1000000);
        s.retryFailed();
        s.rebuild();
        check(s.plan(1u << 20, 1u << 20, loads, 4, evicted, 8, n) == 1 && n == 1 && evicted[0] == 1,
              "... the one that is not on the screen is");
    }

    /// The room taken away by a refusal comes back.
    void testCapacityBack()
    {
        std::vector<StreamInfo> infos = {info(1024), info(1024), info(1024)};
        TextureStreamer s;
        s.init(infos.data(), 3, 8 * 1024, 64 * 1024);
        s.clearScores();
        s.addScore(0, 5);
        s.rebuild();
        step(s);
        s.shrinkCapacity();
        check(s.texelCapacity() == 1024, "a refusal: the area is taken to hold what is in it");
        s.setCapacity(6 * 1024, 64 * 1024);
        check(s.texelCapacity() == 6 * 1024, "the memory asked again: the room is back");
        s.setCapacity(0, 0);
        check(s.texelCapacity() == 1024 && s.paletteCapacity() == s.paletteBytesUsed(),
              "never less than what is in it");
    }

    /// Two pieces of memory as one area with a barrier between: no texture
    /// lies across it, and it never makes way.
    void testBarrier()
    {
        // Pieces of 3 KB and 2 KB: [0, 3072) barrier [3104, 5152).
        std::vector<StreamInfo> infos = {info(2048), info(2048), info(2048), info(1024)};
        TextureStreamer s;
        s.init(infos.data(), 4, 3072 + TextureStreamer::kBarrierBytes + 2048, 64 * 1024);
        check(s.addBarrier(3072), "the barrier goes where it is put");
        auto across = [&]()
        {
            const auto &a = s.texelArena();
            for (int i = 0; i < a.n; ++i)
            {
                if (a.b[i].owner < 0)
                    continue;
                if (a.b[i].off < 3072 + TextureStreamer::kBarrierBytes && a.b[i].off + a.b[i].size > 3072)
                    return true;
            }
            return false;
        };
        for (int round = 0; round < 50; ++round)
        {
            s.clearScores();
            for (int id = 0; id < 4; ++id)
                s.addScore(id, static_cast<std::uint32_t>(1 + (id * 7 + round * 13) % 97) * 1000);
            s.rebuild();
            step(s);
            check(!across(), "nothing lies across the barrier");
            check(sound(s.texelArena()), "the area stays sound");
            const auto &a = s.texelArena();
            bool there = false;
            for (int i = 0; i < a.n; ++i)
                there = there || (a.b[i].owner < 0 && a.b[i].off == 3072);
            check(there, "the barrier never makes way");
        }
        // Two of 2 KB fit (one in each piece); a third never does.
        check(s.residentCount() <= 3, "at most what the pieces hold");

        // The sums fit and the pieces do not: four of 1280 bytes, in 3 KB and
        // 2 KB, are three. Pinned, the fourth would be flat for good; with
        // none pinned, it takes the place of one not in view once it is.
        std::vector<StreamInfo> four = {info(1280), info(1280), info(1280), info(1280)};
        TextureStreamer f;
        f.init(four.data(), 4, 3072 + TextureStreamer::kBarrierBytes + 2048, 64 * 1024);
        f.addBarrier(3072);
        check(f.allFit(), "four of 1280 bytes add up to what two pieces of 3 KB and 2 KB hold");
        std::uint32_t t = 0, p = 0;
        int pinned = 0, left = -1;
        for (int id = 0; id < 4; ++id)
        {
            if (f.pin(id, t, p))
            {
                f.loaded(id);
                ++pinned;
            }
            else
            {
                left = id;
            }
        }
        check(pinned == 3 && left >= 0, "... but only three of them fit in the pieces");
        f.unpinAll();
        f.clearScores();
        for (int id = 0; id < 4; ++id)
            f.addScore(id, id == left ? 1u << 20 : 10u);
        f.rebuild();
        step(f);
        check(f.state(left) == StreamState::Staged || f.state(left) == StreamState::Resident,
              "... and the one left out, in view, comes in in the place of one that is not");
    }

    // A texture placed where another was let go waits two frames before it
    // is written; one placed in space free for longer is written at once.
    void testFreedWait()
    {
        // Room for five texels, palettes aplenty: four here, two wanted.
        std::vector<StreamInfo> tex(6, info(1024));
        TextureStreamer s;
        s.init(tex.data(), 6, 5 * 1024, 16 * 1024);
        s.setClock(10);
        s.clearScores();
        for (int i = 0; i < 4; ++i)
            s.addScore(i, 1);
        s.rebuild();
        StreamLoad loads[8];
        int evicted[8], n = 0;
        int made = s.plan(1u << 20, 1u << 20, loads, 8, evicted, 8, n);
        bool now = made == 4;
        for (int i = 0; i < made; ++i)
        {
            now = now && s.readyAt(loads[i]) == 10;
            s.loaded(loads[i].id);
        }
        check(now, "into an empty area: written at once");

        s.setClock(20);
        s.clearScores();
        s.addScore(4, 1u << 21); // the fifth place, free from the start
        s.addScore(5, 1u << 20); // only where one of the four was
        s.rebuild();
        made = s.plan(1u << 20, 1u << 20, loads, 8, evicted, 8, n);
        check(made == 2 && n == 1 && loads[0].id == 4 && loads[1].id == 5, "one in free space, one making way");
        check(s.readyAt(loads[0]) == 20, "the one in space never used: at once");
        check(s.readyAt(loads[1]) == 22, "the one where a texture was let go: two frames on");
        const StreamLoad over = loads[1];
        s.setClock(21);
        check(s.readyAt(over) == 22, "a frame on, still the same frame");
        s.setClock(22);
        check(s.readyAt(over) == 22, "and then now");
        s.setClock(23);
        check(s.readyAt(over) == 23, "forgotten after");

        // Texels in free space, the palette where one was let go: it waits.
        std::vector<StreamInfo> pal(3, info(1024, 512));
        TextureStreamer q;
        q.init(pal.data(), 3, 8 * 1024, 1024);
        q.setClock(5);
        q.clearScores();
        q.addScore(0, 1);
        q.addScore(1, 1);
        q.rebuild();
        made = q.plan(1u << 20, 1u << 20, loads, 8, evicted, 8, n);
        for (int i = 0; i < made; ++i)
            q.loaded(loads[i].id);
        q.setClock(6);
        q.clearScores();
        q.addScore(2, 1000);
        q.rebuild();
        made = q.plan(1u << 20, 1u << 20, loads, 8, evicted, 8, n);
        check(made == 1 && n == 1 && q.readyAt(loads[0]) == 8, "a palette's place counts as much as the texels'");

        // More let go at once than is remembered: nothing is written early.
        std::vector<StreamInfo> small(82, info(32, 32));
        small[80] = info(40 * 32, 32);
        small[81] = info(32, 2048); // (its colours too big for the gap: in free space as well)
        TextureStreamer r;
        r.init(small.data(), 82, 80 * 32 + 32, 16 * 1024);
        r.setClock(100);
        r.clearScores();
        for (int i = 0; i < 80; ++i)
            r.addScore(i, 1);
        r.rebuild();
        for (int round = 0; round < 20; ++round)
        {
            made = r.plan(1u << 20, 1u << 20, loads, 8, evicted, 8, n);
            for (int i = 0; i < made; ++i)
                r.loaded(loads[i].id);
            r.rebuild();
        }
        check(r.residentCount() == 80, "eighty small ones in");
        r.setClock(200);
        r.clearScores();
        r.addScore(80, 1u << 20);
        r.addScore(81, 1u << 10);
        r.rebuild();
        int many[64];
        made = r.plan(1u << 20, 1u << 20, loads, 8, many, 64, n);
        check(made == 2 && n == 40 && loads[0].id == 80 && loads[1].id == 81,
              "the big one makes forty make way, the last small one goes in the free end");
        check(r.readyAt(loads[0]) == 202 && r.readyAt(loads[1]) == 202,
              "eighty pieces let go: past what is remembered, everything waits");
        r.setClock(202);
        check(r.readyAt(loads[1]) == 202, "two frames on, both may be written");
    }

    // Plans made every frame while earlier loads still wait to be written:
    // whatever is written is written where nothing was let go less than two
    // frames before (followed here apart from the planner's own record).
    void testPipelined()
    {
        std::uint32_t seed = 777;
        auto rnd = [&]()
        {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 8;
        };
        constexpr int kCount = 100;
        std::vector<StreamInfo> tex;
        for (int i = 0; i < kCount; ++i)
            tex.push_back(info(512 * (1 + rnd() % 8), 128 * (1 + rnd() % 8)));
        TextureStreamer s;
        s.init(tex.data(), kCount, 24 * 1024, 8 * 1024);
        struct Place
        {
            std::uint32_t t, tSize, p, pSize;
        };
        struct Gone
        {
            Place at;
            std::uint32_t frame;
        };
        struct Waiting
        {
            StreamLoad load;
            std::uint32_t ready;
        };
        std::vector<Place> where(kCount);
        std::vector<Gone> gone;
        std::vector<Waiting> waiting;
        bool safe = true, sound2 = true;
        int written = 0, atOnce = 0, held = 0;
        for (std::uint32_t frame = 1; frame < 4000; ++frame)
        {
            // (The console's upload: what may be written now is, and is drawn.)
            for (std::size_t k = 0; k < waiting.size();)
            {
                if (static_cast<std::int32_t>(frame - waiting[k].ready) < 0)
                {
                    ++k;
                    continue;
                }
                const Place &a = where[waiting[k].load.id];
                for (const Gone &g : gone)
                {
                    if (frame - g.frame >= TextureStreamer::kFreedFrames)
                        continue;
                    const bool t = g.at.t < a.t + a.tSize && a.t < g.at.t + g.at.tSize;
                    const bool p = g.at.p < a.p + a.pSize && a.p < g.at.p + g.at.pSize;
                    safe = safe && !t && !p;
                }
                s.loaded(waiting[k].load.id);
                ++written;
                waiting.erase(waiting.begin() + static_cast<long>(k));
            }
            // (The console's update: this frame's plan, as earlier loads wait.)
            s.setClock(frame);
            if (frame % 7 == 0)
            {
                s.clearScores();
                const int base = static_cast<int>(rnd() % kCount);
                for (int k = 0; k < 25; ++k)
                    s.addScore((base + k) % kCount, 1 + rnd() % 1000);
                s.retryFailed();
            }
            s.rebuild();
            StreamLoad loads[8];
            int evicted[16], nEvicted = 0;
            const int made = s.plan(4096, 32 * 1024, loads, 8, evicted, 16, nEvicted);
            for (int k = 0; k < nEvicted; ++k)
                gone.push_back({where[evicted[k]], frame});
            for (int k = 0; k < made; ++k)
            {
                const StreamLoad &l = loads[k];
                where[l.id] = {l.texelOffset, ByteArena<kStreamMaxBlocks>::round(tex[l.id].texelBytes),
                               l.paletteOffset, ByteArena<kStreamMaxBlocks>::round(tex[l.id].paletteBytes)};
                const std::uint32_t ready = s.readyAt(l);
                (ready == frame ? atOnce : held) += 1;
                waiting.push_back({l, ready});
            }
            sound2 = sound2 && sound(s.texelArena()) && sound(s.paletteArena());
        }
        check(safe, "four thousand frames of plans in flight: nothing written over what was let go two frames before");
        check(sound2, "and the areas stay sound");
        check(written > 200 && atOnce > 20 && held > 20, "with loads both written at once and held back");
    }

    // The textures the eye's leaf can see, found a piece a frame.
    void testPvsWalk()
    {
        struct L
        {
            std::uint16_t firstMark, markCount;
        };
        struct S
        {
            std::uint16_t texture;
        };
        // Leaf 0 sees nothing (the solid one); 1 has surfaces 0 and 1, 2 has
        // 2, 3 has 3 and 4.
        const L leaves[4] = {{0, 0}, {0, 2}, {2, 1}, {3, 2}};
        const std::uint16_t marks[5] = {0, 1, 2, 3, 4};
        const S surfaces[5] = {{5}, {0xFFFF}, {7}, {5}, {9}};
        bool seen[4] = {false, true, false, true};
        auto visible = [&](int leaf) { return seen[leaf]; };

        PvsTextureWalk w;
        w.reset(10);
        check(!w.busy() && w.wanted()[5] == 0, "nothing wanted before a walk");
        w.start(4);
        // Two surfaces a step: a leaf is never cut, so leaf 1 then leaf 3.
        int steps = 0;
        while (!w.step(leaves, marks, surfaces, visible, 2, 64) && steps < 10)
        {
            ++steps;
            check(w.wanted()[5] == 0, "nothing new until the walk is done");
        }
        check(steps == 1, "the walk goes as far as its budget, and on the next frame");
        const std::uint8_t *want = w.wanted();
        check(want[5] && want[9] && !want[7] && !want[0], "the textures of the leaves seen, and only those");
        check(!w.busy() && !w.step(leaves, marks, surfaces, visible, 2, 64), "and then nothing more to do");

        // The eye moves on: the old set stands until the new one is done.
        seen[1] = seen[3] = false;
        seen[2] = true;
        w.start(4);
        w.step(leaves, marks, surfaces, visible, 100, 1);
        check(w.wanted()[5] && !w.wanted()[7], "half way, the leaves seen before still say");
        while (!w.step(leaves, marks, surfaces, visible, 100, 1))
        {
        }
        check(!w.wanted()[5] && w.wanted()[7], "done, the new leaves do");

        // A texture number past the map's: left alone.
        const S wild[5] = {{12}, {12}, {12}, {12}, {12}};
        w.reset(10);
        seen[1] = true;
        w.start(4);
        while (!w.step(leaves, marks, wild, visible, 100, 100))
        {
        }
        bool none = true;
        for (int i = 0; i < kStreamMaxTextures; ++i)
            none = none && w.wanted()[i] == 0;
        check(none, "a texture the map has not is not wanted");
    }

    // What the eye's leaf can see is kept over what it cannot, and read
    // ahead without pushing out the rest of what it can see.
    void testPvsMakesWayLast()
    {
        std::vector<StreamInfo> tex(7, info(1024));
        TextureStreamer s;
        s.init(tex.data(), 7, 4 * 1024, 16 * 1024);
        s.clearScores();
        for (int i = 0; i < 4; ++i)
            s.addScore(i, 1);
        s.rebuild();
        step(s);
        // 0 and 1 in the PVS, 2 and 3 not; 4 in view, 5 and 6 in the PVS.
        s.clearScores();
        s.addScore(0, 300);
        s.addScore(1, 300);
        s.addScore(4, 1u << 20);
        s.addScore(5, 300);
        s.addScore(6, 300);
        s.rebuild();
        StreamLoad loads[8];
        int evicted[8], n = 0;
        const int made = s.plan(1u << 20, 1u << 20, loads, 8, evicted, 8, n);
        for (int i = 0; i < made; ++i)
            s.loaded(loads[i].id);
        check(s.resident(4) && s.resident(0) && s.resident(1), "the one in view comes in, and the PVS stays");
        check(!s.resident(2) && !s.resident(3), "what cannot be seen from here makes way first");
        check(s.resident(5) != s.resident(6), "one read ahead, in what was left");
        check(made == 2 && n == 2, "and no more: the PVS does not push out the PVS");
    }

    /// The palette memory of c2a5e on a DS as it was at frame 307 of a run
    /// where an in-view texture stayed flat for seconds (read off the
    /// console through the emulator's debugger, 2026-11-05): 39 palettes,
    /// 848 bytes free in four gaps, the four that may make way scattered.
    /// Texture 146 (c1a1_crate2, 128x128) was given 1024 bytes by the old
    /// rule, one colour per 16 texels, and could not be placed; its palette
    /// as cooked has 128 colours, 256 bytes, which fits a gap as it is.
    void testFrame307Palettes()
    {
        struct Block
        {
            std::uint32_t off, size;
            int owner;
            bool mayGo;
        };
        static const Block kLayout[] = {
            {0, 512, 42, false},      {512, 64, 2, false},      {576, 1024, 64, false},   {1600, 1024, 56, false},
            {2624, 1024, 36, false},  {3648, 1024, 37, false},  {4672, 1024, 72, false},  {5696, 512, 144, false},
            {6208, 512, 12, true},    {6720, 128, 61, false},   {6848, 1024, 65, false},  {7872, 1024, 60, false},
            {8896, 512, 28, false},   {9408, 256, 39, false},   {9664, 256, 40, false},   {9920, 512, 43, true},
            {10432, 256, 45, false},  {10688, 1024, 46, false}, {11712, 256, 53, true},   {11968, 1024, 34, false},
            {12992, 1024, 55, false}, {14016, 1024, 29, false}, {15040, 1024, 73, false}, {16064, 1024, 121, false},
            {17088, 512, 136, false}, {17600, 256, 35, false},  {17856, 32, 137, false},  {17952, 512, 68, false},
            {18464, 128, 97, false},  {18592, 512, 109, false}, {19264, 128, 71, true},   {19392, 1024, 126, false},
            {20416, 512, 139, false}, {20928, 512, 143, false}, {21440, 256, 145, false}, {21696, 512, 111, false},
            {22464, 1024, 63, false}, {23488, 1024, 141, false}, {24512, 1024, 142, false},
        };
        constexpr std::uint32_t kPaletteBytes = 25904;
        constexpr int kTextures = 168, kFlat = 146;
        auto build = [&](TextureStreamer &s, std::vector<StreamInfo> &tex, std::uint32_t flatPalette) {
            tex.assign(kTextures, info(32, 32));
            tex[kFlat] = info(4096, flatPalette);
            // The gaps, filled while the layout is made and then let go.
            std::vector<Block> order;
            std::uint32_t at = 0;
            int filler = 150;
            for (const Block &b : kLayout)
            {
                if (b.off > at)
                    order.push_back({at, b.off - at, filler++, false});
                order.push_back(b);
                at = b.off + b.size;
            }
            for (const Block &b : order)
                tex[b.owner] = info(32, b.size);
            s.init(tex.data(), kTextures, 1u << 20, kPaletteBytes);
            // In that order, first fit into empty memory: each where the dump had it.
            s.clearScores();
            for (std::size_t i = 0; i < order.size(); ++i)
                s.addScore(order[i].owner, static_cast<std::uint32_t>(100000 - i));
            s.rebuild();
            while (step(s) > 0)
            {
            }
            for (const Block &b : order)
                if (b.owner >= 150 && b.owner < filler)
                    s.failed(b.owner);
            // In view and drawn: the rest, and 146. Wanted a little: the four.
            s.clearScores();
            for (const Block &b : kLayout)
            {
                s.addScore(b.owner, b.mayGo ? 1000 : 1u << 20);
                if (!b.mayGo)
                    s.setRecent(b.owner);
            }
            s.addScore(kFlat, 1u << 20);
            s.setRecent(kFlat);
            s.rebuild();
        };

        {
            TextureStreamer s;
            std::vector<StreamInfo> tex;
            build(s, tex, 1024);
            bool same = s.paletteArena().n == static_cast<int>(sizeof(kLayout) / sizeof(kLayout[0])) &&
                        s.paletteBytesUsed() == kPaletteBytes - 848;
            for (int i = 0; same && i < s.paletteArena().n; ++i)
                same = s.paletteArena().b[i].off == kLayout[i].off && s.paletteArena().b[i].owner == kLayout[i].owner;
            check(same, "frame 307's palette memory is laid out again as the dump had it");
            StreamLoad loads[8];
            int evicted[16], n = 0;
            const int made = s.plan(1u << 20, 1u << 20, loads, 8, evicted, 16, n);
            check(made == 0 && n == 0 && !s.resident(kFlat),
                  "1024 bytes for its palette (one colour per 16 texels): it cannot be placed, nothing makes way");
        }
        {
            TextureStreamer s;
            std::vector<StreamInfo> tex;
            build(s, tex, 256);
            StreamLoad loads[8];
            int evicted[16], n = 0;
            const int made = s.plan(1u << 20, 1u << 20, loads, 8, evicted, 16, n);
            check(made == 1 && loads[0].id == kFlat && n == 0,
                  "its palette as cooked, 256 bytes: placed in a gap as things are, nothing made to go");
        }
    }
} // namespace

int main()
{
    testArena();
    testEverythingFits();
    testWantedComeInBestFirst();
    testMakingWay();
    testPinnedAreSafe();
    testWhatCannotFit();
    testBudgets();
    testStress();
    testRefusal();
    testEvictionsAllTold();
    testRecentStay();
    testCapacityBack();
    testBarrier();
    testFreedWait();
    testPipelined();
    testPvsWalk();
    testPvsMakesWayLast();
    testFrame307Palettes();
    if (g_failures == 0)
        std::printf("test_texturestream: ok\n");
    return g_failures == 0 ? 0 : 1;
}
