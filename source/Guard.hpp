#pragma once

// What keeps watch for memory going wrong, and what says so when it has.
//
// * The heap: every block the program asks for (malloc and the rest, the
//   game library's too: GameDll's resolver) has a header -- its size, who
//   asked for it -- and four bytes after its end that must stay as they
//   were. A block given back is checked; small ones are kept back a while,
//   filled with a pattern that must still be there when they really go. So
//   a write past a block's end, or into one given back, is caught with the
//   block's size and the address of the code that asked for it, instead of
//   breaking the allocator's own lists to be found much later (a crash in
//   free(), far from the culprit: 2026-10-28).
// * The stack: the lowest words of its room (DTCM) hold a pattern, looked
//   at every frame.
// * The game library's code (hl.dsl): summed when it is loaded, and summed
//   again at each map load and save, and on the crash screen.
// * The crash screen (in place of libnds's): the build's name
//   (tools/buildid.py; its ELFs are under build/elf/<name>/), where the
//   fault was -- the program, the library and how far into it, or ITCM --
//   the registers, the code addresses found on the stack, and the three
//   checks above.

#include <cstddef>
#include <cstdint>

namespace hlrt::guard
{
    /// At the start of main(): the crash screen, and the stack's pattern.
    void install();
    /// The game library, loaded at `base`, `size` bytes in all, of which the
    /// first `codeBytes` never change once it is (its code and constants).
    void noteLibrary(const void *base, std::uint32_t size, std::uint32_t codeBytes);
    /// Every frame: the stack's pattern. Halts if it is gone.
    void frame();
    /// Everything checked (heap, library, stack): halts on anything wrong.
    /// `where`: what the game was doing, for the screen.
    void check(const char *where);

    /// Stops the game on the crash screen with `what` and up to three
    /// numbers (a block, its size, who asked for it).
    [[noreturn]] void crash(const char *what, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0);

    /// Blocks and bytes held (the debug readout).
    std::uint32_t liveBlocks();
    std::uint32_t liveBytes();
} // namespace hlrt::guard

extern "C"
{
    extern const char hlBuildId[]; // source/BuildId.gen.cpp (tools/buildid.py)
    extern const char hlVersion[]; // ... and what the title shows

    // The guarded heap, in place of malloc and the rest (build.py: --wrap).
    void *__wrap_malloc(std::size_t size);
    void __wrap_free(void *p);
    void *__wrap_calloc(std::size_t count, std::size_t size);
    void *__wrap_realloc(void *p, std::size_t size);
    void *__wrap_memalign(std::size_t align, std::size_t size);
    void *__wrap_aligned_alloc(std::size_t align, std::size_t size);
    int __wrap_posix_memalign(void **out, std::size_t align, std::size_t size);
}
