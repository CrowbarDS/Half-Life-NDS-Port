#include "net/NetRelay.hpp"

#include <cstdio>
#include <cstring>

#include <nds.h>

#include "HlNet.hpp"

#ifndef HLDS_CHILD

#include <nds/cothread.h>

#include <arpa/inet.h>
#include <dswifi9.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Global.hpp"
#include "HlIni.hpp"
#include "HlRelay.hpp"

// What is learnt of dswifi's Internet side, kept:
//
//   - The IP stack is a cooperative thread (dswifi's own): it only runs when
//     the program yields. swiWaitForVBlank does not -- it halts -- so the
//     frame yields here (pump), and every wait here is cothread_yield_irq.
//   - Local multiplayer is not there in DSi mode: the radio (NetLink) and
//     this are never on at once; NetLink powers one down for the other.

namespace hlrt::netrelay
{
    namespace
    {
        using namespace hldata;

        // (What is said, in the DSi's memory too: a DS has no use for it.)
#define HL_TWL_TEXT(name, text) TWL_DATA const char name[] = text
        HL_TWL_TEXT(kIniName, "server.ini");
        HL_TWL_TEXT(kIniHost, "host");
        HL_TWL_TEXT(kIniPort, "port");
        HL_TWL_TEXT(kText, "%s");
        HL_TWL_TEXT(kNoIni, "No server.ini on the card (_nds/hl_nds/data)");
        HL_TWL_TEXT(kNoStart, "The wireless would not start (%d)");
        HL_TWL_TEXT(kNoNetwork, "No saved Wi-Fi connection would connect");
        HL_TWL_TEXT(kNoName, "The relay's name was not found");
        HL_TWL_TEXT(kNoSocket, "No socket for the relay");
        HL_TWL_TEXT(kNoAnswer, "The relay did not answer (%s:%d)");
#undef HL_TWL_TEXT

        constexpr int kConnectFrames = 20 * 60; ///< to join a saved network
        constexpr int kAnswerFrames = 5 * 60;   ///< for the relay to answer
        constexpr int kRingSlots = 24;

        enum class Role : std::uint8_t
        {
            None,
            Host,
            Scan,
            Client,
        };

        // (Plain variables, a few bytes on a DS too: what is read before it
        // is known to be a DSi.)
        bool started = false;
        /// The library is up with its IP stack: it stays so until the
        /// program ends. (This dswifi cannot be stopped once lwIP has begun:
        /// its wifi_lwip_deinit is a crash, "DSWifi can't be disabled if lwIP
        /// is active" -- seen on a DSi, 2026-11-09.)
        bool wifiUp = false;
        bool iniRead = false;
        bool iniThere = false;
        int failCode = 0;
        char whyText[64] = "";

        struct State
        {
            char host[64];
            int port;
            int sock;
            sockaddr_in server;
            std::uint32_t session;
            Role role;
            std::uint32_t lastPump;
            std::uint32_t lastSent;
            // a host
            RelayOpen open;
            std::uint32_t roomId;
            bool admitting;
            unsigned clientMask;
            std::uint32_t lastOpen;
            // looking
            RelayRoom rooms[kRelayListMost];
            int roomCount;
            std::uint32_t lastList;
            // a client
            std::uint32_t joinId;
            int slot;
            bool refusedNow;
            bool byeNow;
            std::uint32_t lastJoin;
            // what has come, not yet read
            struct Slot
            {
                std::uint16_t size;
                std::uint8_t from;
                std::uint8_t bytes[kNetCmdBytes];
            } ring[kRingSlots];
            int ringHead, ringTail;
            unsigned ringDropped;
            std::uint8_t datagram[kRelayMostBytes + 16];
        };
        // (In a union with nothing to do at start-up: the program's
        // constructors run on a DS too, and a DS has none of this memory.
        // On a DSi it is cleared before main, as all of TWL_BSS is.)
        union Holder
        {
            constexpr Holder() {}
            State state;
        };
        TWL_BSS Holder holder;
        State &s = holder.state;

        TWL_CODE void fail(int code, const char *text)
        {
            failCode = code;
            std::snprintf(whyText, sizeof(whyText), kText, text);
        }

