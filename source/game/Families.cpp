#include "game/Families.hpp"

#if defined(HL_FAMILIES_APART) || defined(HL_NET_APART)
#define HL_SOME_APART 1
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <nds.h>

#include "Global.hpp"
#include "game/FamilyApi.hpp"
#include "game/GameLayout.hpp"
#ifdef HLDS_CHILD
#include "net/GuestLoad.hpp"
#endif

#ifdef HLDS_DEBUG_BUILD
// (The linker script's: where the program's parts end.)
extern "C" char __twl_end__[], __itcm_start[], __itcm_end[], __dtcm_start[];
#endif
#endif

namespace hlrt::families
{
    namespace
    {
        using hldata::familyBit;
        using hldata::GameFamily;

        /// The families this program has as libraries; the rest are linked
        /// in, always there.
        constexpr std::uint8_t kApart =
#ifdef HL_FAMILIES_APART
            familyBit(hldata::kFamilyBoss) | familyBit(hldata::kFamilyMilitary) |
#endif
#ifdef HL_NET_APART
            familyBit(hldata::kFamilyNet) |
#endif
            0;
    } // namespace

    std::uint8_t ofHeader(const hldata::LevelHeader &header)
    {
        using namespace hldata;
        if ((header.families & kLevelFamiliesKnown) == 0)
        {
#ifdef HLDS_CHILD
            // (Sent the game: a map of a match never has a family's monsters,
            // and the host sends families only to a demo's console.)
            if (!guest::demoPlaying())
                return 0;
#endif
            return familyBit(kFamilyBoss) | familyBit(kFamilyMilitary);
        }
        return static_cast<std::uint8_t>(header.families & ~kLevelFamiliesKnown);
    }

    const char *path(GameFamily family)
    {
        static const char *const kPath[hldata::kFamilyCount] = {nullptr, "nitro:/dsl/boss.dsl",
                                                                "nitro:/dsl/military.dsl", "nitro:/dsl/net.dsl"};
        return family < hldata::kFamilyCount ? kPath[family] : nullptr;
    }

    const char *sentPath(GameFamily family)
    {
        static const char *const kSent[hldata::kFamilyCount] = {nullptr, "nitro:/dlplay/boss.dsl",
                                                                "nitro:/dlplay/military.dsl", nullptr};
        return family < hldata::kFamilyCount ? kSent[family] : nullptr;
    }

#ifndef HL_SOME_APART
    // (All of them linked in.)
    bool want(std::uint8_t, const char **) { return true; }
    bool need(GameFamily, const char **) { return true; }
    std::uint8_t loaded() { return 0xFF; }
#else
    namespace
    {
        struct Family
        {
            void *library = nullptr;
            FamilyFuncs funcs{};
        };
        Family families_[hldata::kFamilyCount];
        bool resolverSet = false;
        char why_[160];

#ifdef HLDS_DEBUG_BUILD
        /// (A debug build's extra check, beside the ID's: every address a
        /// family is given is the program's -- its code and data in main
        /// RAM, ITCM or DTCM. One that is not came from another program.)
        bool programs(const char *, std::uint32_t *value, std::uint32_t attributes)
        {
            if ((attributes & (DSL_SYMBOL_MAIN_BINARY | DSL_SYMBOL_UNRESOLVED)) == 0)
                return true;
            const std::uint32_t v = *value & ~1u;
            return (v >= 0x02000000u && v < reinterpret_cast<std::uint32_t>(__twl_end__)) ||
                   (v >= reinterpret_cast<std::uint32_t>(__itcm_start) &&
                    v < reinterpret_cast<std::uint32_t>(__itcm_end)) ||
                   (v >= reinterpret_cast<std::uint32_t>(__dtcm_start) && v < 0x03000000u);
        }
#endif

        /// What was not had, naming the file.
        const char *refuse(const char *name, const char *what)
        {
            std::snprintf(why_, sizeof(why_), "%s: %s", name, what);
            return why_;
        }

        void apply(GameFamily f, bool on)
        {
            Family &family = families_[f];
            family.funcs.attach(on);
#ifdef HL_NET_APART
            if (f == hldata::kFamilyNet)
            {
                gameApi.netStart = on ? family.funcs.netStart : nullptr;
                gameApi.netTick = on ? family.funcs.netTick : nullptr;
                gameApi.netRead = on ? family.funcs.netRead : nullptr;
                gameApi.netUsed = on ? family.funcs.netUsed : nullptr;
                gameApi.netLobby = on ? family.funcs.netLobby : nullptr;
            }
#endif
        }

