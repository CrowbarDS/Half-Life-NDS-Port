#include "platform/Slot2.hpp"

#ifndef HLDS_CHILD

#include <nds.h>

namespace hlrt::slot2
{
    namespace
    {
        /// Less than this is a cart's save RAM or a stray answer, not a Pak.
        constexpr std::size_t kLeastPak = 1024 * 1024;

        std::uint8_t *pakStart = nullptr;
        std::size_t pakSize = 0;
        std::size_t pakUsed = 0;
        bool motor = false;
        bool motorEdge = false;
        std::uint8_t motorMost = 0;
    } // namespace

    void init()
    {
        if (isDSiMode())
            return;
        if (!peripheralSlot2Init(SLOT2_PERIPHERAL_EXTRAM | SLOT2_PERIPHERAL_RUMBLE_ANY))
            return;
        const std::uint32_t has = peripheralSlot2GetSupportMask();
        // Only the first bank: the carts that have more switch them by a
        // command, and what is taken is used across the whole map.
        if ((has & SLOT2_PERIPHERAL_EXTRAM) != 0 && peripheralSlot2RamSize() >= kLeastPak &&
            peripheralSlot2Open(SLOT2_PERIPHERAL_EXTRAM))
        {
            pakStart = reinterpret_cast<std::uint8_t *>(peripheralSlot2RamStart());
            pakSize = peripheralSlot2RamSize() & ~std::size_t(31);
            return;
        }
        if ((has & SLOT2_PERIPHERAL_RUMBLE_ANY) != 0 && peripheralSlot2Open(has & SLOT2_PERIPHERAL_RUMBLE_ANY))
        {
            motor = isRumbleInserted();
            motorEdge = rumbleIsEdgeActivated();
            motorMost = rumbleGetMaxRawStrength();
            if (motorMost == 0)
                motor = false;
        }
    }

    std::size_t pakBytes() { return pakStart != nullptr ? pakSize : 0; }
    bool hasRumble() { return motor; }
    bool rumbleEdge() { return motorEdge; }
    std::uint8_t rumbleMost() { return motorMost; }

    std::uint8_t *take(std::size_t bytes)
    {
        const std::size_t rounded = (bytes + 31) & ~std::size_t(31);
        if (pakStart == nullptr || rounded > pakSize - pakUsed)
            return nullptr;
        std::uint8_t *at = pakStart + pakUsed;
        pakUsed += rounded;
        return at;
    }

    void reset() { pakUsed = 0; }
    std::size_t used() { return pakUsed; }

    void write(std::uint8_t *dst, const void *src, std::size_t bytes)
    {
        const std::uint8_t *from = static_cast<const std::uint8_t *>(src);
        volatile std::uint16_t *to = reinterpret_cast<volatile std::uint16_t *>(dst);
        // Both on a word: words (the bus makes two halfwords of each).
        if (((reinterpret_cast<std::uintptr_t>(from) | reinterpret_cast<std::uintptr_t>(dst)) & 3) == 0)
        {
            volatile std::uint32_t *words = reinterpret_cast<volatile std::uint32_t *>(dst);
            const std::uint32_t *in = reinterpret_cast<const std::uint32_t *>(from);
            const std::size_t count = bytes >> 2;
            for (std::size_t i = 0; i < count; ++i)
                words[i] = in[i];
            to += count * 2;
            from += count * 4;
            bytes &= 3;
        }
        for (; bytes >= 2; bytes -= 2, from += 2)
            *to++ = static_cast<std::uint16_t>(from[0] | (from[1] << 8));
        if (bytes != 0)
            *to = static_cast<std::uint16_t>((*to & 0xFF00) | from[0]);
    }
} // namespace hlrt::slot2

#endif
