#include "global_header.hpp"

#include "game/SaveGame.hpp"

#ifdef HLDS_CHILD
// A console that was sent the game has no card to save on, and keeps no map
// it left: every match begins its map afresh. In a demo its progress is the
// host's to keep (HlDemoSave.hpp).
#include <cstdlib>
#include <cstring>

#include "game/Game.hpp"
#include "net/GuestLoad.hpp"
#include "HlDemoSave.hpp"
#include "HlMapStore.hpp"
#include "HlSave.hpp"

namespace hlrt::SaveGame
{
    namespace
    {
        // Who went on with the player from each map left.
        struct Landed
        {
            std::uint32_t map;
            std::uint8_t mask;
        };
        Landed landedMaps[8];
        std::uint32_t slotFile(int slot)
        {
            return slot == hldata::kSlotAuto ? hldata::kDemoFileAuto : slot == hldata::kSlotArrival ? hldata::kDemoFileArrival : 0;
        }
        bool sinkTo(void *context, const std::uint8_t *bytes, std::size_t size)
        {
            auto *&at = *static_cast<std::uint8_t **>(context);
            std::memcpy(at, bytes, size);
            at += size;
            return true;
        }
        struct From
        {
            const std::uint8_t *at;
            std::size_t left;
        };
        std::size_t sourceFrom(void *context, std::uint8_t *bytes, std::size_t most)
        {
            auto &from = *static_cast<From *>(context);
            const std::size_t take = most < from.left ? most : from.left;
            std::memcpy(bytes, from.at, take);
            from.at += take;
            from.left -= take;
            return take;
        }
        alignas(4) std::uint8_t piece[1024];
    } // namespace

    bool read(hldata::SaveData &out, int slot)
    {
        std::uint8_t *data = nullptr;
        std::uint32_t size = 0;
        if (!guest::demoPlaying() || slotFile(slot) == 0 || !guest::demoFetch(slotFile(slot), data, size))
            return false;
        const bool good = hldata::readSave(data, size, out, nullptr);
        std::free(data);
        return good;
    }
    bool write(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading)
    {
        if (!guest::demoPlaying() || slotFile(slot) == 0)
            return false;
        const std::vector<std::uint8_t> bytes = hldata::writeSave(save, heading);
        std::uint8_t *room = guest::demoSendRoom(static_cast<std::uint32_t>(bytes.size()));
        if (room == nullptr)
            return false;
        std::memcpy(room, bytes.data(), bytes.size());
        guest::demoSendReady(slotFile(slot), room, static_cast<std::uint32_t>(bytes.size()));
        return true;
    }
    bool writeGame(const std::string &map, int slot, const hldata::SaveHeading &heading)
    {
        return write(game.snapshot(map), slot, heading);
    }
    bool remove(int) { return false; }
    bool begin(const hldata::SaveData &, int, const hldata::SaveHeading &) { return false; }
    hldata::SaveWriter::Result step() { return hldata::SaveWriter::Result::Done; }
    bool busy() { return false; }
    bool finish() { return true; }
    const char *whyFailed() { return "the host"; }
    bool leaveMap(const std::string &name, std::uint32_t level)
    {
        if (!guest::demoPlaying())
            return false;
        const std::uint32_t body = game.storedBytes();
        const std::uint32_t whole = sizeof(hldata::MapStoreHead) + body;
        std::uint8_t *room = guest::demoSendRoom(whole);
        if (room == nullptr)
            return false;
        hldata::MapStoreHead head{};
        std::memcpy(head.magic, hldata::kMapStoreMagic, 4);
        head.version = hldata::kMapStoreVersion;
        head.map = hldata::nameHash(name.c_str());
        head.level = level;
        head.bytes = body;
        for (int v = 0; v < hldata::kMaxVisitors; ++v)
            head.sentAway[v] = game.leftWith()[v];
        std::memcpy(room, &head, sizeof(head));
        std::uint8_t *at = room + sizeof(head);
        hldata::detail::StreamOut out;
        out.sink = sinkTo;
        out.context = &at;
        out.buffer = piece;
        out.capacity = sizeof(piece);
        if (!game.storeMap(out) || !out.flush() || out.written != body)
            return false;
        guest::demoSendReady(head.map, room, whole);
        return true;
    }
    int arriveMap(const std::string &name, std::uint32_t level)
    {
        if (!guest::demoPlaying())
            return 0;
        const std::uint32_t map = hldata::nameHash(name.c_str());
        std::uint8_t *data = nullptr;
        std::uint32_t size = 0;
        if (!guest::demoFetch(map, data, size))
            return 0;
        int laid = 0;
        hldata::MapStoreHead head;
        if (size >= sizeof(head))
        {
            std::memcpy(&head, data, sizeof(head));
            if (std::memcmp(head.magic, hldata::kMapStoreMagic, 4) == 0 && head.map == map && head.level == level &&
                head.bytes == size - sizeof(head))
            {
                std::uint8_t mask = 0;
                for (const Landed &entry : landedMaps)
                    mask = entry.map == map ? entry.mask : mask;
                From from{data + sizeof(head), head.bytes};
                hldata::detail::StreamIn in;
                in.source = sourceFrom;
                in.context = &from;
                in.buffer = piece;
                in.capacity = sizeof(piece);
                laid = game.layStored(in, head.sentAway, mask) ? 1 : -1;
            }
        }
        std::free(data);
        return laid;
    }
    void landed(const std::string &left, std::uint8_t mask)
    {
        const std::uint32_t map = hldata::nameHash(left.c_str());
        static int next = 0;
        for (Landed &entry : landedMaps)
        {
            if (entry.map == map)
            {
                entry.mask = mask;
                return;
            }
        }
        landedMaps[next] = Landed{map, mask};
        next = (next + 1) % 8;
    }
    void newRun(std::uint32_t) {}
    bool fromSave(int, const hldata::SaveData &, std::uint32_t) { return false; }
    void dropNamedAuto() {}
    const hldata::RunIndex &runIndex()
    {
        static const hldata::RunIndex none;
        return none;
    }
} // namespace hlrt::SaveGame
#else

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "Global.hpp"
#include "Overlay.hpp"
#include "game/SaveGameCard.hpp"

