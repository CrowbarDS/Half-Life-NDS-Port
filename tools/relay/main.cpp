// hlds-relay: the relay that consoles playing online meet through
// (core/Relay.hpp). One UDP port, one thread.
//
//   hlds-relay [--port N] [--bind ADDRESS] [--quiet]

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "Relay.hpp"

namespace
{
    volatile std::sig_atomic_t g_stop = 0;
    void onSignal(int) { g_stop = 1; }

    std::uint32_t nowMs()
    {
        using namespace std::chrono;
        return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    void usage()
    {
        std::fprintf(stderr, "usage: hlds-relay [--port N] [--bind ADDRESS] [--quiet]\n"
                             "  --port   UDP port to listen on (default %d)\n"
                             "  --bind   local IPv4 address to listen on (default 0.0.0.0, all)\n"
                             "  --quiet  print nothing while running\n",
                     hldata::kRelayPort);
    }
} // namespace

int main(int argc, char **argv)
{
    int port = hldata::kRelayPort;
    std::string bindTo = "0.0.0.0";
    bool quiet = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (arg == "--bind" && i + 1 < argc)
            bindTo = argv[++i];
        else if (arg == "--quiet")
            quiet = true;
        else
        {
            usage();
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }
    if (port <= 0 || port > 65535)
    {
        usage();
        return 2;
    }

    const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        std::perror("socket");
        return 1;
    }
    sockaddr_in here = {};
    here.sin_family = AF_INET;
    here.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, bindTo.c_str(), &here.sin_addr) != 1)
    {
        std::fprintf(stderr, "not an IPv4 address: %s\n", bindTo.c_str());
        return 2;
    }
    if (::bind(sock, reinterpret_cast<sockaddr *>(&here), sizeof(here)) != 0)
    {
        std::perror("bind");
        return 1;
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    hlrelay::Relay relay;
    if (!quiet)
    {
        relay.log = [](const std::string &line)
        {
            const std::time_t t = std::time(nullptr);
            char stamp[16];
            std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&t));
            std::printf("%s %s\n", stamp, line.c_str());
            std::fflush(stdout);
        };
        std::printf("hlds-relay: listening on %s:%d (UDP)\n", bindTo.c_str(), port);
        std::fflush(stdout);
    }

    std::vector<hlrelay::Out> out;
    std::uint8_t bytes[2048];
    std::uint32_t lastTick = nowMs();
    while (!g_stop)
    {
        pollfd fd = {sock, POLLIN, 0};
        const int ready = ::poll(&fd, 1, 100);
        if (ready > 0 && (fd.revents & POLLIN))
        {
            for (;;)
            {
                sockaddr_in from = {};
                socklen_t fromSize = sizeof(from);
                const ssize_t size = ::recvfrom(sock, bytes, sizeof(bytes), MSG_DONTWAIT,
                                                reinterpret_cast<sockaddr *>(&from), &fromSize);
                if (size <= 0)
                    break;
                hlrelay::Address at;
                at.ip = ntohl(from.sin_addr.s_addr);
                at.port = ntohs(from.sin_port);
                relay.receive(at, bytes, static_cast<int>(size), nowMs(), out);
            }
        }
        const std::uint32_t now = nowMs();
        if (now - lastTick >= 100)
        {
            relay.tick(now, out);
            lastTick = now;
        }
        for (const hlrelay::Out &one : out)
        {
            sockaddr_in to = {};
            to.sin_family = AF_INET;
            to.sin_addr.s_addr = htonl(one.to.ip);
            to.sin_port = htons(one.to.port);
            ::sendto(sock, one.bytes.data(), one.bytes.size(), 0, reinterpret_cast<sockaddr *>(&to), sizeof(to));
        }
        out.clear();
    }
    ::close(sock);
    return 0;
}
