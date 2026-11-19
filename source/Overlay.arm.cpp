#include "Overlay.hpp"

#include <cstring>

#if HL_OVL_COUNT > 0
#include <fcntl.h>
#include <unistd.h>

#include <nds/arm9/cache.h>
#endif

// (ARM: it asks the processor's mode, to refuse an interrupt's call.)

extern "C" __attribute__((weak, noreturn)) void hlOvlHalt(const char *what, const char *detail)
{
    (void)what;
    (void)detail;
    for (;;)
    {
    }
}

#if HL_OVL_COUNT > 0
// The linker's word on where each overlay is (tools/ovl.py ld).
extern "C"
{
#define HL_OVL_SYMBOLS(I, name, R, loads)                                                                              \
    extern char __ovl_##name##_start[];                                                                                \
    extern char __ovl_##name##_load_end[];                                                                             \
    extern char __ovl_##name##_end[];
    HL_OVL_OVERLAYS(HL_OVL_SYMBOLS)
#undef HL_OVL_SYMBOLS
#define HL_OVL_REGION_SYMBOLS(R, i)                                                                                    \
    extern char __ovl_##R##_start[];                                                                                   \
    extern char __ovl_##R##_end[];
    HL_OVL_REGIONS(HL_OVL_REGION_SYMBOLS)
#undef HL_OVL_REGION_SYMBOLS
    // (The program's ID, if it has one: the pack must be of it.)
    extern const unsigned char hlProgramId[8] __attribute__((weak));
}
#endif

namespace hlrt::ovl
{
#if HL_OVL_COUNT > 0
    namespace
    {
        struct Known
        {
            const char *name;
            std::uint8_t region;
            bool loads;
            char *start, *loadEnd, *end;
        };
        const Known kKnown[HL_OVL_COUNT] = {
#define HL_OVL_KNOWN(I, n, R, loads) {#n, static_cast<std::uint8_t>(Region::R), loads != 0, __ovl_##n##_start, __ovl_##n##_load_end, __ovl_##n##_end},
            HL_OVL_OVERLAYS(HL_OVL_KNOWN)
#undef HL_OVL_KNOWN
        };
        struct RegionBounds
        {
            char *start, *end;
        };
        const RegionBounds kRegions[HL_OVL_REGION_COUNT] = {
#define HL_OVL_REGION_BOUNDS(R, i) {__ovl_##R##_start, __ovl_##R##_end},
            HL_OVL_REGIONS(HL_OVL_REGION_BOUNDS)
#undef HL_OVL_REGION_BOUNDS
        };

        // The pack's table (tools/ovl.py: PACK), kept from init.
        struct Entry
        {
            char name[16];
            std::uint32_t region, address, load, bss, offset, sum, reserved[2];
        };
        static_assert(sizeof(Entry) == 48, "tools/ovl.py: PACK_ENTRY");
        Entry table[HL_OVL_COUNT];
        int fd = -1;
        Id inRegion[HL_OVL_REGION_COUNT];
        LeaveCheck leaveChecks[HL_OVL_COUNT];

        std::uint32_t sumOf(const void *at, std::uint32_t bytes)
        {
            const std::uint32_t *w = static_cast<const std::uint32_t *>(at);
            std::uint32_t total = 0;
            for (std::uint32_t i = 0; i < bytes / 4; ++i)
                total += w[i];
            return total;
        }

        bool inInterrupt()
        {
            std::uint32_t cpsr;
            asm volatile("mrs %0, cpsr" : "=r"(cpsr));
            return (cpsr & 0x1F) == 0x12; // IRQ mode
        }

        int index(Id id) { return static_cast<int>(id) - 1; }

        [[noreturn]] void fail(const char *what, Id id)
        {
            hlOvlHalt(what, id == Id::None ? "" : kKnown[index(id)].name);
        }
    } // namespace

