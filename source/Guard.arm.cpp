#include "Guard.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>

#include <nds.h>
#include <nds/cpu_asm.h>

// (The linker sends every call of the program's -- and, by GameDll's
// resolver, the game library's -- to malloc and the rest here, to the
// __wrap_ functions: build.py's --wrap. __real_ ones are the allocator's.)

extern "C"
{
    void *__real_malloc(std::size_t size);
    void __real_free(void *p);
    void *__real_memalign(std::size_t align, std::size_t size);
    void *__real_calloc(std::size_t count, std::size_t size);
    void *__real_realloc(void *p, std::size_t size);
    std::size_t malloc_usable_size(void *p);

    // libnds's own, for the crash screen (not in its headers).
    extern const char *exceptionMsg;
    std::uint32_t getExceptionAddress(std::uint32_t opcodeAddress, std::uint32_t thumbState);

    // The linker's marks.
    extern char __text_start[], __text_end[], __sbss_end[], __sp_usr[];
}

namespace hlrt::guard
{
    namespace
    {
        // ---- the heap's counts (both builds)

        std::uint32_t blocks = 0, bytes = 0;
        /// The most `bytes` has been since markPlay(), and what it was then.
        std::uint32_t playBase = 0, playPeak = 0;

#ifndef HLDS_GUARD_FULL
        /// A block of the allocator's taken (+1) or given back (-1): what it
        /// holds, as the allocator says.
        void counted(void *p, int way)
        {
            const std::uint32_t n = static_cast<std::uint32_t>(malloc_usable_size(p));
            const int saved = enterCriticalSection();
            if (way > 0)
            {
                ++blocks;
                bytes += n;
                if (bytes > playPeak)
                    playPeak = bytes;
            }
            else
            {
                --blocks;
                bytes -= n;
            }
            leaveCriticalSection(saved);
        }
#endif

        // ---- the allocator's chain (both builds)
        //
        // libnds's dlmalloc (built without FOOTERS): every chunk is a word
        // before it (the size of the one before, when that one is free) and a
        // word that is its own size with two bits: 1, the one before is in
        // use; 2, this one is. The chunks follow one another from the heap's
        // start (rounded so that what a chunk holds is on eight) to the top
        // chunk, the free room at the end, and after it a last false chunk
        // whose size takes it exactly to sbrk's end. A write past a block's
        // end lands on the next chunk's size: the chain no longer adds up, or
        // its bits disagree. Nothing is added to a block for it.
        struct ChainWalk
        {
            std::uint32_t bad = 0;          ///< the chunk where it went wrong, or 0
            std::uint32_t badHead = 0;      ///< ... its size word
            std::uint32_t before = 0;       ///< ... the chunk before it
            std::uint32_t topFree = 0;      ///< the top chunk
            std::uint32_t largestHole = 0;  ///< the largest free chunk inside, what it can hold
            std::uint32_t inHoles = 0;      ///< ... all of them
        };

