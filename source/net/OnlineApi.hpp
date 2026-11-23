#pragma once

// Online play as a library of its own (dsl/online.dsl, built from
// dll/online/): the relay's client and the IP stack it talks through, loaded
// the first time the console goes online (source/net/OnlineDll.cpp) and kept
// until it is turned off -- the IP stack cannot be stopped once begun. The
// program keeps NetRelay.hpp's functions (source/net/NetRelay.cpp), which
// hand each call to the library through this table.
//
// Like the game's families (source/game/FamilyApi.hpp), the library is
// linked against the program's ELF: what it calls of the program is the
// program's, at its addresses, and the program's ID is stamped into it
// (hlFamilyProgramId); the loader refuses one made for another program.

#include <cstdint>

namespace hlrt
{
    /// Raised whenever OnlineFuncs changes.
    inline constexpr std::uint32_t kOnlineApiVersion = 1;

    /// NetRelay.hpp's functions, as the library has them (each says what it
    /// does there). netrelay::Connect is passed as its number.
    struct OnlineFuncs
    {
        const char *(*unavailable)();
        bool (*holdsWifi)();
        const char *(*why)();
        int (*failStage)();
        void (*connectBegin)();
        std::uint8_t (*connectStep)();
        const char *(*connectText)();
        void (*connectCancel)();
        bool (*ready)();
        void (*keepAlive)();
        void (*shutdown)();
        void (*powerDown)();
        bool (*host)(const char *name, int most);
        unsigned (*clients)();
        void (*hostRoom)(const std::uint8_t *bytes, int size);
        bool (*hostRoomAt)(int index, std::uint8_t *bytes, int &size, int &rssi);
        int (*strength)();
        void (*admit)(bool allow);
        void (*kick)(int c);
        bool (*scan)();
        int (*found)();
        bool (*hostAt)(int index, char *name, int room, int *players, int *most);
        bool (*join)(int index);
        bool (*joined)();
        bool (*reconnecting)();
        bool (*refused)();
        void (*send)(const void *bytes, int size);
        int (*take)(std::uint8_t *into, int &from);
        unsigned (*dropped)();
        void (*address)(std::uint8_t out[6]);
        void (*setName)(const char *name);
        const char *(*refusedText)();
        const char *(*letGo)();
        bool (*knock)(int &ticket, char *name);
        void (*answer)(int ticket, bool yes);
        void (*allowVoice)(bool allow);
        void (*sendVoice)(int player, int number, int rate, const std::uint8_t *packet);
        bool (*takeVoice)(int &player, int &number, int &rate, std::uint8_t *packet);
        void (*radioCounts)(char *out, int room);
    };

    using GetOnlineApiFn = bool (*)(std::uint32_t version, OnlineFuncs *out);
} // namespace hlrt
