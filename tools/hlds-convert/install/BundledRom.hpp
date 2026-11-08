#pragma once

// The console's program, built into the converter (CMake's HLDS_BUNDLED_ROM),
// so that a player needs only the converter: it puts the program on the card
// when the card has none, or one that is not this one.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace hlds::install
{
    /// The program this converter was built with; empty if none
    /// (hlds_bundled_rom: a source made at build time).
    std::span<const std::uint8_t> bundledRom();

    /// The name it has on the card, at its root.
    inline constexpr const char *kRomName = "half-life-nds-port.nds";

    enum class RomPut
    {
        Same,    ///< that program was there already
        Written, ///< there now: there was none, or another
        Failed,
    };
    /// `bytes` at `file`, unless they are there already: written to a
    /// temporary name first, then put in its place.
    RomPut putRom(std::span<const std::uint8_t> bytes, const std::filesystem::path &file, std::string &why);
} // namespace hlds::install
