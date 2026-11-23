// The Expansion Pak probe: what the game's use of a Slot-2 RAM cart rests on
// (source/platform/Slot2.hpp), on a console that has one.
//
//   1. Stores of each width into the Pak, read back as words: a byte store
//      kept, dropped, or written into both halves of its halfword?
//   2. The SDK's mutex (comutex_t, a byte) in the Pak: does a second try
//      fail, as it must? The SDK's Pak heap keeps its lock there.
//   3. The SDK's Pak heap (slot2_memalign / slot2_free): 2,000 blocks taken
//      and given back at random, each filled with words of its own and
//      checked before it goes; every block inside the Pak and on 32 bytes.
//   4. Reading speed: 64 KB read as words and as halfwords, against main
//      RAM, in microseconds.
//
// Results on the screen and added to slot2.txt at the root of the card.
// Runs in DS mode only (a DSi has no slot 2).

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

#include <fat.h>
#include <nds.h>

namespace
{
    std::FILE *out = nullptr;
    std::string outPath;

    void say(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        std::vprintf(format, args);
        va_end(args);
        if (out != nullptr)
        {
            va_start(args, format);
            std::vfprintf(out, format, args);
            va_end(args);
            std::fflush(out);
        }
    }

    void waitForA()
    {
        if (out != nullptr)
        {
            std::fclose(out);
            out = std::fopen(outPath.c_str(), "a");
        }
        std::printf("A: next\n");
        for (;;)
        {
            swiWaitForVBlank();
            scanKeys();
            if (keysDown() & KEY_A)
                break;
        }
        consoleClear();
    }

    std::uint32_t rnd = 0x12345678;
    std::uint32_t next()
    {
        rnd ^= rnd << 13;
        rnd ^= rnd >> 17;
        rnd ^= rnd << 5;
        return rnd;
    }

    void widths(volatile std::uint8_t *pak)
    {
        say("1. Stores into the Pak\n");
        auto *word = reinterpret_cast<volatile std::uint32_t *>(pak);
        auto *half = reinterpret_cast<volatile std::uint16_t *>(pak);
        word[0] = 0x11223344;
        say("  word 11223344 reads %08lx\n", static_cast<unsigned long>(word[0]));
        half[0] = 0xBEEF;
        say("  half BEEF at +0 reads %08lx\n", static_cast<unsigned long>(word[0]));
        word[0] = 0x11223344;
        pak[1] = 0xAA;
        const std::uint32_t got = word[0];
        const char *what = got == 0x1122AA44   ? "kept"
                           : got == 0x11223344 ? "DROPPED"
                           : got == 0x1122AAAA ? "BOTH HALVES"
                                               : "other";
        say("  byte AA at +1 reads %08lx: %s\n", static_cast<unsigned long>(got), what);
        word[0] = 0x11223344;
        pak[0] = 0xAA;
        say("  byte AA at +0 reads %08lx\n", static_cast<unsigned long>(word[0]));
    }

    void mutex(volatile std::uint8_t *pak)
    {
        say("2. comutex_t in the Pak\n");
        auto *word = reinterpret_cast<volatile std::uint32_t *>(pak + 64);
        *word = 0;
        auto *m = reinterpret_cast<comutex_t *>(pak + 64);
        comutex_init(m);
        const bool first = comutex_try_acquire(m);
        const unsigned held = *m;
        const bool second = comutex_try_acquire(m);
        say("  1st try %s, byte then %u, 2nd try %s: %s\n", first ? "got" : "refused", held,
            second ? "got" : "refused", first && !second ? "a lock" : "NO LOCK");
        comutex_release(m);
    }