// The saves' work is in the save overlay (SaveGameBody.cpp): each call here
// reads it in, a halt naming it if a match's screens hold its memory -- a
// match keeps no saves. What is asked every frame, or at every map's load,
// is answered here when there is nothing to do.
#if HL_OVL_COUNT > 0
#define HL_SAVE_WORK()                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        ::hlrt::ovl::load(::hlrt::ovl::Id::Save);                                                                      \
        HL_OVL_GATE(Save);                                                                                             \
    } while (0)
#else
#define HL_SAVE_WORK() ((void)0)
#endif

namespace hlrt::SaveGame
{
    namespace detail
    {
        std::string FatCard::path(const char *name) const { return global.gameSavesDir() + name; }

        bool FatCard::no(const char *call)
        {
            refused = call;
            refusedErrno = errno;
            return false;
        }

        bool FatCard::shut(Open &open)
        {
            if (open.file == nullptr)
                return true;
            const bool ok = std::fclose(open.file) == 0 && !open.failed;
            open = Open();
            return ok;
        }

        bool FatCard::flushWriting()
        {
            if (buffered_ == 0 || writing_.file == nullptr)
            {
                buffered_ = 0;
                return true;
            }
            const bool ok = std::fwrite(buffer_, 1, buffered_, writing_.file) == buffered_;
            buffered_ = 0;
            if (ok)
                return true;
            writing_.failed = true;
            return no("write");
        }

        bool FatCard::shutWriting()
        {
            const bool flushed = flushWriting();
            std::free(buffer_);
            buffer_ = nullptr;
            bufferBytes_ = 0;
            return shut(writing_) && flushed;
        }

        bool FatCard::close()
        {
            const bool wrote = shutWriting();
            shut(reading_);
            return wrote;
        }

