// The divide contract probe: the routines the game divides by (source/HwMath.s,
// reached through --wrap, as in the game) against the library's, word for word.
//
// Quotient and remainder both, on the edge cases (by zero, by -1, the largest
// and smallest numbers, top bits set) and on many random numbers. It also checks
// that r4-r11 are kept and that the CPU's interrupt bit comes back as it went in,
// with interrupts on and with them off, for the root as well.
//
// "PASS" at the bottom of the screen, or the first differences.

#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include <nds.h>

extern "C"
{
    void probeCall(const void *routine, const std::uint32_t *in, std::uint32_t *out);
    std::uint32_t probeMask(std::uint32_t disable);
    void __wrap___aeabi_ldivmod();
    void __wrap___aeabi_uldivmod();
    void __real___aeabi_ldivmod();
    void __real___aeabi_uldivmod();
    void hlHwSqrt64();
}

namespace
{
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    std::uint64_t next()
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return seed;
    }

    constexpr std::uint64_t kEdges[] = {0,
                                        1,
                                        2,
                                        3,
                                        7,
                                        10,
                                        0x7FFFFFFFull,
                                        0x80000000ull,
                                        0xFFFFFFFFull,
                                        0x100000000ull,
                                        0x100000001ull,
                                        0x7FFFFFFFFFFFFFFEull,
                                        0x7FFFFFFFFFFFFFFFull,
                                        0x8000000000000000ull,
                                        0x8000000000000001ull,
                                        0xFFFFFFFF00000000ull,
                                        0xFFFFFFFF80000000ull,
                                        0xFFFFFFFFFFFFFFFDull,
                                        0xFFFFFFFFFFFFFFFEull,
                                        0xFFFFFFFFFFFFFFFFull};

    int compared = 0, wrong = 0, shown = 0, kept = 0, maskBad = 0;

    void one(const char *name, const void *ours, const void *library, std::uint64_t x, std::uint64_t y, std::uint32_t mask)
    {
        const std::uint32_t in[4] = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(x >> 32),
                                     static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(y >> 32)};
        std::uint32_t a[6], b[6];
        probeMask(mask);
        probeCall(ours, in, a);
        probeCall(library, in, b);
        probeMask(0x80);
        ++compared;
        kept += static_cast<int>(a[4]);
        if (a[5] != mask)
            ++maskBad;
        bool same = true;
        for (int i = 0; i < 4; ++i)
            same = same && a[i] == b[i];
        if (same)
            return;
        ++wrong;
        if (shown++ < 3)
        {
            std::printf("%s %08lx%08lx / %08lx%08lx\n", name, (unsigned long)in[1], (unsigned long)in[0], (unsigned long)in[3],
                        (unsigned long)in[2]);
            std::printf(" got q%08lx%08lx r%08lx%08lx\n", (unsigned long)a[1], (unsigned long)a[0], (unsigned long)a[3],
                        (unsigned long)a[2]);
            std::printf(" lib q%08lx%08lx r%08lx%08lx\n", (unsigned long)b[1], (unsigned long)b[0], (unsigned long)b[3],
                        (unsigned long)b[2]);
        }
    }

    void both(std::uint64_t x, std::uint64_t y, std::uint32_t mask)
    {
        one("s", reinterpret_cast<const void *>(__wrap___aeabi_ldivmod), reinterpret_cast<const void *>(__real___aeabi_ldivmod),
            x, y, mask);
        one("u", reinterpret_cast<const void *>(__wrap___aeabi_uldivmod), reinterpret_cast<const void *>(__real___aeabi_uldivmod),
            x, y, mask);
    }

    std::uint32_t softRoot(std::uint64_t value)
    {
        std::uint64_t root = 0, bit = 1ull << 62;
        while (bit > value)
            bit >>= 2;
        while (bit != 0)
        {
            if (value >= root + bit)
            {
                value -= root + bit;
                root = (root >> 1) + bit;
            }
            else
            {
                root >>= 1;
            }
            bit >>= 2;
        }
        return static_cast<std::uint32_t>(root);
    }

    /// Whether `root` is `value`'s square root rounded down, in integers
    /// alone: root^2 <= value < (root + 1)^2.
    bool isRootOf(std::uint32_t root, std::uint64_t value)
    {
        const std::uint64_t r = root;
        return r * r <= value && (r == 0xFFFFFFFFu || (r + 1) * (r + 1) > value);
    }

    // The whole range, as isqrt64 asks it. (From 2^62 up counted apart too:
    // melonDS gets a third of those wrong; a console none.)
    int rootsWrong = 0, rootsWrongHigh = 0, rootsHigh = 0, rootMaskBad = 0;
    void root(std::uint64_t value, std::uint32_t mask)
    {
        const std::uint32_t in[4] = {static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(value >> 32), 0, 0};
        std::uint32_t out[6];
        probeMask(mask);
        probeCall(reinterpret_cast<const void *>(hlHwSqrt64), in, out);
        probeMask(0x80);
        const bool high = (value >> 62) != 0;
        rootsHigh += high;
        if (!isRootOf(softRoot(value), value))
            std::printf("the reference is wrong!\n");
        if (!isRootOf(out[0], value))
        {
            ++rootsWrong;
            rootsWrongHigh += high;
            if (rootsWrong <= 3)
                std::printf("root %08lx%08lx\n got %08lx want %08lx\n", (unsigned long)in[1], (unsigned long)in[0],
                            (unsigned long)out[0], (unsigned long)softRoot(value));
        }
        if (out[5] != mask)
            ++rootMaskBad;
    }
} // namespace

