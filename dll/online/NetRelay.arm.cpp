// Online play's relay client (tools/relay), in the library loaded the first
// time the console goes online (dsl/online.dsl: source/net/OnlineApi.hpp,
// source/net/OnlineDll.cpp), with the IP stack it talks through (dswifi's,
// apart: dswifi_lwip.h). The program's NetRelay.hpp functions hand each call
// here (source/net/NetRelay.cpp); what this calls of the program is the
// program's (build.py links it against the program's ELF).

#include "net/NetRelay.hpp"
#include "net/OnlineApi.hpp"
#include "game/FamilyApi.hpp"

#include <cstdio>
#include <cstring>

#include <nds.h>
#include <nds/cothread.h>

#include <arpa/inet.h>
#include <dswifi9.h>
#include <dswifi_lwip.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Global.hpp"
#include "HlIni.hpp"
#include "HlNet.hpp"
#include "HlRelay.hpp"

// What is learnt of dswifi's Internet side, kept:
//
//   - The IP stack is a cooperative thread (dswifi's own): it only runs when
//     the program yields. swiWaitForVBlank does not -- it halts -- so the
//     frame yields here (pump), and every wait here is cothread_yield_irq.
//   - Local multiplayer is not there in DSi mode: the radio (NetLink) and
//     this are never on at once; NetLink powers one down for the other.

namespace hlrt::relay
{
    using netrelay::Connect;

    // (NetRelay.hpp's, here in this library's own namespace: the program has
    // its own of each, which hand their calls here.)
    const char *unavailable();
    bool holdsWifi();
    const char *why();
    int failStage();
    void connectBegin();
    Connect connectStep();
    const char *connectText();
    void connectCancel();
    bool ready();
    void keepAlive();
    void shutdown();
    void powerDown();
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
    bool reconnecting();
    bool refused();
    void send(const void *bytes, int size);
    int take(std::uint8_t *into, int &from);
    unsigned dropped();
    void address(std::uint8_t out[6]);
    void setName(const char *name);
    const char *refusedText();
    const char *letGo();
    bool knock(int &ticket, char *name);
    void answer(int ticket, bool yes);
    void allowVoice(bool allow);
    void sendVoice(int player, int number, int rate, const std::uint8_t *packet);
    bool takeVoice(int &player, int &number, int &rate, std::uint8_t *packet);
    void radioCounts(char *out, int room);

    namespace
    {
        using namespace hldata;

#define HL_TWL_TEXT(name, text) const char name[] = text
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
        HL_TWL_TEXT(kGameOlder, "Update the game: the relay is v%d, the game v%d");
        HL_TWL_TEXT(kRelayOlder, "The relay is older than the game (v%d, game v%d)");
#undef HL_TWL_TEXT

        constexpr std::uint32_t kConnectFrames = 20 * 60; ///< to join a saved network
        constexpr std::uint32_t kAnswerFrames = 5 * 60; ///< for the relay to answer
        constexpr int kRingSlots = 24;
        constexpr int kHeardSlots = 12; ///< voice packets come, not yet read
        /// Nothing from the relay for this long, while listening (it answers
        /// a Ping each second): the session is taken up again, as from
        /// another address (the network dropped a moment, or the router gave
        /// another port).
        constexpr std::uint32_t kRelayQuietFrames = 3 * 60;
        /// Coming in, not answered for this long: given up.
        constexpr std::uint32_t kEnterFrames = 10 * 60;
        /// Taking a session up again, not answered for this long: given up.
        /// A minute: a home network drops for longer than a few seconds (8,
        /// before -- "The relay stopped answering" on a DSi in a lobby), and
        /// the relay keeps the console and its room 3.5 minutes. NetGame does
        /// not count the host's silence meanwhile (reconnecting()).
        constexpr std::uint32_t kResumeFrames = 60 * 60;
        /// The console's own Wi-Fi not associated, looked at each second, this
        /// many times in a row: the connection is taken for gone.
        constexpr int kWifiDownLooks = 5;
        // Why it was let go, apart from the relay's own reasons (RelayBye).
        constexpr std::uint8_t kByeForgotten = 250; ///< the relay does not know it (Unknown)
        constexpr std::uint8_t kByeNoAnswer = 251;  ///< nothing heard from the relay
        constexpr std::uint8_t kByeWifi = 252;      ///< the console's Wi-Fi connection went

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
            std::uint32_t enterSince;
            /// Taking its session up again (Enter with its token): the
            /// session it had, which the Welcome must give back.
            bool resuming;
            std::uint32_t oldSession;
            /// When the relay was last heard (in its session), and when the
            /// Wi-Fi was last looked at, and how many times in a row it was down.
            std::uint32_t lastHeard;
            std::uint32_t lastPing;
            std::uint32_t lastWifiLook;
            std::uint8_t wifiDown;
            std::uint8_t cookie[kRelayCookieBytes];
            std::uint8_t token[kRelayTokenBytes];
            /// The relay's version, if it answered that it speaks another; 0: not.
            std::uint8_t relayVersion;
            /// Why the relay let it go (RelayBye; 255: it did not say); 0: it has not.
            std::uint8_t byeWhy;
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
        // On a DSi it is cleared before main, as all of is.)
        union Holder
        {
            constexpr Holder() {}
            State state;
        };
        Holder holder;
        State &s = holder.state;

