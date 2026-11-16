#include "game/Families.hpp"

#ifdef HL_FAMILIES_APART
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <filesystem.h>
#include <nds.h>

#include "game/FamilyApi.hpp"
#include "game/GameLayout.hpp"

#ifdef HLDS_DEBUG_BUILD
// (The linker script's: where the program's parts end.)
extern "C" char __twl_end__[], __itcm_start[], __itcm_end[], __dtcm_start[];
#endif
#endif

namespace hlrt::families
{
    std::uint8_t ofHeader(const hldata::LevelHeader &header)
    {
        using namespace hldata;
        if ((header.families & kLevelFamiliesKnown) == 0)
            return familyBit(kFamilyBoss) | familyBit(kFamilyMilitary);
        return static_cast<std::uint8_t>(header.families & ~kLevelFamiliesKnown);
    }

#ifndef HL_FAMILIES_APART
    // (All of them linked in.)
    bool want(std::uint8_t, const char **) { return true; }
    bool need(hldata::GameFamily, const char **) { return true; }
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
        constexpr const char *kPath[hldata::kFamilyCount] = {nullptr, "dsl/boss.dsl", "dsl/military.dsl",
                                                              "dsl/net.dsl"};
        bool filesUp = false;
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
        const char *refuse(const char *path, const char *what)
        {
            std::snprintf(why_, sizeof(why_), "%s: %s", path, what);
            return why_;
        }

        void apply(hldata::GameFamily f, bool on)
        {
            Family &family = families_[f];
            family.funcs.attach(on);
            if (f == hldata::kFamilyNet)
            {
                gameApi.netStart = on ? family.funcs.netStart : nullptr;
                gameApi.netTick = on ? family.funcs.netTick : nullptr;
                gameApi.netRead = on ? family.funcs.netRead : nullptr;
                gameApi.netUsed = on ? family.funcs.netUsed : nullptr;
                gameApi.netLobby = on ? family.funcs.netLobby : nullptr;
            }
        }

        void drop(hldata::GameFamily f)
        {
            Family &family = families_[f];
            if (family.library == nullptr)
                return;
            apply(f, false);
            dlclose(family.library);
            family = Family{};
        }

        bool load(hldata::GameFamily f, const char **why)
        {
            Family &family = families_[f];
            if (family.library != nullptr)
                return true;
            const char *path = kPath[f];
            if (!filesUp)
            {
                // (The ROM's own file system, read through the card's FAT,
                // which Global::Init has brought up.)
                if (!nitroFSInit(nullptr))
                {
                    *why = refuse(path, "the ROM's files cannot be read");
                    return false;
                }
#ifdef HLDS_DEBUG_BUILD
                dsl_set_symbol_resolver(programs);
#else
                dsl_set_symbol_resolver(nullptr);
#endif
                filesUp = true;
            }
            // (A family has no constructors -- tools/dsl_post.py sees to it --
            // so loading one runs nothing of it: one for another program is
            // let go below before anything of it is called.)
            void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
            if (library == nullptr)
            {
                *why = refuse(path, dlerror());
                return false;
            }
            const auto *id = static_cast<const unsigned char *>(dlsym(library, "hlFamilyProgramId"));
            if (id == nullptr || std::memcmp(id, hlProgramId, kProgramIdBytes) != 0)
            {
                dlclose(library);
                *why = refuse(path, id == nullptr ? "not a family of the game"
                                                  : "built for another program than this one (its ID differs)");
                return false;
            }
            const auto api = reinterpret_cast<GetFamilyApiFn>(dlsym(library, "GetFamilyAPI"));
            FamilyFuncs funcs{};
            if (api == nullptr || !api(kFamilyApiVersion, gameLayout(), &hlEngine, &funcs) || funcs.attach == nullptr)
            {
                dlclose(library);
                *why = refuse(path, "not of this build's headers");
                return false;
            }
            family.library = library;
            family.funcs = funcs;
            apply(f, true);
            return true;
        }
    } // namespace

    bool want(std::uint8_t mask, const char **why)
    {
        // Those not wanted first: their memory is free for those that are.
        for (int f = hldata::kFamilyBoss; f < hldata::kFamilyCount; ++f)
        {
            if (!(mask & hldata::familyBit(static_cast<hldata::GameFamily>(f))))
                drop(static_cast<hldata::GameFamily>(f));
        }
        bool all = true;
        for (int f = hldata::kFamilyBoss; f < hldata::kFamilyCount; ++f)
        {
            if (mask & hldata::familyBit(static_cast<hldata::GameFamily>(f)))
                all = load(static_cast<hldata::GameFamily>(f), why) && all;
        }
        return all;
    }

    bool need(hldata::GameFamily family, const char **why)
    {
        return families_[family].library != nullptr || load(family, why);
    }

    std::uint8_t loaded()
    {
        std::uint8_t mask = 0;
        for (int f = hldata::kFamilyBoss; f < hldata::kFamilyCount; ++f)
        {
            if (families_[f].library != nullptr)
                mask |= hldata::familyBit(static_cast<hldata::GameFamily>(f));
        }
        return mask;
    }
#endif
} // namespace hlrt::families