        void FatCard::makeFolder(const char *name) const
        {
            std::string dir = global.gameSavesDir();
            if (!dir.empty() && dir.back() == '/')
                dir.pop_back();
            mkdir(dir.c_str(), 0777);
            const char *slash = std::strrchr(name, '/');
            if (slash != nullptr)
                mkdir((global.gameSavesDir() + std::string(name, slash)).c_str(), 0777);
        }

        bool FatCard::append(const char *name, const std::uint8_t *bytes, std::size_t size, bool first)
        {
            if (first)
            {
                shutWriting();
                if (reading_.name == name)
                    shut(reading_);
                makeFolder(name);
                writing_.file = std::fopen(path(name).c_str(), "wb");
                if (writing_.file == nullptr)
                    return no("open");
                writing_.name = name;
                bufferBytes_ = isDSiMode() ? kWriteBufferDsi : kWriteBufferDs;
                buffer_ = static_cast<std::uint8_t *>(std::malloc(bufferBytes_));
                if (buffer_ == nullptr)
                    bufferBytes_ = 0;
            }
            else if (writing_.file == nullptr || writing_.name != name)
            {
                return false; // (it was let go of in between: begun again by the writer)
            }
            if (buffer_ == nullptr)
            {
                if (std::fwrite(bytes, 1, size, writing_.file) != size)
                {
                    writing_.failed = true;
                    return no("write");
                }
                return true;
            }
            while (size > 0)
            {
                const std::size_t room = bufferBytes_ - buffered_;
                const std::size_t take = size < room ? size : room;
                std::memcpy(buffer_ + buffered_, bytes, take);
                buffered_ += take;
                bytes += take;
                size -= take;
                if (buffered_ == bufferBytes_ && !flushWriting())
                    return false;
            }
            return true;
        }

        bool FatCard::readAt(const char *name, std::size_t offset, std::size_t size, std::vector<std::uint8_t> &out)
        {
            out.clear();
            if (reading_.file == nullptr || reading_.name != name)
            {
                shut(reading_);
                // (A file being written is read back from what is on the
                // card: let go of first.)
                if (writing_.name == name && !shutWriting())
                    return no("close");
                reading_.file = std::fopen(path(name).c_str(), "rb");
                if (reading_.file == nullptr)
                    return no("open to read");
                reading_.name = name;
            }
            if (std::fseek(reading_.file, static_cast<long>(offset), SEEK_SET) != 0)
                return no("seek");
            out.resize(size);
            out.resize(std::fread(out.data(), 1, size, reading_.file));
            return true;
        }

        bool FatCard::finish(const char *name)
        {
            bool ok = true;
            if (writing_.name == name)
                ok = shutWriting();
            if (reading_.name == name)
                shut(reading_);
            return ok || no("close");
        }

        bool FatCard::read(const char *name, std::vector<std::uint8_t> &out, std::size_t most)
        {
            close();
            out.clear();
            std::FILE *file = std::fopen(path(name).c_str(), "rb");
            if (file == nullptr)
                return false;
            // (As long as it is, and no longer: a save is tens of kilobytes,
            // the most one may be far more.)
            std::fseek(file, 0, SEEK_END);
            const long length = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            const std::size_t want = length < 0 ? 0 : (static_cast<std::size_t>(length) < most ? length : most);
            out.resize(want);
            const std::size_t got = want != 0 ? std::fread(out.data(), 1, want, file) : 0;
            std::fclose(file);
            out.resize(got);
            return got == want;
        }

        bool FatCard::write(const char *name, const std::uint8_t *bytes, std::size_t size)
        {
            close();
            makeFolder(name);
            std::FILE *file = std::fopen(path(name).c_str(), "wb");
            if (file == nullptr)
                return no("open");
            const bool wrote = std::fwrite(bytes, 1, size, file) == size || no("write");
            return (std::fclose(file) == 0 || no("close")) && wrote;
        }

        bool FatCard::remove(const char *name)
        {
            close();
            return unlink(path(name).c_str()) == 0 || no("remove");
        }

        bool FatCard::rename(const char *from, const char *to)
        {
            close();
            return ::rename(path(from).c_str(), path(to).c_str()) == 0 || no("rename");
        }