        ChainWalk walkChain()
        {
            ChainWalk w;
            const std::uint32_t start = reinterpret_cast<std::uint32_t>(getHeapStart());
            const std::uint32_t end = reinterpret_cast<std::uint32_t>(getHeapEnd());
            if (end <= start + 64)
                return w; // (nothing taken yet)
            auto word = [](std::uint32_t a) { return *reinterpret_cast<const volatile std::uint32_t *>(a); };
            // The false chunk at the end: its size takes it to sbrk's end.
            auto isLast = [&](std::uint32_t c) {
                if (c + 8 > end)
                    return false;
                const std::uint32_t head = word(c + 4);
                return (head & 3) == 0 && c + (head & ~7u) == end;
            };
            std::uint32_t c = start + ((8 - ((start + 8) & 7)) & 7);
            std::uint32_t before = 0;
            bool beforeFree = false;
            std::uint32_t lastFree = 0;
            auto fail = [&](std::uint32_t at) {
                w.bad = at;
                w.badHead = at + 8 <= end ? word(at + 4) : 0;
                w.before = before;
                return w;
            };
            while (true)
            {
                if (c + 8 > end || (c & 7) != 0)
                    return fail(c);
                if (isLast(c))
                {
                    // (What was before it is the top chunk: free.)
                    if (!beforeFree)
                        return fail(c);
                    w.topFree = lastFree;
                    // (Counted among the holes as it was met: not one.)
                    w.inHoles -= lastFree > 4 ? lastFree - 4 : 0;
                    return w;
                }
                const std::uint32_t head = word(c + 4);
                const std::uint32_t size = head & ~7u;
                const std::uint32_t next = c + size;
                const bool lastNext = size >= 8 && next <= end && isLast(next);
                if (size < (lastNext ? 8u : 16u) || next > end || ((head & 1) != 0) == beforeFree)
                    return fail(c);
                const bool free = (head & 2) == 0;
                if (free)
                {
                    // (Two free chunks side by side are always made one;
                    // and a free one's size is also in front of the next.)
                    if (beforeFree || (!lastNext && word(next) != size))
                        return fail(c);
                    const std::uint32_t holds = size - 4;
                    w.inHoles += holds;
                    if (!lastNext && holds > w.largestHole)
                        w.largestHole = holds;
                    lastFree = size;
                }
                before = c;
                beforeFree = free;
                c = next;
            }
        }

#ifdef HLDS_GUARD_FULL
        // ---- the heap

        constexpr std::uint32_t kLive = 0x4C495645;  // "LIVE"
        constexpr std::uint32_t kGone = 0x474F4E45;  // "GONE": given back, held
        constexpr std::uint8_t kTail[4] = {0x5A, 0xA5, 0xC3, 0x3C};
        constexpr std::uint8_t kPoison = 0xDB;

        /// In front of every block. (24 bytes: what malloc gives stays on
        /// eight.)
        struct Block
        {
            std::uint32_t magic;
            std::uint32_t size;   ///< what was asked for
            std::uint32_t caller; ///< the code that asked
            std::uint16_t lead;   ///< from what the allocator gave to the block
            std::uint16_t align;  ///< as asked (8: malloc's own)
            Block *prev, *next;   ///< the blocks held (given back: who gave it back, in prev)
        };
        static_assert(sizeof(Block) == 24);

        Block *first = nullptr;

        /// Small blocks given back are held a while, filled: a write into
        /// one is seen when it really goes (or at a check).
        constexpr int kHeld = 64;
        constexpr std::uint32_t kHeldMost = 4096;      ///< bytes of one block
        constexpr std::uint32_t kHeldBytes = 32 * 1024; ///< ... of all of them
        Block *held[kHeld];
        int heldAt = 0, heldCount = 0;
        std::uint32_t heldBytes = 0;

        std::uint8_t *userOf(Block *b) { return reinterpret_cast<std::uint8_t *>(b) + sizeof(Block); }
        Block *blockOf(void *p) { return reinterpret_cast<Block *>(static_cast<std::uint8_t *>(p) - sizeof(Block)); }
        void *rawOf(Block *b) { return userOf(b) - b->lead; }

        bool tailIntact(Block *b)
        {
            const std::uint8_t *t = userOf(b) + b->size;
            return t[0] == kTail[0] && t[1] == kTail[1] && t[2] == kTail[2] && t[3] == kTail[3];
        }

        /// Where the first byte of a held block that is not the pattern is,
        /// or -1.
        int poisonBroken(Block *b)
        {
            const std::uint8_t *p = userOf(b);
            for (std::uint32_t i = 0; i < b->size; ++i)
            {
                if (p[i] != kPoison)
                    return static_cast<int>(i);
            }
            return -1;
        }

        /// A held block let go for good, its pattern looked at first.
        void release(Block *b)
        {
            const int broken = poisonBroken(b);
            if (broken >= 0)
                crash("HEAP: written after it was freed", reinterpret_cast<std::uint32_t>(userOf(b)) + broken,
                      b->size, b->caller);
            heldBytes -= b->size;
            b->magic = 0;
            __real_free(rawOf(b));
        }

