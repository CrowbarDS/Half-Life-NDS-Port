// The card-read probe: what a read of a map's archive costs during play, by
// where in the file it is, with FatFs's fast seek and without.
//
// A DS reads its card on the ARM9 (DLDI): the game waits for every byte. What
// a page of the world read in play may cost a frame is the WORST of these, not
// the mean. Without fast seek, a seek walks the file's cluster chain through
// the FAT, so a read far into a big archive may cost more than one near its
// start; NEA gives every mounted archive a 2 KB table (fatInitLookupCache),
// which holds 255 fragments of the file.
//
// For each of the card's biggest map archives (and c1a3, c2a5, c1a3a,
// c2a2b1 if there): its size and how many fragments it is in, then, with
// fast seek off and on (a table big enough for 2,047 fragments):
//   cold   the first 4 KB read after the file is opened, far into it
//   near   4 KB at random sector-aligned places in its first 256 KB
//   far    4 KB at random sector-aligned places in its last quarter
//   far+4  the same, 4 bytes past a sector (a member on a word, as now)
//   seq    4 KB reads one after the other from its middle
//   16K    16 KB at once, far
// Each as the least, mean, 95th percentile and most, in microseconds.
// Everything goes to <card>/_nds/hl_nds/cardread.txt (appended); the screen
// shows the far figures. Nothing on the card is changed but that file.

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <fat.h>
#include <nds.h>
#include <nds/arm9/dldi.h>

namespace
{
    constexpr int kReads = 48;
    constexpr int kSeqReads = 32;
    constexpr int kBigReads = 8;
    constexpr std::uint32_t kPage = 4096;
    constexpr std::uint32_t kBig = 16384;
    constexpr std::uint32_t kSector = 512;
    /// The fast-seek table the probe gives a file: 2 + 2 words a fragment.
    constexpr std::uint32_t kTableBytes = 16384;
    /// NEA's (NEANPAC.c): what it holds.
    constexpr int kNeaFragments = (2048 / 4 - 2) / 2;
    constexpr std::size_t kMostArchives = 6;

    alignas(32) std::uint8_t buffer[kBig];
    std::FILE *out = nullptr;

    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    std::uint32_t next()
    {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        return static_cast<std::uint32_t>(seed >> 16);
    }

    /// A line to the file (and the screen, when `screen`).
    void say(bool screen, const char *format, ...)
    {
        char line[160];
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        if (out != nullptr)
            std::fprintf(out, "%s\n", line);
        if (screen)
            std::printf("%s\n", line);
    }

    struct Spread
    {
        std::uint32_t least = 0, mean = 0, p95 = 0, most = 0;
        int failed = 0;
    };

    /// `bytes` at each of `at`, each read timed whole (the seek with it).
    Spread timeReads(int fd, const std::vector<std::uint32_t> &at, std::uint32_t bytes)
    {
        std::vector<std::uint32_t> us;
        Spread s;
        for (std::uint32_t offset : at)
        {
            const std::uint32_t before = cpuGetTiming();
            bool ok = lseek(fd, static_cast<off_t>(offset), SEEK_SET) == static_cast<off_t>(offset);
            for (std::uint32_t done = 0; ok && done < bytes;)
            {
                const ssize_t got = read(fd, buffer + done, bytes - done);
                ok = got > 0;
                done += ok ? static_cast<std::uint32_t>(got) : 0;
            }
            const std::uint32_t ticks = cpuGetTiming() - before;
            if (!ok)
            {
                ++s.failed;
                continue;
            }
            us.push_back(timerTicks2usec(ticks));
        }
        if (us.empty())
            return s;
        std::sort(us.begin(), us.end());
        std::uint64_t sum = 0;
        for (std::uint32_t u : us)
            sum += u;
        s.least = us.front();
        s.most = us.back();
        s.mean = static_cast<std::uint32_t>(sum / us.size());
        s.p95 = us[(us.size() * 95) / 100 < us.size() ? (us.size() * 95) / 100 : us.size() - 1];
        return s;
    }

    /// `count` places, each on a sector plus `past`, in [from, to).
    std::vector<std::uint32_t> places(std::uint32_t from, std::uint32_t to, int count, std::uint32_t past)
    {
        std::vector<std::uint32_t> at;
        const std::uint32_t sectors = to > from + kSector ? (to - from) / kSector : 1;
        for (int i = 0; i < count; ++i)
            at.push_back(from + (next() % sectors) * kSector + past);
        return at;
    }

    void line(const char *what, const Spread &s, bool screen)
    {
        say(screen, "  %-6s %6lu %6lu %6lu %6lu us%s", what, static_cast<unsigned long>(s.least),
            static_cast<unsigned long>(s.mean), static_cast<unsigned long>(s.p95),
            static_cast<unsigned long>(s.most), s.failed != 0 ? " (reads FAILED)" : "");
    }

    /// How many fragments the file is in: what a table of four words asks
    /// for. (On another descriptor, closed at once: a table too small is
    /// left half made on it, and seeks through it would go astray.)
    int fragmentsOf(const char *path)
    {
        const int fd = open(path, O_RDONLY);
        if (fd < 0)
            return -1;
        const int need = fatInitLookupCache(fd, 16);
        close(fd);
        if (need < 0)
            return need; // (not on FAT, or no memory)
        return need == 0 ? 1 : (need - 2) / 2;
    }

