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
    bool remove(int) { return false; }
    bool begin(const hldata::SaveData &, int, const hldata::SaveHeading &) { return false; }
    hldata::SaveWriter::Result step() { return hldata::SaveWriter::Result::Done; }
    bool busy() { return false; }
    bool finish() { return true; }
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
#include <cstring>

#include "Global.hpp"
#include "Guard.hpp"
#include "audio/Sound.hpp"
#include "game/Game.hpp"

namespace hlrt::SaveGame
{
    namespace
    {
        /// The saves' folder on the card.
        struct FatCard : hldata::SaveCard
        {
            std::string path(const char *name) const { return global.savesDir() + name; }

            // The files being written and read a piece at a time (SaveWriter,
            // the maps left: HlMapStore.hpp), kept open from one piece to the
            // next -- opening one is the slow part: one being written, and one
            // being read (a map left's file copied into a save, a save's out
            // into the run's folder: both at once).
            struct Open
            {
                std::FILE *file = nullptr;
                std::string name;
                bool failed = false;
            };
            Open writing_, reading_;
            static bool shut(Open &open)
            {
                if (open.file == nullptr)
                    return true;
                const bool ok = std::fclose(open.file) == 0 && !open.failed;
                open = Open();
                return ok;
            }
            bool close()
            {
                const bool wrote = shut(writing_);
                shut(reading_);
                return wrote;
            }
            /// The folder `name` is to be in, made if it is not there (it
            /// usually is): the saves', or a folder in it (run/).
            void makeFolder(const char *name) const
            {
                std::string dir = global.savesDir();
                if (!dir.empty() && dir.back() == '/')
                    dir.pop_back();
                mkdir(dir.c_str(), 0777);
                const char *slash = std::strrchr(name, '/');
                if (slash != nullptr)
                    mkdir((global.savesDir() + std::string(name, slash)).c_str(), 0777);
            }
            bool append(const char *name, const std::uint8_t *bytes, std::size_t size, bool first) override
            {
                if (first)
                {
                    shut(writing_);
                    if (reading_.name == name)
                        shut(reading_);
                    makeFolder(name);
                    writing_.file = std::fopen(path(name).c_str(), "wb");
                    if (writing_.file == nullptr)
                        return false;
                    writing_.name = name;
                }
                else if (writing_.file == nullptr || writing_.name != name)
                {
                    return false; // (it was let go of in between: begun again by the writer)
                }
                if (std::fwrite(bytes, 1, size, writing_.file) != size)
                {
                    writing_.failed = true;
                    return false;
                }
                return true;
            }
            bool readAt(const char *name, std::size_t offset, std::size_t size, std::vector<std::uint8_t> &out) override
            {
                out.clear();
                if (reading_.file == nullptr || reading_.name != name)
                {
                    shut(reading_);
                    // (A file being written is read back from what is on the
                    // card: let go of first.)
                    if (writing_.name == name && !shut(writing_))
                        return false;
                    reading_.file = std::fopen(path(name).c_str(), "rb");
                    if (reading_.file == nullptr)
                        return false;
                    reading_.name = name;
                }
                if (std::fseek(reading_.file, static_cast<long>(offset), SEEK_SET) != 0)
                    return false;
                out.resize(size);
                out.resize(std::fread(out.data(), 1, size, reading_.file));
                return true;
            }
            bool finish(const char *name) override
            {
                bool ok = true;
                if (writing_.name == name)
                    ok = shut(writing_);
                if (reading_.name == name)
                    shut(reading_);
                return ok;
            }

            bool read(const char *name, std::vector<std::uint8_t> &out, std::size_t most) override
            {
                close();
                out.clear();
                std::FILE *file = std::fopen(path(name).c_str(), "rb");
                if (file == nullptr)
                    return false;
                // (As long as it is, and no longer: a save is tens of
                // kilobytes, the most one may be far more.)
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
            bool write(const char *name, const std::uint8_t *bytes, std::size_t size) override
            {
                close();
                makeFolder(name);
                std::FILE *file = std::fopen(path(name).c_str(), "wb");
                if (file == nullptr)
                    return false;
                const bool wrote = std::fwrite(bytes, 1, size, file) == size;
                return std::fclose(file) == 0 && wrote;
            }
            bool remove(const char *name) override
            {
                close();
                return unlink(path(name).c_str()) == 0;
            }
            bool rename(const char *from, const char *to) override
            {
                close();
                return ::rename(path(from).c_str(), path(to).c_str()) == 0;
            }
            /// The files in the saves' folder (one look at the folder, not a
            /// hundred files asked for one by one).
            bool list(std::vector<std::string> &names) override
            {
                close();
                names.clear();
                std::string dir = global.savesDir();
                if (!dir.empty() && dir.back() == '/')
                    dir.pop_back();
                DIR *folder = opendir(dir.c_str());
                if (folder == nullptr)
                {
                    // (No folder: no saves. Any other refusal is not "none":
                    // the files are asked for by name instead.)
                    return errno == ENOENT;
                }
                while (const dirent *entry = readdir(folder))
                {
                    if (entry->d_type != DT_DIR)
                        names.emplace_back(entry->d_name);
                }
                closedir(folder);
                return true;
            }
        };
        FatCard card;
        hldata::SaveIndex list;
        hldata::SaveWriter writer;
        bool listed = false;

        void need()
        {
            if (listed || !global.hasFat)
                return;
            hldata::saveIndexLoad(card, list);
            listed = true;
        }