        TWL_CODE void readIni()
        {
            if (iniRead)
                return;
            iniRead = true;
            static TWL_DATA const char kRead[] = "rb";
            std::FILE *file = std::fopen(global.dataPath(kIniName).c_str(), kRead);
            if (file == nullptr)
                return;
            char text[512];
            const std::size_t size = std::fread(text, 1, sizeof(text), file);
            std::fclose(file);
            iniThere = iniValue(text, size, kIniHost, s.host, sizeof(s.host)) && s.host[0] != 0;
            s.port = iniNumber(text, size, kIniPort, kRelayPort, 1, 65535);
        }

        TWL_CODE void sendRaw(const std::uint8_t *bytes, int size)
        {
            if (s.sock < 0 || size <= 0)
                return;
            sendto(s.sock, bytes, static_cast<std::size_t>(size), 0, reinterpret_cast<const sockaddr *>(&s.server),
                   sizeof(s.server));
            s.lastSent = Global::vblanks();
        }

        TWL_CODE void sendSmall(RelayKind kind, int value = -1)
        {
            std::uint8_t bytes[kRelayHeadBytes + 1];
            sendRaw(bytes, relaySmallWrite(bytes, kind, s.session, value));
        }

        TWL_CODE void sendOpen()
        {
            std::uint8_t bytes[kRelayHeadBytes + kRelayOpenBytes];
            sendRaw(bytes, relayOpenWrite(bytes, s.session, s.open));
            sendSmall(RelayKind::Admit, s.admitting ? 1 : 0);
            s.lastOpen = Global::vblanks();
        }

        TWL_CODE void sendJoin()
        {
            std::uint8_t bytes[kRelayHeadBytes + 4];
            sendRaw(bytes, relayRoomIdWrite(bytes, RelayKind::Join, s.session, s.joinId));
            s.lastJoin = Global::vblanks();
        }

        TWL_CODE void keep(int from, const std::uint8_t *frame, int size)
        {
            const int next = (s.ringTail + 1) % kRingSlots;
            if (size <= 0 || size > kNetCmdBytes || next == s.ringHead)
            {
                if (next == s.ringHead)
                    ++s.ringDropped;
                return;
            }
            State::Slot &slot = s.ring[s.ringTail];
            std::memcpy(slot.bytes, frame, static_cast<std::size_t>(size));
            slot.size = static_cast<std::uint16_t>(size);
            slot.from = static_cast<std::uint8_t>(from);
            s.ringTail = next;
        }

        /// One datagram of the relay's. True if it was its answer to Ping.
        TWL_CODE bool heard(const std::uint8_t *bytes, int size)
        {
            RelayHead head;
            if (!relayHeadRead(bytes, size, head) || head.session != s.session)
                return false;
            switch (head.kind)
            {
            case RelayKind::Pong:
                return true;
            case RelayKind::Opened:
                if (s.role == Role::Host)
                    relayRoomIdRead(bytes, size, s.roomId);
                break;
            case RelayKind::Rooms:
                if (s.role == Role::Scan)
                {
                    const int count = relayRoomsRead(bytes, size, s.rooms, kRelayListMost);
                    if (count >= 0)
                        s.roomCount = count;
                }
                break;
            case RelayKind::Joined:
                if (s.role == Role::Client && s.slot < 0)
                {
                    const int slot = relaySmallRead(bytes, size);
                    if (slot >= 1 && slot < kNetPlayers)
                        s.slot = slot;
                }
                break;
            case RelayKind::Refused:
                if (s.role == Role::Client && s.slot < 0)
                    s.refusedNow = true;
                break;
            case RelayKind::Status:
                if (s.role == Role::Host)
                {
                    const int mask = relaySmallRead(bytes, size);
                    if (mask >= 0)
                        s.clientMask = static_cast<unsigned>(mask) & (((1u << kNetPlayers) - 1) & ~1u);
                }
                break;
            case RelayKind::Data:
            {
                int who = 0;
                const std::uint8_t *frame = nullptr;
                const int length = relayDataRead(bytes, size, who, frame);
                if (s.role == Role::Host && who >= 1 && who < kNetPlayers)
                    keep(who, frame, length);
                else if (s.role == Role::Client && s.slot > 0 && who == 0)
                    keep(0, frame, length);
                break;
            }
            case RelayKind::Bye:
                // A client: out (the host went, or sent it away). A host: the
                // relay let it go (it had been quiet too long) -- its room is
                // opened again.
                if (s.role == Role::Client)
                    s.byeNow = true;
                else if (s.role == Role::Host)
                {
                    s.roomId = 0;
                    s.clientMask = 0;
                }
                break;
            default:
                break;
            }
            return false;
        }

