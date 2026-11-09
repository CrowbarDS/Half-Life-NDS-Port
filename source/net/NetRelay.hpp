#pragma once

// Online: the relay (tools/relay) in place of the radio, over the Internet
// from a DSi. The same frames as through the air go through it
// (shared/HlRelay.hpp), so what is above -- NetGame, the lobby, the match --
// does not know which it is: NetLink hands its calls here while online
// (netlink::useRelay).
//
// The relay's address is the card's _nds/hl_nds/data/server.ini:
//
//     host = relay.example.org
//     port = 27500
//
// The console's own saved Wi-Fi connections are used (DSi mode: WPA too),
// which is why it is a DSi's alone: a DS would need the IP stack's RAM, and
// its own Wi-Fi reaches few networks. The code and the buffers here are in
// the DSi's memory (TWL_CODE, TWL_BSS), and so is the IP stack
// (tools/wifi_twl.py): a DS loads none of it, and must never call it.
//
// Nothing here runs in an interrupt: what has come is read in the frame
// (take), and the IP stack, a thread of its own, is let run there.

#include <cstdint>

namespace hlrt::netrelay
{
    /// Whether this console can go online at all (a DSi, not a console sent
    /// the game), and whether the card says where the relay is. The reason
    /// if not; null if so.
    const char *unavailable();

    /// Whether the Wi-Fi has been started for the Internet. It then stays so
    /// until the program ends (the library cannot be stopped once its IP
    /// stack has begun), in the DSi's mode, where the radio's local play is
    /// not: the local wireless needs the game started again.
    bool holdsWifi();

    /// Why the last start failed, in words for the menu.
    const char *why();
    int failStage();

    /// Going online without stopping the program: connectBegin, then
    /// connectStep once a frame until it says Ready (host and scan may then
    /// be asked) or Failed (why() says why). A step may take a moment of its
    /// own -- the Wi-Fi started, the relay's name asked of the network -- but
    /// none waits for the network or the relay. Once Ready, Ready at once
    /// until powerDown.
    enum class Connect : std::uint8_t
    {
        Off,
        Starting, ///< the Wi-Fi being started
        Joining,  ///< one of the console's saved networks being joined
        Finding,  ///< the relay's name being looked up
        Waiting,  ///< for the relay to answer
        Ready,
        Failed,
    };
    void connectBegin();
    Connect connectStep();
    /// What it is doing now, in words for the screen.
    const char *connectText();
    /// Given up before Ready (the Wi-Fi stays started: holdsWifi).
    void connectCancel();
    bool ready();

    /// Out of the room, and the relay told; the Wi-Fi stays connected.
    void shutdown();
    /// ... and the Wi-Fi given back altogether.
    void powerDown();

    // As NetLink's (its comments say what each is).
    bool host(const char *name, int most);
    unsigned clients();
    void hostRoom(const std::uint8_t *bytes, int size);
    bool hostRoomAt(int index, std::uint8_t *bytes, int &size, int &rssi);
    int strength();
    void admit(bool allow);
    void kick(int c);
    bool scan();
    int found();
    bool hostAt(int index, char *name, int room, int *players, int *most);
    bool join(int index);
    bool joined();
    bool refused();
    void send(const void *bytes, int size);
    int take(std::uint8_t *into, int &from);
    unsigned dropped();
    void address(std::uint8_t out[6]);
} // namespace hlrt::netrelay