    void probeArchive(const std::string &path, std::uint32_t size)
    {
        const int fragments = fragmentsOf(path.c_str());
        say(true, "%s", path.c_str() + path.rfind('/') + 1);
        say(false, "  %lu bytes, %d fragment(s)%s", static_cast<unsigned long>(size), fragments,
            fragments > kNeaFragments ? " -- MORE than NEA's 2 KB table holds" : "");
        if (size < 2 * kBig)
            return;
        const std::uint32_t nearEnd = std::min<std::uint32_t>(256 * 1024, size - kPage);
        const std::uint32_t farFrom = size - size / 4;
        const std::uint32_t farEnd = size - kBig;
        for (int fast = 0; fast < 2; ++fast)
        {
            const int fd = open(path.c_str(), O_RDONLY);
            if (fd < 0)
            {
                say(true, "  cannot open");
                return;
            }
            if (fast != 0)
            {
                const int r = fatInitLookupCache(fd, kTableBytes);
                if (r != 0)
                {
                    say(true, "  fast seek: table refused (%d)", r);
                    close(fd);
                    continue;
                }
            }
            say(true, " fast seek %s   least  mean   p95   most", fast != 0 ? "ON " : "OFF");
            line("cold", timeReads(fd, places(farFrom, farEnd, 1, 0), kPage), false);
            line("near", timeReads(fd, places(0, nearEnd, kReads, 0), kPage), false);
            line("far", timeReads(fd, places(farFrom, farEnd, kReads, 0), kPage), true);
            line("far+4", timeReads(fd, places(farFrom, farEnd, kReads, 4), kPage), false);
            {
                std::vector<std::uint32_t> at;
                for (int i = 0; i < kSeqReads; ++i)
                    at.push_back((size / 2 & ~(kSector - 1)) + static_cast<std::uint32_t>(i) * kPage);
                line("seq", timeReads(fd, at, kPage), false);
            }
            line("16K", timeReads(fd, places(farFrom, farEnd, kBigReads, 0), kBig), true);
            close(fd);
        }
    }
} // namespace

int main()
{
    consoleDemoInit();
    if (isDSiMode())
        setCpuClock(true); // as the game runs
    cpuStartTiming(0);

    std::printf("HL card read probe\n\n");
    if (!fatInitDefault())
    {
        std::printf("No card found.\n");
        for (;;)
            swiWaitForVBlank();
    }
    const std::string root = std::string(fatGetDefaultDrive()) + "_nds/hl_nds/";
    out = std::fopen((root + "cardread.txt").c_str(), "a");
    const char *slot = isDSiMode() ? "DSi mode" : "DS mode";
    const char *dldi = io_dldi_data != nullptr ? io_dldi_data->friendlyName : "none";
    say(true, "== %s, %s, DLDI: %s", slot, fatGetDefaultDrive(), dldi);

    // The card's biggest map archives, and the named heavy ones.
    struct Archive
    {
        std::string path;
        std::uint32_t size;
    };
    std::vector<Archive> archives;
    const std::string maps = root + "data/maps/";
    if (DIR *dir = opendir(maps.c_str()))
    {
        while (const dirent *entry = readdir(dir))
        {
            const std::size_t length = std::strlen(entry->d_name);
            if (length < 6 || std::strcmp(entry->d_name + length - 5, ".npac") != 0)
                continue;
            struct stat st;
            const std::string path = maps + entry->d_name;
            if (stat(path.c_str(), &st) == 0)
                archives.push_back({path, static_cast<std::uint32_t>(st.st_size)});
        }
        closedir(dir);
    }
    if (archives.empty())
    {
        say(true, "No map archives in %s", maps.c_str());
        if (out != nullptr)
            std::fclose(out);
        for (;;)
            swiWaitForVBlank();
    }
    std::sort(archives.begin(), archives.end(), [](const Archive &a, const Archive &b) { return a.size > b.size; });
    std::vector<Archive> chosen;
    for (const char *name : {"c1a3.npac", "c2a5.npac", "c1a3a.npac", "c2a2b1.npac"})
    {
        for (const Archive &a : archives)
        {
            if (a.path.size() > std::strlen(name) && a.path.compare(a.path.size() - std::strlen(name), std::string::npos, name) == 0)
                chosen.push_back(a);
        }
    }
    for (const Archive &a : archives)
    {
        if (chosen.size() >= kMostArchives)
            break;
        bool there = false;
        for (const Archive &c : chosen)
            there = there || c.path == a.path;
        if (!there)
            chosen.push_back(a);
    }

    for (const Archive &a : chosen)
    {
        probeArchive(a.path, a.size);
        if (out != nullptr)
            std::fflush(out);
    }

    // The cluster size, last: it may scan the FAT for the free space.
    {
        struct statvfs vfs;
        if (statvfs(root.c_str(), &vfs) == 0)
            say(true, "cluster %lu bytes", static_cast<unsigned long>(vfs.f_bsize));
    }
    say(true, "== done");
    if (out != nullptr)
        std::fclose(out);
    std::printf("\nWritten to _nds/hl_nds/\ncardread.txt\n");
    for (;;)
        swiWaitForVBlank();
}
