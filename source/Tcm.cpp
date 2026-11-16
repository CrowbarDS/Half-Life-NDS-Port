#include "Tcm.hpp"

#include <cstring>

#include <nds.h>
#include <nds/arm9/tcm_alloc.h>

namespace hlrt::tcm
{
    namespace
    {
        constexpr int kMostPlaced = 16;
        Placed list[kMostPlaced];
        int listed = 0;
        bool sealed = false;
    } // namespace

    void *placeCode(const void *from, std::size_t bytes, const char *owner)
    {
        if (sealed || listed >= kMostPlaced || bytes == 0)
            return nullptr;
        void *to = itcm_malloc(bytes);
        if (to == nullptr)
            return nullptr;
        // (ITCM is not cached: what is written there is what runs. The
        // bytes are read from main RAM through the cache, as they are.)
        std::memcpy(to, from, bytes);
        list[listed++] = Placed{owner, reinterpret_cast<std::uint32_t>(to) & 0x7FFFu, static_cast<std::uint32_t>(bytes)};
        return to;
    }

    void seal() { sealed = true; }

    int placedCount() { return listed; }
    const Placed &placed(int i) { return list[i]; }
} // namespace hlrt::tcm
