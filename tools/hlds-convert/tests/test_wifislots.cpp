// ONLINE SETTINGS' Wi-Fi connections (shared/HlWifiSlots.hpp): wifi.ini read
// and written back the same, and each key as the wireless library takes it.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "HlWifiSlots.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

int main()
{
    // Addresses, in Wifi_SetIP's order (the first number in the lowest byte).
    std::uint32_t ip = 0;
    check(wifiParseIp("192.168.1.50", ip) && ip == (192u | 168u << 8 | 1u << 16 | 50u << 24), "an address");
    char text[16];
    wifiFormatIp(ip, text);
    check(std::strcmp(text, "192.168.1.50") == 0, "... and back");
    check(!wifiParseIp("192.168.1", ip) && !wifiParseIp("192.168.1.256", ip) && !wifiParseIp("1.2.3.4.5", ip) &&
              !wifiParseIp("a.b.c.d", ip) && !wifiParseIp("", ip) && !wifiParseIp("1..2.3", ip),
          "not addresses");
    wifiFormatIp(0, text);
    check(text[0] == 0, "none is empty");

    // Keys.
    std::uint8_t key[64];
    WifiSlot slot;
    slot.security = WifiSecurity::Open;
    check(wifiKeyBytes(slot, key) == 0, "an open network has none");
    slot.security = WifiSecurity::Wep;
    std::snprintf(slot.key, sizeof(slot.key), "abcde");
    check(wifiKeyBytes(slot, key) == 5 && std::memcmp(key, "abcde", 5) == 0, "WEP: five characters");
    std::snprintf(slot.key, sizeof(slot.key), "0102030405");
    check(wifiKeyBytes(slot, key) == 5 && key[0] == 1 && key[4] == 5, "WEP: ten hex digits, as five bytes");
    std::snprintf(slot.key, sizeof(slot.key), "00112233445566778899AABBcc");
    check(wifiKeyBytes(slot, key) == 13 && key[10] == 0xAA && key[12] == 0xCC, "WEP: 26 hex digits");
    std::snprintf(slot.key, sizeof(slot.key), "abcdef");
    check(wifiKeyBytes(slot, key) == -1, "WEP: six characters are not a key");
    slot.security = WifiSecurity::Wpa;
    std::snprintf(slot.key, sizeof(slot.key), "seven77");
    check(wifiKeyBytes(slot, key) == -1, "WPA: a passphrase is 8 characters at least");
    std::snprintf(slot.key, sizeof(slot.key), "correct horse");
    check(wifiKeyBytes(slot, key) == 13 && std::memcmp(key, "correct horse", 13) == 0, "WPA: a passphrase as typed");

    // A file written and read back.
    WifiSlot slots[kWifiSlots];
    std::snprintf(slots[0].ssid, sizeof(slots[0].ssid), "Home Net");
    slots[0].security = WifiSecurity::Wpa;
    std::snprintf(slots[0].key, sizeof(slots[0].key), "a long passphrase");
    std::snprintf(slots[2].ssid, sizeof(slots[2].ssid), "Old#WEP");
    slots[2].security = WifiSecurity::Wep;
    std::snprintf(slots[2].key, sizeof(slots[2].key), "0102030405");
    slots[2].manualIp = true;
    wifiParseIp("10.0.0.20", slots[2].ip);
    wifiParseIp("255.255.255.0", slots[2].mask);
    wifiParseIp("10.0.0.1", slots[2].gateway);
    wifiParseIp("10.0.0.1", slots[2].dns1);
    char file[2048];
    const int length = wifiSlotsWrite(slots, file, sizeof(file));
    check(length > 0 && std::strstr(file, "slot2.") == nullptr, "written, an empty slot left out");
    WifiSlot back[kWifiSlots];
    wifiSlotsRead(file, static_cast<std::size_t>(length), back);
    check(std::strcmp(back[0].ssid, "Home Net") == 0 && back[0].security == WifiSecurity::Wpa &&
              std::strcmp(back[0].key, "a long passphrase") == 0 && !back[0].manualIp,
          "a WPA slot comes back, its address given by the network");
    check(!back[1].used(), "the empty one stays empty");
    check(std::strcmp(back[2].ssid, "Old#WEP") == 0 && back[2].security == WifiSecurity::Wep && back[2].manualIp &&
              back[2].ip == slots[2].ip && back[2].mask == slots[2].mask && back[2].gateway == slots[2].gateway &&
              back[2].dns1 == slots[2].dns1 && back[2].dns2 == 0,
          "a WEP slot with its own address comes back whole");
    check(wifiSlotsWrite(slots, file, 64) == -1, "too little room says so");

    // As a person might write it.
    const char *hand = "# mine\n[anything]\nSLOT1.SSID =  Cafe  \nslot1.Security = WPA2\nslot1.key = pass word 1\n"
                       "slot1.ip = 300.1.1.1\n";
    wifiSlotsRead(hand, std::strlen(hand), back);
    check(std::strcmp(back[0].ssid, "Cafe") == 0 && back[0].security == WifiSecurity::Wpa &&
              std::strcmp(back[0].key, "pass word 1") == 0 && !back[0].manualIp,
          "keys in any case, WPA2 as WPA, a bad address taken as none");

    if (g_failures == 0)
        std::printf("test_wifislots: ok\n");
    return g_failures == 0 ? 0 : 1;
}
