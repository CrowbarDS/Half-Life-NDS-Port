#include "global_header.hpp"

#include "game/SaveGame.hpp"

#include "DebugLog.hpp"

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
    const void *stackOverflowed() { return nullptr; }
    std::uint32_t stackPeak() { return 0; }
    bool cardInUse() { return false; }
    bool begin(const hldata::SaveData &, int, const hldata::SaveHeading &) { return false; }
    hldata::SaveWriter::Result step(bool) { return hldata::SaveWriter::Result::Done; }
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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <nds/cothread.h>

#include "Global.hpp"
#include "Overlay.hpp"
#include "game/SaveGameCard.hpp"

// (Profiling: the card's reads and writes made here said to be the saves':
// perf.txt's slowest reads -- the overlay's own read in among them.)
#ifdef HLDS_PROFILE
#define HL_SAVE_WHO const ::hlrt::DebugLog::Perf::CardWho cardWho("save")
#else
#define HL_SAVE_WHO ((void)0)
#endif

// The saves' work is in the save overlay (SaveGameBody.cpp): each call here
// reads it in, a halt naming it if a match's screens hold its memory -- a
// match keeps no saves. What is asked every frame, or at every map's load,
// is answered here when there is nothing to do.
#if HL_OVL_COUNT > 0
#define HL_SAVE_WORK()                                                                                                 \
    HL_SAVE_WHO;                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        ::hlrt::ovl::load(::hlrt::ovl::Id::Save);                                                                      \
        HL_OVL_GATE(Save);                                                                                             \
    } while (0)
#else
#define HL_SAVE_WORK() HL_SAVE_WHO
#endif

namespace hlrt::SaveGame
{
    namespace
    {
        // The save thread (below), and how deep it is in the card's calls.
        // The card is one: what the frame reads of it meanwhile (the music,
        // a sound) waits for the call the thread is in -- and the lock on
        // the card's files is not handed over in turn: let go and taken
        // again at once, the frame would wait for the whole save (2026-11-27,
        // a DSpico: 93 blanks). So the thread gives the frame its turn after
        // each call, and the frame's streams leave the card alone while it is
        // in one, if what they have read ahead lasts (cardInUse).
        // (DSi code: on a DS there is no thread, and only `saveThread < 0`
        // is ever asked.)
        cothread_t saveThread = -1;
        volatile int cardDepth = 0;
        HL_TWL_CODE bool onSaveThread() { return cothread_get_current() == saveThread; }
        HL_TWL_CODE void enterCard()
        {
            if (onSaveThread())
                ++cardDepth;
        }
        HL_TWL_CODE void leaveCard()
        {
            if (onSaveThread() && --cardDepth == 0)
                cothread_yield();
        }
        /// Between two pieces of a write: the frame's turn, out of the card.
        HL_TWL_CODE void giveTurn()
        {
            if (!onSaveThread())
                return;
            const int depth = cardDepth;
            cardDepth = 0;
            cothread_yield();
            cardDepth = depth;
        }
        struct CardCall
        {
            CardCall()
            {
                if (saveThread >= 0)
                    enterCard();
            }
            ~CardCall()
            {
                if (saveThread >= 0)
                    leaveCard();
            }
        };
        /// What the save thread writes in one call at most: a piece of the
        /// card's work the frame may have to wait for (one of the card's
        /// writes, or two).
        constexpr std::size_t kThreadPiece = 16 * 1024;
    } // namespace

