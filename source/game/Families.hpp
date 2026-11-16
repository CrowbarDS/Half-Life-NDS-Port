#pragma once

// The game's families, loaded with the map that needs them (FamilyApi.hpp).
// What a program has linked in instead is always there: the parent has none
// of them linked in (HL_FAMILIES_APART, HL_NET_APART); the Download Play
// child has the match's rules (every guest needs them from the lobby on),
// and is sent the others by the host when a demo's map needs them.

#include <cstdint>

#include "HlLevel.hpp"

namespace hlrt::families
{
    /// The families (familyBit) a map needs by its header: what it says,
    /// or, from a file cooked before it said, every family a map can have.
    std::uint8_t ofHeader(const hldata::LevelHeader &header);

    /// Those of `mask` loaded and set up, every other let go. Done between
    /// maps, with nothing of the old map left that runs their code. False,
    /// with why (naming the file), if one cannot be had: those that could
    /// are loaded all the same.
    bool want(std::uint8_t mask, const char **why);

    /// `family` loaded if it is not, the others kept. False, with why, if
    /// it cannot be had.
    bool need(hldata::GameFamily family, const char **why);

    /// Those loaded now, or linked in (familyBit).
    std::uint8_t loaded();

    /// The ROM's file of `family` for this program (dsl/), and the one built
    /// for the Download Play child (dlplay/, what a host sends it). Null: none.
    const char *path(hldata::GameFamily family);
    const char *sentPath(hldata::GameFamily family);
} // namespace hlrt::families
