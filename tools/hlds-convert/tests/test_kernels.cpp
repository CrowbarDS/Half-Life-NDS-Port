// Moving the game library's functions into ITCM (shared/HlKernelMove.hpp):
// the branches aimed again from where a copy is, as the CPU reads them; the
// jump an old entry becomes; the table, and the sum tools/dsl_kernels.py
// makes of a function's bytes.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "HlKernelMove.hpp"

using namespace hldata::kmove;

namespace
{
    int failures = 0;
    void check(bool ok, const char *what)
    {
        if (!ok)
        {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    std::uint32_t armBl(std::uint32_t at, std::uint32_t to, std::uint32_t cond = 0xE)
    {
        const std::uint32_t imm = ((to - (at + 8)) >> 2) & 0x00FFFFFFu;
        return (cond << 28) | 0x0B000000u | imm;
    }

    void testArm()
    {
        // A call from the library (main RAM) into its own code, then the
        // same from the copy at ITCM's mirror under main RAM.
        const std::uint32_t at = 0x0210A000, to = 0x02108000;
        std::uint32_t insn = armBl(at, to), target = 0;
        check(armTarget(insn, at, target) && target == to, "an ARM BL's target, read back");
        const std::uint32_t moved = 0x01FF9010;
        check(armRetarget(insn, moved, to) && armTarget(insn, moved, target) && target == to,
              "... aimed again from the copy, it still goes there");
        check((insn & 0xFF000000u) == 0xEB000000u, "... still a BL, always");
        // A B (a tail call) as well, and its condition kept.
        std::uint32_t b = 0x1A000000u | (((to - (at + 8)) >> 2) & 0x00FFFFFFu); // bne
        check(armRetarget(b, moved, to) && armTarget(b, moved, target) && target == to && (b >> 24) == 0x1Au,
              "a conditional B, aimed again, keeps its condition");
        // Too far: ITCM's own 0x0100xxxx to the top of main RAM is in reach,
        // 32 MB is not.
        std::uint32_t far = armBl(at, to);
        check(!armRetarget(far, 0x01000000, 0x03100000), "a call more than 32 MB away is refused");
        // A BLX into Thumb, at a halfword.
        std::uint32_t blx = 0xFA000000u;
        check(armRetarget(blx, moved, 0x02108002) && armTarget(blx, moved, target) && target == 0x02108002,
              "a BLX to a halfword: its H bit");
        check(!armTarget(0xE59F0004u, at, target), "an ldr is no branch");
    }

    void testThumb()
    {
        const std::uint32_t at = 0x02100002, to = 0x02180000;
        const std::int32_t offset = static_cast<std::int32_t>(to - (at + 4));
        std::uint16_t high = static_cast<std::uint16_t>(0xF000u | ((static_cast<std::uint32_t>(offset) >> 12) & 0x7FFu));
        std::uint16_t low = static_cast<std::uint16_t>(0xF800u | ((static_cast<std::uint32_t>(offset) >> 1) & 0x7FFu));
        std::uint32_t target = 0;
        check(thumbTarget(high, low, at, target) && target == to, "a Thumb BL's target, read back");
        const std::uint32_t moved = 0x01FFA002;
        check(thumbRetarget(high, low, moved, to) && thumbTarget(high, low, moved, target) && target == to,
              "... aimed again from ITCM's mirror under main RAM: in reach");
        check(!thumbRetarget(high, low, 0x01000002, to), "... but not from ITCM itself: 17 MB is past a Thumb BL");
        // A BLX (into ARM): a word's target.
        std::uint16_t xh = 0xF000u, xl = 0xE800u;
        check(thumbRetarget(xh, xl, moved, 0x02108000) && thumbTarget(xh, xl, moved, target) && target == 0x02108000,
              "a Thumb BLX into ARM, aimed again");
    }

    void testEntryJumps()
    {
        std::uint8_t out[12] = {};
        std::uint32_t w0 = 0, w1 = 0;
        check(entryJumpBytes(0x02100000, false) == 8, "an ARM entry jump: 8 bytes");
        writeEntryJump(out, 0x02100000, false, 0x01FF9000);
        std::memcpy(&w0, out, 4);
        std::memcpy(&w1, out + 4, 4);
        check(w0 == 0xE51FF004u && w1 == 0x01FF9000u, "... ldr pc, [pc, #-4] and where to");
        // Thumb, on a word: bx pc, a nop, then the ARM jump, to a Thumb copy.
        std::uint16_t h0 = 0, h1 = 0;
        check(entryJumpBytes(0x02100000, true) == 12, "a Thumb entry jump on a word: 12 bytes");
        writeEntryJump(out, 0x02100000, true, 0x01FF9000);
        std::memcpy(&h0, out, 2);
        std::memcpy(&h1, out + 2, 2);
        std::memcpy(&w0, out + 4, 4);
        std::memcpy(&w1, out + 8, 4);
        check(h0 == kThumbBxPc && h1 == kThumbNop && w0 == 0xE51FF004u && w1 == 0x01FF9001u,
              "... bx pc, nop, the ARM jump, and the copy's address marked Thumb");
        // ... and between words: the ARM jump straight after bx pc.
        check(entryJumpBytes(0x02100002, true) == 10, "a Thumb entry jump between words: 10 bytes");
        writeEntryJump(out, 0x02100002, true, 0x01FF9000);
        std::memcpy(&h0, out, 2);
        std::memcpy(&w0, out + 2, 4);
        std::memcpy(&w1, out + 6, 4);
        check(h0 == kThumbBxPc && w0 == 0xE51FF004u && w1 == 0x01FF9001u, "... bx pc and the ARM jump");
    }

    void testTable()
    {
        // As tools/dsl_kernels.py writes one: a kernel of 24 bytes, a branch
        // at 4 and an address at 16.
        std::vector<std::uint8_t> t;
        auto put32 = [&](std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                t.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        };
        put32(kTableMagic);
        put32(kTableVersion);
        put32(1);
        put32(0x819C);
        put32(24);
        put32(0xD631A645u); // (the tool's sum: kernel_sum of bytes 1..24, those words as 0)
        t.push_back(0);
        t.push_back(4);
        t.push_back(2);
        t.push_back(0);
        for (char c : std::string("kern"))
            t.push_back(static_cast<std::uint8_t>(c));
        put32(4u | (static_cast<std::uint32_t>(kWordArmBranch) << 30));
        put32(16u | (static_cast<std::uint32_t>(kWordAddress) << 30));
        TableReader reader(t.data(), t.size());
        Kernel k;
        check(reader.ok && reader.next(k), "a table read");
        check(k.offset == 0x819C && k.bytes == 24 && k.wordCount == 2 && std::strncmp(k.name, "kern", k.nameBytes) == 0,
              "... its kernel: where, how big, its name, its words");
        check(k.wordOffset(0) == 4 && k.wordKind(0) == kWordArmBranch && k.wordOffset(1) == 16 &&
                  k.wordKind(1) == kWordAddress,
              "... each word's offset and kind");
        std::uint8_t code[24];
        for (int i = 0; i < 24; ++i)
            code[i] = static_cast<std::uint8_t>(i + 1);
        check(kernelSum(code, k) == k.sum, "the sum, the same as the tool's");
        code[5] = 0xAA; // (in a listed word: what the loader writes there)
        check(kernelSum(code, k) == k.sum, "... whatever the loader wrote into its words");
        code[9] = 0xAA;
        check(kernelSum(code, k) != k.sum, "... but not anything else");
        check(!reader.next(k), "... and no more after it");
        t[0] ^= 1;
        TableReader bad(t.data(), t.size());
        check(!bad.ok, "a table that is not one is not read");
    }
} // namespace

int main()
{
    testArm();
    testThumb();
    testEntryJumps();
    testTable();
    std::printf("test_kernels: %s\n", failures == 0 ? "passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
