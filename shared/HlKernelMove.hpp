#pragma once

// Moving a function of the game library, as it was built, into ITCM at load
// (source/game/Kernels.cpp): its bytes copied, the calls in it aimed again
// from where they now are, and its old entry made a jump to the copy, so
// that every caller -- none of them changed -- runs the copy.
//
// What may be moved is decided at build time (tools/dsl_kernels.py): only
// functions whose every reference outside themselves is a word the loader
// fills with an address (moved as it is) or a call or jump (aimed again here);
// their table (dsl/hl.kernels) gives each one's place, size, sum, and the
// offsets of those words, so that nothing else in it -- a constant that looks
// like a branch -- is ever touched.
//
// std-only: the host's tests check the encodings (test_kernels).

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hldata::kmove
{
    // ------------------------------------------------------------ branches

    /// An ARM B, BL or BLX (immediate): where it goes, from `at` (its own
    /// address). False if `insn` is none of them.
    inline bool armTarget(std::uint32_t insn, std::uint32_t at, std::uint32_t &target)
    {
        if ((insn & 0x0E000000u) != 0x0A000000u)
            return false;
        std::int32_t offset = static_cast<std::int32_t>(insn << 8) >> 6; // imm24, sign-extended, * 4
        if ((insn & 0xF0000000u) == 0xF0000000u) // BLX: H, the halfword
            offset |= static_cast<std::int32_t>((insn >> 23) & 2u);
        target = at + 8u + static_cast<std::uint32_t>(offset);
        return true;
    }

    /// The same branch made to go to `target` from `at`. False if it cannot
    /// reach (32 MB either way) or a B/BL would land between words.
    inline bool armRetarget(std::uint32_t &insn, std::uint32_t at, std::uint32_t target)
    {
        if ((insn & 0x0E000000u) != 0x0A000000u)
            return false;
        const std::int32_t offset = static_cast<std::int32_t>(target - (at + 8u));
        if (offset < -0x02000000 || offset > 0x01FFFFFE)
            return false;
        const std::uint32_t imm = (static_cast<std::uint32_t>(offset) >> 2) & 0x00FFFFFFu;
        if ((insn & 0xF0000000u) == 0xF0000000u)
        {
            if (offset & 1)
                return false;
            insn = (insn & 0xFE000000u) | ((static_cast<std::uint32_t>(offset) & 2u) << 23) | imm;
            return true;
        }
        if (offset & 3)
            return false;
        insn = (insn & 0xFF000000u) | imm;
        return true;
    }

    /// A Thumb BL or BLX (the ARMv5 pair: a high half, then a low): where it
    /// goes, from `at` (the first half's address). False if it is not one.
    inline bool thumbTarget(std::uint16_t high, std::uint16_t low, std::uint32_t at, std::uint32_t &target)
    {
        if ((high & 0xF800u) != 0xF000u)
            return false;
        const bool bl = (low & 0xF800u) == 0xF800u, blx = (low & 0xF800u) == 0xE800u;
        if (!bl && !blx)
            return false;
        const std::int32_t offset =
            (static_cast<std::int32_t>(static_cast<std::uint32_t>(high & 0x7FFu) << 21) >> 9) |
            static_cast<std::int32_t>((low & 0x7FFu) << 1);
        target = at + 4u + static_cast<std::uint32_t>(offset);
        if (blx)
            target &= ~3u;
        return true;
    }

    /// The same made to go to `target` from `at`. False if it cannot reach
    /// (4 MB either way).
    inline bool thumbRetarget(std::uint16_t &high, std::uint16_t &low, std::uint32_t at, std::uint32_t target)
    {
        std::uint32_t was = 0;
        if (!thumbTarget(high, low, at, was))
            return false;
        const bool blx = (low & 0xF800u) == 0xE800u;
        // (A BLX's target is reckoned from the word its own address is in.)
        const std::uint32_t from = blx ? ((at + 4u) & ~3u) : at + 4u;
        const std::int32_t offset = static_cast<std::int32_t>(target - from);
        if (offset < -0x400000 || offset > 0x3FFFFE || (offset & 1) || (blx && (offset & 2)))
            return false;
        const std::uint32_t o = static_cast<std::uint32_t>(offset);
        high = static_cast<std::uint16_t>(0xF000u | ((o >> 12) & 0x7FFu));
        low = static_cast<std::uint16_t>((low & 0xF800u) | ((o >> 1) & 0x7FFu));
        return true;
    }

    // ---------------------------------------------------------- entry jumps

    inline constexpr std::uint32_t kArmLdrPcMinus4 = 0xE51FF004u; ///< ldr pc, [pc, #-4]
    inline constexpr std::uint16_t kThumbBxPc = 0x4778u;           ///< bx pc
    inline constexpr std::uint16_t kThumbNop = 0x46C0u;            ///< mov r8, r8

    /// How many bytes an entry jump at `entry` takes: 8 for an ARM function;
    /// for a Thumb one, `bx pc` into ARM and then the same, 10 or 12 as the
    /// ARM half falls on the next word or the one after.
    inline std::size_t entryJumpBytes(std::uint32_t entry, bool thumb)
    {
        return thumb ? ((entry & 2u) ? 10u : 12u) : 8u;
    }

    /// Writes into `out` (the old entry's bytes, entryJumpBytes of them) a
    /// jump to `to` that keeps every register the caller passed: `ldr pc`
    /// from the word after (ARM), after `bx pc` (Thumb). `to` has its low bit
    /// set for a Thumb copy, as `ldr pc` then goes into Thumb.
    inline void writeEntryJump(std::uint8_t *out, std::uint32_t entry, bool thumb, std::uint32_t to)
    {
        auto put32 = [](std::uint8_t *p, std::uint32_t v) { std::memcpy(p, &v, 4); };
        auto put16 = [](std::uint8_t *p, std::uint16_t v) { std::memcpy(p, &v, 2); };
        if (!thumb)
        {
            put32(out, kArmLdrPcMinus4);
            put32(out + 4, to);
            return;
        }
        put16(out, kThumbBxPc);
        std::size_t at = 2;
        if (!(entry & 2u))
        {
            // (`bx pc` goes to the word after the next halfword: a nop first.)
            put16(out + 2, kThumbNop);
            at = 4;
        }
        put32(out + at, kArmLdrPcMinus4);
        put32(out + at + 4, to | 1u);
    }

    // --------------------------------------------------------------- table

    inline constexpr std::uint32_t kTableMagic = 0x4E4B4C48u; // "HLKN"
    inline constexpr std::uint32_t kTableVersion = 1;

    /// What a word at one of a kernel's offsets is.
    inline constexpr std::uint8_t kWordAddress = 0; ///< filled by the loader with an address: left as it is
    inline constexpr std::uint8_t kWordArmBranch = 1; ///< an ARM B/BL/BLX: aimed again
    inline constexpr std::uint8_t kWordThumbBranch = 2; ///< a Thumb BL/BLX pair: aimed again

    inline constexpr std::uint8_t kKernelThumb = 1; ///< its code is Thumb

    /// One function to move: in the table, as tools/dsl_kernels.py writes it
    /// (little-endian): offset, bytes, sum (u32 each); flags, the length of
    /// its name, the number of words (u8, u8, u16); its name; and per word,
    /// its offset in the function (u32: the low 30 bits) and kind (the top 2).
    struct Kernel
    {
        std::uint32_t offset = 0; ///< in the library, from where it is loaded
        std::uint32_t bytes = 0;
        std::uint32_t sum = 0; ///< kernelSum of its bytes as built
        std::uint8_t flags = 0;
        const char *name = nullptr; ///< (in the table; not ended by a 0)
        std::uint8_t nameBytes = 0;
        const std::uint8_t *words = nullptr; ///< wordCount of them, 4 bytes each
        std::uint16_t wordCount = 0;

        std::uint32_t wordOffset(int i) const
        {
            std::uint32_t v = 0;
            std::memcpy(&v, words + 4 * i, 4);
            return v & 0x3FFFFFFFu;
        }
        std::uint8_t wordKind(int i) const
        {
            std::uint32_t v = 0;
            std::memcpy(&v, words + 4 * i, 4);
            return static_cast<std::uint8_t>(v >> 30);
        }
    };

    /// Walks a table: `next` gives each kernel in turn, false at its end or
    /// at anything that does not hold together.
    struct TableReader
    {
        const std::uint8_t *data = nullptr;
        std::size_t size = 0, at = 0;
        std::uint32_t count = 0, read = 0;
        bool ok = false;

        TableReader(const std::uint8_t *d, std::size_t n) : data(d), size(n)
        {
            if (n < 12)
                return;
            std::uint32_t magic = 0, version = 0;
            std::memcpy(&magic, d, 4);
            std::memcpy(&version, d + 4, 4);
            std::memcpy(&count, d + 8, 4);
            ok = magic == kTableMagic && version == kTableVersion;
            at = 12;
        }

        bool next(Kernel &k)
        {
            if (!ok || read >= count || at + 16 > size)
                return false;
            std::memcpy(&k.offset, data + at, 4);
            std::memcpy(&k.bytes, data + at + 4, 4);
            std::memcpy(&k.sum, data + at + 8, 4);
            k.flags = data[at + 12];
            k.nameBytes = data[at + 13];
            std::memcpy(&k.wordCount, data + at + 14, 2);
            at += 16;
            if (at + k.nameBytes + 4u * k.wordCount > size)
            {
                ok = false;
                return false;
            }
            k.name = reinterpret_cast<const char *>(data + at);
            at += k.nameBytes;
            k.words = data + at;
            at += 4u * k.wordCount;
            ++read;
            return true;
        }
    };

    /// A function's bytes summed (FNV-1a), its listed words taken as 0: what
    /// the loader writes there differs from the build's, and the rest must
    /// not. The same here as in tools/dsl_kernels.py.
    inline std::uint32_t kernelSum(const std::uint8_t *code, const Kernel &k)
    {
        std::uint32_t h = 0x811C9DC5u;
        for (std::uint32_t i = 0; i < k.bytes; ++i)
        {
            bool skipped = false;
            for (int w = 0; w < k.wordCount && !skipped; ++w)
                skipped = i - k.wordOffset(w) < 4u;
            h = (h ^ (skipped ? 0u : code[i])) * 0x01000193u;
        }
        return h;
    }
} // namespace hldata::kmove