        /// What has come, read; the IP stack let run; and what is said again
        /// until it is answered, said. True if the relay answered a Ping.
        TWL_CODE bool drain()
        {
            // (Twice: once for what has come in, once for what was sent.)
            cothread_yield();
            cothread_yield();
            bool pong = false;
            for (;;)
            {
                sockaddr_in from = {};
                socklen_t fromSize = sizeof(from);
                const int size = recvfrom(s.sock, s.datagram, sizeof(s.datagram), MSG_DONTWAIT,
                                          reinterpret_cast<sockaddr *>(&from), &fromSize);
                if (size <= 0)
                    break;
                if (from.sin_addr.s_addr != s.server.sin_addr.s_addr || from.sin_port != s.server.sin_port)
                    continue;
                pong = heard(s.datagram, size) || pong;
            }
            return pong;
        }

        TWL_CODE void pump()
        {
            if (!started)
                return;
            const std::uint32_t now = Global::vblanks();
            if (now == s.lastPump)
                return;
            s.lastPump = now;
            drain();
            switch (s.role)
            {
            case Role::Host:
                // (Its room, said again now and then: to be sure the relay
                // has it, and that it is still there.)
                if (now - s.lastOpen >= (s.roomId == 0 ? 30u : 60u))
                    sendOpen();
                break;
            case Role::Scan:
                if (now - s.lastList >= 60)
                {
                    sendSmall(RelayKind::List);
                    s.lastList = now;
                }
                break;
            case Role::Client:
                if (s.slot < 0 && !s.refusedNow && now - s.lastJoin >= 30)
                    sendJoin();
                break;
            default:
                break;
            }
            // (Something every second at least, or the relay lets it go.)
            if (now - s.lastSent >= 60)
                sendSmall(RelayKind::Ping);
        }

        TWL_CODE void closeSocket()
        {
            if (s.sock >= 0)
            {
                closesocket(s.sock);
                s.sock = -1;
            }
        }

        /// Off the network; the library itself stays (wifiUp).
        TWL_CODE void wifiOff()
        {
            Wifi_DisconnectAP();
        }

        TWL_CODE bool start()
        {
            if (started)
                return true;
            if (const char *no = unavailable())
            {
                fail(1, no);
                return false;
            }
            std::memset(s.ring, 0, sizeof(s.ring));
            s.ringHead = s.ringTail = 0;
            s.sock = -1;
            s.role = Role::None;
            if (!wifiUp && !Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE))
            {
                char text[48];
                std::snprintf(text, sizeof(text), kNoStart, Wifi_GetInitFailStage());
                fail(2, text);
                return false;
            }
            wifiUp = true;
            // The console's own saved connections.
            Wifi_AutoConnect();
            int status = ASSOCSTATUS_DISCONNECTED;
            for (int i = 0; i < kConnectFrames; ++i)
            {
                status = Wifi_AssocStatus();
                if (status == ASSOCSTATUS_ASSOCIATED || status == ASSOCSTATUS_CANNOTCONNECT)
                    break;
                cothread_yield_irq(IRQ_VBLANK);
            }
            if (status != ASSOCSTATUS_ASSOCIATED)
            {
                fail(3, kNoNetwork);
                wifiOff();
                return false;
            }
            // Where the relay is.
            addrinfo hints = {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            addrinfo *found = nullptr;
            if (getaddrinfo(s.host, nullptr, &hints, &found) != 0 || found == nullptr)
            {
                fail(4, kNoName);
                wifiOff();
                return false;
            }
            s.server = *reinterpret_cast<const sockaddr_in *>(found->ai_addr);
            s.server.sin_port = htons(static_cast<std::uint16_t>(s.port));
            freeaddrinfo(found);
            // (Never waited on: every read is MSG_DONTWAIT, and a datagram
            // is sent at once.)
            s.sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (s.sock < 0)
            {
                fail(5, kNoSocket);
                closeSocket();
                wifiOff();
                return false;
            }
            // A session of its own: a console started again is another one.
            std::uint8_t mac[6] = {};
            Wifi_GetData(WIFIGETDATA_MACADDRESS, 6, mac);
            s.session = (Global::vblanks() * 2654435761u) ^ (static_cast<std::uint32_t>(mac[2]) << 24 | mac[3] << 16 |
                                                             mac[4] << 8 | mac[5]) ^ Wifi_GetIP();
            if (s.session == 0)
                s.session = 1;
            // ... and the relay is there.
            started = true;
            bool answered = false;
            for (int i = 0; i < kAnswerFrames && !answered; ++i)
            {
                if (i % 30 == 0)
                    sendSmall(RelayKind::Ping);
                answered = drain();
                cothread_yield_irq(IRQ_VBLANK);
            }
            if (!answered)
            {
                started = false;
                char text[64];
                std::snprintf(text, sizeof(text), kNoAnswer, s.host, s.port);
                fail(6, text);
                closeSocket();
                wifiOff();
                return false;
            }
            s.lastPump = s.lastSent = Global::vblanks();
            failCode = 0;
            whyText[0] = 0;
            return true;
        }
    } // namespace

