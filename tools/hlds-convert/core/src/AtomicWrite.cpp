#include "hlds/AtomicWrite.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace hlds
{
    namespace
    {
        /// Files that have already been read by a previous map do not need to
        /// be reprocessed for the next map. Biggest offenders include
        /// halflife.wad and shared map sounds and models.
        constexpr std::uint64_t kCacheLimit = 384ull * 1024 * 1024;
        constexpr std::uint64_t kCacheFileLimit = 96ull * 1024 * 1024;

        struct CachedFile
        {
            std::shared_ptr<const std::vector<std::uint8_t>> bytes;
            std::uintmax_t size = 0;
            std::filesystem::file_time_type time;
            std::uint64_t used = 0;
        };

        std::mutex cacheMutex;
        std::map<std::string, CachedFile> cache;
        std::uint64_t cacheTotal = 0;
        std::uint64_t cacheClock = 0;
        std::atomic<std::uint64_t> tempCounter{0};

        bool worthKeeping(const std::filesystem::path &path)
        {
            return path.parent_path().filename() != "maps";
        }

#ifdef _WIN32
        /// Windows refuses to replace a file anything has open: another map's
        /// cook reading the same shared file, or the antivirus looking at a
        /// new one. Neither holds on to it for long. (The old file is not
        /// removed first: its name stays taken until the last reader has
        /// gone, and if this fails there would be no file at all.)
        void moveIntoPlace(const std::filesystem::path &from, const std::filesystem::path &to,
                           std::error_code &ec)
        {
            for (int attempt = 1;; ++attempt)
            {
                if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING))
                {
                    ec.clear();
                    return;
                }
                ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
                if (attempt == 20) // (some ten seconds in all)
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));
            }
        }
#else
        void moveIntoPlace(const std::filesystem::path &from, const std::filesystem::path &to,
                           std::error_code &ec)
        {
            std::filesystem::rename(from, to, ec);
        }
#endif
    } // namespace

    bool writeFileAtomic(const std::filesystem::path &path,
                         const std::vector<std::uint8_t> &bytes, std::string &error)
    {
        std::error_code ec;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                error = "cannot create " + path.parent_path().string() + ": " + ec.message();
                return false;
            }
        }
        // Unique name so multiple simultaneous cooks same shared file don't conflict.)
        std::filesystem::path temp = path;
        temp += ".tmp" + std::to_string(tempCounter.fetch_add(1));
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                error = "cannot write " + temp.string();
                return false;
            }
            out.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out)
            {
                error = "write failed for " + temp.string() + " (is the card full?)";
                out.close();
                std::filesystem::remove(temp, ec);
                return false;
            }
        }
        moveIntoPlace(temp, path, ec);
        if (ec)
        {
            error = "cannot move " + temp.string() + " into place: " + ec.message();
            std::filesystem::remove(temp, ec);
            return false;
        }
        // Attempt to r/w the file before silently failing.
        for (int attempt = 0; attempt < 40; ++attempt)
        {
            std::ifstream check(path, std::ios::binary);
            if (check)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return true;
    }

    bool readFile(const std::filesystem::path &path, std::vector<std::uint8_t> &bytes,
                  std::string &error)
    {
        std::error_code ec;
        const bool keep = worthKeeping(path);
        const std::string key = path.string();
        std::uintmax_t statSize = 0;
        std::filesystem::file_time_type time{};
        if (keep)
        {
            statSize = std::filesystem::file_size(path, ec);
            if (!ec)
                time = std::filesystem::last_write_time(path, ec);
            if (!ec)
            {
                std::shared_ptr<const std::vector<std::uint8_t>> found;
                {
                    const std::lock_guard<std::mutex> lock(cacheMutex);
                    const auto it = cache.find(key);
                    if (it != cache.end() && it->second.size == statSize && it->second.time == time)
                    {
                        it->second.used = ++cacheClock;
                        found = it->second.bytes;
                    }
                }
                if (found)
                {
                    bytes = *found;
                    return true;
                }
            }
        }
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        // Try again a handful of times when opening files as r/w access might have just been granted.
        for (int attempt = 1; !in && attempt <= 5 && std::filesystem::exists(path, ec); ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * attempt));
            in.clear();
            in.open(path, std::ios::binary | std::ios::ate);
        }
        if (!in)
        {
            error = "cannot open " + path.string();
            return false;
        }
        const std::streamoff size = in.tellg();
        in.seekg(0, std::ios::beg);
        bytes.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
        if (size > 0 && !in.read(reinterpret_cast<char *>(bytes.data()), size))
        {
            bytes.clear();
            error = "cannot read " + path.string();
            return false;
        }
        if (keep && !ec && bytes.size() <= kCacheFileLimit)
        {
            auto copy = std::make_shared<const std::vector<std::uint8_t>>(bytes);
            const std::lock_guard<std::mutex> lock(cacheMutex);
            const auto old = cache.find(key);
            if (old != cache.end())
            {
                cacheTotal -= old->second.bytes->size();
                cache.erase(old);
            }
            while (cacheTotal + copy->size() > kCacheLimit && !cache.empty())
            {
                auto oldest = cache.begin();
                for (auto it = cache.begin(); it != cache.end(); ++it)
                {
                    if (it->second.used < oldest->second.used)
                        oldest = it;
                }
                cacheTotal -= oldest->second.bytes->size();
                cache.erase(oldest);
            }
            cacheTotal += copy->size();
            cache[key] = CachedFile{copy, statSize, time, ++cacheClock};
        }
        return true;
    }
} // namespace hlds