    bool init(const char *path, const char **why)
    {
        const char *ignored = nullptr;
        if (why == nullptr)
            why = &ignored;
        fd = open(path != nullptr ? path : "nitro:/ovl/arm9.ovl", O_RDONLY);
        if (fd < 0)
        {
            *why = "no overlay pack (ovl/arm9.ovl)";
            return false;
        }
        unsigned char head[32];
        if (read(fd, head, sizeof(head)) != static_cast<ssize_t>(sizeof(head)) ||
            std::memcmp(head, "HLOV", 4) != 0 || head[4] != 1 || head[5] != 0 || head[6] != HL_OVL_COUNT ||
            head[7] != 0)
        {
            *why = "the overlay pack is not this program's (its kind or its count)";
            close(fd);
            fd = -1;
            return false;
        }
        if (hlProgramId != nullptr && std::memcmp(head + 8, hlProgramId, 8) != 0)
        {
            *why = "the overlay pack was made with another build of the program";
            close(fd);
            fd = -1;
            return false;
        }
        if (read(fd, table, sizeof(table)) != static_cast<ssize_t>(sizeof(table)))
        {
            *why = "the overlay pack is cut short";
            close(fd);
            fd = -1;
            return false;
        }
        for (int i = 0; i < HL_OVL_COUNT; ++i)
        {
            const Known &k = kKnown[i];
            const Entry &e = table[i];
            const std::uint32_t load = static_cast<std::uint32_t>(k.loadEnd - k.start);
            const std::uint32_t bss = static_cast<std::uint32_t>(k.end - k.loadEnd);
            if (std::strncmp(e.name, k.name, sizeof(e.name)) != 0 || e.region != k.region ||
                e.address != reinterpret_cast<std::uintptr_t>(k.start) || e.load != load || e.bss != bss)
            {
                *why = "the overlay pack does not match the program's overlays";
                close(fd);
                fd = -1;
                return false;
            }
        }
        for (Id &r : inRegion)
            r = Id::None;
        return true;
    }

    void load(Id id)
    {
        if (id == Id::None)
            return;
        const int i = index(id);
        const Known &k = kKnown[i];
        Id &there = inRegion[k.region];
        if (there == id)
            return;
        if (inInterrupt())
            fail("An overlay was asked for from an interrupt:", id);
        if (fd < 0)
            fail("No overlay pack to load from:", id);
        if (there != Id::None && leaveChecks[index(there)] != nullptr && !leaveChecks[index(there)]())
            fail("An overlay was still in use as it was to be replaced:", there);
        // Nothing of what was there is kept: its lines in the cache go too.
        there = Id::None;
        const RegionBounds &b = kRegions[k.region];
        DC_InvalidateRange(b.start, static_cast<std::uint32_t>(b.end - b.start));
        const Entry &e = table[i];
        if (e.load != 0)
        {
            if (lseek(fd, static_cast<off_t>(e.offset), SEEK_SET) != static_cast<off_t>(e.offset) ||
                read(fd, k.start, e.load) != static_cast<ssize_t>(e.load))
                fail("An overlay could not be read from the card:", id);
            DC_FlushRange(k.start, e.load);
        }
        std::memset(k.loadEnd, 0, e.bss);
        DC_FlushRange(k.loadEnd, e.bss);
        if (e.load != 0)
        {
            IC_InvalidateRange(k.start, e.load);
            if (sumOf(k.start, e.load) != e.sum)
                fail("An overlay was not read whole from the card:", id);
        }
        there = id;
    }

    Id current(Region r) { return inRegion[static_cast<int>(r)]; }

    bool loaded(Id id) { return id != Id::None && inRegion[kKnown[index(id)].region] == id; }

    void missing(Id want, const char *who)
    {
        (void)who;
        fail("A gate found its overlay not loaded:", want);
    }

    const char *name(Id id) { return id == Id::None ? "none" : kKnown[index(id)].name; }

    bool whole()
    {
        for (int r = 0; r < HL_OVL_REGION_COUNT; ++r)
        {
            const Id id = inRegion[r];
            if (id == Id::None)
                continue;
            const Entry &e = table[index(id)];
            if (e.load != 0 && sumOf(kKnown[index(id)].start, e.load) != e.sum)
                return false;
        }
        return true;
    }

    Id place(std::uint32_t address, std::uint32_t &offset)
    {
        for (int r = 0; r < HL_OVL_REGION_COUNT; ++r)
        {
            const RegionBounds &b = kRegions[r];
            if (address < reinterpret_cast<std::uintptr_t>(b.start) || address >= reinterpret_cast<std::uintptr_t>(b.end))
                continue;
            offset = address - reinterpret_cast<std::uintptr_t>(b.start);
            return inRegion[r];
        }
        offset = 0;
        return Id::None;
    }

    void setLeaveCheck(Id out, LeaveCheck check)
    {
        if (out != Id::None)
            leaveChecks[index(out)] = check;
    }
#else
    // (No overlays: everything there is, is always there.)
    bool init(const char *, const char **) { return true; }
    void load(Id) {}
    Id current(Region) { return Id::None; }
    bool loaded(Id) { return true; }
    void missing(Id, const char *) { hlOvlHalt("A gate found its overlay not loaded:", ""); }
    const char *name(Id) { return "none"; }
    bool whole() { return true; }
    Id place(std::uint32_t, std::uint32_t &offset)
    {
        offset = 0;
        return Id::None;
    }
    void setLeaveCheck(Id, LeaveCheck) {}
#endif
} // namespace hlrt::ovl
