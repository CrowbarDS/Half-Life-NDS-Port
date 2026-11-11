#pragma once

// What is in a DS's or DS Lite's GBA slot (slot 2): a Memory Expansion Pak
// (Nintendo's, or the RAM of a slot-2 flash cart) or a Rumble Pak. A DSi or a
// 3DS has no slot 2, and neither has the console that was sent the game.
//
// THE PAK'S RAM. Eight megabytes on Nintendo's, behind a 16-bit bus, slow, and
// with no byte writes (a byte written is not written): it is a store the game
// copies FROM -- a map's textures, its sounds that main RAM has no room for --
// never something read where the time goes, nor what the sound hardware or the
// 3D engine reads by themselves. What is put in it is put for one map (take,
// then reset when the map is let go), through write, which stores halfwords and
// words only.
//
// A 3-in-1 cart has RAM and a motor at the same addresses, one at a time: the
// RAM is used, and the cart counts as having no motor.

#include <cstddef>
#include <cstdint>

namespace hlrt::slot2
{
#ifndef HLDS_CHILD
    /// Looks at the slot, once, at start-up (Global::Init). Nothing in DSi mode.
    void init();

    /// Bytes of the Pak's RAM the game may use; 0 with none.
    std::size_t pakBytes();
    /// A motor to drive (platform/Rumble.hpp).
    bool hasRumble();
    /// The motor starts on a change from off to on, not while it is held on.
    bool rumbleEdge();
    /// The strongest value the motor takes.
    std::uint8_t rumbleMost();

    /// `bytes` of the Pak for the map being played, 32-byte aligned; null when
    /// it is full or there is no Pak.
    std::uint8_t *take(std::size_t bytes);
    /// The map is let go: all it took is free again.
    void reset();
    /// Bytes taken for this map.
    std::size_t used();

    /// Copies `bytes` from `src` (anywhere, any alignment) to `dst` in the Pak
    /// (even address) with 16- and 32-bit stores only. An odd last byte keeps
    /// the byte after it as it was.
    void write(std::uint8_t *dst, const void *src, std::size_t bytes);
#else
    inline void init() {}
    inline std::size_t pakBytes() { return 0; }
    inline bool hasRumble() { return false; }
    inline bool rumbleEdge() { return false; }
    inline std::uint8_t rumbleMost() { return 0; }
    inline std::uint8_t *take(std::size_t) { return nullptr; }
    inline void reset() {}
    inline std::size_t used() { return 0; }
    inline void write(std::uint8_t *, const void *, std::size_t) {}
#endif
} // namespace hlrt::slot2
