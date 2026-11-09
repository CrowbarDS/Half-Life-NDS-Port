#include "net/MapShare.hpp"

#ifndef HLDS_CHILD

#include <cstring>
#include <nds.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "Global.hpp"
#include "HlAirBank.hpp"
#include "HlLevel.hpp"
#ifndef HLDS_CHILD
#define HL_NPAC_CODE TWL_CODE __attribute__((noinline))
#endif
#include "HlNpacIndex.hpp"

namespace hlrt
{
    namespace
    {
        /// Read in pieces of this, as many a frame as kSlice: what a frame
        /// can spare of a lobby's (a 3 MB map in about three seconds).
        constexpr int kRead = 2048;
        constexpr int kSlice = 8;
        // (All of it in the DSi's memory: a DS has none of this. Nothing here
        // with a constructor -- they run on a DS too; TWL_BSS is cleared.)
        TWL_BSS std::uint8_t readRoom[kRead];

        /// What this card's own maps were found to come to.
        struct Known
        {
            std::uint32_t hash, bytes, crc;
        };
        TWL_BSS Known known[8];
        TWL_BSS int knownNext;

#define HL_TWL_TEXT(name, text) TWL_DATA const char name[] = text
        HL_TWL_TEXT(kFolder, "%smaps/net");
        HL_TWL_TEXT(kReceived, "%s/%s-%08lx.npac");
        HL_TWL_TEXT(kPart, "%s/coming.part");
        HL_TWL_TEXT(kInFolder, "%s/%s");
        HL_TWL_TEXT(kNpac, ".npac");
        HL_TWL_TEXT(kRead_, "rb");
        HL_TWL_TEXT(kWrite, "wb");
        HL_TWL_TEXT(kCommon, "%scommon.npac");
        HL_TWL_TEXT(kMaps, "%smaps");
        HL_TWL_TEXT(kInMaps, "%smaps/%s");

        // ------------------------------------------- MapBuild's, one at a time
        /// A member of the archive being put together, by its ID: its path,
        /// where it is stored in the file and how long, how it is packed, and
        /// where on this card the same is (`source` -1: nowhere -- the host's).
        struct Planned
        {
            std::uint32_t path, at, stored, packing, from, crc;
            std::int16_t source;
            std::uint16_t spare;
        };
        constexpr int kPlanMost = 2048;
        TWL_BSS Planned plan[kPlanMost];
        TWL_BSS std::uint16_t byPath[kPlanMost]; ///< plan's IDs, in the order of their paths
        TWL_BSS int planCount, sourced;
        /// The archives on this card something is copied from.
        constexpr int kSourcesMost = 256;
        TWL_BSS char sources[kSourcesMost][64];
        TWL_BSS int sourceCount;
        /// By piece of what MapCard sends: wanted of the host (not found on
        /// this card), and come.
        constexpr std::uint32_t kPiecesMost = 65535;
        TWL_BSS std::uint32_t needed[(kPiecesMost + 32) / 32], got[(kPiecesMost + 32) / 32];
        /// What MapIndex sent: the name, then the archive's start.
        TWL_BSS std::uint8_t indexRoom[56 * 1024];
        TWL_BSS std::uint32_t indexBytes, indexHead;
        /// (All of it sent: the name, from its first piece.)
        TWL_BSS std::uint8_t sentHead[hldata::kMapCardHeadBytes];
        /// An archive's start read to find what it has; then what is copied.
        TWL_BSS std::uint8_t scanRoom[64 * 1024];
        /// The file's own write buffer: the card takes large writes far
        /// faster than many small ones.
        TWL_BSS std::uint8_t writeRoom[16 * 1024];

        /// The host: its map's archive up to its members, and each member's
        /// extent and crc32 (MapSum's membersOf).
        TWL_BSS std::uint8_t hostHead[48 * 1024];
        struct HostMember
        {
            std::uint32_t at, stored, crc;
        };
        TWL_BSS HostMember hostMembers[kPlanMost];
        TWL_BSS std::uint32_t hostHave, hostHeadBytes, hostFiles, hostNext, hostFor;

