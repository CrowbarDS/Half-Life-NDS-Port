#include "Limits.hpp"

namespace hlrelay
{
    namespace
    {
        struct Named
        {
            const char *name;
            std::uint32_t Limits::*field;
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
        };
    } // namespace

    bool Limits::set(const std::string &name, std::uint32_t value)
    {
        if (value == 0)
            return false;
        for (const Named &one : kNamed)
        {
            if (name == one.name)
            {
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
               "             data-per-s         a console's Data datagrams (90)\n"
               "             host-bytes-per-s   a host's Data bytes, which go to each of its clients (65536)\n"
               "             client-bytes-per-s a client's Data bytes (24576)\n"
               "             voice-per-s        a console's Voice datagrams (45)\n"
               "             peers-per-ip       consoles known at one address at once (16)\n"
               "             rooms-per-ip       rooms open from one address at once (4)\n"
               "             knocks-per-ip      consoles of one address waiting at doors at once (8)\n"
               "             out-bytes-per-s    bytes the relay sends in all (12582912)\n"
               "             Each takes up to twice its rate at once (voice: its rate),\n"
               "             then its rate.\n";
    }
} // namespace hlrelay
