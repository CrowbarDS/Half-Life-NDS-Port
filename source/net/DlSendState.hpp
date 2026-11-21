#pragma once

// What SEND THE GAME knows, kept in the program (DlSend.cpp), and its work,
// in the front end's overlay (DlSendBody.cpp), reached through DlSend.cpp's
// gates: for those two alone.

#include <cstddef>
#include <cstdint>
#include <string>

#include "net/DlSend.hpp"
#include "net/NetLink.hpp"

namespace hlrt::dlsend::detail
{
    inline constexpr const char *kChildPath = "nitro:/dlplay/child.nds";

    extern State now;
    extern void *rom;
    extern std::size_t romSize;
    extern int wanted;
    extern unsigned seen;
    extern std::uint32_t firstAt, newestAt, movedAt, startedAt;
    extern int lastSum;
    // The radio's account, a second at a time (rate()).
    extern netlink::DlCounts countsWere;
    extern std::uint32_t countedAt;
    extern char rateLine[48];

    // The overlay's (DlSendBody.cpp).
    bool beginBody(const char *description, int most, std::string &why);
    void frameBody();
    void describeBody(int n, char *status, int room, const char *&name);
} // namespace hlrt::dlsend::detail
