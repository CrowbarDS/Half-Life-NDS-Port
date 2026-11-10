#include "Limits.hpp"

namespace hlrelay
{
    namespace
    {
        struct Named
        {
            const char *name;
            std::uint32_t Limits::*field;
            bool minutes = false; ///< a time in minutes: 0 is never, and kMostMinutes at most
        };
        constexpr Named kNamed[] = {
            {"hello-per-s", &Limits::helloPerSecond},
            {"control-per-s", &Limits::controlPerSecond},
            {"data-per-s", &Limits::dataPerSecond},
            {"host-bytes-per-s", &Limits::hostBytesPerSecond},
            {"client-bytes-per-s", &Limits::clientBytesPerSecond},
            {"voice-per-s", &Limits::voicePerSecond},
            {"peers-per-ip", &Limits::peersPerIp},
            {"rooms-per-ip", &Limits::roomsPerIp},
            {"knocks-per-ip", &Limits::knocksPerIp},
            {"out-bytes-per-s", &Limits::outBytesPerSecond},
            {"empty-room-minutes", &Limits::emptyRoomMinutes, true},
            {"room-max-minutes", &Limits::roomMaxMinutes, true},
            {"silent-seconds", &Limits::silentSeconds},
        };
    } // namespace

    bool Limits::set(const std::string &name, std::uint32_t value)
    {
        for (const Named &one : kNamed)
        {
            if (name == one.name)
            {
                if (one.minutes ? value > kMostMinutes : value == 0)
                    return false;
                if (one.field == &Limits::silentSeconds && value > kMostSilentSeconds)
                    return false;
                this->*one.field = value;
                return true;
            }
        }
        return false;
    }

    const char *Limits::help()
    {
        return "             hello-per-s        Hello and Enter from one address, before it is known (10)\n"
               "             control-per-s      a console's datagrams other than Data and Voice (30)\n"
               "             data-per-s         a console's Data datagrams (400)\n"
               "             host-bytes-per-s   a host's Data bytes, which go to each of its clients (262144)\n"
               "             client-bytes-per-s a client's Data bytes (49152)\n"
               "             voice-per-s        a console's Voice datagrams (45)\n"
               "             peers-per-ip       consoles known at one address at once (16)\n"
               "             rooms-per-ip       rooms open from one address at once (4)\n"
               "             knocks-per-ip      consoles of one address waiting at doors at once (8)\n"
               "             out-bytes-per-s    bytes the relay sends in all (12582912)\n"
               "             empty-room-minutes a room nobody is in or asking to join: closed after (30; 0 never)\n"
               "             room-max-minutes   any room: closed after (0, never)\n"
               "             silent-seconds     a console saying nothing this long is let go (210; 3600 at most)\n"
               "             Each takes up to twice its rate at once (voice: its rate),\n"
               "             then its rate.\n";
    }
} // namespace hlrelay