        HL_TWL_CODE bool bit(const std::uint32_t *bits, std::uint32_t n) { return (bits[n >> 5] >> (n & 31)) & 1; }
        HL_TWL_CODE void setBit(std::uint32_t *bits, std::uint32_t n) { bits[n >> 5] |= 1u << (n & 31); }

        /// plan's ID of the member at `path`; -1: none.
        HL_TWL_CODE int planned(std::uint32_t path)
        {
            int low = 0, high = planCount - 1;
            while (low <= high)
            {
                const int middle = (low + high) >> 1;
                const std::uint32_t at = plan[byPath[middle]].path;
                if (at == path)
                    return byPath[middle];
                if (at < path)
                    low = middle + 1;
                else
                    high = middle - 1;
            }
            return -1;
        }

        HL_TWL_TEXT(kDataDir, "%s_nds/hl_nds/data/");
        /// The card's data folder (Global::dataDir, without building a string).
        TWL_BSS char dataDirRoom[64];
        HL_TWL_CODE const char *dataDir()
        {
            if (dataDirRoom[0] == 0)
                std::snprintf(dataDirRoom, sizeof(dataDirRoom), kDataDir, global.fatDeviceCPP.c_str());
            return dataDirRoom;
        }

        HL_TWL_CODE void folder(char *out, int room)
        {
            std::snprintf(out, static_cast<std::size_t>(room), kFolder, dataDir());
        }

