// Which map follows a match (shared/HlNetNext.hpp): the rotation going round,
// what is left out, and the list being edited.

#include <cstdio>
#include <vector>

#include "HlNetNext.hpp"

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
    if (g_failures == 0)
        std::printf("test_netnext: ok\n");
    return g_failures == 0 ? 0 : 1;
}