        void releaseOldest()
        {
            const int at = (heldAt - heldCount + kHeld) % kHeld;
            Block *b = held[at];
            held[at] = nullptr;
            --heldCount;
            release(b);
        }

        void releaseAll()
        {
            while (heldCount > 0)
                releaseOldest();
        }

        void *take(std::size_t align, std::size_t size, std::uint32_t caller)
        {
            if (size > 0x0FFFFFFF)
                return nullptr;
            const std::size_t lead = align <= 8 ? sizeof(Block) : (sizeof(Block) + align - 1) & ~(align - 1);
            const std::size_t whole = lead + size + sizeof(kTail);
            const int saved = enterCriticalSection();
            void *raw = align <= 8 ? __real_malloc(whole) : __real_memalign(align, whole);
            if (raw == nullptr && heldCount > 0)
            {
                // (Short: what is held goes first.)
                releaseAll();
                raw = align <= 8 ? __real_malloc(whole) : __real_memalign(align, whole);
            }
            if (raw == nullptr)
            {
                leaveCriticalSection(saved);
                return nullptr;
            }
            std::uint8_t *user = static_cast<std::uint8_t *>(raw) + lead;
            Block *b = blockOf(user);
            b->magic = kLive;
            b->size = static_cast<std::uint32_t>(size);
            b->caller = caller;
            b->lead = static_cast<std::uint16_t>(lead);
            b->align = static_cast<std::uint16_t>(align <= 8 ? 8 : align);
            std::memcpy(user + size, kTail, sizeof(kTail));
            b->prev = nullptr;
            b->next = first;
            if (first != nullptr)
                first->prev = b;
            first = b;
            ++blocks;
            bytes += b->size;
            if (bytes > playPeak)
                playPeak = bytes;
            leaveCriticalSection(saved);
            return user;
        }

        /// The block `p` is the user part of, checked; halts if it is not
        /// one, or has been written past.
        Block *checked(void *p, std::uint32_t caller)
        {
            Block *b = blockOf(p);
            if (b->magic == kGone)
                crash("HEAP: freed twice", reinterpret_cast<std::uint32_t>(p), b->size, caller);
            if (b->magic != kLive)
                crash("HEAP: freed, not a block", reinterpret_cast<std::uint32_t>(p), b->magic, caller);
            if (!tailIntact(b))
                crash("HEAP: written past its end", reinterpret_cast<std::uint32_t>(p), b->size, b->caller);
            return b;
        }

        void give(void *p, std::uint32_t caller)
        {
            if (p == nullptr)
                return;
            const int saved = enterCriticalSection();
            Block *b = checked(p, caller);
            if (b->prev != nullptr)
                b->prev->next = b->next;
            else
                first = b->next;
            if (b->next != nullptr)
                b->next->prev = b->prev;
            --blocks;
            bytes -= b->size;
            if (b->size > kHeldMost)
            {
                b->magic = 0;
                __real_free(rawOf(b));
                leaveCriticalSection(saved);
                return;
            }
            while (heldCount > 0 && (heldCount == kHeld || heldBytes + b->size > kHeldBytes))
                releaseOldest();
            b->magic = kGone;
            b->prev = reinterpret_cast<Block *>(caller);
            b->next = nullptr;
            std::memset(userOf(b), kPoison, b->size);
            held[heldAt] = b;
            heldAt = (heldAt + 1) % kHeld;
            ++heldCount;
            heldBytes += b->size;
            leaveCriticalSection(saved);
        }

#else
        // ---- the heap, in a release build: the allocator's own blocks as
        // they are (nothing added to them), counted. What is checked is the
        // allocator's chain itself (allocatorBroken, below).

        /// Kept back, and let go only when a block cannot be had otherwise:
        /// the last room on a map that fills the heap. (The full guard has
        /// its held blocks for that; without it, maps that start with them
        /// stopped short for want of a few kilobytes.)
        constexpr std::size_t kReserve = 32 * 1024;
        void *reserve = nullptr;
        /// When it was let go: in the load, or in play (perf.txt).
        bool inPlay = false;
        std::uint32_t letGoLoad = 0, letGoPlay = 0, letGoAsked = 0;