        void fail(int code, const char *text)
        {
            failCode = code;
            std::snprintf(whyText, sizeof(whyText), kText, text);
        }

        void readIni()
        {
            if (iniRead)
                return;
            iniRead = true;
            static const char kRead[] = "rb";
            std::FILE *file = std::fopen(global.dataPath(kIniName).c_str(), kRead);
            if (file == nullptr)
                return;
            char text[512];
            const std::size_t size = std::fread(text, 1, sizeof(text), file);
            std::fclose(file);
            iniThere = iniValue(text, size, kIniHost, s.host, sizeof(s.host)) && s.host[0] != 0;
            s.port = iniNumber(text, size, kIniPort, kRelayPort, 1, 65535);
        }

        void sendRaw(const std::uint8_t *bytes, int size)
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

        void sendSmall(RelayKind kind, int value = -1)
        {
            std::uint8_t bytes[kRelayHeadBytes + 1];
            sendRaw(bytes, relaySmallWrite(bytes, kind, s.session, value));
        }

        void sendAdmit()
        {
            sendSmall(RelayKind::Admit, (s.admitting ? kRelayAdmitIn : 0) | (s.voice ? kRelayAdmitVoice : 0));
        }

        void sendOpen()
        {
            std::uint8_t bytes[kRelayHeadBytes + kRelayOpenBytes];
            sendRaw(bytes, relayOpenWrite(bytes, s.session, s.open));
            sendAdmit();
            s.lastOpen = Global::vblanks();
        }

        void sendJoin()
        {
            std::uint8_t bytes[kRelayHeadBytes + 4 + kNetTextBytes];
            sendRaw(bytes, relayJoinWrite(bytes, s.session, s.joinId, s.name));
            s.lastJoin = Global::vblanks();
        }

        /// Hello, or Enter with the cookie: whichever it is at.
        void sendEntering()
        {
            std::uint8_t bytes[kRelayEnterBytes > kRelayHelloBytes ? kRelayEnterBytes : kRelayHelloBytes];
            if (s.entering == 1)
                sendRaw(bytes, relayHelloWrite(bytes, s.session));
            else
                sendRaw(bytes, relayEnterWrite(bytes, s.session, s.cookie, s.resuming ? s.token : nullptr));
            s.lastEnter = Global::vblanks();
        }

        /// Coming in afresh, with a new nonce: the relay knows nothing of
        /// this console until it is answered.
        void beginEnter()
        {
            s.session = (s.session * 1664525u + 1013904223u) ^ Global::vblanks();
            if (s.session == 0)
                s.session = 1;
            s.in = false;
            s.entering = 1;
            s.enterTries = 0;
            s.enterSince = Global::vblanks();
            sendEntering();
        }

        /// No longer known to the relay, or no longer reaching it: `why`
        /// kept for the menu (letGo).
        void sayGoodbye()
        {
            // Said whether or not it is heard (twice, as a Bye always is):
            // the relay lets the console and its room go now, not after its
            // 3.5 minutes -- a room left behind was still listed beside the
            // host's next one. With the session the relay knows: the one
            // before, while taking it up again.
            if (s.sock < 0 || (!s.in && !s.resuming))
                return;
            std::uint8_t bytes[kRelayHeadBytes + 1];
            const int size = relaySmallWrite(bytes, RelayKind::Bye, s.resuming ? s.oldSession : s.session, -1);
            for (int i = 0; i < 2; ++i)
                sendto(s.sock, bytes, static_cast<std::size_t>(size), 0, reinterpret_cast<const sockaddr *>(&s.server),
                       sizeof(s.server));
        }

