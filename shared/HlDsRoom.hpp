#pragma once
// What a map takes of a DS's heap, as the converter can tell beforehand.
//
// A DS (not a DSi) has one heap for everything, and a map gets what the
// engine, the game library and the common archive leave: kDsMapHeap,
// measured in melonDS's DS mode as each map's load begins (perf.txt's room
// line, "before the map"). Into it go the level file, whole; the models as a
// DS loads them, whole; the weapons' skins, kept in main memory until one is
// taken up; the game's arrays; the sounds kept with the map; and a little of
// everything else (kDsMapTables, measured on maps that fit). Play then needs
// kDsPlayRoom on top (the engine's kPlayReserve), or what is loaded on first
// sight, and the sounds read on first play, have nowhere to go.
//
// The converter makes a map fit this by cooking a DS lighter models and
// fewer, lower sounds; a DSi has the full ones beside them. The game's
// families the map needs come out of the same heap, loaded first
// (source/game/Families.hpp).

#include <cstddef>
#include <cstdint>

#include "HlLevel.hpp"

namespace hldata
{
    /// The heap free as a DS begins to load a map, before its families, less
    /// a margin for arriving by a level change rather than at a start.
    /// Measured 2,295,312 bytes on 2026-11-17 (c2a5f, c4a1b, c2a5e alike),
    /// with the animation keys' cache (source/data/KeyCache.cpp) in the
    /// program: 6,128 less than on 2026-11-16 (2,301,440, the game's core
    /// linked into the program and its families apart; 2,175,824 with them
    /// linked in, 2,110,768 with the whole game a library). Measure again
    /// (tools/emu/room.sh ds) whenever the program grows.
    inline constexpr std::size_t kDsMapHeap = 2295312 - 32 * 1024;
    /// What each family takes of the heap loaded (GameFamily's order):
    /// measured in melonDS's DS mode on 2026-11-16, 70,764 and 60,724 bytes
    /// for the bosses and the soldiers (perf.txt's room line), the rules
    /// reckoned from their file (35,544) and the loader's own the same;
    /// rounded up. tools/dsl_family.py prints each as it is built: raise
    /// these when a family grows past them.
    inline constexpr std::size_t kDsFamilyBytes[kFamilyCount] = {0, 72 * 1024, 62 * 1024, 37 * 1024};
    /// What the families in `mask` (familyBit) take of the heap.
    inline std::size_t dsFamilyBytes(std::uint8_t mask)
    {
        std::size_t bytes = 0;
        for (int f = 0; f < kFamilyCount; ++f)
            bytes += (mask & familyBit(static_cast<GameFamily>(f))) ? kDsFamilyBytes[f] : 0;
        return bytes;
    }
    /// What a map's load takes besides the files counted here: its sound
    /// table, sprite and model lists, the texture stream's tables, the
    /// archive's members open. Measured 71-75 KB (c1a0, c1a1, c1a4f).
    inline constexpr std::size_t kDsMapTables = 80 * 1024;
    /// Kept free for play: source/main.cpp's kPlayReserve.
    inline constexpr std::size_t kDsPlayRoom = 160 * 1024;
    /// The game's arrays (dll/hl/Game.arm.cpp): for each actor and logic record.
    inline constexpr std::size_t kDsActorBytes = 264;
    inline constexpr std::size_t kDsLogicBytes = 56;

    /// Animation keys apart (HlModel.hpp's kModelKeysApart): a DS keeps
    /// what it reads of them in one block, of this size -- the same here and
    /// on the console (source/data/KeyCache.cpp), from the map's largest
    /// clip's keys and all its keys, in bytes. At least three of the largest
    /// clip and kDsKeyCacheLeast; never more than all of them.
    inline constexpr std::size_t kDsKeyCacheLeast = 96 * 1024;
    inline std::size_t dsKeyCacheBytes(std::size_t largestClip, std::size_t allKeys)
    {
        std::size_t want = 3 * largestClip > kDsKeyCacheLeast ? 3 * largestClip : kDsKeyCacheLeast;
        want = want < allKeys ? want : allKeys;
        want = want > largestClip ? want : largestClip;
        return (want + 3) & ~std::size_t(3);
    }
    /// ... and what it keeps of each clip and each model to find them: the
    /// cache's own tables (KeyCache's Clip and Model).
    inline constexpr std::size_t kDsKeyClipBytes = 12;
    inline constexpr std::size_t kDsKeyModelBytes = 64;

    struct DsMapCost
    {
        std::size_t level = 0;      ///< the level file
        /// The model files a DS loads (not lazy, not DSi-only); with their
        /// keys apart, the files without them, and the keys' cache and tables.
        std::size_t models = 0;
        std::size_t heldSkins = 0;  ///< weapons' skins kept in main memory
        std::size_t sounds = 0;     ///< the sounds kept with the map, as a DS has them
        std::uint32_t actors = 0;
        std::uint32_t logicRecords = 0;
        std::uint8_t families = 0;  ///< the game's families it needs (familyBit)
    };

    /// All but the sounds: what the map needs before any is loaded, play's
    /// room included.
    inline std::size_t dsMapNeedBeforeSounds(const DsMapCost &cost)
    {
        return cost.level + cost.models + cost.heldSkins + kDsMapTables + kDsPlayRoom +
               cost.actors * kDsActorBytes + cost.logicRecords * kDsLogicBytes + dsFamilyBytes(cost.families);
    }

    /// What is left for its sounds (0 if nothing is).
    inline std::size_t dsSoundRoom(const DsMapCost &cost)
    {
        const std::size_t need = dsMapNeedBeforeSounds(cost);
        return need < kDsMapHeap ? kDsMapHeap - need : 0;
    }

    /// Positive: bytes to spare with every kept sound loaded; negative: how
    /// far short the map is.
    inline long dsMapSpare(const DsMapCost &cost)
    {
        return static_cast<long>(kDsMapHeap) - static_cast<long>(dsMapNeedBeforeSounds(cost) + cost.sounds);
    }
} // namespace hldata