        void *take(std::size_t align, std::size_t size, std::uint32_t)
        {
            void *p = align <= 8 ? __real_malloc(size) : __real_memalign(align, size);
            if (p == nullptr && reserve != nullptr)
            {
                __real_free(reserve);
                reserve = nullptr;
                ++(inPlay ? letGoPlay : letGoLoad);
                letGoAsked = static_cast<std::uint32_t>(size);
                p = align <= 8 ? __real_malloc(size) : __real_memalign(align, size);
            }
            if (p != nullptr)
                counted(p, 1);
            return p;
        }

        void give(void *p, std::uint32_t)
        {
            if (p == nullptr)
                return;
            counted(p, -1);
            __real_free(p);
        }
#endif
        std::uint32_t callerOf(void *returnAddress) { return reinterpret_cast<std::uint32_t>(returnAddress); }

        // ---- the library and the stack

        const std::uint8_t *libraryBase = nullptr;
        std::uint32_t librarySize = 0, libraryCode = 0, librarySum = 0;

        std::uint32_t sum(const std::uint8_t *p, std::uint32_t n)
        {
            const std::uint32_t *w = reinterpret_cast<const std::uint32_t *>(p);
            std::uint32_t h = 0x811C9DC5;
            for (std::uint32_t i = 0; i < n / 4; ++i)
                h = (h ^ w[i]) * 0x01000193;
            return h;
        }

        bool libraryIntact()
        {
            return libraryBase == nullptr || sum(libraryBase, libraryCode) == librarySum;
        }

        constexpr std::uint32_t kStackWords = 16;
        constexpr std::uint32_t kStackMark = 0x5354414B; // "STAK"

        std::uint32_t *stackFloor()
        {
            return reinterpret_cast<std::uint32_t *>((reinterpret_cast<std::uint32_t>(__sbss_end) + 3) & ~3u);
        }

        bool stackIntact()
        {
            const std::uint32_t *w = stackFloor();
            for (std::uint32_t i = 0; i < kStackWords; ++i)
            {
                if (w[i] != (kStackMark ^ i))
                    return false;
            }
            return true;
        }

#ifdef HLDS_GUARD_FULL
        /// The first block whose header or end is wrong, or null.
        Block *heapBroken()
        {
            std::uint32_t n = 0;
            for (Block *b = first; b != nullptr; b = b->next)
            {
                if (b->magic != kLive || !tailIntact(b) || ++n > blocks)
                    return b;
            }
            return nullptr;
        }

        Block *heldBroken()
        {
            for (int i = 0; i < kHeld; ++i)
            {
                if (held[i] != nullptr && (held[i]->magic != kGone || poisonBroken(held[i]) >= 0))
                    return held[i];
            }
            return nullptr;
        }

#endif

        // ---- the crash screen

        void print(const char *s) { consolePrintString(s); }
        void hex(std::uint32_t v) { consolePrintUnsigned(v, 16); }
        void at(int x, int y) { consoleSetCursor(nullptr, x, y); }

        /// What an address is: the program's code, the library's (how far
        /// in), ITCM; or nothing.
        const char *place(std::uint32_t a, std::uint32_t &offset)
        {
            offset = a;
            if (a >= reinterpret_cast<std::uint32_t>(__text_start) && a < reinterpret_cast<std::uint32_t>(__text_end))
                return "main";
            if (libraryBase != nullptr && a >= reinterpret_cast<std::uint32_t>(libraryBase) &&
                a < reinterpret_cast<std::uint32_t>(libraryBase) + libraryCode)
            {
                offset = a - reinterpret_cast<std::uint32_t>(libraryBase);
                return "dsl";
            }
            if (a >= 0x01000000 && a < 0x02000000)
            {
                offset = a & 0x7FFF;
                return "itcm";
            }
            return nullptr;
        }

        void placed(std::uint32_t a)
        {
            std::uint32_t offset = 0;
            const char *where = place(a, offset);
            hex(a);
            if (where != nullptr && where[0] != 'm')
            {
                print(" ");
                print(where);
                print("+");
                hex(offset);
            }
            else if (where != nullptr)
                print(" main");
        }