        // The maps left, kept to come back to (HlMapStore.hpp): the index of
        // them, read once; a piece of a file at a time, here.
        hldata::RunIndex run;
        bool runKnown = false;
        std::uint8_t piece[hldata::kMapStorePiece];
        // (What a piece is read into: let go of after each use -- a block
        // left in the middle of the heap is one the next map's level may
        // not have room for, on a DS.)
        std::vector<std::uint8_t> scratch;
        void letGo()
        {
            std::vector<std::uint8_t>().swap(scratch);
        }
        /// The game's own saves name the run's folder; the player's hold a
        /// copy of it, to outlast it.
        bool namesFolder(int slot)
        {
            return slot == hldata::kSlotAuto || slot == hldata::kSlotArrival;
        }

        void needRun()
        {
            if (runKnown || !global.hasFat)
                return;
            hldata::runIndexRead(card, run);
            runKnown = true;
        }
    } // namespace

    bool leaveMap(const std::string &name, std::uint32_t level)
    {
        if (!global.hasFat)
            return false;
        finish();
        needRun();
        const std::uint32_t bytes = game.storedBytes();
        return hldata::runStore(card, run, name.c_str(), level, bytes, game.leftWith(), piece,
                                [](hldata::detail::StreamOut &out) { game.storeMap(out); });
    }

    int arriveMap(const std::string &name, std::uint32_t level)
    {
        if (!global.hasFat)
            return 0;
        finish();
        needRun();
        const int k = hldata::runFind(run, name.c_str());
        if (k < 0)
            return 0;
        const hldata::RunEntry entry = run.entries[k];
        int laid = 0;
        if (hldata::runCheck(card, entry, level, scratch))
        {
            hldata::runLay(card, entry, scratch, piece,
                           [&](hldata::detail::StreamIn &in)
                           {
                               laid = game.layStored(in, entry.sentAway, entry.landed) ? 1 : (in.ok ? 0 : -1);
                               return laid > 0;
                           });
        }
        // (Laid, or of no use: either way not kept now. The map the player
        // is in is in memory, and in the saves.)
        hldata::runDrop(card, run, k);
        hldata::runIndexWrite(card, run);
        letGo();
        return laid;
    }

    void landed(const std::string &left, std::uint8_t mask)
    {
        if (!global.hasFat)
            return;
        needRun();
        const int k = hldata::runFind(run, left.c_str());
        if (k < 0 || run.entries[k].landed == mask)
            return;
        // (A save being written a step at a time is finished first: the
        // index's write would let go of its file half-way.)
        finish();
        run.entries[k].landed = mask;
        hldata::runIndexWrite(card, run);
    }

    void newRun(std::uint32_t runId)
    {
        if (!global.hasFat)
            return;
        finish();
        needRun();
        hldata::runClear(card, run, runId);
    }

    bool fromSave(int slot, const hldata::SaveData &save, std::uint32_t runId)
    {
        if (!global.hasFat)
            return false;
        finish();
        needRun();
        const std::uint32_t wasRun = run.runId, wasGeneration = run.generation;
        hldata::runFromSave(card, slot, save, run, runId, scratch);
        letGo();
        return run.runId != wasRun || run.generation != wasGeneration;
    }

    void dropNamedAuto()
    {
        if (!global.hasFat)
            return;
        need();
        finish();
        if (list.slots[hldata::kSlotAuto].status != hldata::kSaveGood)
            return;
        static hldata::SaveData held; // not on the stack: it holds vectors of vectors
        held = hldata::SaveData();
        if (hldata::saveSlotRead(card, hldata::kSlotAuto, held) && held.visitedByRef)
            hldata::saveSlotForget(card, list, hldata::kSlotAuto);
        held = hldata::SaveData();
    }

    const hldata::RunIndex &runIndex()
    {
        needRun();
        return run;
    }

    const hldata::SaveIndex &index()
    {
        need();
        return list;
    }

    bool read(hldata::SaveData &out, int slot)
    {
        if (!global.hasFat)
            return false;
        need();
        finish();
        return hldata::saveSlotRead(card, slot, out);
    }

    bool write(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading)
    {
        if (!global.hasFat)
            return false;
        guard::check("save");
        need();
        finish();
        needRun();
        // (With the maps left: copied in -- or, for the game's own two, only
        // named: the folder changes only at a way out, a new game or a load,
        // and each makes those again.) The card's writing holds up the
        // ARM7 on a DSi: the sound plays straight from its channels for
        // that long (Sound::holdPipeline).
        sound.holdPipeline(true);
        const bool wrote =
            hldata::saveSlotWrite(card, list, slot, save, heading, &run, namesFolder(slot));
        sound.holdPipeline(false);
        return wrote;
    }

    bool remove(int slot)
    {
        if (!global.hasFat)
            return false;
        need();
        finish();
        return hldata::saveSlotDelete(card, list, slot);
    }

    bool begin(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading)
    {
        if (!global.hasFat || writer.busy())
            return false;
        guard::check("save");
        need();
        needRun();
        // (A piece a frame, each short enough for the sound to go on through
        // it: the pipeline, and the music in it, are left running.)
        return writer.begin(card, list, slot, save, heading, &run, namesFolder(slot));
    }

    hldata::SaveWriter::Result step()
    {
        return writer.step();
    }

    bool busy()
    {
        return writer.busy();
    }

    bool finish()
    {
        if (!writer.busy())
            return true;
        // (All that is left in one go: as write's.)
        sound.holdPipeline(true);
        const bool done = writer.finish();
        sound.holdPipeline(false);
        return done;
    }
} // namespace hlrt::SaveGame
#endif
