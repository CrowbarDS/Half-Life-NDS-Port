#include "net/NetRelay.hpp"

#include <cstdio>
#include <cstring>

#include "net/OnlineApi.hpp"
#include "net/OnlineDll.hpp"

#ifndef HLDS_CHILD
#include "Global.hpp"
#endif

// The program's side of online play: each call handed to the online library
// (dll/online/NetRelay.arm.cpp, through source/net/OnlineApi.hpp's table),
// which is loaded the first time the console goes online (connectBegin).
// Before that, what it would answer: not online, nothing heard.

namespace hlrt::netrelay
{
#ifdef HLDS_CHILD
    // (A console sent the game is never online.)
    namespace
    {
        const OnlineFuncs *api() { return nullptr; }
    } // namespace

    const char *unavailable() { return "Not on a console sent the game"; }
    const char *why() { return unavailable(); }
    void connectBegin() {}
    Connect connectStep() { return Connect::Failed; }
    int failStage() { return 1; }
#else
    namespace
    {
        const OnlineFuncs *api() { return online::funcs(); }

        /// The library would not load: why, for the menu.
        const char *loadWhy = nullptr;
    } // namespace

    const char *unavailable()
    {
        if (const OnlineFuncs *f = api())
            return f->unavailable();
        // (The library's own question, asked without it: whether the card
        // says where the relay is.)
        if (std::FILE *file = std::fopen(global.dataPath("server.ini").c_str(), "rb"))
        {
            std::fclose(file);
            return nullptr;
        }
        return "No server.ini on the card (_nds/hl_nds/data)";
    }

    const char *why()
    {
        if (const OnlineFuncs *f = api())
            return f->why();
        return loadWhy != nullptr ? loadWhy : "";
    }

    int failStage()
    {
        if (const OnlineFuncs *f = api())
            return f->failStage();
        return loadWhy != nullptr ? 8 : 0;
    }

    void connectBegin()
    {
        loadWhy = nullptr;
        if (!online::load(&loadWhy))
            return;
        api()->connectBegin();
    }

    Connect connectStep()
    {
        if (const OnlineFuncs *f = api())
            return static_cast<Connect>(f->connectStep());
        return Connect::Failed;
    }
#endif

    bool holdsWifi()
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->holdsWifi();
    }

    const char *connectText()
    {
        const OnlineFuncs *f = api();
        return f != nullptr ? f->connectText() : "";
    }

    void connectCancel()
    {
        if (const OnlineFuncs *f = api())
            f->connectCancel();
    }

    bool ready()
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->ready();
    }

    void keepAlive()
    {
        if (const OnlineFuncs *f = api())
            f->keepAlive();
    }

    void shutdown()
    {
        if (const OnlineFuncs *f = api())
            f->shutdown();
    }

    void powerDown()
    {
        if (const OnlineFuncs *f = api())
            f->powerDown();
    }

    bool host(const char *name, int most)
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->host(name, most);
    }

    unsigned clients()
    {
        const OnlineFuncs *f = api();
        return f != nullptr ? f->clients() : 0;
    }

    void hostRoom(const std::uint8_t *bytes, int size)
    {
        if (const OnlineFuncs *f = api())
            f->hostRoom(bytes, size);
    }

    bool hostRoomAt(int index, std::uint8_t *bytes, int &size, int &rssi)
    {
        if (const OnlineFuncs *f = api())
            return f->hostRoomAt(index, bytes, size, rssi);
        size = rssi = 0;
        return false;
    }

    int strength()
    {
        const OnlineFuncs *f = api();
        return f != nullptr ? f->strength() : 0;
    }

    void admit(bool allow)
    {
        if (const OnlineFuncs *f = api())
            f->admit(allow);
    }

    void kick(int c)
    {
        if (const OnlineFuncs *f = api())
            f->kick(c);
    }

    bool scan()
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->scan();
    }

    int found()
    {
        const OnlineFuncs *f = api();
        return f != nullptr ? f->found() : 0;
    }

    bool hostAt(int index, char *name, int room, int *players, int *most)
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->hostAt(index, name, room, players, most);
    }

    bool join(int index)
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->join(index);
    }

    bool joined()
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->joined();
    }

    bool reconnecting()
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->reconnecting();
    }

    bool refused()
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->refused();
    }

    void send(const void *bytes, int size)
    {
        if (const OnlineFuncs *f = api())
            f->send(bytes, size);
    }

    int take(std::uint8_t *into, int &from)
    {
        const OnlineFuncs *f = api();
        return f != nullptr ? f->take(into, from) : 0;
    }

    unsigned dropped()
    {
        const OnlineFuncs *f = api();
        return f != nullptr ? f->dropped() : 0;
    }

    void address(std::uint8_t out[6])
    {
        if (const OnlineFuncs *f = api())
            f->address(out);
        else
            std::memset(out, 0, 6);
    }

    void setName(const char *name)
    {
        if (const OnlineFuncs *f = api())
            f->setName(name);
    }

    const char *refusedText()
    {
        const OnlineFuncs *f = api();
        return f != nullptr ? f->refusedText() : "";
    }

    const char *letGo()
    {
        const OnlineFuncs *f = api();
        return f != nullptr ? f->letGo() : nullptr;
    }

    bool knock(int &ticket, char *name)
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->knock(ticket, name);
    }

    void answer(int ticket, bool yes)
    {
        if (const OnlineFuncs *f = api())
            f->answer(ticket, yes);
    }

#ifndef HLDS_CHILD
    void allowVoice(bool allow)
    {
        if (const OnlineFuncs *f = api())
            f->allowVoice(allow);
    }

    void sendVoice(int player, int number, int rate, const std::uint8_t *packet)
    {
        if (const OnlineFuncs *f = api())
            f->sendVoice(player, number, rate, packet);
    }

    bool takeVoice(int &player, int &number, int &rate, std::uint8_t *packet)
    {
        const OnlineFuncs *f = api();
        return f != nullptr && f->takeVoice(player, number, rate, packet);
    }
#endif

    void radioCounts(char *out, int room)
    {
        if (const OnlineFuncs *f = api())
            f->radioCounts(out, room);
        else if (room > 0)
            out[0] = 0;
    }
} // namespace hlrt::netrelay