    TWL_CODE static const char *unavailableTwl()
    {
        readIni();
        return iniThere ? nullptr : kNoIni;
    }

    const char *unavailable()
    {
        if (!isDSiMode())
            return "Online play needs a DSi";
        return unavailableTwl();
    }

    bool holdsWifi()
    {
        return wifiUp;
    }

    const char *why()
    {
        return whyText;
    }

    int failStage()
    {
        return failCode;
    }

    TWL_CODE void shutdown()
    {
        if (!started)
            return;
        // (Said twice: there is no waiting to hear it arrived.)
        if (s.role != Role::None && s.role != Role::Scan)
        {
            sendSmall(RelayKind::Bye);
            sendSmall(RelayKind::Bye);
            drain();
        }
        // (A new session: what the relay knew of this one is gone with it.)
        s.session = s.session * 1664525u + 1013904223u;
        if (s.session == 0)
            s.session = 1;
        s.role = Role::None;
        s.roomId = 0;
        s.clientMask = 0;
        s.slot = -1;
        s.ringHead = s.ringTail = 0;
    }

    TWL_CODE void powerDown()
    {
        if (!started)
            return;
        shutdown();
        closeSocket();
        wifiOff();
        started = false;
    }

    TWL_CODE bool host(const char *name, int most)
    {
        if (!start())
            return false;
        if (s.role == Role::Host)
        {
            admit(true);
            return true;
        }
        shutdown();
        s.role = Role::Host;
        s.open = RelayOpen();
        std::snprintf(s.open.name, sizeof(s.open.name), "%s", name);
        s.open.most = static_cast<std::uint8_t>(most < 2 ? 2 : (most > kNetPlayers ? kNetPlayers : most));
        s.roomId = 0;
        s.clientMask = 0;
        s.admitting = true;
        sendOpen();
        return true;
    }

    TWL_CODE unsigned clients()
    {
        pump();
        return started && s.role == Role::Host ? s.clientMask : 0;
    }

    TWL_CODE void hostRoom(const std::uint8_t *bytes, int size)
    {
        if (!started || s.role != Role::Host)
            return;
        std::memset(s.open.room, 0, sizeof(s.open.room));
        std::memcpy(s.open.room, bytes, static_cast<std::size_t>(size < kNetRoomBytes ? size : kNetRoomBytes));
        sendOpen();
    }

    TWL_CODE bool hostRoomAt(int index, std::uint8_t *bytes, int &size, int &rssi)
    {
        size = 0;
        rssi = 0;
        if (!started || s.role != Role::Scan || index < 0 || index >= s.roomCount)
            return false;
        std::memcpy(bytes, s.rooms[index].room, kNetRoomBytes);
        size = kNetRoomBytes;
        rssi = 255; // (four bars: there is no signal to speak of)
        return true;
    }

    TWL_CODE int strength()
    {
        return joined() ? 255 : 0;
    }

    TWL_CODE void admit(bool allow)
    {
        if (!started || s.role != Role::Host)
            return;
        s.admitting = allow;
        sendSmall(RelayKind::Admit, allow ? 1 : 0);
    }