        void letGoNow(std::uint8_t why)
        {
            sayGoodbye();
            s.byeWhy = why;
            s.in = false;
            s.entering = 0;
            s.resuming = false;
            if (s.role == Role::Client)
                s.byeNow = true;
        }

        /// Its session taken up again, its role and place kept: the relay
        /// gave it a token for this. (Until answered, nothing else is sent.)
        void resume()
        {
            if (s.resuming)
                return;
            s.resuming = true;
            s.oldSession = s.session;
            beginEnter();
        }

        /// Coming in: said again until answered (each half a second). An Enter
        /// not answered a few times (its cookie gone stale, or lost): Hello
        /// again.
        /// How long since `at`, at `now` (vblanks); 0 if `at` is later. The
        /// stamps are taken as things happen -- a datagram heard while drain()
        /// lets the IP stack run, after the frame's `now` was read -- and one
        /// a vblank after `now` was, unsigned, four billion vblanks ago: "the
        /// relay silent", a resume begun, and it given up at once ("The relay
        /// stopped answering", seconds after joining: a burst of datagrams
        /// makes a drain long).
        std::uint32_t since(std::uint32_t now, std::uint32_t at)
        {
            return static_cast<std::int32_t>(now - at) > 0 ? now - at : 0;
        }

        void stepEnter(std::uint32_t now)
        {
            if (s.in || s.entering == 0 || since(now, s.lastEnter) < 30)
                return;
            if (s.entering == 2 && ++s.enterTries >= 4)
            {
                s.entering = 1;
                s.enterTries = 0;
            }
            sendEntering();
        }

        /// Come in: what the role it has says first, said now.
        void sayRole()
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

        void keep(int from, const std::uint8_t *frame, int size)
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
        bool heard(const std::uint8_t *bytes, int size)
        {
            // (The relay speaks another version: kept, for the menu to say.)
            int version = 0;
            std::uint32_t echoed = 0;
            if (relayVersionRead(bytes, size, version, echoed))
            {
                if (echoed == s.session && version != kRelayVersion)
                    s.relayVersion = static_cast<std::uint8_t>(version);
                return false;
            }
            RelayHead head;
            if (!relayHeadRead(bytes, size, head) || head.session != s.session)
                return false;
            if (!s.in && head.kind != RelayKind::Challenge && head.kind != RelayKind::Welcome)
                return false;
            s.lastHeard = Global::vblanks();
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
                    const bool resumed = s.resuming;
                    s.resuming = false;
                    s.session = session;
                    s.entering = 0;
                    s.in = true;
                    s.wifiDown = 0;
                    if (!resumed)
                        sayRole();
                    else if (session != s.oldSession)
                    {
                        // (Another session: the relay did not know its token
                        // -- started again since. What it was is gone.)
                        sendSmall(RelayKind::Bye);
                        letGoNow(kByeForgotten);
                    }
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
            case RelayKind::Unknown:
                // The relay does not know it here: from another address (its
                // router gave it another port), or the relay was started
                // again, or let it go without its hearing. Its session is
                // taken up again if it can be (the Welcome says).
                resume();
                break;
            case RelayKind::Bye:
            {
                // Let go: no longer known to the relay (whatever it does
                // next, it comes in again), and why kept for the menu (letGo).
                const int why = relaySmallRead(bytes, size);
                s.byeWhy = static_cast<std::uint8_t>(why > 0 && why < 255 ? why : 255);
                s.in = false;
                s.entering = 0;
                if (s.role == Role::Client)
                    s.byeNow = true;
                break;
            }
            default:
                break;
            }
            return false;
        }

        /// What has come, read; the IP stack let run; and what is said again
        /// until it is answered, said. True if the relay answered a Ping.
        bool drain()
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