        [[noreturn]] void screen(const char *what, bool exception, const std::uint32_t *regs, std::uint32_t pc,
                                 std::uint32_t addr, const std::uint32_t *numbers)
        {
            REG_IME = 0;
            vramSetBankH(VRAM_H_LCD); // (the touch screen's: not over the console's)
            consoleDemoInit();
            BG_PALETTE_SUB[0] = RGB15(15, 0, 0);
            BG_PALETTE_SUB[255] = RGB15(31, 31, 31);

            at(0, 0);
            print("CRASH ");
            print(hlBuildId);
            at(0, 1);
            print(what);
            at(0, 3);
            print("pc ");
            placed(pc);
            if (exception)
            {
                at(0, 4);
                print("ad ");
                hex(addr);
                print(" lr ");
                placed(regs[14]);
                static const char *const kNames[14] = {"r0", "r1", "r2", "r3", "r4", "r5", "r6",
                                                       "r7", "r8", "r9", "rA", "rB", "rC", "sp"};
                for (int i = 0; i < 14; ++i)
                {
                    at((i & 1) * 13, 6 + i / 2);
                    print(kNames[i]);
                    print(" ");
                    hex(regs[i]);
                }
            }
            else
            {
                static const char *const kWhat[3] = {"block ", "size  ", "by    "};
                for (int i = 0; i < 3; ++i)
                {
                    at(0, 5 + i);
                    print(kWhat[i]);
                    if (i == 2)
                        placed(numbers[i]);
                    else
                        hex(numbers[i]);
                }
                // A block of the heap's: the last of it, and what is past
                // its end -- the 4 bytes that must stay, and on -- for what
                // they look like (whose writing it is).
                const std::uint32_t block = numbers[0], size = numbers[1];
                if (block >= 0x02000000 && block < 0x03000000 && size < 0x01000000)
                {
                    static const char kDigits[] = "0123456789ABCDEF";
                    auto bytes = [&](int row, const char *label, std::uint32_t from, int count)
                    {
                        at(0, row);
                        print(label);
                        char text[3] = {};
                        for (int n = 0; n < count; ++n)
                        {
                            const std::uint8_t b = *reinterpret_cast<const volatile std::uint8_t *>(from + n);
                            text[0] = kDigits[b >> 4];
                            text[1] = kDigits[b & 15];
                            print(text);
                            print(n == 3 || n == 7 ? " " : "");
                        }
                    };
                    bytes(9, "end-8 ", block + size - 8, 8);
                    bytes(10, "end   ", block + size, 8);
                    bytes(11, "end+8 ", block + size + 8, 8);
                }
            }
            // Code addresses on the stack, nearest first: what called what,
            // most likely.
            at(0, 14);
            print("stack:");
            const std::uint32_t *sp = reinterpret_cast<const std::uint32_t *>(regs[13] & ~3u);
            const std::uint32_t *top = reinterpret_cast<const std::uint32_t *>(__sp_usr);
            int shown = 0;
            for (const std::uint32_t *w = sp; w < top && w < sp + 1024 && shown < 8; ++w)
            {
                std::uint32_t offset = 0;
                if (place(*w, offset) == nullptr)
                    continue;
                at((shown & 1) * 16, 15 + shown / 2);
                placed(*w);
                ++shown;
            }
            // The checks.
#ifdef HLDS_GUARD_FULL
            const bool heap = heapBroken() == nullptr && heldBroken() == nullptr && walkChain().bad == 0;
#else
            const bool heap = walkChain().bad == 0;
#endif
            at(0, 20);
            print(heap ? "heap ok " : "heap BAD ");
            print(libraryIntact() ? "dsl ok " : "dsl BAD ");
            print(stackIntact() ? "stack ok" : "stack BAD");
            at(0, 21);
            print("dsl ");
            hex(reinterpret_cast<std::uint32_t>(libraryBase));
            print(" ");
            hex(libraryCode);
            at(0, 23);
            print("build/elf/<name>: addr2line");
            for (;;)
                swiWaitForVBlank();
        }