    TWL_CODE void kick(int c)
    {
        if (started && s.role == Role::Host && c >= 1 && c < kNetPlayers)
            sendSmall(RelayKind::Kick, c);
    }

    TWL_CODE bool scan()
    {
        if (!start())
            return false;
        shutdown();
        s.role = Role::Scan;
        s.roomCount = 0;
        sendSmall(RelayKind::List);
        s.lastList = Global::vblanks();
        return true;
    }

    TWL_CODE int found()
    {
        pump();
        return started && s.role == Role::Scan ? s.roomCount : 0;
    }

    TWL_CODE bool hostAt(int index, char *name, int room, int *players, int *most)
    {
        if (!started || s.role != Role::Scan || index < 0 || index >= s.roomCount || !s.rooms[index].open)
            return false;
        if (name != nullptr && room > 0)
            std::snprintf(name, static_cast<std::size_t>(room), "%s", s.rooms[index].name);
        if (players != nullptr)
            *players = s.rooms[index].players;
        if (most != nullptr)
            *most = s.rooms[index].most;
        return true;
    }

    TWL_CODE bool join(int index)
    {
        if (!started || s.role != Role::Scan || index < 0 || index >= s.roomCount)
            return false;
        s.joinId = s.rooms[index].id;
        s.role = Role::Client;
        s.slot = -1;
        s.refusedNow = s.byeNow = false;
        s.ringHead = s.ringTail = 0;
        sendJoin();
        return true;
    }

    TWL_CODE bool joined()
    {
        pump();
        return started && s.role == Role::Client && s.slot > 0 && !s.byeNow;
    }

    TWL_CODE bool refused()
    {
        return started && s.role == Role::Client && s.refusedNow;
    }

    TWL_CODE void send(const void *bytes, int size)
    {
        if (!started || size <= 0)
            return;
        std::uint8_t datagram[kRelayMostBytes];
        if (s.role == Role::Host)
            sendRaw(datagram, relayDataWrite(datagram, s.session, 0, bytes, size));
        else if (s.role == Role::Client && s.slot > 0 && !s.byeNow)
            sendRaw(datagram, relayDataWrite(datagram, s.session, s.slot, bytes, size));
    }

    TWL_CODE int take(std::uint8_t *into, int &from)
    {
        pump();
        if (!started || s.ringHead == s.ringTail)
            return 0;
        const State::Slot &slot = s.ring[s.ringHead];
        const int size = slot.size;
        std::memcpy(into, slot.bytes, static_cast<std::size_t>(size));
        from = slot.from;
        s.ringHead = (s.ringHead + 1) % kRingSlots;
        return size;
    }

    TWL_CODE unsigned dropped()
    {
        return started ? s.ringDropped : 0;
    }

    TWL_CODE void address(std::uint8_t out[6])
    {
        std::memset(out, 0, 6);
        if (started)
            Wifi_GetData(WIFIGETDATA_MACADDRESS, 6, out);
    }
} // namespace hlrt::netrelay

#else // HLDS_CHILD: a console sent the game is never online.

namespace hlrt::netrelay
{
    const char *unavailable() { return "Not on a console sent the game"; }
    const char *why() { return unavailable(); }
    bool holdsWifi() { return false; }
    int failStage() { return 1; }
    void shutdown() {}
    void powerDown() {}
    bool host(const char *, int) { return false; }
    unsigned clients() { return 0; }
    void hostRoom(const std::uint8_t *, int) {}
    bool hostRoomAt(int, std::uint8_t *, int &size, int &rssi)
    {
        size = rssi = 0;
        return false;
    }
    int strength() { return 0; }
    void admit(bool) {}
    void kick(int) {}
    bool scan() { return false; }
    int found() { return 0; }
    bool hostAt(int, char *, int, int *, int *) { return false; }
    bool join(int) { return false; }
    bool joined() { return false; }
    bool refused() { return false; }
    void send(const void *, int) {}
    int take(std::uint8_t *, int &) { return 0; }
    unsigned dropped() { return 0; }
    void address(std::uint8_t out[6]) { std::memset(out, 0, 6); }
} // namespace hlrt::netrelay

#endif