        /// "<name>-<crc>.npac": the name and the crc, if it is one.
        HL_TWL_CODE bool parse(const char *file, char *name, std::uint32_t &crc)
        {
            const char *dash = std::strrchr(file, '-');
            if (dash == nullptr || dash == file || dash - file >= hldata::kMapCardHeadBytes ||
                std::strcmp(dash + 9, kNpac) != 0)
                return false;
            crc = 0;
            for (int i = 1; i <= 8; ++i)
            {
                const char c = dash[i];
                const int digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1);
                if (digit < 0)
                    return false;
                crc = crc << 4 | static_cast<std::uint32_t>(digit);
            }
            std::memcpy(name, file, static_cast<std::size_t>(dash - file));
            name[dash - file] = 0;
            return true;
        }
    } // namespace

    HL_TWL_CODE bool MapSum::begin(const char *path, const char *name, std::uint32_t membersOf)
    {
        stop();
        membersOf_ = membersOf;
        if (membersOf != 0)
        {
            hostFor = 0;
            hostHave = hostHeadBytes = hostFiles = hostNext = 0;
        }
        file_ = std::fopen(path, kRead_);
        if (file_ == nullptr)
            return false;
        // (Read straight into readRoom: no buffer of stdio's on the heap.)
        std::setvbuf(file_, nullptr, _IONBF, 0);
        std::uint8_t head[hldata::kMapCardHeadBytes];
        hldata::mapCardHead(name, head);
        running_ = hldata::crc32Update(hldata::kCrcStart, head, sizeof(head));
        bytes = sizeof(head);
        crc = 0;
        good = false;
        return true;
    }

    HL_TWL_CODE bool MapSum::step()
    {
        if (file_ == nullptr)
            return true;
        for (int n = 0; n < kSlice; ++n)
        {
            const std::size_t got = std::fread(readRoom, 1, kRead, file_);
            running_ = hldata::crc32Update(running_, readRoom, got);
            if (membersOf_ != 0)
                members(readRoom, bytes - hldata::kMapCardHeadBytes, static_cast<std::uint32_t>(got));
            bytes += static_cast<std::uint32_t>(got);
            if (got < static_cast<std::size_t>(kRead))
            {
                good = std::ferror(file_) == 0;
                crc = hldata::crc32Finish(running_);
                if (membersOf_ != 0 && good && hostHeadBytes != 0 && hostNext >= hostFiles)
                    hostFor = membersOf_;
                std::fclose(file_);
                file_ = nullptr;
                return true;
            }
        }
        return false;
    }

    /// `bytes` of the file from `at`: its head kept, until the members it
    /// says are known; then what each comes to.
    HL_TWL_CODE void MapSum::members(const std::uint8_t *data, std::uint32_t at, std::uint32_t bytes)
    {
        if (hostHeadBytes == 0)
        {
            if (at < sizeof(hostHead))
            {
                const std::uint32_t take = bytes < sizeof(hostHead) - at ? bytes : static_cast<std::uint32_t>(sizeof(hostHead)) - at;
                std::memcpy(hostHead + at, data, take);
                hostHave = at + take;
            }
            const std::uint32_t head = hldata::npacHeadBytes(hostHead, hostHave);
            if (head == 0)
            {
                if (hostHave >= sizeof(hostHead))
                    membersOf_ = 0; // (an index too big to send: none)
                return;
            }
            bool fits = true;
            const bool read = hldata::npacMembers(hostHead, head, hostFiles,
                                                  [&](std::uint32_t id, std::uint32_t, std::uint32_t from, std::uint32_t stored, std::uint32_t) {
                                                      if (id >= static_cast<std::uint32_t>(kPlanMost))
                                                      {
                                                          fits = false;
                                                          return;
                                                      }
                                                      hostMembers[id] = HostMember{from, stored, hldata::kCrcStart};
                                                  });
            for (std::uint32_t i = 1; fits && i < hostFiles; ++i)
                fits = hostMembers[i].at >= hostMembers[i - 1].at + hostMembers[i - 1].stored;
            if (!read || !fits || hostFiles == 0 || hostFiles > static_cast<std::uint32_t>(kPlanMost))
            {
                membersOf_ = 0;
                return;
            }
            hostHeadBytes = head;
            hostNext = 0;
        }
        // What each member comes to, as its bytes go by (in order: they are).
        std::uint32_t from = at;
        const std::uint32_t end = at + bytes;
        while (from < end && hostNext < hostFiles)
        {
            HostMember &m = hostMembers[hostNext];
            if (from < m.at)
            {
                from = m.at;
                continue;
            }
            const std::uint32_t stop = m.at + m.stored;
            const std::uint32_t take = (end < stop ? end : stop) - from;
            m.crc = hldata::crc32Update(m.crc, data + (from - at), take);
            from += take;
            if (from >= stop)
            {
                m.crc = hldata::crc32Finish(m.crc);
                ++hostNext;
            }
        }
        // (Members of nothing, at the end: done with as they are.)
        while (hostNext < hostFiles && hostMembers[hostNext].stored == 0 && hostMembers[hostNext].at <= end)
        {
            hostMembers[hostNext].crc = hldata::crc32Finish(hostMembers[hostNext].crc);
            ++hostNext;
        }
    }

    HL_TWL_CODE void MapSum::stop()
    {
        if (file_ != nullptr)
            std::fclose(file_);
        file_ = nullptr;
    }


    // ---------------------------------------------------------- MapBuild

    HL_TWL_CODE bool MapBuild::begin(const std::uint8_t *index, std::uint32_t bytes, std::uint32_t sent)
    {
        stop();
        if (bytes <= static_cast<std::uint32_t>(hldata::kMapCardHeadBytes) || bytes > sizeof(indexRoom) || sent <= bytes ||
            (sent + hldata::kFetchPiece - 1) / hldata::kFetchPiece > kPiecesMost)
            return false;
        std::memcpy(indexRoom, index, bytes);
        indexBytes = bytes;
        const std::uint8_t *archive = indexRoom + hldata::kMapCardHeadBytes;
        const std::uint32_t head = hldata::npacHeadBytes(archive, bytes - hldata::kMapCardHeadBytes);
        if (head == 0)
            return false;
        // Its members, from its own index.
        std::uint32_t files = 0;
        bool fits = true;
        planCount = 0;
        const std::uint32_t file = sent - hldata::kMapCardHeadBytes;
        const bool read = hldata::npacMembers(archive, head, files,
                                              [&](std::uint32_t id, std::uint32_t path, std::uint32_t at, std::uint32_t stored,
                                                  std::uint32_t packing) {
                                                  if (id >= static_cast<std::uint32_t>(kPlanMost) || at < head || at + stored > file)
                                                  {
                                                      fits = false;
                                                      return;
                                                  }
                                                  plan[id] = Planned{path, at, stored, packing, 0, 0, -1, 0};
                                              });
        // ... then what each comes to (a word each).
        if (!read || !fits || files == 0 || files > static_cast<std::uint32_t>(kPlanMost) ||
            bytes != hldata::kMapCardHeadBytes + head + files * 4)
            return false;
        planCount = static_cast<int>(files);
        const std::uint8_t *crcs = archive + head;
        for (int i = 0; i < planCount; ++i)
            plan[i].crc = static_cast<std::uint32_t>(crcs[i * 4] | crcs[i * 4 + 1] << 8 | crcs[i * 4 + 2] << 16) |
                          static_cast<std::uint32_t>(crcs[i * 4 + 3]) << 24;
        indexHead = head;
        for (int i = 1; i < planCount; ++i)
        {
            if (plan[i].at < plan[i - 1].at + plan[i - 1].stored)
                return false; // (in file-ID order, one after another: NpacWriter's)
        }
        // By path, to find them by (a shell sort: the DSi's own code, no library's).
        for (int i = 0; i < planCount; ++i)
            byPath[i] = static_cast<std::uint16_t>(i);
        for (int gap = planCount / 2; gap > 0; gap /= 2)
        {
            for (int i = gap; i < planCount; ++i)
            {
                const std::uint16_t id = byPath[i];
                int j = i;
                for (; j >= gap && plan[byPath[j - gap]].path > plan[id].path; j -= gap)
                    byPath[j] = byPath[j - gap];
                byPath[j] = id;
            }
        }
        sent_ = sent;
        pieces_ = (sent + hldata::kFetchPiece - 1) / hldata::kFetchPiece;
        std::memset(needed, 0, sizeof(needed));
        std::memset(got, 0, sizeof(got));
        sourced = 0;
        sourceCount = 0;
        lacking_ = first_ = toCome_ = come_ = copied_ = toCopy_ = 0;
        archives_ = 0;
        common_ = false;
        own_ = false;
        stage_ = Stage::Finding;
        return true;
    }

    HL_TWL_CODE bool MapBuild::beginWhole(std::uint32_t sent)
    {
        stop();
        if (sent <= static_cast<std::uint32_t>(hldata::kMapCardHeadBytes) ||
            (sent + hldata::kFetchPiece - 1) / hldata::kFetchPiece > kPiecesMost)
            return false;
        indexBytes = 0;
        planCount = 0;
        std::memset(sentHead, 0, sizeof(sentHead));
        sent_ = sent;
        pieces_ = (sent + hldata::kFetchPiece - 1) / hldata::kFetchPiece;
        std::memset(needed, 0, sizeof(needed));
        std::memset(got, 0, sizeof(got));
        lacking_ = first_ = toCome_ = come_ = copied_ = toCopy_ = 0;
        need(0, sent);
        char part[160];
        mapshare::partPath(part, sizeof(part));
        file_ = std::fopen(part, kWrite);
        if (file_ == nullptr)
        {
            stage_ = Stage::Failed;
            return true;
        }
        std::setvbuf(file_, reinterpret_cast<char *>(writeRoom), _IOFBF, sizeof(writeRoom));
        pos_ = 0;
        stage_ = Stage::Fetching;
        return true;
    }

    HL_TWL_CODE void MapBuild::need(std::uint32_t from, std::uint32_t bytes)
    {
        if (bytes == 0)
            return;
        const std::uint32_t last = (from + bytes - 1) / hldata::kFetchPiece;
        for (std::uint32_t p = from / hldata::kFetchPiece; p <= last && p < pieces_; ++p)
        {
            if (bit(needed, p))
                continue;
            setBit(needed, p);
            ++lacking_;
            const std::uint32_t at = p * hldata::kFetchPiece;
            toCome_ += sent_ - at < static_cast<std::uint32_t>(hldata::kFetchPiece) ? sent_ - at : hldata::kFetchPiece;
        }
    }

    HL_TWL_CODE bool MapBuild::want(std::uint32_t p) const { return bit(needed, p) && !bit(got, p); }

    HL_TWL_CODE void MapBuild::readArchive(const char *path)
    {
        ++archives_;
        std::FILE *file = std::fopen(path, kRead_);
        if (file == nullptr)
            return;
        std::setvbuf(file, nullptr, _IONBF, 0);
        const std::size_t read = std::fread(scanRoom, 1, sizeof(scanRoom), file);
        std::fclose(file);
        const std::uint32_t head = hldata::npacHeadBytes(scanRoom, read);
        if (head == 0)
            return;
        int slot = -1;
        std::uint32_t files = 0;
        hldata::npacMembers(scanRoom, head, files,
                            [&](std::uint32_t, std::uint32_t named, std::uint32_t at, std::uint32_t stored, std::uint32_t packing) {
                                const int id = planned(named);
                                if (id < 0 || plan[id].source >= 0 || plan[id].stored != stored || plan[id].packing != packing ||
                                    stored == 0)
                                    return;
                                if (slot < 0)
                                {
                                    if (sourceCount >= kSourcesMost || std::strlen(path) >= sizeof(sources[0]))
                                        return;
                                    slot = sourceCount++;
                                    std::strcpy(sources[slot], path);
                                }
                                plan[id].source = static_cast<std::int16_t>(slot);
                                plan[id].from = at;
                                ++sourced;
                            });
    }

    HL_TWL_CODE MapBuild::Stage MapBuild::step()
    {
        if (stage_ == Stage::Finding)
        {
            // The card's archives, two a frame: common.npac first, then the
            // maps' -- until each member has been found, or all are read.
            bool done = sourced >= planCount;
            char path[160];
            char own[hldata::kMapCardHeadBytes + 8];
            const bool named = hldata::mapCardName(indexRoom, own);
            if (named)
                std::strcat(own, kNpac);
            for (int n = 0; n < 2 && !done; ++n)
            {
                // This card's own of its name first (another version: most
                // of it the same), then what every map shares, then the rest.
                if (!own_)
                {
                    own_ = true;
                    if (named)
                    {
                        std::snprintf(path, sizeof(path), kInMaps, dataDir(), own);
                        readArchive(path);
                    }
                    continue;
                }
                if (!common_)
                {
                    common_ = true;
                    std::snprintf(path, sizeof(path), kCommon, dataDir());
                    readArchive(path);
                    continue;
                }
                if (dir_ == nullptr)
                {
                    std::snprintf(path, sizeof(path), kMaps, dataDir());
                    dir_ = opendir(path);
                    if (dir_ == nullptr)
                    {
                        done = true;
                        break;
                    }
                }
                const dirent *entry = readdir(static_cast<DIR *>(dir_));
                if (entry == nullptr)
                {
                    done = true;
                    break;
                }
                const std::size_t length = std::strlen(entry->d_name);
                if (length > 5 && std::strcmp(entry->d_name + length - 5, kNpac) == 0 && !(named && std::strcmp(entry->d_name, own) == 0))
                {
                    std::snprintf(path, sizeof(path), kInMaps, dataDir(), entry->d_name);
                    readArchive(path);
                }
                done = sourced >= planCount;
            }
            if (!done)
                return stage_;
            if (dir_ != nullptr)
                closedir(static_cast<DIR *>(dir_));
            dir_ = nullptr;
            // Found: the file begun -- the archive's own head, as the host's
            // index has it -- and what was not found, asked of the host.
            char part[160];
            mapshare::partPath(part, sizeof(part));
            file_ = std::fopen(part, kWrite);
            if (file_ == nullptr)
            {
                stage_ = Stage::Failed;
                return stage_;
            }
            std::setvbuf(file_, reinterpret_cast<char *>(writeRoom), _IOFBF, sizeof(writeRoom));
            pos_ = 0;
            if (!writeAt(0, indexRoom + hldata::kMapCardHeadBytes, indexHead))
            {
                stage_ = Stage::Failed;
                return stage_;
            }
            for (int i = 0; i < planCount; ++i)
            {
                if (plan[i].source >= 0)
                    toCopy_ += plan[i].stored;
                else
                    need(hldata::kMapCardHeadBytes + plan[i].at, plan[i].stored);
            }
            member_ = 0;
            memberDone_ = 0;
            sourceAt_ = -1;
            stage_ = Stage::Copying;
            return stage_;
        }
        if (stage_ == Stage::Copying && !copySlice())
        {
            stage_ = Stage::Failed;
            return stage_;
        }
        return stage_;
    }

    HL_TWL_CODE bool MapBuild::copySlice()
    {
        static TWL_DATA const std::uint8_t kPad[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        const std::uint32_t head = indexHead;
        const std::uint32_t file = sent_ - hldata::kMapCardHeadBytes;
        std::uint32_t budget = 48 * 1024;
        while (member_ < planCount && budget > 0)
        {
            Planned &m = plan[member_];
            if (memberDone_ == 0)
            {
                memberCrc_ = hldata::kCrcStart;
                // (Between it and the one before: padding, FFh -- as the
                // archive was written.)
                const std::uint32_t before = member_ == 0 ? head : plan[member_ - 1].at + plan[member_ - 1].stored;
                for (std::uint32_t at = before; at < m.at; at += 4)
                {
                    if (!writeAt(at, kPad, m.at - at < 4 ? m.at - at : 4))
                        return false;
                }
            }
            if (m.source >= 0 && m.source != sourceAt_)
            {
                if (source_ != nullptr)
                    std::fclose(source_);
                source_ = std::fopen(sources[m.source], kRead_);
                sourceAt_ = m.source;
                if (source_ != nullptr)
                    std::setvbuf(source_, nullptr, _IONBF, 0);
            }
            std::uint32_t chunk = m.stored - memberDone_;
            chunk = chunk < sizeof(scanRoom) ? chunk : static_cast<std::uint32_t>(sizeof(scanRoom));
            chunk = chunk < budget ? chunk : budget;
            const bool copied = m.source >= 0 && source_ != nullptr &&
                                std::fseek(source_, static_cast<long>(m.from + memberDone_), SEEK_SET) == 0 &&
                                std::fread(scanRoom, 1, chunk, source_) == chunk;
            if (m.source >= 0 && !copied)
            {
                // (Not to be read after all: the host's.)
                toCopy_ -= m.stored - memberDone_;
                need(hldata::kMapCardHeadBytes + m.at + memberDone_, m.stored - memberDone_);
                m.source = -1;
            }
            if (m.source < 0)
            {
                ++member_;
                memberDone_ = 0;
                continue;
            }
            if (!writeAt(m.at + memberDone_, scanRoom, chunk))
                return false;
            memberCrc_ = hldata::crc32Update(memberCrc_, scanRoom, chunk);
            memberDone_ += chunk;
            copied_ += chunk;
            budget -= chunk;
            if (memberDone_ >= m.stored)
            {
                // Not what the host's is (one of the same name and length --
                // another map's level, say): the host's after all.
                if (hldata::crc32Finish(memberCrc_) != m.crc)
                    need(hldata::kMapCardHeadBytes + m.at, m.stored);
                ++member_;
                memberDone_ = 0;
            }
        }
        if (member_ < planCount)
            return true;
        // All of it: the padding to the end, and on to what the host sends.
        const std::uint32_t last = planCount > 0 ? plan[planCount - 1].at + plan[planCount - 1].stored : head;
        for (std::uint32_t at = last; at < file; at += 4)
        {
            if (!writeAt(at, kPad, file - at < 4 ? file - at : 4))
                return false;
        }
        if (source_ != nullptr)
            std::fclose(source_);
        source_ = nullptr;
        stage_ = Stage::Fetching;
        return true;
    }

    HL_TWL_CODE bool MapBuild::writeAt(std::uint32_t at, const std::uint8_t *data, std::uint32_t bytes)
    {
        if (file_ == nullptr)
            return false;
        if (at != pos_ && std::fseek(file_, static_cast<long>(at), SEEK_SET) != 0)
            return false;
        if (std::fwrite(data, 1, bytes, file_) != bytes)
            return false;
        pos_ = at + bytes;
        return true;
    }

    HL_TWL_CODE bool MapBuild::piece(std::uint32_t index, const std::uint8_t *data, int count)
    {
        if (stage_ != Stage::Fetching || index >= pieces_ || !want(index))
            return true;
        const std::uint32_t at = index * hldata::kFetchPiece;
        const std::uint32_t expect = sent_ - at < static_cast<std::uint32_t>(hldata::kFetchPiece) ? sent_ - at : hldata::kFetchPiece;
        if (count != static_cast<int>(expect))
            return true;
        // (Its first bytes, the name, are not of the file.)
        std::uint32_t skip = 0;
        if (at < static_cast<std::uint32_t>(hldata::kMapCardHeadBytes))
        {
            skip = hldata::kMapCardHeadBytes - at;
            std::memcpy(sentHead + at, data, skip);
        }
        if (!writeAt(at + skip - hldata::kMapCardHeadBytes, data + skip, expect - skip))
        {
            stage_ = Stage::Failed;
            return false;
        }
        setBit(got, index);
        --lacking_;
        come_ += expect;
        return true;
    }

    HL_TWL_CODE void MapBuild::have(std::uint32_t &base, std::uint64_t &bits)
    {
        while (first_ < pieces_ && !want(first_))
            ++first_;
        base = first_;
        bits = 0;
        for (std::uint32_t k = 0; k < static_cast<std::uint32_t>(hldata::kFetchWindow); ++k)
        {
            const std::uint32_t p = first_ + 1 + k;
            if (p >= pieces_ || !want(p))
                bits |= std::uint64_t(1) << k;
        }
    }

    HL_TWL_CODE bool MapBuild::close()
    {
        bool good = true;
        if (file_ != nullptr)
            good = std::fclose(file_) == 0;
        file_ = nullptr;
        if (source_ != nullptr)
            std::fclose(source_);
        source_ = nullptr;
        return good;
    }

    HL_TWL_CODE void MapBuild::stop()
    {
        const bool had = file_ != nullptr || stage_ != Stage::Off;
        close();
        if (dir_ != nullptr)
            closedir(static_cast<DIR *>(dir_));
        dir_ = nullptr;
        stage_ = Stage::Off;
        if (had)
        {
            char part[160];
            mapshare::partPath(part, sizeof(part));
            std::remove(part);
        }
    }

    HL_TWL_CODE bool MapBuild::name(char *out) const
    {
        return indexBytes > static_cast<std::uint32_t>(hldata::kMapCardHeadBytes) ? hldata::mapCardName(indexRoom, out)
                                                                                    : hldata::mapCardName(sentHead, out);
    }

    HL_TWL_CODE int MapBuild::percent() const
    {
        if (stage_ == Stage::Finding)
            return planCount == 0 ? 0 : static_cast<int>(static_cast<std::uint32_t>(sourced) * 100u / static_cast<std::uint32_t>(planCount));
        if (stage_ == Stage::Copying)
            return toCopy_ == 0 ? 100 : static_cast<int>(static_cast<std::uint64_t>(copied_) * 100u / toCopy_);
        return toCome_ == 0 ? 100 : static_cast<int>(static_cast<std::uint64_t>(come_) * 100u / toCome_);
    }

    namespace mapshare
    {
        HL_TWL_CODE void receivedPath(char *out, int room, const char *name, std::uint32_t crc)
        {
            char where[96];
            folder(where, sizeof(where));
            std::snprintf(out, static_cast<std::size_t>(room), kReceived, where, name,
                          static_cast<unsigned long>(crc));
        }

        HL_TWL_CODE void partPath(char *out, int room)
        {
            char where[96];
            folder(where, sizeof(where));
            mkdir(where, 0777);
            std::snprintf(out, static_cast<std::size_t>(room), kPart, where);
        }

        HL_TWL_CODE bool received(std::uint32_t hash, std::uint32_t crc, char *name)
        {
            char where[96];
            folder(where, sizeof(where));
            DIR *dir = opendir(where);
            if (dir == nullptr)
                return false;
            bool found = false;
            while (const dirent *entry = readdir(dir))
            {
                std::uint32_t said = 0;
                if (parse(entry->d_name, name, said) && said == crc && hldata::nameHash(name) == hash)
                {
                    found = true;
                    break;
                }
            }
            closedir(dir);
            return found;
        }

        HL_TWL_CODE bool keep(const char *name, std::uint32_t crc)
        {
            char where[96], path[384], part[160];
            folder(where, sizeof(where));
            // (Others of its name first: one at a time, as the host has it.)
            if (DIR *dir = opendir(where))
            {
                char other[hldata::kMapCardHeadBytes];
                std::uint32_t said = 0;
                while (const dirent *entry = readdir(dir))
                {
                    if (parse(entry->d_name, other, said) && std::strcmp(other, name) == 0)
                    {
                        std::snprintf(path, sizeof(path), kInFolder, where, entry->d_name);
                        std::remove(path);
                    }
                }
                closedir(dir);
            }
            receivedPath(path, sizeof(path), name, crc);
            partPath(part, sizeof(part));
            return std::rename(part, path) == 0;
        }

        HL_TWL_CODE bool indexFor(std::uint32_t hash) { return hash != 0 && hostFor == hash; }

        HL_TWL_CODE std::uint32_t indexBlob(std::uint8_t *out, std::uint32_t room, const char *name)
        {
            const std::uint32_t bytes = hldata::kMapCardHeadBytes + hostHeadBytes + hostFiles * 4;
            if (hostFor == 0 || bytes > room)
                return 0;
            hldata::mapCardHead(name, out);
            std::memcpy(out + hldata::kMapCardHeadBytes, hostHead, hostHeadBytes);
            std::uint8_t *crcs = out + hldata::kMapCardHeadBytes + hostHeadBytes;
            for (std::uint32_t i = 0; i < hostFiles; ++i)
            {
                const std::uint32_t c = hostMembers[i].crc;
                crcs[i * 4] = static_cast<std::uint8_t>(c);
                crcs[i * 4 + 1] = static_cast<std::uint8_t>(c >> 8);
                crcs[i * 4 + 2] = static_cast<std::uint8_t>(c >> 16);
                crcs[i * 4 + 3] = static_cast<std::uint8_t>(c >> 24);
            }
            return bytes;
        }

        HL_TWL_CODE bool knownSum(std::uint32_t hash, std::uint32_t bytes, std::uint32_t &crc)
        {
            for (const Known &k : known)
            {
                if (k.hash == hash && k.bytes == bytes && hash != 0)
                {
                    crc = k.crc;
                    return true;
                }
            }
            return false;
        }

        HL_TWL_CODE void rememberSum(std::uint32_t hash, std::uint32_t bytes, std::uint32_t crc)
        {
            for (Known &k : known)
            {
                if (k.hash == hash)
                {
                    k = Known{hash, bytes, crc};
                    return;
                }
            }
            known[knownNext] = Known{hash, bytes, crc};
            knownNext = (knownNext + 1) % static_cast<int>(sizeof(known) / sizeof(known[0]));
        }

        HL_TWL_CODE bool room(std::uint32_t bytes)
        {
            struct statvfs card;
            if (statvfs(dataDir(), &card) != 0 || card.f_frsize == 0)
                return true;
            // (And a little to spare: the card's own files grow too.)
            const std::uint64_t free = static_cast<std::uint64_t>(card.f_bavail) * card.f_frsize;
            return free >= static_cast<std::uint64_t>(bytes) + 1024 * 1024;
        }

        HL_TWL_CODE std::uint32_t fileBytes(const char *path)
        {
            struct stat about;
            return stat(path, &about) == 0 && about.st_size > 0 ? static_cast<std::uint32_t>(about.st_size) : 0;
        }
    } // namespace mapshare
} // namespace hlrt

#endif