        [[noreturn]] void onException()
        {
            const std::uint32_t mode = getCPSR() & CPSR_MODE_MASK;
            const std::uint32_t thumb = *(EXCEPTION_STACK_TOP - 3) & CPSR_FLAG_T;
            std::uint32_t regs[16];
            for (int i = 0; i < 16; ++i)
                regs[i] = static_cast<std::uint32_t>(exceptionRegisters[i]);
            const char *what = exceptionMsg;
            std::uint32_t pc = 0, addr = 0;
            if (what == nullptr && mode == CPSR_MODE_ABORT)
            {
                what = "Data abort";
                pc = regs[15] - 8;
                addr = place(pc, addr) != nullptr ? getExceptionAddress(pc, thumb) : pc;
            }
            else
            {
                if (what == nullptr)
                    what = mode == CPSR_MODE_UNDEFINED ? "Undefined instruction" : "Unknown error";
                pc = regs[15] - (thumb ? 2 : 4);
                addr = pc;
            }
            screen(what, true, regs, pc, addr, nullptr);
        }
    } // namespace

    void install()
    {
        std::uint32_t *w = stackFloor();
        for (std::uint32_t i = 0; i < kStackWords; ++i)
            w[i] = kStackMark ^ i;
        setExceptionHandler(onException);
    }

    void noteLibrary(const void *base, std::uint32_t size, std::uint32_t codeBytes)
    {
        libraryBase = static_cast<const std::uint8_t *>(base);
        librarySize = size;
        libraryCode = codeBytes < size ? codeBytes : size;
        librarySum = sum(libraryBase, libraryCode);
    }

    std::uint32_t *stackLimit() { return stackFloor() + kStackWords; }

    void frame()
    {
        if (!stackIntact())
            crash("STACK: overflowed (DTCM)", reinterpret_cast<std::uint32_t>(stackFloor()), kStackWords * 4);
    }

    void check(const char *where)
    {
        frame();
        const int saved = enterCriticalSection();
#ifdef HLDS_GUARD_FULL
        Block *bad = heapBroken();
        if (bad != nullptr)
            crash(bad->magic != kLive ? "HEAP: a block's header broken" : "HEAP: written past its end",
                  reinterpret_cast<std::uint32_t>(userOf(bad)), bad->size, bad->caller);
        bad = heldBroken();
        if (bad != nullptr)
            crash("HEAP: written after it was freed", reinterpret_cast<std::uint32_t>(userOf(bad)), bad->size,
                  bad->caller);
#endif
        // (The block's address, the size word found there, the chunk
        // before: whose end it was written past.)
        const ChainWalk chain = walkChain();
        if (chain.bad != 0)
            crash("HEAP: the allocator's chain broken", chain.bad, chain.badHead, chain.before);
        leaveCriticalSection(saved);
        if (!libraryIntact())
            crash("DSL: its code was written over", reinterpret_cast<std::uint32_t>(libraryBase), libraryCode,
                  reinterpret_cast<std::uint32_t>(where));
    }

    void crash(const char *what, std::uint32_t a, std::uint32_t b, std::uint32_t c)
    {
        std::uint32_t regs[16] = {};
        regs[13] = reinterpret_cast<std::uint32_t>(__builtin_frame_address(0));
        const std::uint32_t numbers[3] = {a, b, c};
        screen(what, false, regs, callerOf(__builtin_return_address(0)), 0, numbers);
    }

    std::uint32_t liveBlocks() { return blocks; }
    std::uint32_t liveBytes() { return bytes; }

    void renewReserve()
    {
#ifndef HLDS_GUARD_FULL
        if (reserve == nullptr)
            reserve = __real_malloc(kReserve);
        inPlay = false;
        letGoLoad = letGoPlay = letGoAsked = 0;
#endif
    }

    ReserveState reserveState()
    {
        ReserveState state;
#ifndef HLDS_GUARD_FULL
        state.held = reserve != nullptr;
        state.letGoLoad = letGoLoad;
        state.letGoPlay = letGoPlay;
        state.lastAsked = letGoAsked;
#endif
        return state;
    }

