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
// fewer, lower sounds; a DSi has the full ones beside them.

#include <cstddef>
#include <cstdint>

namespace hldata
{
    /// The heap free as a DS begins to load a map (2,127,504 bytes measured,
    /// 2026-11-14, the same on every map tried), less a margin for arriving
    /// by a level change rather than at a start.
    inline constexpr std::size_t kDsMapHeap = 2127504 - 32 * 1024;
    /// What a map's load takes besides the files counted here: its sound
    /// table, sprite and model lists, the texture stream's tables, the
    /// archive's members open. Measured 71-75 KB (c1a0, c1a1, c1a4f).
    inline constexpr std::size_t kDsMapTables = 80 * 1024;
    /// Kept free for play: source/main.cpp's kPlayReserve.
    inline constexpr std::size_t kDsPlayRoom = 160 * 1024;
    /// The game's arrays (dll/hl/Game.arm.cpp): for each actor and logic record.
    inline constexpr std::size_t kDsActorBytes = 264;
    inline constexpr std::size_t kDsLogicBytes = 56;

    struct DsMapCost
    {
        std::size_t level = 0;      ///< the level file
        std::size_t models = 0;     ///< the model files a DS loads (not lazy, not DSi-only)
        std::size_t heldSkins = 0;  ///< weapons' skins kept in main memory
        std::size_t sounds = 0;     ///< the sounds kept with the map, as a DS has them
        std::uint32_t actors = 0;
        std::uint32_t logicRecords = 0;
    };

    /// All but the sounds: what the map needs before any is loaded, play's
    /// room included.
    inline std::size_t dsMapNeedBeforeSounds(const DsMapCost &cost)
    {
        return cost.level + cost.models + cost.heldSkins + kDsMapTables + kDsPlayRoom +
               cost.actors * kDsActorBytes + cost.logicRecords * kDsLogicBytes;
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
