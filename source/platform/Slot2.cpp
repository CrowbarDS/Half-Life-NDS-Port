#include "platform/Slot2.hpp"

#ifndef HLDS_CHILD

#include <nds.h>

#ifdef HLDS_DEBUG_BUILD
#include "Guard.hpp"
#endif

namespace hlrt::slot2
{
    namespace
    {
        /// Less than this is a cart's save RAM or a stray answer, not a Pak.
        constexpr std::size_t kLeastPak = 1024 * 1024;

        /// Before each block, in the Pak: the map's blocks chained through
        /// it (the Pak keeps the list: main RAM has no room for one), and the
        /// block's size. Read as words, written as words.
        struct Head
        {
            Head *next;
            std::uint32_t bytes;
            std::uint32_t spare[6];
        };
        static_assert(sizeof(Head) == 32);

        bool pak = false;
        std::size_t pakSize = 0;
        Head *mapBlocks = nullptr;
        std::size_t mapUsed = 0;
        std::size_t keptBytes = 0;
        bool motor = false;
        bool motorEdge = false;
        std::uint8_t motorMost = 0;
#ifdef HLDS_DEBUG_BUILD
        cothread_t mainThread = 0;
#endif

        void onMain()
        {
#ifdef HLDS_DEBUG_BUILD
            if (cothread_get_current() != mainThread)
                guard::crash("Expansion Pak used off the main thread");
#endif
        }

        void setWord(volatile std::uint32_t *at, std::uint32_t value) { *at = value; }

        /// A block of the SDK's heap with its head written; null if none.
        Head *block(std::size_t bytes, Head *next)
        {
            const std::size_t rounded = (bytes + 31) & ~std::size_t(31);
            if (!pak || rounded < bytes || rounded > pakSize)
                return nullptr;
            auto *head = static_cast<Head *>(slot2_memalign(32, sizeof(Head) + rounded));
            if (head == nullptr)
                return nullptr;
            setWord(reinterpret_cast<volatile std::uint32_t *>(&head->next), reinterpret_cast<std::uintptr_t>(next));
            setWord(reinterpret_cast<volatile std::uint32_t *>(&head->bytes), static_cast<std::uint32_t>(rounded));
            return head;
        }
    } // namespace

    void init()
    {
#ifdef HLDS_DEBUG_BUILD
        mainThread = cothread_get_current();
#endif
        if (isDSiMode())
            return;
        if (!peripheralSlot2Init(SLOT2_PERIPHERAL_EXTRAM | SLOT2_PERIPHERAL_RUMBLE_ANY))
            return;
        const std::uint32_t has = peripheralSlot2GetSupportMask();
        // Only the first bank (the SDK's heap is over it alone): the carts
        // that have more switch them by a command.
        if ((has & SLOT2_PERIPHERAL_EXTRAM) != 0 && peripheralSlot2RamSize() >= kLeastPak &&
            peripheralSlot2Open(SLOT2_PERIPHERAL_EXTRAM))
        {
            pak = true;
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

    std::size_t pakBytes() { return pak ? pakSize : 0; }
    bool hasRumble() { return motor; }
    bool rumbleEdge() { return motorEdge; }
    std::uint8_t rumbleMost() { return motorMost; }

    std::uint8_t *take(std::size_t bytes)
    {
        onMain();
        Head *head = block(bytes, mapBlocks);
        if (head == nullptr)
            return nullptr;
        mapBlocks = head;
        mapUsed += head->bytes;
        return reinterpret_cast<std::uint8_t *>(head + 1);
    }

    void reset()
    {
        onMain();
        for (Head *head = mapBlocks; head != nullptr;)
        {
            Head *next = head->next;
            slot2_free(head);
            head = next;
        }
        mapBlocks = nullptr;
        mapUsed = 0;
    }

    std::size_t used() { return mapUsed; }

    std::uint8_t *keep(std::size_t bytes)
    {
        onMain();
        Head *head = block(bytes, nullptr);
        if (head == nullptr)
            return nullptr;
        keptBytes += head->bytes;
        return reinterpret_cast<std::uint8_t *>(head + 1);
    }

    void let(std::uint8_t *at)
    {
        onMain();
        if (at == nullptr)
            return;
        Head *head = reinterpret_cast<Head *>(at) - 1;
        keptBytes -= head->bytes;
        slot2_free(head);
    }

    std::size_t kept() { return keptBytes; }

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
