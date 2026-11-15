// hlds-relay: the relay that consoles playing online meet through
// (core/Relay.hpp). One UDP port, one thread.
//
//   hlds-relay [--port N] [--bind ADDRESS] [--log-ips trunc|hash|full]
//              [--limit NAME=VALUE]... [--status-file PATH] [--quiet]

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "Relay.hpp"
#include "Serve.hpp"

namespace
{
    volatile std::sig_atomic_t g_stop = 0;
    void onSignal(int) { g_stop = 1; }

    std::uint32_t nowMs()
    {
        using namespace std::chrono;
        return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    /// The relay's UDP socket, as Serve reads and sends through it.
    class PosixSocket : public hlrelay::Socket
    {
    public:
        explicit PosixSocket(int fd) : fd_(fd) {}
        int receive(hlrelay::Address &from, std::uint8_t *bytes, int room) override
        {
            sockaddr_in at = {};
            socklen_t atSize = sizeof(at);
            const ssize_t size = ::recvfrom(fd_, bytes, static_cast<std::size_t>(room), MSG_DONTWAIT,
                                            reinterpret_cast<sockaddr *>(&at), &atSize);
            if (size <= 0)
                return 0;
            from.ip = ntohl(at.sin_addr.s_addr);
            from.port = ntohs(at.sin_port);
            return static_cast<int>(size);
        }
        bool send(const hlrelay::Address &to, const std::uint8_t *bytes, int size) override
        {
            sockaddr_in at = {};
            at.sin_family = AF_INET;
            at.sin_addr.s_addr = htonl(to.ip);
            at.sin_port = htons(to.port);
            return ::sendto(fd_, bytes, static_cast<std::size_t>(size), MSG_DONTWAIT,
                            reinterpret_cast<sockaddr *>(&at), sizeof(at)) == size;
        }

    private:
        int fd_;
    };

    /// `size` bytes from the kernel's random source; false if it gave none.
    bool randomBytes(std::uint8_t *out, std::size_t size)
    {
        while (size > 0)
        {
            const ssize_t got = ::getrandom(out, size, 0);
            if (got <= 0)
                return false;
            out += got;
            size -= static_cast<std::size_t>(got);
        }
        return true;
    }

    /// How often --status-file is written.
    constexpr std::uint32_t kStatusFileMs = 5000;

    /// The website's counts written to `path`, whole (a name beside it, then
    /// moved over it: a reader never sees half), readable by the web server.
    void writeStatus(const std::string &path, const std::string &json)
    {
        const std::string part = path + ".part";
        std::FILE *f = std::fopen(part.c_str(), "w");
        if (f == nullptr)
            return;
        const bool whole = std::fwrite(json.data(), 1, json.size(), f) == json.size();
        ::fchmod(::fileno(f), 0644);
        if (std::fclose(f) != 0 || !whole || std::rename(part.c_str(), path.c_str()) != 0)
            std::remove(part.c_str());
    }

    void usage()
    {
        std::fprintf(stderr, "usage: hlds-relay [--port N] [--bind ADDRESS] [--log-ips trunc|hash|full]\n"
                             "                  [--limit NAME=VALUE]... [--status-file PATH] [--quiet]\n"
                             "  --port     UDP port to listen on (default %d)\n"
                             "  --bind     local IPv4 address to listen on (default 0.0.0.0, all)\n"
                             "  --log-ips  how consoles' addresses are logged: trunc, the first three\n"
                             "             numbers (default); hash, a hash under a secret renewed each\n"
                             "             day (UTC); full, address and port (for debugging only)\n"
                             "  --limit    one of these limits (default in brackets), as often as needed:\n"
                             "%s"
                             "  --status-file  write how many consoles are online, rooms open and\n"
                             "             consoles in them to PATH (JSON, each 5 s; removed on stop),\n"
                             "             for a web server to give the project's website\n"
                             "  --quiet    print nothing while running\n",
                     hldata::kRelayPort, hlrelay::Limits::help());
    }
} // namespace

int main(int argc, char **argv)
{
    int port = hldata::kRelayPort;
    std::string bindTo = "0.0.0.0";
    bool quiet = false;
    std::string statusFile;
    hlrelay::IpLog ipLog = hlrelay::IpLog::Trunc;
    hlrelay::Limits limits;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (arg == "--bind" && i + 1 < argc)
            bindTo = argv[++i];
        else if (arg == "--log-ips" && i + 1 < argc)
        {
            const std::string how = argv[++i];
            if (how == "trunc")
                ipLog = hlrelay::IpLog::Trunc;
            else if (how == "hash")
                ipLog = hlrelay::IpLog::Hash;
            else if (how == "full")
                ipLog = hlrelay::IpLog::Full;
            else
            {
                usage();
                return 2;
            }
        }
        else if (arg == "--limit" && i + 1 < argc)
        {
            const std::string what = argv[++i];
            const std::size_t equals = what.find('=');
            char *end = nullptr;
            const unsigned long value =
                equals == std::string::npos ? 0 : std::strtoul(what.c_str() + equals + 1, &end, 10);
            if (equals == std::string::npos || end == nullptr || *end != 0 || value > 0xFFFFFFFFul ||
                !limits.set(what.substr(0, equals), static_cast<std::uint32_t>(value)))
            {
                std::fprintf(stderr, "not a limit: %s\n", what.c_str());
                usage();
                return 2;
            }
        }
        else if (arg == "--status-file" && i + 1 < argc)
            statusFile = argv[++i];
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
    // (Room for a burst of datagrams while a round is busy: as much as the
    // system lets a program ask for -- net.core.rmem_max says how much.)
    {
        const int want = 4 * 1024 * 1024;
        ::setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &want, sizeof(want));
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    hlrelay::Relay relay;
    relay.ipLog = ipLog;
    relay.limits = limits;
    // (Its own secret, new each time it starts: a cookie made before is no
    // good after.)
    {
        std::uint8_t secret[hlrelay::Relay::kSecretBytes];
        if (!randomBytes(secret, sizeof(secret)))
        {
            std::perror("getrandom");
            return 1;
        }
        relay.setSecret(secret);
    }
    // (The secret hashed addresses are made under, renewed each day: one
    // day's hashes cannot be matched with another's.)
    std::time_t saltDay = -1;
    auto renewSalt = [&]
    {
        const std::time_t day = std::time(nullptr) / 86400;
        if (day == saltDay)
            return true;
        std::uint8_t salt[16];
        if (!randomBytes(salt, sizeof(salt)))
            return false;
        relay.setLogSalt(salt);
        saltDay = day;
        return true;
    };
    if (!renewSalt())
    {
        std::perror("getrandom");
        return 1;
    }
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

    PosixSocket socket(sock);
    hlrelay::Serve serve(relay, socket, nowMs());
    bool more = false;
    std::uint32_t statusAt = nowMs() - kStatusFileMs;
    while (!g_stop)
    {
        // (More may be waiting after a full round: no wait then.)
        pollfd fd = {sock, POLLIN, 0};
        ::poll(&fd, 1, more ? 0 : static_cast<int>(hlrelay::Serve::kTickMs));
        more = serve.round(nowMs());
        renewSalt();
        if (!statusFile.empty() && nowMs() - statusAt >= kStatusFileMs)
        {
            statusAt = nowMs();
            writeStatus(statusFile, serve.statusJson(static_cast<std::uint64_t>(std::time(nullptr))));
        }
    }
    // (Stopped, by systemd or by hand: every console told, so that each says
    // why it is back in the menu. The website's counts go with it.)
    serve.stop();
    if (!statusFile.empty())
        std::remove(statusFile.c_str());
    ::close(sock);
    return 0;
}