        bool FatCard::list(std::vector<std::string> &names)
        {
            close();
            names.clear();
            std::string dir = global.gameSavesDir();
            if (!dir.empty() && dir.back() == '/')
                dir.pop_back();
            DIR *folder = opendir(dir.c_str());
            if (folder == nullptr)
            {
                // (No folder: no saves. Any other refusal is not "none": the
                // files are asked for by name instead.)
                if (errno == ENOENT)
                    return true;
                return no("list");
            }
            while (const dirent *entry = readdir(folder))
            {
                if (entry->d_type != DT_DIR)
                    names.emplace_back(entry->d_name);
            }
            closedir(folder);
            return true;
        }

        FatCard card;
        hldata::SaveIndex list;
        hldata::SaveWriter writer;
        bool listed = false;
        hldata::RunIndex run;
        bool runKnown = false;
        std::uint8_t piece[hldata::kMapStorePiece];
        std::vector<std::uint8_t> scratch;
        hldata::SaveData held;
    } // namespace detail

    using namespace detail;

    HL_OVL_GATED bool leaveMap(const std::string &name, std::uint32_t level)
    {
        HL_SAVE_WORK();
        return work::leaveMap(name, level);
    }

    HL_OVL_GATED int arriveMap(const std::string &name, std::uint32_t level)
    {
        HL_SAVE_WORK();
        return work::arriveMap(name, level);
    }

    HL_OVL_GATED void landed(const std::string &left, std::uint8_t mask)
    {
        HL_SAVE_WORK();
        work::landed(left, mask);
    }

    HL_OVL_GATED void newRun(std::uint32_t runId)
    {
        HL_SAVE_WORK();
        work::newRun(runId);
    }

    HL_OVL_GATED bool fromSave(int slot, const hldata::SaveData &save, std::uint32_t runId)
    {
        HL_SAVE_WORK();
        return work::fromSave(slot, save, runId);
    }

    HL_OVL_GATED void dropNamedAuto()
    {
        HL_SAVE_WORK();
        work::dropNamedAuto();
    }

    HL_OVL_GATED const hldata::RunIndex &runIndex()
    {
        HL_SAVE_WORK();
        return work::runIndex();
    }

#ifndef HLDS_CHILD
    HL_TWL_CODE void forget()
    {
        // (Both read whole when next asked for: runIndexRead starts afresh.)
        finish();
        list.clear();
        listed = false;
        runKnown = false;
    }
#endif

    HL_OVL_GATED const hldata::SaveIndex &index()
    {
        HL_SAVE_WORK();
        return work::index();
    }

    HL_OVL_GATED bool read(hldata::SaveData &out, int slot)
    {
        HL_SAVE_WORK();
        return work::read(out, slot);
    }

    HL_OVL_GATED bool write(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading)
    {
        HL_SAVE_WORK();
        return work::write(save, slot, heading);
    }

    HL_OVL_GATED bool writeGame(const std::string &map, int slot, const hldata::SaveHeading &heading)
    {
        HL_SAVE_WORK();
        return work::writeGame(map, slot, heading);
    }

    HL_OVL_GATED bool remove(int slot)
    {
        HL_SAVE_WORK();
        return work::remove(slot);
    }

    HL_OVL_GATED bool begin(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading)
    {
        HL_SAVE_WORK();
        return work::begin(save, slot, heading);
    }

    HL_OVL_GATED hldata::SaveWriter::Result step()
    {
        HL_SAVE_WORK();
        return work::step();
    }

    bool busy()
    {
        return writer.busy();
    }

    const char *whyFailed()
    {
        static char why[48];
        if (card.refused[0] != 0)
            std::snprintf(why, sizeof(why), "%s, %s %d", writer.failedAt(), card.refused, card.refusedErrno);
        else
            std::snprintf(why, sizeof(why), "%s", writer.failedAt());
        card.refused = "";
        card.refusedErrno = 0;
        return why;
    }

    HL_OVL_GATED bool finish()
    {
        // (Every map's load asks, a match's too: nothing to finish, nothing
        // read in.)
        if (!writer.busy())
            return true;
        HL_SAVE_WORK();
        return work::finish();
    }
} // namespace hlrt::SaveGame
#endif
