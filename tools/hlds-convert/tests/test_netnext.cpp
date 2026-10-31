// Which map follows a match (shared/HlNetNext.hpp): the rotation going round,
// what is left out, and the list being edited.

#include <cstdio>
#include <cstring>
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
        check(!vote.cast(1, 1) && vote.tally[2] == 2 && vote.tally[1] == 1, "a vote made is final");
        check(!vote.cast(5, 0), "who came late has no vote");
        check(!vote.cast(3, 4) && !vote.cast(3, -1), "no map that is not offered");
        check(!vote.allVoted(), "one has yet to vote");
        check(vote.cast(3, 0) && vote.allVoted(), "... and now everyone has: it is over");
        NetVote nobody = offered(2);
        nobody.voters = 0;
        check(!nobody.allVoted(), "with nobody to vote, it is not over by itself");

        // The result: what it came to first, then the most votes, none with none.
        NetVote ranked = offered(6);
        ranked.tally[1] = 1;
        ranked.tally[2] = 3;
        ranked.tally[4] = 2;
        ranked.tally[5] = 1;
        ranked.decided = 2;
        int top[3];
        check(ranked.top(top, 3) == 3 && top[0] == 2 && top[1] == 4 && top[2] == 1, "the top three, in order");
        NetVote drawn = offered(3);
        drawn.decided = 1;
        check(drawn.top(top, 3) == 1 && top[0] == 1, "no votes: only what was drawn");

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
        check(left.drop(2) == 1 && left.drop(1) == 1, "who leaves says what their vote was for");
        check(left.tally[1] == 0 && left.tally[0] == 1 && left.winner(0) == 0, "... and takes it back");
        check(!left.cast(1, 0), "... and has no other");
        check(left.drop(3) == -1 && left.allVoted(), "who had not voted is not waited for");

        NetVote done = offered(2);
        done.decided = 0;
        check(!done.cast(0, 1), "no vote once it is decided");

        // A long name comes in two parts.
        NetVote named;
        named.setName(3, true, "XYZ");
        named.setName(3, false, "abcdefghijk");
        check(std::strcmp(named.names[3], "abcdefghijkXYZ") == 0 && named.count == 4, "a long name, whole, in either order");
        NetVote cut;
        cut.setName(0, false, "abc");
        check(std::strcmp(cut.names[0], "abc") == 0, "a short name in one");
    }

    void testList()
    {
        check(netVoteScrolled(0, -1, 19) == 0, "no sliding past the first");
        check(netVoteScrolled(0, 1, 19) == 1, "a button at a time");
        check(netVoteScrolled(16, 1, 19) == 16, "... and not past the last");
        check(netVoteScrolled(0, 1, 2) == 0, "a list that fits does not slide");

        // The pictures kept: the few, never what is on the screen.
        NetVotePictures kept;
        const int shown[3] = {4, 5, 6};
        for (int map = 0; map < NetVotePictures::kPlaces; ++map)
        {
            const int place = kept.room(shown, 3);
            kept.slot[place] = static_cast<std::int8_t>(map);
            kept.touch(place);
        }
        const int place = kept.room(shown, 3);
        check(kept.slot[place] == 0, "full: the one used longest ago goes");
        kept.touch(kept.find(0));
        check(kept.slot[kept.room(shown, 3)] == 1, "... used again, it stays");
        const int all[6] = {0, 1, 2, 3, 4, 5};
        check(kept.room(all, 6) == -1, "... and what is on the screen always stays");

        // Which to have next.
        const int rows[3] = {3, 4, 5};
        check(netVoteWanted(rows, 3, 3, 19, 0) == 3, "what is shown first");
        check(netVoteWanted(rows, 3, 3, 19, 1u << 3 | 1u << 4) == 5, "... all of it");
        check(netVoteWanted(rows, 3, 3, 19, 1u << 3 | 1u << 4 | 1u << 5) == 6, "then the next along");
        check(netVoteWanted(rows, 3, 3, 19, 1u << 3 | 1u << 4 | 1u << 5 | 1u << 6) == 2, "then the one before");
        const int ends[3] = {0, 1, -1};
        check(netVoteWanted(ends, 3, 0, 2, 3u) == -1, "nothing more: nothing wanted");
    }

    void testChoosers()
    {
        // Take turns: round the seats, past those empty.
        const unsigned here = 1u << 0 | 1u << 2 | 1u << 5;
        check(netNextSeat(-1, here) == 0, "the first turn is the first seat's");
        check(netNextSeat(0, here) == 2, "the next seat with someone");
        check(netNextSeat(2, here) == 5, "... past those empty");
        check(netNextSeat(5, here) == 0, "... and round again");
        check(netNextSeat(2, 1u << 2) == 2, "alone: their turn every time");
        check(netNextSeat(0, 0) == -1, "nobody: nobody's");
        check(netNextSeat(2, here & ~(1u << 5)) == 0, "who has left is passed over");

        // One who chooses alone: their vote ends it; nobody else has one.
        NetVote alone;
        alone.count = 3;
        alone.chooser = 2;
        alone.voters = 1u << 2;
        check(!alone.cast(0, 1), "only the one who chooses");
        check(!alone.allVoted(), "... has yet to");
        check(alone.cast(2, 1) && alone.allVoted() && alone.winner(0) == 1, "... and their choice is what it comes to");
        NetVote gone;
        gone.count = 3;
        gone.chooser = 2;
        gone.voters = 1u << 2;
        gone.drop(2);
        check(gone.voters == 0, "the one who chooses gone: nobody can");
        bool any[3] = {};
        for (std::uint32_t draw = 0; draw < 3; ++draw)
            any[gone.winner(draw)] = true;
        check(any[0] && any[1] && any[2], "... and it is drawn from them all");
    }

    void testPalette()
    {
        std::uint16_t pixels[100];
        const std::uint16_t red = 0x8000 | 31, blue = 0x8000 | 31 << 10;
        for (int n = 0; n < 100; ++n)
            pixels[n] = n < 70 ? red : blue;
        std::uint16_t palette[16];
        previewPalette(pixels, 100, palette);
        const int atRed = previewNearest(palette, red), atBlue = previewNearest(palette, blue);
        check(palette[atRed] == red && palette[atBlue] == blue, "a colour of the picture is kept as it is");
        check(atRed >= 1 && atBlue >= 1 && atRed != atBlue, "... and never as colour 0, which is see-through");

        // A picture with many colours: every pixel stays near its own.
        std::uint16_t ramp[1024];
        for (int n = 0; n < 1024; ++n)
            ramp[n] = static_cast<std::uint16_t>(0x8000 | (n & 31) | ((n >> 5) & 31) << 10 | ((n * 3) & 31) << 5);
        previewPalette(ramp, 1024, palette);
        int worst = 0;
        bool none = true;
        for (const std::uint16_t c : ramp)
        {
            const int p = previewNearest(palette, c);
            none = none && p >= 1;
            const int r = (c & 31) - (palette[p] & 31), g = (c >> 5 & 31) - (palette[p] >> 5 & 31),
                      b = (c >> 10 & 31) - (palette[p] >> 10 & 31);
            const int far = r * r + g * g + b * b;
            worst = far > worst ? far : worst;
        }
        check(none && worst < 500, "many colours: none far from what it is drawn in");
        // A flat picture: no harm.
        std::uint16_t flat[16];
        for (std::uint16_t &c : flat)
            c = 0x8000 | 9 | 9 << 5 | 9 << 10;
        previewPalette(flat, 16, palette);
        check(palette[previewNearest(palette, flat[0])] == flat[0], "one colour alone");
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
    testList();
    testChoosers();
    testPalette();
    if (g_failures == 0)
        std::printf("test_netnext: ok\n");
    return g_failures == 0 ? 0 : 1;
}
