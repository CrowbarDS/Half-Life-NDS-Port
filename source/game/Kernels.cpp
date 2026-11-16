#include "game/Kernels.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <nds.h>

#include "HlKernelMove.hpp"
#include "Tcm.hpp"
#ifdef HLDS_PROFILE
#include "Global.hpp"
#endif

namespace hlrt::kernels
{
    namespace kmove = hldata::kmove;

    namespace
    {
        constexpr std::uint32_t kItcm = 0x01000000u;
        constexpr std::uint32_t kItcmSize = 0x8000u;
        /// ITCM's last mirror, just under main RAM (GameDll's nearItcm): the
        /// copies run there, so that a call from one reaches main RAM even
        /// as a Thumb BL (4 MB), and the library's calls into ITCM, which
        /// the loader already aimed at it, stay as they are.
        constexpr std::uint32_t kItcmNear = 0x02000000u - kItcmSize;

        std::uint32_t near(std::uint32_t at) { return kItcmNear | (at & (kItcmSize - 1)); }

        /// Every call in the copy aimed again from where it now is: to the
        /// copy itself where it called itself, to ITCM's mirror where it
        /// called into ITCM, elsewhere as before. False if one would not reach.
        bool aimCalls(const kmove::Kernel &k, const std::uint8_t *was, std::uint8_t *copy, std::uint32_t runAt)
        {
            const std::uint32_t from = reinterpret_cast<std::uint32_t>(was);
            for (int i = 0; i < k.wordCount; ++i)
            {
                const std::uint32_t off = k.wordOffset(i);
                const std::uint8_t kind = k.wordKind(i);
                if (kind == kmove::kWordAddress)
                    continue;
                if (off + 4 > k.bytes)
                    return false;
                auto place = [&](std::uint32_t target)
                {
                    if (target - from < k.bytes)
                        return runAt + (target - from);
                    if (target - kItcm < kItcmSize)
                        return near(target);
                    return target;
                };
                if (kind == kmove::kWordArmBranch)
                {
                    std::uint32_t insn = 0, target = 0;
                    std::memcpy(&insn, was + off, 4);
                    if (!kmove::armTarget(insn, from + off, target) ||
                        !kmove::armRetarget(insn, runAt + off, place(target)))
                        return false;
                    std::memcpy(copy + off, &insn, 4);
                }
                else
                {
                    std::uint16_t high = 0, low = 0;
                    std::uint32_t target = 0;
                    std::memcpy(&high, was + off, 2);
                    std::memcpy(&low, was + off + 2, 2);
                    if (!kmove::thumbTarget(high, low, from + off, target) ||
                        !kmove::thumbRetarget(high, low, runAt + off, place(target)))
                        return false;
                    std::memcpy(copy + off, &high, 2);
                    std::memcpy(copy + off + 2, &low, 2);
                }
            }
            return true;
        }

#ifdef HLDS_PROFILE
        // (So that tools/pcsample.py can name the copies' pieces of ITCM.)
        void note(const kmove::Kernel &k, std::uint32_t at, const char *what)
        {
            if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
            {
                std::fprintf(log, "kernel: %.*s itcm+%lx %lu %s\n", k.nameBytes, k.name, static_cast<unsigned long>(at),
                             static_cast<unsigned long>(k.bytes), what);
                std::fclose(log);
            }
        }
#endif
    } // namespace

    int place(std::uint8_t *base, std::uint32_t codeBytes)
    {
        std::FILE *file = std::fopen("dsl/hl.kernels", "rb");
        if (file == nullptr)
            return 0;
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        auto *table = size > 0 && size < 16384 ? static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(size)))
                                              : nullptr;
        const bool read = table != nullptr &&
                          std::fread(table, 1, static_cast<std::size_t>(size), file) == static_cast<std::size_t>(size);
        std::fclose(file);
        int moved = 0;
        kmove::TableReader reader(table, read ? static_cast<std::size_t>(size) : 0);
        kmove::Kernel k;
        while (read && reader.next(k))
        {
            const bool thumb = (k.flags & kmove::kKernelThumb) != 0;
            std::uint8_t *was = base + k.offset;
            const std::uint32_t entry = reinterpret_cast<std::uint32_t>(was);
            const std::size_t jump = kmove::entryJumpBytes(entry, thumb);
            // (As it was built: else the library is not the one the table
            // was made with, and it is left alone.)
            if (k.offset + k.bytes > codeBytes || k.bytes < jump || kmove::kernelSum(was, k) != k.sum)
            {
#ifdef HLDS_PROFILE
                note(k, 0, "left: not as built");
#endif
                continue;
            }
            auto *copy = static_cast<std::uint8_t *>(tcm::placeCode(was, k.bytes, k.name));
            if (copy == nullptr)
            {
#ifdef HLDS_PROFILE
                note(k, 0, "left: no room");
#endif
                continue;
            }
            const std::uint32_t runAt = near(reinterpret_cast<std::uint32_t>(copy));
            if (!aimCalls(k, was, copy, runAt))
            {
                // (Its room stays taken -- nothing in ITCM is let go -- and
                // unused; the library runs its own, untouched.)
#ifdef HLDS_PROFILE
                note(k, runAt & (kItcmSize - 1), "left: a call would not reach");
#endif
                continue;
            }
            // The old entry: a jump to the copy, for every caller as it is.
            // (Written through the data cache, then out to memory, and the
            // instruction cache told: it may hold the old entry.)
            kmove::writeEntryJump(was, entry, thumb, runAt);
            DC_FlushRange(was, jump);
            IC_InvalidateRange(was, jump);
            IC_InvalidateRange(reinterpret_cast<const void *>(runAt), k.bytes);
            ++moved;
#ifdef HLDS_PROFILE
            note(k, runAt & (kItcmSize - 1), "moved");
#endif
        }
        std::free(table);
        return moved;
    }
} // namespace hlrt::kernels