        void pump()
        {
            if (!started)
                return;
            std::uint32_t now = Global::vblanks();
            if (now == s.lastPump)
                return;
            // (Not listening for a while -- a map loading, nothing asked of
            // the relay: the relay's silence meanwhile is not counted; nor
            // the time a coming in or a taking up again has had to be
            // answered, which ended at once after a long load.)
            if (since(now, s.lastPump) > 60)
            {
                s.lastHeard = now;
                s.enterSince = now;
                s.lastEnter = now - 30;
                s.lastWifiLook = now;
            }
            s.lastPump = now;
            drain();
            // (What drain() heard was stamped as it came: the time read again.)
            now = Global::vblanks();
            if (s.in)
            {
                // The relay answers a Ping each second: nothing for long
                // while listening, it is gone. And the console's own Wi-Fi.
                if (since(now, s.lastHeard) >= kRelayQuietFrames)
                    resume();
                else if (since(now, s.lastWifiLook) >= 60)
                {
                    s.lastWifiLook = now;
                    s.wifiDown = Wifi_AssocStatus() == ASSOCSTATUS_ASSOCIATED ? 0 : s.wifiDown + 1;
                    if (s.wifiDown >= kWifiDownLooks)
                        letGoNow(kByeWifi);
                }
            }
            // (Looking for games: let go, it looks again -- a relay started
            // again is back in the list once it is there.)
            if (!s.in && s.role == Role::Scan && s.byeWhy != 0)
            {
                s.byeWhy = 0;
                s.roomCount = 0;
                beginEnter();
            }
            if (!s.in)
            {
                // Coming in not answered for long: given up (a host, a client;
                // anyone taking its session up again).
                if (s.entering != 0 && (s.role != Role::Scan || s.resuming) &&
                    since(now, s.enterSince) >= (s.resuming ? kResumeFrames : kEnterFrames))
                    letGoNow(kByeNoAnswer);
                stepEnter(now);
                return;
            }
            switch (s.role)
            {
            case Role::Host:
                // (Its room, said again now and then: to be sure the relay
                // has it, and that it is still there.)
                if (since(now, s.lastOpen) >= (s.roomId == 0 ? 30u : 60u))
                    sendOpen();
                break;
            case Role::Scan:
                if (since(now, s.lastList) >= 60)
                {
                    sendSmall(RelayKind::List);
                    s.lastList = now;
                }
                break;
            case Role::Client:
                if (s.slot < 0 && !s.refusedNow && since(now, s.lastJoin) >= 30)
                    sendJoin();
                break;
            default:
                break;
            }
            // (A Ping each second, whatever else is sent: the relay lets go
            // of a console it does not hear, and its Pong is what says the
            // relay is there -- a client hears nothing else while its host
            // loads a map.)
            if (since(now, s.lastPing) >= 60)
            {
                sendSmall(RelayKind::Ping);
                s.lastPing = now;
            }
        }

        void closeSocket()
        {
            if (s.sock >= 0)
            {
                closesocket(s.sock);
                s.sock = -1;
            }
        }

        /// Off the network; the library itself stays (wifiUp).
        void wifiOff()
        {
            Wifi_DisconnectAP();
        }

        // Going online, a step a frame (connectStep): the menu shows how far
        // it has got, and can be left, instead of a frozen screen for as long
        // as a network takes to join and the relay to answer.
        Connect phase = Connect::Off;
        std::uint32_t phaseAt = 0;

        void failConnect(int code, const char *text)
        {
            fail(code, text);
            closeSocket();
            wifiOff();
            phase = Connect::Failed;
        }

