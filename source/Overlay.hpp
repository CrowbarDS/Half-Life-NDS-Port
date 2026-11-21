#pragma once

// Overlays: code and data that share one region of main memory, one phase
// at a time, read from the ROM when the phase begins (tools/ovl.py makes
// the regions, checks them and packs the overlays; docs/TECHNICAL.md).
//
// A DS's map heap begins where the program ends. A region is as large as
// its largest overlay, so what the program saves is the rest of them: code
// only the menus run and a buffer only a map uses, say, in one place.
//
// What is in an overlay: whole files (tools/ovl.py's manifest), or a
// function or a variable put there by attribute:
//   HL_OVL_CODE(front) void drawProfile();   code in overlay `front`
//   HL_OVL_BSS(map) static char buffer[N];   zeroed data in overlay `map`
//   HL_OVL_RODATA(front) const char text[] = "...";   constants in `front`
// The program calls into one only through a GATE, a function of its own:
//   HL_OVL_GATED void drawProfile() { HL_OVL_GATE(Front); detail::drawProfile(); }
// which is the overlay loaded, or a halt that names it -- never a jump into
// whatever else is in its region. (tools/ovl.py check fails the link on a
// reference into an overlay from anywhere else: a gate is known by its
// section, .text.ovlgate.*, and must check.)
//
// Nothing of it in a program without overlays, nor in the Download Play
// child, which has no card to read them from.

#include <cstdint>

#if __has_include("OverlayList.gen.hpp") && !defined(HLDS_CHILD)
#include "OverlayList.gen.hpp"
#endif
#ifndef HL_OVL_OVERLAYS
#define HL_OVL_REGIONS(X)
#define HL_OVL_OVERLAYS(X)
#define HL_OVL_COUNT 0
#define HL_OVL_REGION_COUNT 0
#endif

#define HL_OVL_STR2(x) #x
#define HL_OVL_STR(x) HL_OVL_STR2(x)
#if HL_OVL_COUNT > 0
#define HL_OVL_CODE(name) __attribute__((section(".ovl." #name ".text." HL_OVL_STR(__COUNTER__)), noinline))
#define HL_OVL_BSS(name) __attribute__((section(".ovl." #name ".bss." HL_OVL_STR(__COUNTER__))))
#define HL_OVL_RODATA(name) __attribute__((section(".ovl." #name ".rodata." HL_OVL_STR(__COUNTER__))))
#else
// (No overlays -- the child, a program without regions: the program's own,
// and free to be inlined into its gate.)
#define HL_OVL_CODE(name)
#define HL_OVL_BSS(name)
#define HL_OVL_RODATA(name)
#endif
#define HL_OVL_GATED __attribute__((noinline, section(".text.ovlgate." HL_OVL_STR(__COUNTER__))))

namespace hlrt::ovl
{
    enum class Region : std::uint8_t
    {
#define HL_OVL_REGION_ENUM(R, i) R = i,
        HL_OVL_REGIONS(HL_OVL_REGION_ENUM)
#undef HL_OVL_REGION_ENUM
    };

    /// None, then the manifest's overlays in order.
    enum class Id : std::uint8_t
    {
        None,
#define HL_OVL_ID_ENUM(I, name, R, loads) I,
        HL_OVL_OVERLAYS(HL_OVL_ID_ENUM)
#undef HL_OVL_ID_ENUM
    };

    /// Open the pack (nitro:/ovl/arm9.ovl, or `path`) for good and check it
    /// against the program: the same overlays, at the same addresses, of the
    /// same sizes, made with this program. False (and `why`) if not; nothing
    /// may be loaded then. (HL_OVL_FROM_ROM: the overlays are the ROM's own
    /// overlay table's files instead, opened by their ids in its FAT and
    /// checked by their sizes; `path` is not used.)
    bool init(const char *path, const char **why);

    /// The phase that needs `id` begins: it is put in its region, whatever
    /// was there gone (its code, and its data with it). Nothing if it is
    /// there already. Never from an interrupt, nor from an overlay's own
    /// code (tools/ovl.py: R4). A read that fails halts, naming it.
    void load(Id id);

    /// The same, for a phase that may be put off: false, and nothing done,
    /// if what is in the region may not leave yet (its leave check). A read
    /// that fails still halts.
    bool tryLoad(Id id);

    /// What is in region `r` now (None at first).
    Id current(Region r);

    /// Whether `id` is in its region now.
    bool loaded(Id id);

    /// A gate found its overlay not there: a halt that says which, and who
    /// wanted it, and what is there instead.
    [[noreturn]] void missing(Id want, const char *who);

    /// An overlay's name ("front"), for the crash screen.
    const char *name(Id id);

    /// Whether every loaded overlay's code is as it was read (its sum): the
    /// guard's check of what nothing should write.
    bool whole();

    /// Whether `address` is in a region; if it is, what is loaded there
    /// (None: nothing) and how far into the region: for the crash screen's
    /// "front+40".
    bool place(std::uint32_t address, Id &id, std::uint32_t &offset);

    /// Before `out` leaves its region: whether it may (a menu's picture
    /// still shown, a map still mounted: no). A halt names it if not.
    using LeaveCheck = bool (*)();
    void setLeaveCheck(Id out, LeaveCheck check);
} // namespace hlrt::ovl

#if HL_OVL_COUNT > 0
#define HL_OVL_GATE(I)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!::hlrt::ovl::loaded(::hlrt::ovl::Id::I)) [[unlikely]]                                                     \
            ::hlrt::ovl::missing(::hlrt::ovl::Id::I, __func__);                                                        \
    } while (0)
#else
#define HL_OVL_GATE(I) ((void)0)
#endif

// What the loader does when it cannot go on: the program's own halt (the
// crash screen, Guard.arm.cpp), or, without one, a stop.
extern "C" [[noreturn]] void hlOvlHalt(const char *what, const char *detail);
