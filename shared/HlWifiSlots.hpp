#pragma once

// The game's own Wi-Fi connections for online play (ONLINE SETTINGS): three,
// tried in order before the console's saved ones, kept on the card in
// _nds/hl_nds/data/wifi.ini as lines a person can read and write:
//
//     slot1.ssid = MyNetwork
//     slot1.security = wpa        (open, wep or wpa)
//     slot1.key = the passphrase  (WEP: 5 or 13 characters, or 10 or 26 hex digits)
//     slot1.ip = auto             (or an address: then mask, gateway, dns1, dns2)
//
// The key is kept as it was typed, in plain text: the card is the player's.
// A DS reaches open and WEP networks only; a DSi in its own mode WPA and WPA2
// as well.
//
// std-only, no heap: the menu reads and writes it (source/ui/WifiSettings.cpp),
// the online library reads it as it connects (dll/online/), and the
// converter's tests run it.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "HlIni.hpp"

// Where the functions below are put: the program's menu, the only one of
// it that uses them, puts them in the front end's overlay (source/ui/Menu.cpp
// defines this before it takes this header); elsewhere, where they fall.
#ifndef HL_WIFI_SLOTS_CODE
#define HL_WIFI_SLOTS_CODE
#endif
// ... and how they are linked: a library linked against the program's ELF
// (dll/online/) has copies of its own, which do not answer to the program's
// names (theirs are in that overlay, which a library may not reach).
#ifndef HL_WIFI_SLOTS_LINKAGE
#define HL_WIFI_SLOTS_LINKAGE inline
#endif

namespace hldata
{
    inline constexpr int kWifiSlots = 3;
    inline constexpr const char *kWifiFile = "wifi.ini";

    enum class WifiSecurity : std::uint8_t
    {
        Open,
        Wep,
        Wpa, ///< WPA or WPA2: a DSi's alone
    };

    struct WifiSlot
    {
        char ssid[33] = {};
        WifiSecurity security = WifiSecurity::Open;
        char key[65] = {}; ///< as typed
        bool manualIp = false;
        /// With manualIp: in the order Wifi_SetIP takes them (the first number
        /// in the lowest byte); 0 for a DNS not given.
        std::uint32_t ip = 0, mask = 0, gateway = 0, dns1 = 0, dns2 = 0;

        bool used() const { return ssid[0] != 0; }
    };

    /// "a.b.c.d" as Wifi_SetIP takes it; false if it is not one.
    HL_WIFI_SLOTS_LINKAGE HL_WIFI_SLOTS_CODE bool wifiParseIp(const char *text, std::uint32_t &out)
    {
        std::uint32_t value = 0;
        int part = 0, digits = 0, number = 0;
        for (const char *c = text;; ++c)
        {
            if (*c >= '0' && *c <= '9')
            {
                number = number * 10 + (*c - '0');
                if (++digits > 3 || number > 255)
                    return false;
            }
            else if (*c == '.' || *c == 0)
            {
                if (digits == 0 || part > 3)
                    return false;
                value |= static_cast<std::uint32_t>(number) << (8 * part);
                ++part;
                number = digits = 0;
                if (*c == 0)
                    break;
            }
            else
                return false;
        }
        if (part != 4)
            return false;
        out = value;
        return true;
    }

    /// An address as text ("" for 0): `out` holds 16.
    HL_WIFI_SLOTS_LINKAGE HL_WIFI_SLOTS_CODE void wifiFormatIp(std::uint32_t ip, char *out)
    {
        if (ip == 0)
        {
            out[0] = 0;
            return;
        }
        std::snprintf(out, 16, "%u.%u.%u.%u", static_cast<unsigned>(ip & 0xFF), static_cast<unsigned>((ip >> 8) & 0xFF),
                      static_cast<unsigned>((ip >> 16) & 0xFF), static_cast<unsigned>(ip >> 24));
    }

    namespace wifi_detail
    {
        inline int hexDigit(char c)
        {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        }

        inline bool allHex(const char *text, std::size_t length)
        {
            for (std::size_t i = 0; i < length; ++i)
            {
                if (hexDigit(text[i]) < 0)
                    return false;
            }
            return true;
        }
    } // namespace wifi_detail