        void stepConnect()
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
                s.relayVersion = 0;
                s.byeWhy = 0;
                s.resuming = false;
                // (The IP stack, which is this library's, handed to the
                // wireless library first: it starts it with the radio.)
                if (!wifiUp)
                    Wifi_LwipInstall();
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
                if (s.relayVersion != 0)
                {
                    char text[64];
                    std::snprintf(text, sizeof(text), s.relayVersion > kRelayVersion ? kGameOlder : kRelayOlder,
                                  s.relayVersion, kRelayVersion);
                    failConnect(7, text);
                }
                else if (s.in)
                {
                    started = true;
                    s.lastPump = s.lastSent = s.lastHeard = s.lastPing = s.lastWifiLook = now;
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

    void connectBegin()
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

    Connect connectStep()
    {
        stepConnect();
        return phase;
    }

    const char *connectText()
    {
        static const char kStarting[] = "Starting the wireless...";
        static const char kJoining[] = "Joining a saved network...";
        static const char kFinding[] = "Finding the relay...";
        static const char kWaiting[] = "Waiting for the relay...";
        return phase == Connect::Starting  ? kStarting
               : phase == Connect::Joining ? kJoining
               : phase == Connect::Finding ? kFinding
                                           : kWaiting;
    }

    void connectCancel()
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

    void keepAlive()
    {
        if (started)
            pump();
    }

    const char *unavailable()
    {
        readIni();
        return iniThere ? nullptr : kNoIni;
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

    void shutdown()
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
        s.byeWhy = 0;
        s.resuming = false;
        s.role = Role::None;
        s.roomId = 0;
        s.clientMask = 0;
        s.slot = -1;
        s.ringHead = s.ringTail = 0;
        s.heardHead = s.heardTail = 0;
        s.voice = false;
    }

    void powerDown()
    {
        if (!started)
            return;
        shutdown();
        closeSocket();
        wifiOff();
        started = false;
        phase = Connect::Off;
    }

    bool host(const char *name, int most)
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

    unsigned clients()
    {
        pump();
        return started && s.role == Role::Host ? s.clientMask : 0;
    }

    void hostRoom(const std::uint8_t *bytes, int size)
    {
        if (!started || s.role != Role::Host)
            return;
        std::memset(s.open.room, 0, sizeof(s.open.room));
        std::memcpy(s.open.room, bytes, static_cast<std::size_t>(size < kNetRoomBytes ? size : kNetRoomBytes));
        sendOpen();
    }

    bool hostRoomAt(int index, std::uint8_t *bytes, int &size, int &rssi)
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

    int strength()
    {
        return joined() ? 255 : 0;
    }

    void admit(bool allow)
    {
        if (!started || s.role != Role::Host)
            return;
        s.admitting = allow;
        sendAdmit();
    }

    void allowVoice(bool allow)
    {
        if (!started || s.role != Role::Host || s.voice == allow)
            return;
        s.voice = allow;
        sendAdmit();
    }

    void sendVoice(int player, int number, int rate, const std::uint8_t *packet)
    {
        if (!started || !((s.role == Role::Host && s.roomId != 0) || (s.role == Role::Client && s.slot > 0 && !s.byeNow)))
            return;
        std::uint8_t datagram[kRelayVoiceBytes];
        sendRaw(datagram, relayVoiceWrite(datagram, s.session, player, number, rate, packet));
    }

    bool takeVoice(int &player, int &number, int &rate, std::uint8_t *packet)
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

    void kick(int c)
    {
        if (started && s.role == Role::Host && c >= 1 && c < kNetPlayers)
            sendSmall(RelayKind::Kick, c);
    }

    bool scan()
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

    int found()
    {
        pump();
        return started && s.role == Role::Scan ? s.roomCount : 0;
    }

    bool hostAt(int index, char *name, int room, int *players, int *most)
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

    bool join(int index)
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

    void setName(const char *name)
    {
        std::memset(s.name, 0, sizeof(s.name));
        std::snprintf(s.name, sizeof(s.name), "%s", name);
    }

    const char *refusedText()
    {
        static const char kDeclined[] = "The host did not let you in.";
        static const char kNoAnswer[] = "The host did not answer.";
        static const char kGone[] = "That game is no longer there.";
        static const char kFull[] = "That game is full, or a match is on.";
        static const char kBusy[] = "Too many on your network are asking to join.";
        switch (static_cast<RelayRefuse>(s.refuseWhy))
        {
        case RelayRefuse::Declined: return kDeclined;
        case RelayRefuse::NoAnswer: return kNoAnswer;
        case RelayRefuse::NoRoom: return kGone;
        case RelayRefuse::Busy: return kBusy;
        default: return kFull;
        }
    }

    const char *letGo()
    {
        static const char kHostLeft[] = "The host has left.";
        static const char kHostSilent[] = "The host stopped answering.";
        static const char kKicked[] = "The host has let you go.";
        static const char kSilent[] = "The relay heard nothing from this console for too long.";
        static const char kExpired[] = "The room closed: nobody joined it for a long time.";
        static const char kStopping[] = "The relay was stopped (restarted or updated).";
        static const char kTimeUp[] = "The room had been open as long as the relay allows.";
        static const char kOther[] = "The relay let this console go.";
        static const char kForgot[] = "The relay was restarted, or lost this console.";
        static const char kQuiet[] = "The relay stopped answering.";
        static const char kWifi[] = "The Wi-Fi connection was lost.";
        if (!started || s.byeWhy == 0)
            return nullptr;
        switch (static_cast<RelayBye>(s.byeWhy))
        {
        case RelayBye::HostLeft: return kHostLeft;
        case RelayBye::HostSilent: return kHostSilent;
        case RelayBye::Kicked: return kKicked;
        case RelayBye::Silent: return kSilent;
        case RelayBye::RoomExpired: return kExpired;
        case RelayBye::RelayStopping: return kStopping;
        case RelayBye::RoomTimeUp: return kTimeUp;
        default: break;
        }
        switch (s.byeWhy)
        {
        case kByeForgotten: return kForgot;
        case kByeNoAnswer: return kQuiet;
        case kByeWifi: return kWifi;
        default: return kOther;
        }
    }

    bool knock(int &ticket, char *name)
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

    void answer(int ticket, bool yes)
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

    void radioCounts(char *out, int room)
    {
        static const char kFormat[] = "rx %lu lost %lu tx %lu rej %lu a7 %lu";
        std::snprintf(out, static_cast<std::size_t>(room), kFormat,
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_RXQUEUEDPACKETS)),
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_RXQUEUEDLOST)),
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_TXQUEUEDPACKETS)),
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_TXQUEUEDREJECTED)),
                      static_cast<unsigned long>(Wifi_GetStats(WSTAT_ARM7_UPDATES)));
    }

    bool joined()
    {
        pump();
        return started && s.role == Role::Client && s.slot > 0 && !s.byeNow;
    }

    bool reconnecting()
    {
        return started && s.resuming && s.entering != 0;
    }

    bool refused()
    {
        return started && s.role == Role::Client && s.refusedNow;
    }

    void send(const void *bytes, int size)
    {
        if (!started || size <= 0)
            return;
        std::uint8_t datagram[kRelayMostBytes];
        if (s.role == Role::Host)
            sendRaw(datagram, relayDataWrite(datagram, s.session, 0, bytes, size));
        else if (s.role == Role::Client && s.slot > 0 && !s.byeNow)
            sendRaw(datagram, relayDataWrite(datagram, s.session, s.slot, bytes, size));
    }

    int take(std::uint8_t *into, int &from)
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

    unsigned dropped()
    {
        return started ? s.ringDropped : 0;
    }

    void address(std::uint8_t out[6])
    {
        std::memset(out, 0, 6);
        if (started)
            Wifi_GetData(WIFIGETDATA_MACADDRESS, 6, out);
    }
} // namespace hlrt::relay

