#include "net/OnlineDll.hpp"

#include "net/OnlineApi.hpp"

#ifndef HLDS_CHILD
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <malloc.h>
#include <nds.h>

#include "Global.hpp"
#include "HlDsRoom.hpp"
#include "game/FamilyApi.hpp"
#include "Settings.hpp"
#include "render/HudDraw.hpp"

// (The linker script's: where the program ends, and its heap begins.)
extern "C" char __end__[];
#endif

namespace hlrt::online
{
#ifdef HLDS_CHILD
    // (A console sent the game is never online.)
    const OnlineFuncs *funcs() { return nullptr; }
    bool load(const char **why)
    {
        *why = "Not on a console sent the game";
        return false;
    }
    std::size_t heapBytes() { return 0; }
    bool campaignBlocked() { return false; }
#else
    namespace
    {
        constexpr const char *kPath = "nitro:/dsl/online.dsl";
        void *library = nullptr;
        OnlineFuncs table{};
        bool loaded = false;
        std::size_t bytes = 0;
        char why_[160];

        const char *refuse(const char *what)
        {
            std::snprintf(why_, sizeof(why_), "online.dsl: %s", what);
            return why_;
        }

        std::size_t heapUsed()
        {
            const struct mallinfo info = mallinfo();
            return static_cast<std::size_t>(info.uordblks);
        }
    } // namespace

    const OnlineFuncs *funcs() { return loaded ? &table : nullptr; }

    bool load(const char **why)
    {
        if (loaded)
            return true;
        // As low on the heap as can be, as the families are: the title's
        // moving picture (a megabyte, at the heap's top) put away first, and
        // read again after. The library stays for good: anything it parted
        // would stay parted.
        const bool menu = hudDraw.menuShowing();
        if (menu)
            hudDraw.hideMenu();
        const std::size_t before = heapUsed();
        library = dlopen(kPath, RTLD_NOW | RTLD_LOCAL);
        bool ok = library != nullptr;
        if (!ok)
            *why = refuse(dlerror());
        if (ok)
        {
            // (No constructors -- tools/dsl_family.py sees to it -- so nothing
            // of one made for another program has run.)
            const auto *id = static_cast<const unsigned char *>(dlsym(library, "hlFamilyProgramId"));
            const auto api = reinterpret_cast<GetOnlineApiFn>(dlsym(library, "GetOnlineAPI"));
            if (id == nullptr || std::memcmp(id, hlProgramId, kProgramIdBytes) != 0)
            {
                *why = refuse(id == nullptr ? "not the game's" : "built for another program than this one (its ID differs)");
                ok = false;
            }
            else if (api == nullptr || !api(kOnlineApiVersion, &table))
            {
                *why = refuse("not of this build's headers");
                ok = false;
            }
            if (!ok)
            {
                dlclose(library);
                library = nullptr;
            }
        }
        if (ok)
        {
            loaded = true;
            bytes = heapUsed() - before;
            if (perfLogging())
            {
                if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
                {
                    std::fprintf(log, "online: library at %08lx, %lu bytes of the heap\n",
                                 static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(table.connectBegin)),
                                 static_cast<unsigned long>(bytes));
                    std::fclose(log);
                }
            }
        }
        if (menu)
            hudDraw.showMenu();
        return ok;
    }

    std::size_t heapBytes() { return bytes; }

    bool campaignBlocked()
    {
        if (!loaded || isDSiMode())
            return false;
        // What the heaviest campaign map can spare: kDsMapHeap's margin (32
        // KB), and what the program has shrunk by since it was measured
        // (tools/arm9-report.sh's "map heap margin"). The IP stack's own
        // heap besides the library's (its packets, its thread) is counted
        // in as the library's again: about as much.
        const long shrunk = static_cast<long>(hldata::kDsMapHeapProgramEnd) -
                            static_cast<long>(reinterpret_cast<std::uintptr_t>(__end__));
        const long margin = 32 * 1024 + shrunk;
        return static_cast<long>(2 * bytes) > margin;
    }
#endif
} // namespace hlrt::online