    void markPlay()
    {
#ifndef HLDS_GUARD_FULL
        inPlay = true;
#endif
        const int saved = enterCriticalSection();
        playBase = playPeak = bytes;
        leaveCriticalSection(saved);
    }

    std::uint32_t playRise() { return playPeak - playBase; }

    HeapRoom heapRoom()
    {
        // From the allocator's chain: the top is what sbrk has not given
        // yet and the free top chunk below it, one piece; the holes are the
        // free chunks inside.
        const int saved = enterCriticalSection();
        const ChainWalk chain = walkChain();
        leaveCriticalSection(saved);
        HeapRoom room;
        room.top = static_cast<std::uint32_t>(getHeapLimit() - getHeapEnd());
        if (chain.bad == 0)
        {
            room.top += chain.topFree > 4 ? chain.topFree - 4 : 0;
            room.largestHole = chain.largestHole;
            room.inHoles = chain.inHoles;
        }
        return room;
    }
} // namespace hlrt::guard

// ---- what the linker sends malloc and the rest to

extern "C"
{
    void *__wrap_malloc(std::size_t size)
    {
        return hlrt::guard::take(8, size, reinterpret_cast<std::uint32_t>(__builtin_return_address(0)));
    }

    void __wrap_free(void *p)
    {
        hlrt::guard::give(p, reinterpret_cast<std::uint32_t>(__builtin_return_address(0)));
    }

    void *__wrap_calloc(std::size_t count, std::size_t size)
    {
        if (size != 0 && count > 0x0FFFFFFF / size)
            return nullptr;
#ifdef HLDS_GUARD_FULL
        void *p = hlrt::guard::take(8, count * size, reinterpret_cast<std::uint32_t>(__builtin_return_address(0)));
        if (p != nullptr)
            std::memset(p, 0, count * size);
#else
        void *p = __real_calloc(count, size);
        if (p != nullptr)
            hlrt::guard::counted(p, 1);
#endif
        return p;
    }

    void *__wrap_memalign(std::size_t align, std::size_t size)
    {
        if (align == 0 || (align & (align - 1)) != 0 || align > 0x8000)
            return nullptr;
        return hlrt::guard::take(align, size, reinterpret_cast<std::uint32_t>(__builtin_return_address(0)));
    }

    void *__wrap_aligned_alloc(std::size_t align, std::size_t size)
    {
        if (align == 0 || (align & (align - 1)) != 0 || align > 0x8000)
            return nullptr;
        return hlrt::guard::take(align, size, reinterpret_cast<std::uint32_t>(__builtin_return_address(0)));
    }

    int __wrap_posix_memalign(void **out, std::size_t align, std::size_t size)
    {
        if (align < sizeof(void *) || (align & (align - 1)) != 0 || align > 0x8000)
            return EINVAL;
        void *p = hlrt::guard::take(align, size, reinterpret_cast<std::uint32_t>(__builtin_return_address(0)));
        if (p == nullptr)
            return ENOMEM;
        *out = p;
        return 0;
    }

    void *__wrap_realloc(void *p, std::size_t size)
    {
        const std::uint32_t caller = reinterpret_cast<std::uint32_t>(__builtin_return_address(0));
        if (p == nullptr)
            return hlrt::guard::take(8, size, caller);
        if (size == 0)
        {
            hlrt::guard::give(p, caller);
            return nullptr;
        }
#ifdef HLDS_GUARD_FULL
        const int saved = enterCriticalSection();
        hlrt::guard::Block *b = hlrt::guard::checked(p, caller);
        const std::uint32_t had = b->size, align = b->align;
        leaveCriticalSection(saved);
        void *q = hlrt::guard::take(align, size, caller);
        if (q == nullptr)
            return nullptr; // (the old block stays, as realloc's does)
        std::memcpy(q, p, had < size ? had : size);
        hlrt::guard::give(p, caller);
        return q;
#else
        hlrt::guard::counted(p, -1);
        void *q = __real_realloc(p, size);
        hlrt::guard::counted(q != nullptr ? q : p, 1);
        return q;
#endif
    }
}