    void heap(std::uintptr_t start, std::uintptr_t end)
    {
        say("3. The SDK's Pak heap\n");
        constexpr int kSlots = 64;
        std::uint32_t *blocks[kSlots] = {};
        std::uint32_t sizes[kSlots] = {};
        std::uint32_t seeds[kSlots] = {};
        int bad = 0, outside = 0, unaligned = 0, failed = 0, taken = 0;
        for (int round = 0; round < 2000; ++round)
        {
            const int s = static_cast<int>(next() % kSlots);
            if (blocks[s] != nullptr)
            {
                std::uint32_t r = seeds[s];
                for (std::uint32_t i = 0; i < sizes[s] / 4; ++i)
                {
                    r = r * 1664525u + 1013904223u;
                    if (blocks[s][i] != r)
                    {
                        ++bad;
                        break;
                    }
                }
                slot2_free(blocks[s]);
                blocks[s] = nullptr;
                continue;
            }
            const std::uint32_t bytes = 4 + (next() % (96 * 1024)) / 4 * 4;
            auto *b = static_cast<std::uint32_t *>(slot2_memalign(32, bytes));
            if (b == nullptr)
            {
                ++failed;
                continue;
            }
            ++taken;
            const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(b);
            if (at < start || at + bytes > end)
                ++outside;
            if ((at & 31) != 0)
                ++unaligned;
            seeds[s] = next();
            std::uint32_t r = seeds[s];
            for (std::uint32_t i = 0; i < bytes / 4; ++i)
            {
                r = r * 1664525u + 1013904223u;
                b[i] = r;
            }
            blocks[s] = b;
            sizes[s] = bytes;
        }
        for (int s = 0; s < kSlots; ++s)
            slot2_free(blocks[s]);
        // (All given back: one block of nearly the whole Pak again.)
        void *whole = slot2_malloc((end - start) - 64 * 1024);
        say("  %d taken, %d refused, %d spoiled,\n  %d outside, %d off 32; whole again: %s\n", taken, failed, bad,
            outside, unaligned, whole != nullptr ? "yes" : "NO");
        slot2_free(whole);
    }

    std::uint32_t micros(std::uint32_t ticks) { return static_cast<std::uint32_t>((ticks * 1000ull) / 33514ull); }

    void speed(const volatile std::uint8_t *pak)
    {
        say("4. Reading 64 KB, us\n");
        static std::uint32_t mainRam[16384];
        std::uint32_t sum = 0;
        auto words = [&](const volatile std::uint32_t *p) {
            cpuStartTiming(0);
            for (int i = 0; i < 16384; ++i)
                sum += p[i];
            return micros(cpuEndTiming());
        };
        auto halves = [&](const volatile std::uint16_t *p) {
            cpuStartTiming(0);
            for (int i = 0; i < 32768; ++i)
                sum += p[i];
            return micros(cpuEndTiming());
        };
        const std::uint32_t pw = words(reinterpret_cast<const volatile std::uint32_t *>(pak));
        const std::uint32_t ph = halves(reinterpret_cast<const volatile std::uint16_t *>(pak));
        const std::uint32_t mw = words(mainRam);
        say("  Pak words %lu, halves %lu;\n  main RAM words %lu (%lx)\n", static_cast<unsigned long>(pw),
            static_cast<unsigned long>(ph), static_cast<unsigned long>(mw), static_cast<unsigned long>(sum & 0xF));
    }
} // namespace

int main()
{
    consoleDemoInit();
    std::printf("HL Expansion Pak probe\n\n");
    if (fatInitDefault())
    {
        outPath = std::string(fatGetDefaultDrive()) + "slot2.txt";
        out = std::fopen(outPath.c_str(), "a");
    }
    if (isDSiMode())
    {
        say("DSi mode: no slot 2.\n");
        waitForA();
        return 0;
    }
    if (!peripheralSlot2Init(SLOT2_PERIPHERAL_EXTRAM) || !peripheralSlot2Open(SLOT2_PERIPHERAL_EXTRAM) ||
        peripheralSlot2RamStart() == nullptr)
    {
        say("No RAM in slot 2.\n");
        waitForA();
        return 0;
    }
    auto *pak = reinterpret_cast<volatile std::uint8_t *>(peripheralSlot2RamStart());
    const std::uint32_t size = peripheralSlot2RamSize();
    say("== %s, %lu KB at %p, %lu bank(s)\n", peripheralSlot2GetName(), static_cast<unsigned long>(size / 1024),
        static_cast<const void *>(const_cast<std::uint8_t *>(pak)),
        static_cast<unsigned long>(peripheralSlot2RamBanks()));
    // (1, 2 and 4 before the heap is made: its first call lays it over the
    // whole Pak.)
    widths(pak);
    mutex(pak);
    speed(pak);
    heap(reinterpret_cast<std::uintptr_t>(pak), reinterpret_cast<std::uintptr_t>(pak) + size);
    say("== end\n");
    for (;;)
        waitForA();
}
