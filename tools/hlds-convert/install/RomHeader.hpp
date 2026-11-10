#pragma once

// The console's program made for the console it is to run on. One ROM runs on
// every DS, DSi and 3DS; what differs is who takes it for what it is, by the
// game code in its header:
//
//   WHLD  TWiLight Menu++ on a 3DS takes a homebrew whose code begins with W
//         (and whose version is 0x57) as able to draw for the wide screen,
//         and offers 16:10 for it. But melonDS, and the loaders of the older
//         DS flashcards, take it for a game bought in a shop, and never give
//         it the card's driver (DLDI): it then finds no SD card.
//   ####  what every homebrew is: patched and started by everything.
//
// The game never reads its own code: only who starts it, and how, changes.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hlds::install
{
    enum class Device
    {
        Dsi,      ///< a DSi: TWiLight Menu++, hiyaCFW, Unlaunch
        ThreeDs,  ///< a 3DS: TWiLight Menu++, which offers 16:10 for it
        Ds,       ///< a DS or DS Lite, or any console from a DS flashcard
        Emulator, ///< melonDS and the others
    };
    /// What the command line writes when it is not told: what everything starts.
    inline constexpr Device kDefaultDevice = Device::Ds;

    /// `dsi`, `3ds`, `ds`, `emulator` (and back); false for another name.
    bool deviceFromName(std::string_view name, Device &device);
    const char *deviceName(Device device);
    /// In words, for a person.
    const char *deviceTitle(Device device);

    /// The header's own check (at 0x15E): CRC-16 (0xA001 reflected, from
    /// 0xFFFF) of its first 0x15E bytes. Any `bytes` will do.
    std::uint16_t headerCrc(std::span<const std::uint8_t> bytes);

    inline constexpr std::size_t kHeaderCrcAt = 0x15E;
    inline constexpr std::size_t kGameCodeAt = 0x0C;

    /// `rom` (this game's: its code WHLD or ####, its header's check right)
    /// made for `device`, into `out`: its game code, and the check put
    /// right. False, with `why`, for what is not this game's ROM.
    bool romFor(std::span<const std::uint8_t> rom, Device device, std::vector<std::uint8_t> &out, std::string &why);
} // namespace hlds::install
