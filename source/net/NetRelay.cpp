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

        constexpr std::uint32_t kConnectFrames = 20 * 60; ///< to join a saved network
        constexpr std::uint32_t kAnswerFrames = 5 * 60; ///< for the relay to answer
        constexpr int kRingSlots = 24;
        constexpr int kHeardSlots = 12; ///< voice packets come, not yet read

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
            /// What it says carries: the session the relay gave it -- or,
            /// coming in, its nonce.
            std::uint32_t session;
            // Coming in (HlRelay.hpp): the relay knows it only once it has
            // sent back the cookie it was given. `in`: it has (Welcome).
            bool in;
            std::uint8_t entering; ///< 0: not coming in; 1: saying Hello; 2: Enter
            std::uint8_t enterTries;
            std::uint32_t lastEnter;
            std::uint8_t cookie[kRelayCookieBytes];
            std::uint8_t token[kRelayTokenBytes];
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
            std::uint8_t refuseWhy; ///< RelayRefuse
            bool byeNow;
            std::uint32_t lastJoin;
            char name[kNetTextBytes]; ///< who asks to come in, as the host is shown
            // a host: those at the door, and what it answered last (said
            // again if the relay asks again: the answer may have been lost)
            struct Knocker
            {
                std::uint8_t ticket; ///< 0: none
                char name[kNetTextBytes];
            } knocks[kRelayKnocksMost];
            struct Answered
            {
                std::uint8_t ticket;
                bool yes;
            } answered[kRelayKnocksMost];
            int answeredNext;
            // what has come, not yet read
            struct Slot
            {
                std::uint16_t size;
                std::uint8_t from;
                std::uint8_t bytes[kNetCmdBytes];
            } ring[kRingSlots];
            int ringHead, ringTail;
            unsigned ringDropped;
            // what is said aloud, come, not yet read (apart from the game's
            // frames: a voice never takes their room); and whether a host
            // lets them talk
            struct Heard
            {
                std::uint8_t player, number;
                std::uint16_t rate;
                std::uint8_t packet[kVoiceAdpcmBytes];
            } heard[kHeardSlots];
            int heardHead, heardTail;
            bool voice;
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
            if (s.sock < 0 || size <= kRelayHeadBytes - 1)
                return;
            // (Not known to the relay yet: it would only let it fall.)
            if (!s.in && bytes[3] != static_cast<std::uint8_t>(RelayKind::Hello) &&
                bytes[3] != static_cast<std::uint8_t>(RelayKind::Enter))
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

        TWL_CODE void sendAdmit()
        {
            sendSmall(RelayKind::Admit, (s.admitting ? kRelayAdmitIn : 0) | (s.voice ? kRelayAdmitVoice : 0));
        }

        TWL_CODE void sendOpen()
        {
            std::uint8_t bytes[kRelayHeadBytes + kRelayOpenBytes];
            sendRaw(bytes, relayOpenWrite(bytes, s.session, s.open));
            sendAdmit();
            s.lastOpen = Global::vblanks();
        }

        TWL_CODE void sendJoin()
        {
            std::uint8_t bytes[kRelayHeadBytes + 4 + kNetTextBytes];
            sendRaw(bytes, relayJoinWrite(bytes, s.session, s.joinId, s.name));
            s.lastJoin = Global::vblanks();
        }

        /// Hello, or Enter with the cookie: whichever it is at.
        TWL_CODE void sendEntering()
        {
            std::uint8_t bytes[kRelayEnterBytes > kRelayHelloBytes ? kRelayEnterBytes : kRelayHelloBytes];
            if (s.entering == 1)
                sendRaw(bytes, relayHelloWrite(bytes, s.session));
            else
                sendRaw(bytes, relayEnterWrite(bytes, s.session, s.cookie, nullptr));
            s.lastEnter = Global::vblanks();
        }

        /// Coming in afresh, with a new nonce: the relay knows nothing of
        /// this console until it is answered.
        TWL_CODE void beginEnter()
        {
            s.session = (s.session * 1664525u + 1013904223u) ^ Global::vblanks();
            if (s.session == 0)
                s.session = 1;
            s.in = false;
            s.entering = 1;
            s.enterTries = 0;
            sendEntering();
        }

        /// Coming in: said again until answered (each half a second). An Enter
        /// not answered a few times (its cookie gone stale, or lost): Hello
        /// again.
        TWL_CODE void stepEnter(std::uint32_t now)
        {
            if (s.in || s.entering == 0 || now - s.lastEnter < 30)
                return;
            if (s.entering == 2 && ++s.enterTries >= 4)
            {
                s.entering = 1;
                s.enterTries = 0;
            }
            sendEntering();
        }

        /// Come in: what the role it has says first, said now.
        TWL_CODE void sayRole()
        {
            switch (s.role)
            {
            case Role::Host:
                sendOpen();
                break;
            case Role::Scan:
                sendSmall(RelayKind::List);
                s.lastList = Global::vblanks();
                break;
            case Role::Client:
                if (s.slot < 0 && !s.refusedNow)
                    sendJoin();
                break;
            default:
                break;
            }
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
            if (!s.in && head.kind != RelayKind::Challenge && head.kind != RelayKind::Welcome)
                return false;
            switch (head.kind)
            {
            case RelayKind::Challenge:
                if (s.entering == 1 && relayChallengeRead(bytes, size, s.cookie))
                {
                    s.entering = 2;
                    s.enterTries = 0;
                    sendEntering();
                }
                break;
            case RelayKind::Welcome:
            {
                std::uint32_t session = 0;
                if (s.entering == 2 && relayWelcomeRead(bytes, size, session, s.token))
                {
                    s.session = session;
                    s.entering = 0;
                    s.in = true;
                    sayRole();
                }
                break;
            }
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
                {
                    s.refusedNow = true;
                    s.refuseWhy = static_cast<std::uint8_t>(relaySmallRead(bytes, size));
                }
                break;
            case RelayKind::Knock:
            {
                int ticket = 0;
                char name[kNetTextBytes];
                if (s.role != Role::Host || !relayKnockRead(bytes, size, ticket, name) || ticket == 0)
                    break;
                // (Answered already: the answer again.)
                for (const State::Answered &done : s.answered)
                {
                    if (done.ticket == ticket)
                    {
                        std::uint8_t again[kRelayHeadBytes + 2];
                        sendRaw(again, relayAnswerWrite(again, s.session, ticket, done.yes));
                        return false;
                    }
                }
                State::Knocker *free = nullptr;
                for (State::Knocker &k : s.knocks)
                {
                    if (k.ticket == ticket)
                        return false; // (asked already: waiting for the host)
                    if (k.ticket == 0 && free == nullptr)
                        free = &k;
                }
                if (free != nullptr)
                {
                    free->ticket = static_cast<std::uint8_t>(ticket);
                    std::memcpy(free->name, name, kNetTextBytes);
                }
                break;
            }
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
            case RelayKind::Voice:
            {
                RelayVoice voice;
                const int next = (s.heardTail + 1) % kHeardSlots;
                if ((s.role != Role::Host && !(s.role == Role::Client && s.slot > 0)) ||
                    !relayVoiceRead(bytes, size, voice) || next == s.heardHead)
                    break;
                State::Heard &h = s.heard[s.heardTail];
                h.player = voice.player;
                h.number = voice.number;
                h.rate = voice.rate;
                std::memcpy(h.packet, voice.packet, kVoiceAdpcmBytes);
                s.heardTail = next;
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
            if (!s.in)
            {
                stepEnter(now);
                return;
            }
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

        // Going online, a step a frame (connectStep): the menu shows how far
        // it has got, and can be left, instead of a frozen screen for as long
        // as a network takes to join and the relay to answer.
        Connect phase = Connect::Off;
        std::uint32_t phaseAt = 0;

        TWL_CODE void failConnect(int code, const char *text)
        {
            fail(code, text);
            closeSocket();
            wifiOff();
            phase = Connect::Failed;
        }

        TWL_CODE void stepConnect()
        {
            const std::uint32_t now = Global::vblanks();
            switch (phase)
            {
            case Connect::Starting:
                std::memset(s.ring, 0, sizeof(s.ring));
                s.ringHead = s.ringTail = 0;
                s.sock = -1;
                s.role = Role::None;
                s.in = false;
                s.entering = 0;
                if (!wifiUp && !Wifi_InitDefault(INIT_ONLY | WIFI_ATTEMPT_DSI_MODE))
                {
                    char text[48];
                    std::snprintf(text, sizeof(text), kNoStart, Wifi_GetInitFailStage());
                    fail(2, text);
                    phase = Connect::Failed;
                    return;
                }
                wifiUp = true;
                // The console's own saved connections.
                Wifi_AutoConnect();
                phase = Connect::Joining;
                phaseAt = now;
                return;
            case Connect::Joining:
            {
                // (The IP stack's thread gets its address meanwhile.)
                cothread_yield();
                const int status = Wifi_AssocStatus();
                if (status == ASSOCSTATUS_ASSOCIATED)
                    phase = Connect::Finding;
                else if (status == ASSOCSTATUS_CANNOTCONNECT || now - phaseAt >= kConnectFrames)
                    failConnect(3, kNoNetwork);
                return;
            }
            case Connect::Finding:
            {
                // Where the relay is. (A name asked of the network's DNS waits
                // for its answer: a moment.)
                addrinfo hints = {};
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_DGRAM;
                addrinfo *found = nullptr;
                if (getaddrinfo(s.host, nullptr, &hints, &found) != 0 || found == nullptr)
                {
                    failConnect(4, kNoName);
                    return;
                }
                s.server = *reinterpret_cast<const sockaddr_in *>(found->ai_addr);
                s.server.sin_port = htons(static_cast<std::uint16_t>(s.port));
                freeaddrinfo(found);
                // (Never waited on: every read is MSG_DONTWAIT, and a datagram
                // is sent at once.)
                s.sock = socket(AF_INET, SOCK_DGRAM, 0);
                if (s.sock < 0)
                {
                    failConnect(5, kNoSocket);
                    return;
                }
                // (The first nonce's seed: a console started again is
                // another one.)
                std::uint8_t mac[6] = {};
                Wifi_GetData(WIFIGETDATA_MACADDRESS, 6, mac);
                s.session = (now * 2654435761u) ^
                            (static_cast<std::uint32_t>(mac[2]) << 24 | mac[3] << 16 | mac[4] << 8 | mac[5]) ^
                            Wifi_GetIP();
                if (s.session == 0)
                    s.session = 1;
                phase = Connect::Waiting;
                phaseAt = now;
                beginEnter();
                return;
            }
            case Connect::Waiting:
                // ... and the relay is there, and lets it in.
                drain();
                stepEnter(now);
                if (s.in)
                {
                    started = true;
                    s.lastPump = s.lastSent = now;
                    failCode = 0;
                    whyText[0] = 0;
                    phase = Connect::Ready;
                }
                else if (now - phaseAt >= kAnswerFrames)
                {
                    char text[64];
                    std::snprintf(text, sizeof(text), kNoAnswer, s.host, s.port);
                    failConnect(6, text);
                }
                return;
            default:
                return;
            }
        }
    } // namespace

    TWL_CODE void connectBegin()
    {
        if (started)
        {
            phase = Connect::Ready;
            return;
        }
        if (const char *no = unavailable())
        {
            fail(1, no);
            phase = Connect::Failed;
            return;
        }
        phase = Connect::Starting;
        phaseAt = Global::vblanks();
    }

    TWL_CODE Connect connectStep()
    {
        stepConnect();
        return phase;
    }

    TWL_CODE const char *connectText()
    {
        static TWL_DATA const char kStarting[] = "Starting the wireless...";
        static TWL_DATA const char kJoining[] = "Joining a saved network...";
        static TWL_DATA const char kFinding[] = "Finding the relay...";
        static TWL_DATA const char kWaiting[] = "Waiting for the relay...";
        return phase == Connect::Starting  ? kStarting
               : phase == Connect::Joining ? kJoining
               : phase == Connect::Finding ? kFinding
                                           : kWaiting;
    }

    TWL_CODE void connectCancel()
    {
        if (started || phase == Connect::Off)
            return;
        closeSocket();
        if (wifiUp)
            wifiOff();
        phase = Connect::Off;
    }

    bool ready()
    {
        return started;
    }

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
        if (s.in)
        {
            sendSmall(RelayKind::Bye);
            sendSmall(RelayKind::Bye);
            drain();
        }
        // (What the relay knew of it is gone with it: whatever it does next,
        // it comes in again first.)
        s.in = false;
        s.entering = 0;
        s.role = Role::None;
        s.roomId = 0;
        s.clientMask = 0;
        s.slot = -1;
        s.ringHead = s.ringTail = 0;
        s.heardHead = s.heardTail = 0;
        s.voice = false;
    }

    TWL_CODE void powerDown()
    {
        if (!started)
            return;
        shutdown();
        closeSocket();
        wifiOff();
        started = false;
        phase = Connect::Off;
    }

    TWL_CODE bool host(const char *name, int most)
    {
        // (Online first: connectBegin, connectStep.)
        if (!started)
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
        std::memset(s.knocks, 0, sizeof(s.knocks));
        std::memset(s.answered, 0, sizeof(s.answered));
        s.answeredNext = 0;
        s.lastOpen = Global::vblanks();
        beginEnter(); // (its room said once it is in: sayRole)
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
        sendAdmit();
    }

    TWL_CODE void allowVoice(bool allow)
    {
        if (!started || s.role != Role::Host || s.voice == allow)
            return;
        s.voice = allow;
        sendAdmit();
    }

    TWL_CODE void sendVoice(int player, int number, int rate, const std::uint8_t *packet)
    {
        if (!started || !((s.role == Role::Host && s.roomId != 0) || (s.role == Role::Client && s.slot > 0 && !s.byeNow)))
            return;
        std::uint8_t datagram[kRelayVoiceBytes];
        sendRaw(datagram, relayVoiceWrite(datagram, s.session, player, number, rate, packet));
    }

    TWL_CODE bool takeVoice(int &player, int &number, int &rate, std::uint8_t *packet)
    {
        pump();
        if (!started || s.heardHead == s.heardTail)
            return false;
        const State::Heard &h = s.heard[s.heardHead];
        player = h.player;
        number = h.number;
        rate = h.rate;
        std::memcpy(packet, h.packet, kVoiceAdpcmBytes);
        s.heardHead = (s.heardHead + 1) % kHeardSlots;
        return true;
    }

    TWL_CODE void kick(int c)
    {
        if (started && s.role == Role::Host && c >= 1 && c < kNetPlayers)
            sendSmall(RelayKind::Kick, c);
    }

    TWL_CODE bool scan()
    {
        if (!started)
            return false;
        shutdown();
        s.role = Role::Scan;
        s.roomCount = 0;
        s.lastList = Global::vblanks();
        beginEnter(); // (the list asked once it is in: sayRole)
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
        s.refuseWhy = 0;
        s.ringHead = s.ringTail = 0;
        sendJoin();
        return true;
    }

    TWL_CODE void setName(const char *name)
    {
        std::memset(s.name, 0, sizeof(s.name));
        std::snprintf(s.name, sizeof(s.name), "%s", name);
    }

    TWL_CODE const char *refusedText()
    {
        static TWL_DATA const char kDeclined[] = "The host did not let you in.";
        static TWL_DATA const char kNoAnswer[] = "The host did not answer.";
        static TWL_DATA const char kGone[] = "That game is no longer there.";
        static TWL_DATA const char kFull[] = "That game is full, or a match is on.";
        switch (static_cast<RelayRefuse>(s.refuseWhy))
        {
        case RelayRefuse::Declined: return kDeclined;
        case RelayRefuse::NoAnswer: return kNoAnswer;
        case RelayRefuse::NoRoom: return kGone;
        default: return kFull;
        }
    }

    TWL_CODE bool knock(int &ticket, char *name)
    {
        pump();
        if (!started || s.role != Role::Host)
            return false;
        for (const State::Knocker &k : s.knocks)
        {
            if (k.ticket != 0)
            {
                ticket = k.ticket;
                std::memcpy(name, k.name, kNetTextBytes);
                return true;
            }
        }
        return false;
    }

    TWL_CODE void answer(int ticket, bool yes)
    {
        if (!started || s.role != Role::Host)
            return;
        for (State::Knocker &k : s.knocks)
            if (k.ticket == ticket)
                k.ticket = 0;
        s.answered[s.answeredNext].ticket = static_cast<std::uint8_t>(ticket);
        s.answered[s.answeredNext].yes = yes;
        s.answeredNext = (s.answeredNext + 1) % kRelayKnocksMost;
        std::uint8_t bytes[kRelayHeadBytes + 2];
        sendRaw(bytes, relayAnswerWrite(bytes, s.session, ticket, yes));
    }

    TWL_CODE void radioCounts(char *out, int room)
    {
        static TWL_DATA const char kFormat[] = "rx %lu lost %lu tx %lu rej %lu a7 %lu";
        std::snprintf(out, static_cast<std::size_t>(room), kFormat,
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_RXQUEUEDPACKETS)),
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_RXQUEUEDLOST)),
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_TXQUEUEDPACKETS)),
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_TXQUEUEDREJECTED)),
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_ARM7_UPDATES)));
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
    void connectBegin() {}
    Connect connectStep() { return Connect::Failed; }
    const char *connectText() { return ""; }
    void connectCancel() {}
    bool ready() { return false; }
    void setName(const char *) {}
    const char *refusedText() { return ""; }
    bool knock(int &, char *) { return false; }
    void answer(int, bool) {}
    void radioCounts(char *out, int room)
    {
        if (room > 0)
            out[0] = 0;
    }
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
