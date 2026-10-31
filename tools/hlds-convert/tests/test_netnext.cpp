// Which map follows a match (shared/HlNetNext.hpp): the rotation going round,
// what is left out, and the list being edited.

#include <cstdio>
#include <vector>

#include "HlNetNext.hpp"
#include "HlNetVote.hpp"
#include "HlPreview.hpp"

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

    NetMapInfo map(const char *name, bool fits, std::uint32_t need = 3000)
    {
        NetMapInfo info;
        info.name = name;
        info.guestFits = fits;
        info.guestNeed = fits ? need : 9000;
        return info;
    }

    const std::vector<NetMapInfo> kMaps = {map("aaa", true), map("bbb", false), map("ccc", true), map("ddd", true)};

    void testCandidates()
    {
        int out[8];
        check(netNextCandidates(kMaps, nameHash("aaa"), false, 0, out, 8) == 3 && out[0] == 1,
              "never the map just played");
        check(netNextCandidates(kMaps, nameHash("aaa"), true, 0, out, 8) == 2 && out[0] == 2 && out[1] == 3,
              "with guests, only what fits them");
        check(netNextCandidates(kMaps, 0, true, 10000, out, 8) == 4, "a DSi's room fits more");
        check(netNextCandidates(kMaps, 0, false, 0, out, 2) == 2, "no more than there is room for");
        const std::vector<NetMapInfo> one = {map("aaa", true)};
        check(netNextCandidates(one, nameHash("aaa"), false, 0, out, 8) == 0, "only the map just played: none");
    }

    void testRotation()
    {
        const std::uint32_t list[3] = {nameHash("aaa"), nameHash("ccc"), nameHash("ddd")};
        int played = 0;
        const int expect[6] = {2, 3, 0, 2, 3, 0};
        bool loops = true;
        for (int n = 0; n < 6; ++n)
        {
            const int next = netNextRotation(kMaps, list, 3, nameHash(kMaps[static_cast<std::size_t>(played)].name.c_str()), false, 0);
            loops = loops && next == expect[n];
            played = next;
        }
        check(loops, "the rotation goes round and round");
        check(netNextRotation(kMaps, list, 3, nameHash("bbb"), false, 0) == 0, "a map not on it: from the start");
        check(netNextRotation(kMaps, list, 0, nameHash("aaa"), false, 0) == -1, "an empty rotation: none");

        const std::uint32_t skips[3] = {nameHash("aaa"), nameHash("bbb"), nameHash("ccc")};
        check(netNextRotation(kMaps, skips, 3, nameHash("aaa"), true, 0) == 2, "one that does not fit is passed over");
        const std::uint32_t gone[2] = {nameHash("nope"), nameHash("ccc")};
        check(netNextRotation(kMaps, gone, 2, nameHash("aaa"), false, 0) == 2, "... and one no longer on the card");
        const std::uint32_t alone[1] = {nameHash("aaa")};
        check(netNextRotation(kMaps, alone, 1, nameHash("aaa"), false, 0) == -1, "never the same map twice running");
        const std::uint32_t twice[2] = {nameHash("aaa"), nameHash("aaa")};
        check(netNextRotation(kMaps, twice, 2, nameHash("aaa"), false, 0) == -1, "... even listed twice");
    }

    NetVote offered(int count)
    {
        NetVote vote;
        vote.count = static_cast<std::uint8_t>(count);
        vote.voters = 0x0F;
        return vote;
    }

    void testVote()
    {
        NetVote vote = offered(4);
        check(vote.cast(0, 2) && vote.cast(1, 2) && vote.cast(2, 1), "votes cast");
        check(vote.tally[2] == 2 && vote.tally[1] == 1, "... and counted");
        check(vote.winner(0) == 2 && vote.winner(12345) == 2, "the most chosen wins, whatever the draw");
        check(vote.cast(1, 1) && vote.tally[2] == 1 && vote.tally[1] == 2, "a changed mind counts once");
        check(!vote.cast(1, 1) && vote.tally[1] == 2, "... and the same again does nothing");
        check(!vote.cast(5, 0), "who came late has no vote");
        check(!vote.cast(0, 4) && !vote.cast(0, -1), "no map that is not offered");

        // A tie: only those level, each for some draw.
        NetVote tie = offered(4);
        tie.cast(0, 0);
        tie.cast(1, 3);
        bool first = false, last = false, other = false;
        for (std::uint32_t draw = 0; draw < 20; ++draw)
        {
            const int w = tie.winner(draw);
            first = first || w == 0;
            last = last || w == 3;
            other = other || (w != 0 && w != 3);
        }
        check(first && last && !other, "a tie is drawn between those level");

        // Nobody: any of them.
        const NetVote none = offered(3);
        bool seen[3] = {};
        for (std::uint32_t draw = 0; draw < 3; ++draw)
            seen[none.winner(draw)] = true;
        check(seen[0] && seen[1] && seen[2], "no votes: drawn from them all");
        check(NetVote().winner(0) == -1, "nothing offered: nothing wins");

        // Who leaves mid-vote.
        NetVote left = offered(2);
        left.cast(0, 0);
        left.cast(1, 1);
        left.cast(2, 1);
        left.drop(2);
        left.drop(1);
        check(left.tally[1] == 0 && left.tally[0] == 1 && left.winner(0) == 0, "who leaves takes their vote back");
        check(!left.cast(1, 0), "... and has no other");

        NetVote done = offered(2);
        done.decided = 0;
        check(!done.cast(0, 1), "no vote once it is decided");
    }

    void testDraw()
    {
        int list[10];
        for (int n = 0; n < 10; ++n)
            list[n] = n;
        const int some = netDrawSome(list, 10, 77, 4);
        bool distinct = some == 4;
        for (int a = 0; a < 4; ++a)
        {
            for (int b = a + 1; b < 4; ++b)
                distinct = distinct && list[a] != list[b];
        }
        check(distinct, "four different maps drawn");
        int two[2] = {5, 6};
        check(netDrawSome(two, 2, 1, 4) == 2, "no more than there are");
        check(netVoteMs(5) == 13000 && netVoteMs(30) == 30000, "a vote has its time, however short the scores' is");
    }

    void testPalette()
    {
        std::uint16_t pixels[100];
        const std::uint16_t red = 0x8000 | 31, blue = 0x8000 | 31 << 10;
        for (int n = 0; n < 100; ++n)
            pixels[n] = n < 70 ? red : blue;
        std::uint16_t palette[16];
        previewPalette(pixels, 100, palette);
        const std::uint16_t nearRed = palette[previewNearest(palette, red)], nearBlue = palette[previewNearest(palette, blue)];
        check((nearRed & 31) >= 28 && (nearRed >> 10 & 31) <= 3, "red is drawn red");
        check((nearBlue >> 10 & 31) >= 28 && (nearBlue & 31) <= 3, "blue is drawn blue");
        check(palette[0] == nearRed, "the commonest colour comes first");
    }

    void testEditing()
    {
        std::uint32_t list[3] = {};
        std::uint8_t count = 0;
        check(netRotationAdd(list, count, 3, 11) && netRotationAdd(list, count, 3, 22) && netRotationAdd(list, count, 3, 33),
              "maps added");
        check(!netRotationAdd(list, count, 3, 44) && count == 3, "no more than it holds");
        check(netRotationMove(list, count, 2, -1) && list[1] == 33 && list[2] == 22, "moved up");
        check(!netRotationMove(list, count, 0, -1) && !netRotationMove(list, count, 2, 1), "not past either end");
        check(netRotationRemove(list, count, 0) && count == 2 && list[0] == 33 && list[1] == 22 && list[2] == 0, "removed");
        check(!netRotationRemove(list, count, 5) && count == 2, "nothing there to remove");
    }
} // namespace

int main()
{
    testCandidates();
    testRotation();
    testEditing();
    testVote();
    testDraw();
    testPalette();
    if (g_failures == 0)
        std::printf("test_netnext: ok\n");
    return g_failures == 0 ? 0 : 1;
}