    bool cardInUse() { return cardDepth > 0; }

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
            if (buffered_ == 0 || writing_.fd < 0)
            {
                buffered_ = 0;
                return true;
            }
            const bool ok = writeAll(writing_.fd, buffer_, buffered_);
            buffered_ = 0;
            if (ok)
                return true;
            writing_.failed = true;
            return no("write");
        }

        bool FatCard::shutWriting()
        {
            const bool flushed = flushWriting();
            const bool cutOk = writing_.fd < 0 || writing_.failed || cut(writing_.fd, writing_.was);
            std::free(buffer_);
            buffer_ = nullptr;
            bufferBytes_ = 0;
            bool closed = true;
            if (writing_.fd >= 0)
                closed = ::close(writing_.fd) == 0 && !writing_.failed;
            writing_ = Writing();
            return closed && flushed && cutOk;
        }

        int FatCard::overwrite(const char *name, long &was)
        {
            was = 0;
            const std::string where = path(name);
            int fd = ::open(where.c_str(), O_RDWR);
            if (fd >= 0)
            {
                was = ::lseek(fd, 0, SEEK_END);
                if (was >= 0 && ::lseek(fd, 0, SEEK_SET) == 0)
                    return fd;
                ::close(fd);
                return -1;
            }
            makeFolder(name);
            return ::open(where.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }

        bool FatCard::cut(int fd, long was)
        {
            // (Only if it is shorter than it was: as long or longer, its end
            // is where the writing stopped.)
            const long now = ::lseek(fd, 0, SEEK_CUR);
            if (now < 0)
                return no("tell");
            if (now >= was)
                return true;
            return ftruncate(fd, now) == 0 || no("cut");
        }

        bool FatCard::writeAll(int fd, const std::uint8_t *bytes, std::size_t size)
        {
            const std::size_t most = saveThread >= 0 ? kThreadPiece : size;
            for (bool first = true; size > 0; first = false)
            {
                if (!first)
                    giveTurn();
                const ssize_t n = ::write(fd, bytes, size < most ? size : most);
                if (n <= 0)
                    return false;
                bytes += n;
                size -= static_cast<std::size_t>(n);
            }
            return true;
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
            const CardCall call;
            if (first)
            {
                shutWriting();
                if (reading_.name == name)
                    shut(reading_);
                writing_.fd = overwrite(name, writing_.was);
                if (writing_.fd < 0)
                    return no("open");
                writing_.name = name;
                bufferBytes_ = isDSiMode() ? kWriteBufferDsi : kWriteBufferDs;
                buffer_ = static_cast<std::uint8_t *>(std::malloc(bufferBytes_));
                if (buffer_ == nullptr)
                    bufferBytes_ = 0;
            }
            else if (writing_.fd < 0 || writing_.name != name)
            {
                return false; // (it was let go of in between: begun again by the writer)
            }
            if (buffer_ == nullptr)
            {
                if (!writeAll(writing_.fd, bytes, size))
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
            const CardCall call;
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
            const CardCall call;
            bool ok = true;
            if (writing_.name == name)
                ok = shutWriting();
            if (reading_.name == name)
                shut(reading_);
            return ok || no("close");
        }

        bool FatCard::read(const char *name, std::vector<std::uint8_t> &out, std::size_t most)
        {
            const CardCall call;
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
            const CardCall call;
            close();
            long was = 0;
            const int fd = overwrite(name, was);
            if (fd < 0)
                return no("open");
            const bool wrote = (writeAll(fd, bytes, size) || no("write")) && cut(fd, was);
            return (::close(fd) == 0 || no("close")) && wrote;
        }

        bool FatCard::remove(const char *name)
        {
            const CardCall call;
            close();
            return unlink(path(name).c_str()) == 0 || no("remove");
        }

        bool FatCard::rename(const char *from, const char *to)
        {
            const CardCall call;
            close();
            return ::rename(path(from).c_str(), path(to).c_str()) == 0 || no("rename");
        }

        bool FatCard::list(std::vector<std::string> &names)
        {
            const CardCall call;
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

    namespace
    {
        // The save thread. Where the card's work is not the ARM9's -- the
        // DSi's SD, or a flashcart's driver run on the ARM7 -- the ARM9 only
        // waits for it, and libnds lets the other threads run meanwhile: a
        // step-at-a-time save is written here, under the frame and the think
        // worker, so the game goes on while the card writes (some 200 ms a
        // write on a DSpico). With the driver on the ARM9 it would only stop
        // the game as before, at a moment the frame does not choose: there,
        // the frame steps it (and holds its last part for a quiet moment).
        // Only this thread touches the card and the writer while it has a
        // save; everything else of the saves' finishes it first (waitThread).
        // A DSi's only: a DS's heap has no 6 KB to spare for its stack (the
        // tightest map leaves it a few), and steps its saves in the frame.
        constexpr int kSavePriority = -2;
        // (Its stack: taken once from the heap, when the list of saves is
        // first read -- before play -- and kept. Its first words are a
        // pattern the guard looks at every frame.)
        constexpr std::size_t kStackBytes = 6 * 1024;
        std::uint32_t *stack = nullptr;
        constexpr std::uint32_t kGuardWords = 16;
        constexpr std::uint32_t kGuardMark = 0x53415645u; // "SAVE"
#ifdef HLDS_PROFILE
        constexpr std::uint32_t kStackPaint = 0x7E1A5B3Du;
#endif
        bool made = false;
        cosema_t go = 0, over = 0;
        // It has a save (set by the frame, cleared by it when the save is
        // done); its result, not yet said by step().
        volatile bool running = false, unsaid = false;
        volatile hldata::SaveWriter::Result result = hldata::SaveWriter::Result::Done;

        HL_OVL_GATED hldata::SaveWriter::Result stepOnThread()
        {
            HL_SAVE_WORK();
            return work::step(true);
        }

        HL_TWL_CODE int saveWork(void *)
        {
            for (;;)
            {
                cosema_wait(&go);
                hldata::SaveWriter::Result r = hldata::SaveWriter::Result::Busy;
                while ((r = stepOnThread()) == hldata::SaveWriter::Result::Busy)
                    cothread_yield();
                result = r;
                running = false;
                cosema_signal(&over);
            }
            return 0;
        }

        HL_TWL_CODE bool makeThread()
        {
            if (global.cardOnArm9())
                return false;
            stack = static_cast<std::uint32_t *>(std::malloc(kStackBytes));
            if (stack == nullptr)
                return false;
#ifdef HLDS_PROFILE
            for (std::size_t i = 0; i < kStackBytes / 4; ++i)
                stack[i] = kStackPaint;
#endif
            for (std::uint32_t i = 0; i < kGuardWords; ++i)
                stack[i] = kGuardMark ^ i;
            cosema_init(&go, 0);
            cosema_init(&over, 0);
            saveThread = cothread_create_priority_manual(saveWork, nullptr, stack, kStackBytes, kSavePriority, 0);
            made = saveThread >= 0;
            return made;
        }

        /// The thread, made the first time it is asked for (DSi code: a DS
        /// has none of it). False: not here.
        bool threaded()
        {
            if (made)
                return true;
            if (!global.hasFat || !global.isDSi || stack != nullptr)
                return false;
            return makeThread();
        }
    } // namespace

    namespace
    {
        HL_TWL_CODE bool waitForThread()
        {
            while (running)
                cosema_wait(&over);
            if (!unsaid)
                return true;
            unsaid = false;
            return result == hldata::SaveWriter::Result::Done;
        }
        HL_TWL_CODE void handOff()
        {
            while (cosema_try_wait(&over))
            {
            }
            unsaid = true;
            running = true;
            cosema_signal(&go);
        }
        HL_TWL_CODE const void *stackBroken()
        {
            for (std::uint32_t i = 0; i < kGuardWords; ++i)
            {
                if (stack[i] != (kGuardMark ^ i))
                    return stack;
            }
            return nullptr;
        }
    } // namespace

    namespace detail
    {
        bool waitThread() { return made ? waitForThread() : true; }
    } // namespace detail

    const void *stackOverflowed() { return made ? stackBroken() : nullptr; }

    std::uint32_t stackPeak()
    {
#ifdef HLDS_PROFILE
        if (!made)
            return 0;
        std::size_t untouched = kGuardWords;
        while (untouched < kStackBytes / 4 && stack[untouched] == kStackPaint)
            ++untouched;
        return static_cast<std::uint32_t>(kStackBytes - untouched * 4);
#else
        return 0;
#endif
    }

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
    HL_OVL_GATED void chooseDldiMode()
    {
        HL_SAVE_WORK();
        work::chooseDldiMode();
    }

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
        threaded(); // (made here, before play: it takes its stack from the heap)
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

    HL_OVL_GATED hldata::SaveWriter::Result step(bool quiet)
    {
        if (running)
            return hldata::SaveWriter::Result::Busy;
        if (unsaid)
        {
            unsaid = false;
            return result;
        }
        // (Handed to the save thread, where there is one, in one go. Never
        // made here: in play, nothing is taken from the heap.)
        if (writer.busy() && made)
        {
            handOff();
            return hldata::SaveWriter::Result::Busy;
        }
        HL_SAVE_WORK();
        return work::step(quiet);
    }

    bool busy()
    {
        return writer.busy() || running || unsaid;
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
        if (!busy())
            return true;
        HL_SAVE_WORK();
        return work::finish();
    }
} // namespace hlrt::SaveGame
#endif