    /// The key as the wireless library takes it (Wifi_ConnectSecureAP), into
    /// `out` (64 bytes): its length, 0 for an open network, -1 if it is not
    /// one the network's kind can have -- WEP 5, 13 or 16 characters, or
    /// twice as many hex digits; WPA a passphrase of 8 to 63 characters, or
    /// 64 hex digits.
    HL_WIFI_SLOTS_LINKAGE HL_WIFI_SLOTS_CODE int wifiKeyBytes(const WifiSlot &slot, std::uint8_t *out)
    {
        using namespace wifi_detail;
        const std::size_t length = std::strlen(slot.key);
        switch (slot.security)
        {
        case WifiSecurity::Open:
            return 0;
        case WifiSecurity::Wep:
            if ((length == 10 || length == 26 || length == 32) && allHex(slot.key, length))
            {
                for (std::size_t i = 0; i < length / 2; ++i)
                    out[i] = static_cast<std::uint8_t>(hexDigit(slot.key[2 * i]) << 4 | hexDigit(slot.key[2 * i + 1]));
                return static_cast<int>(length / 2);
            }
            if (length == 5 || length == 13 || length == 16)
            {
                std::memcpy(out, slot.key, length);
                return static_cast<int>(length);
            }
            return -1;
        case WifiSecurity::Wpa:
            if ((length >= 8 && length <= 63) || (length == 64 && allHex(slot.key, length)))
            {
                std::memcpy(out, slot.key, length);
                return static_cast<int>(length);
            }
            return -1;
        }
        return -1;
    }

    /// The slots in `text` (wifi.ini, `size` bytes); those it does not name
    /// are left empty.
    HL_WIFI_SLOTS_LINKAGE HL_WIFI_SLOTS_CODE void wifiSlotsRead(const char *text, std::size_t size, WifiSlot (&slots)[kWifiSlots])
    {
        for (int s = 0; s < kWifiSlots; ++s)
        {
            WifiSlot &slot = slots[s] = WifiSlot{};
            char key[24], value[24];
            auto read = [&](const char *name, char *out, std::size_t room) {
                std::snprintf(key, sizeof(key), "slot%d.%s", s + 1, name);
                return iniValue(text, size, key, out, room);
            };
            auto get = [&](const char *name) { return read(name, value, sizeof(value)); };
            read("ssid", slot.ssid, sizeof(slot.ssid));
            if (get("security"))
            {
                for (char *c = value; *c != 0; ++c)
                    *c = ini_detail::lower(*c);
                slot.security = std::strcmp(value, "wpa") == 0 || std::strcmp(value, "wpa2") == 0 ? WifiSecurity::Wpa
                                : std::strcmp(value, "wep") == 0                                      ? WifiSecurity::Wep
                                                                                                      : WifiSecurity::Open;
            }
            read("key", slot.key, sizeof(slot.key));
            if (get("ip") && wifiParseIp(value, slot.ip))
            {
                slot.manualIp = true;
                if (get("mask"))
                    wifiParseIp(value, slot.mask);
                if (get("gateway"))
                    wifiParseIp(value, slot.gateway);
                if (get("dns1"))
                    wifiParseIp(value, slot.dns1);
                if (get("dns2"))
                    wifiParseIp(value, slot.dns2);
            }
        }
    }

    /// The slots as wifi.ini's text, into `out` (`room` bytes, ended): its
    /// length, or -1 if it would not fit (2 KB is enough).
    HL_WIFI_SLOTS_LINKAGE HL_WIFI_SLOTS_CODE int wifiSlotsWrite(const WifiSlot (&slots)[kWifiSlots], char *out, std::size_t room)
    {
        if (room == 0)
            return -1;
        std::size_t at = 0;
        bool fits = true;
        auto append = [&](const char *line) {
            const std::size_t n = std::strlen(line);
            if (!fits || at + n >= room)
            {
                fits = false;
                return;
            }
            std::memcpy(out + at, line, n);
            at += n;
            out[at] = 0;
        };
        out[0] = 0;
        append("# Half-Life DS: Wi-Fi connections for online play (ONLINE SETTINGS).\n"
               "# Tried in order before the console's own. Keys are kept as typed, in plain text.\n");
        static const char *const kSecurity[] = {"open", "wep", "wpa"};
        static const char *const kNames[] = {"ip", "mask", "gateway", "dns1", "dns2"};
        char line[128];
        for (int s = 0; s < kWifiSlots; ++s)
        {
            const WifiSlot &slot = slots[s];
            if (!slot.used())
                continue;
            std::snprintf(line, sizeof(line), "\nslot%d.ssid = %s\n", s + 1, slot.ssid);
            append(line);
            std::snprintf(line, sizeof(line), "slot%d.security = %s\n", s + 1, kSecurity[static_cast<int>(slot.security)]);
            append(line);
            if (slot.security != WifiSecurity::Open)
            {
                std::snprintf(line, sizeof(line), "slot%d.key = %s\n", s + 1, slot.key);
                append(line);
            }
            if (!slot.manualIp)
            {
                std::snprintf(line, sizeof(line), "slot%d.ip = auto\n", s + 1);
                append(line);
                continue;
            }
            const std::uint32_t parts[] = {slot.ip, slot.mask, slot.gateway, slot.dns1, slot.dns2};
            for (int p = 0; p < 5; ++p)
            {
                char ip[16];
                wifiFormatIp(parts[p], ip);
                if (ip[0] == 0)
                    continue;
                std::snprintf(line, sizeof(line), "slot%d.%s = %s\n", s + 1, kNames[p], ip);
                append(line);
            }
        }
        return fits ? static_cast<int>(at) : -1;
    }
} // namespace hldata