int main()
{
    consoleDemoInit();
    std::printf("HwMath against the library\n\n");
    for (const std::uint32_t mask : {0u, 0x80u})
    {
        for (const std::uint64_t x : kEdges)
        {
            for (const std::uint64_t y : kEdges)
            {
                both(x, y, mask);
                both(-x, y, mask);
                both(x, -y, mask);
            }
            root(x, mask);
        }
    }
    // Around the squares at the top of the range, and a value whose root a
    // double gets one too high.
    for (const std::uint64_t r : {0x7FFFFFFEull, 0x7FFFFFFFull, 0x80000000ull, 0x80000001ull, 0xBFFFFFFFull,
                                  0xFFFFFFFDull, 0xFFFFFFFEull, 0xFFFFFFFFull})
    {
        root(r * r - 1, 0);
        root(r * r, 0x80);
        if (r != 0xFFFFFFFFull)
            root(r * r + 1, 0);
    }
    root(9007199326062755ull, 0);
    const int edges = compared;
    constexpr int kRandom = 20000;
    for (int n = 0; n < kRandom; ++n)
    {
        // Every width, so that short and long numbers meet.
        const std::uint64_t x = next() >> (next() & 63), y = next() >> (next() & 63);
        const std::uint32_t mask = (n & 1) != 0 ? 0x80u : 0u;
        both(x, y, mask);
        both(static_cast<std::uint64_t>(-static_cast<std::int64_t>(x)), y, mask);
        both(x, static_cast<std::uint64_t>(-static_cast<std::int64_t>(y)), mask);
        root(next() >> (next() & 63), mask);
        root(next() | (1ull << 62), mask); // (2^62 and up)
    }
    probeMask(0);
    std::printf("\nedge cases %d, random %d\n", edges, compared - edges);
    std::printf("answers wrong      %d\n", wrong);
    std::printf("registers changed  %d\n", kept);
    std::printf("interrupt bit bad  %d\n", maskBad);
    std::printf("roots wrong        %d\n", rootsWrong);
    std::printf("2^62 and up wrong  %d of %d\n", rootsWrongHigh, rootsHigh);
    std::printf("root's bit bad     %d\n", rootMaskBad);
    const bool pass = wrong == 0 && kept == 0 && maskBad == 0 && rootsWrong == 0 && rootMaskBad == 0;
    std::printf("\n%s\n", pass ? "PASS" : "FAIL");
    while (true)
        swiWaitForVBlank();
    return 0;
}