/// The ID of the program this library is for: tools/dsl_family.py writes it
/// over this placeholder (tools/program_id.py's).
extern "C" __attribute__((visibility("default"), used)) const unsigned char hlFamilyProgramId[hlrt::kProgramIdBytes] = {
    'H', 'L', 'P', 'R', 'O', 'G', 'I', 'D'};

extern "C" __attribute__((visibility("default"))) bool GetOnlineAPI(std::uint32_t version, hlrt::OnlineFuncs *out)
{
    using namespace hlrt::relay;
    if (version != hlrt::kOnlineApiVersion || out == nullptr)
        return false;
    *out = hlrt::OnlineFuncs{};
    out->unavailable = unavailable;
    out->holdsWifi = holdsWifi;
    out->why = why;
    out->failStage = failStage;
    out->connectBegin = connectBegin;
    out->connectStep = []() { return static_cast<std::uint8_t>(connectStep()); };
    out->connectText = connectText;
    out->connectCancel = connectCancel;
    out->ready = ready;
    out->keepAlive = keepAlive;
    out->shutdown = shutdown;
    out->powerDown = powerDown;
    out->host = host;
    out->clients = clients;
    out->hostRoom = hostRoom;
    out->hostRoomAt = hostRoomAt;
    out->strength = strength;
    out->admit = admit;
    out->kick = kick;
    out->scan = scan;
    out->found = found;
    out->hostAt = hostAt;
    out->join = join;
    out->joined = joined;
    out->reconnecting = reconnecting;
    out->refused = refused;
    out->send = send;
    out->take = take;
    out->dropped = dropped;
    out->address = address;
    out->setName = setName;
    out->refusedText = refusedText;
    out->letGo = letGo;
    out->knock = knock;
    out->answer = answer;
    out->allowVoice = allowVoice;
    out->sendVoice = sendVoice;
    out->takeVoice = takeVoice;
    out->radioCounts = radioCounts;
    return true;
}