        void drop(GameFamily f)
        {
            Family &family = families_[f];
            if (family.library == nullptr)
                return;
            apply(f, false);
            dlclose(family.library);
            family = Family{};
        }

        /// The library opened: from the ROM's files, or, sent the game, from
        /// the host (into memory, then out of it).
        void *open(GameFamily f, const char *&name, const char **why)
        {
#ifdef HLDS_CHILD
            name = hldata::kFamilyNames[f];
            std::uint8_t *data = nullptr;
            std::uint32_t size = 0;
            const char *fetched = nullptr;
            if (!guest::familyFetch(f, data, size, &fetched))
            {
                *why = refuse(name, fetched != nullptr ? fetched : "not sent by the host");
                return nullptr;
            }
            FILE *file = fmemopen(data, size, "rb");
            void *library = file != nullptr ? dlopen_FILE(file, RTLD_NOW | RTLD_LOCAL) : nullptr;
            if (file != nullptr)
                std::fclose(file);
            std::free(data);
#else
            name = path(f);
            void *library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
            if (library == nullptr)
                *why = refuse(name, dlerror());
            return library;
        }

        bool load(GameFamily f, const char **why)
        {
            Family &family = families_[f];
            if (family.library != nullptr)
                return true;
            if (!resolverSet)
            {
#ifdef HLDS_DEBUG_BUILD
                dsl_set_symbol_resolver(programs);
#else
                dsl_set_symbol_resolver(nullptr);
#endif
                resolverSet = true;
            }
            // (A family has no constructors -- tools/dsl_family.py sees to
            // it -- so loading one runs nothing of it: one for another
            // program is let go below before anything of it is called.)
            const char *name = nullptr;
            void *library = open(f, name, why);
            if (library == nullptr)
                return false;
            const auto *id = static_cast<const unsigned char *>(dlsym(library, "hlFamilyProgramId"));
            if (id == nullptr || std::memcmp(id, hlProgramId, kProgramIdBytes) != 0)
            {
                dlclose(library);
                *why = refuse(name, id == nullptr ? "not a family of the game"
                                                  : "built for another program than this one (its ID differs)");
                return false;
            }
            const auto api = reinterpret_cast<GetFamilyApiFn>(dlsym(library, "GetFamilyAPI"));
            FamilyFuncs funcs{};
            if (api == nullptr || !api(kFamilyApiVersion, gameLayout(), &hlEngine, &funcs) || funcs.attach == nullptr)
            {
                dlclose(library);
                *why = refuse(name, "not of this build's headers");
                return false;
            }
            family.library = library;
            family.funcs = funcs;
#ifdef HLDS_ITCM_OFF
            // (The build that measures the instruction cache: where the
            // family's code lies, for tools/icache_rank.py to name what of
            // the heap's lines is its -- by its GetFamilyAPI, whose place in
            // the family's own ELF says the rest.)
            if (std::FILE *log = std::fopen(global.dataPath("perf.txt").c_str(), "a"))
            {
                static const char *const kName[hldata::kFamilyCount] = {"", "boss", "military", "net"};
                std::fprintf(log, "family: %s %08lx\n", kName[f], static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(api)));
                std::fclose(log);
            }
#endif
            apply(f, true);
            return true;
        }
    } // namespace

    bool want(std::uint8_t mask, const char **why)
    {
        // Those not wanted first: their memory is free for those that are.
        for (int f = hldata::kFamilyBoss; f < hldata::kFamilyCount; ++f)
        {
            const std::uint8_t bit = familyBit(static_cast<GameFamily>(f));
            if ((kApart & bit) && !(mask & bit))
                drop(static_cast<GameFamily>(f));
        }
        bool all = true;
        for (int f = hldata::kFamilyBoss; f < hldata::kFamilyCount; ++f)
        {
            const std::uint8_t bit = familyBit(static_cast<GameFamily>(f));
            if ((kApart & bit) && (mask & bit))
                all = load(static_cast<GameFamily>(f), why) && all;
        }
        return all;
    }

    bool need(GameFamily family, const char **why)
    {
        return !(kApart & familyBit(family)) || families_[family].library != nullptr || load(family, why);
    }

    std::uint8_t loaded()
    {
        std::uint8_t mask = static_cast<std::uint8_t>(~kApart);
        for (int f = hldata::kFamilyBoss; f < hldata::kFamilyCount; ++f)
        {
            if (families_[f].library != nullptr)
                mask |= familyBit(static_cast<GameFamily>(f));
        }
        return mask;
    }
#endif
} // namespace hlrt::families
