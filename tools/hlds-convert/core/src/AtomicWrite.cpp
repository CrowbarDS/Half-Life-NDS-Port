#include "hlds/AtomicWrite.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
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
#else
#include <fcntl.h>
#include <unistd.h>
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

        /// What was read of a file before is not its contents once it has
        /// been written again: a FAT card keeps times to two seconds, and a
        /// file rewritten with the same size within them looked the same.
        void forget(const std::filesystem::path &path)
        {
            const std::lock_guard<std::mutex> lock(cacheMutex);
            const auto it = cache.find(path.string());
            if (it != cache.end())
            {
                cacheTotal -= it->second.bytes->size();
                cache.erase(it);
            }
        }

        /// This process's number: a temporary name of its own, so that two
        /// converters at once (the window and the command line, on one
        /// stage) never write into each other's.
        unsigned long processId()
        {
#ifdef _WIN32
            return static_cast<unsigned long>(GetCurrentProcessId());
#else
            return static_cast<unsigned long>(::getpid());
#endif
        }

        /// The bytes into a new file, and onto the disk itself before this
        /// returns -- not only into the system's cache, which a card pulled
        /// out, or the power going, loses: the renamed file would then be
        /// there at its full size, and hold nothing.
        bool writeDurably(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes,
                          std::string &error)
        {
#ifdef _WIN32
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                error = "cannot write " + path.string();
                return false;
            }
            std::size_t done = 0;
            bool ok = true;
            while (ok && done < bytes.size())
            {
                const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 24));
                DWORD wrote = 0;
                ok = WriteFile(file, bytes.data() + done, chunk, &wrote, nullptr) && wrote == chunk;
                done += wrote;
            }
            ok = ok && FlushFileBuffers(file);
            ok = CloseHandle(file) && ok;
#else
            const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (file < 0)
            {
                error = "cannot write " + path.string();
                return false;
            }
            std::size_t done = 0;
            bool ok = true;
            while (ok && done < bytes.size())
            {
                const ssize_t wrote = ::write(file, bytes.data() + done, bytes.size() - done);
                ok = wrote > 0;
                if (ok)
                    done += static_cast<std::size_t>(wrote);
            }
            ok = ok && ::fsync(file) == 0;
            ok = ::close(file) == 0 && ok;
#endif
            if (!ok)
                error = "write failed for " + path.string() + " (is the card full?)";
            return ok;
        }

        /// The folder's own entry for the new name, onto the disk as well
        /// (where the system has such a thing to ask for).
        void syncFolder(const std::filesystem::path &folder)
        {
#ifndef _WIN32
            const int dir = ::open(folder.empty() ? "." : folder.c_str(), O_RDONLY);
            if (dir >= 0)
            {
                ::fsync(dir);
                ::close(dir);
            }
#else
            (void)folder;
#endif
        }

        /// Whether the file under `path` is `bytes`, read from the disk.
        bool holds(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::vector<char> block(1u << 16);
            std::size_t at = 0;
            while (in)
            {
                in.read(block.data(), static_cast<std::streamsize>(block.size()));
                const std::size_t got = static_cast<std::size_t>(in.gcount());
                if (got == 0)
                    break;
                if (at + got > bytes.size() || std::memcmp(block.data(), bytes.data() + at, got) != 0)
                    return false;
                at += got;
            }
            return at == bytes.size();
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
        // (A name of its own: several maps cooked at once write the same
        // shared file, and so may another converter running on this stage.)
        std::filesystem::path temp = path;
        temp += ".tmp" + std::to_string(processId()) + "-" + std::to_string(tempCounter.fetch_add(1));
        if (!writeDurably(temp, bytes, error))
        {
            std::filesystem::remove(temp, ec);
            return false;
        }
        moveIntoPlace(temp, path, ec);
        if (ec)
        {
            error = "cannot move " + temp.string() + " into place: " + ec.message();
            std::filesystem::remove(temp, ec);
            return false;
        }
        syncFolder(path.parent_path());
        forget(path);
        // What is under the name now is what was meant, whole. (Windows may
        // keep it from being opened for a moment: the antivirus looking at
        // a new file.)
        for (int attempt = 0;; ++attempt)
        {
            if (holds(path, bytes))
                return true;
            if (attempt == 40)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        error = "what was written is not what was read back from " + path.string();
        return false;
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

    bool isTempName(const std::filesystem::path &path)
    {
        return path.extension().string().rfind(".tmp", 0) == 0;
    }

    std::string processTag()
    {
        return std::to_string(processId());
    }
} // namespace hlds
