#include "RomHeader.hpp"

#include <cstring>

namespace hlds::install
{
    namespace
    {
        struct Named
        {
            Device device;
            const char *name;
            const char *title;
        };
        constexpr Named kNamed[] = {
            {Device::Dsi, "dsi", "a DSi"},
            {Device::ThreeDs, "3ds", "a 3DS"},
            {Device::Ds, "ds", "a DS, DS Lite or DS flashcard"},
            {Device::Emulator, "emulator", "an emulator"},
        };
        constexpr char kWide[4] = {'W', 'H', 'L', 'D'};
        constexpr char kHomebrew[4] = {'#', '#', '#', '#'};
    } // namespace

    bool deviceFromName(std::string_view name, Device &device)
    {
        for (const Named &one : kNamed)
        {
            if (name == one.name)
            {
                device = one.device;
                return true;
            }
        }
        return false;
    }

    const char *deviceName(Device device)
    {
        for (const Named &one : kNamed)
            if (one.device == device)
                return one.name;
        return "?";
    }

    const char *deviceTitle(Device device)
    {
        for (const Named &one : kNamed)
            if (one.device == device)
                return one.title;
        return "?";
    }

    std::uint16_t headerCrc(std::span<const std::uint8_t> bytes)
    {
        std::uint16_t crc = 0xFFFF;
        for (const std::uint8_t byte : bytes)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc & 1) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001) : static_cast<std::uint16_t>(crc >> 1);
        }
        return crc;
    }

    bool romFor(std::span<const std::uint8_t> rom, Device device, std::vector<std::uint8_t> &out, std::string &why)
    {
        if (rom.size() < 0x200)
        {
            why = "too short to be the console's program";
            return false;
        }
        const char *code = reinterpret_cast<const char *>(rom.data() + kGameCodeAt);
        if (std::memcmp(code, kWide, 4) != 0 && std::memcmp(code, kHomebrew, 4) != 0)
        {
            why = "not this game's program: its game code is \"" + std::string(code, 4) + "\"";
            return false;
        }
        const std::uint16_t had = static_cast<std::uint16_t>(rom[kHeaderCrcAt] | rom[kHeaderCrcAt + 1] << 8);
        if (headerCrc(rom.first(kHeaderCrcAt)) != had)
        {
            why = "its header's own check is wrong: not a whole program";
            return false;
        }
        out.assign(rom.begin(), rom.end());
        std::memcpy(out.data() + kGameCodeAt, device == Device::ThreeDs ? kWide : kHomebrew, 4);
        const std::uint16_t crc = headerCrc(std::span<const std::uint8_t>(out).first(kHeaderCrcAt));
        out[kHeaderCrcAt] = static_cast<std::uint8_t>(crc);
        out[kHeaderCrcAt + 1] = static_cast<std::uint8_t>(crc >> 8);
        return true;
    }
} // namespace hlds::install
