#pragma once

// The game's families, loaded with the map that needs them (FamilyApi.hpp).
// A program built with them linked in (no HL_FAMILIES_APART: the Download
// Play child, for now) has them all, always, and these do nothing.

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

    /// Those loaded now (familyBit).
    std::uint8_t loaded();
} // namespace hlrt::families
