#pragma once

// The maps the player has left, kept for coming back to: as the game keeps
// every level of the chapter as it was left, so that a door opened is open,
// a monster killed lies where it fell, a charger drained is drained.
//
// The whole state of a map (HlSaveState.hpp's, and the minds of its
// monsters: HlAiSave.hpp's) is written, as the player leaves it, to a file of
// its own in a folder of the run on the card -- run/<map>.mst -- a piece at a
// time, never all of it in memory: a DS has a few hundred kilobytes to spare
// at most, and a map's state is forty. Only a small index of them is kept in
// memory (RunIndex), and on the card beside them. Coming back by a level
// change, the file is checked whole, then laid over the map begun afresh;
// and it is gone from the folder (the folder never holds the map the player
// is in: that one is in memory, and in the saves).
//
// A file is of one cooked map: its checksum is in it, and a map cooked again
// since has its file let go -- it is come back to as it begins, with only
// what the saves' per-map bits remember (mapRestore). So are the maps let go
// to keep the folder within kStoredMapsMost and kStoredBytesMost: the oldest
// left first.
//
// Those who went along with the player (makeArrival's) are written as they
// were, and their actors are noted in the file's heading: once they have come
// into the next map (applyArrival placed them), the map they left has them
// taken out as it is laid back -- the scientist who followed is not in two
// maps. One who could not be placed is still where he stood.
//
// std-only and inline: the host tests run it (test_revisit), over the same
// small interface to a folder of files the saves use (HlSaveFiles.hpp).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "HlSaveFiles.hpp"
#include "HlSaveState.hpp"
#include "HlAiSave.hpp"

namespace hldata
{
    inline constexpr char kMapStoreMagic[4] = {'H', 'L', 'M', 'V'};
    inline constexpr char kMapStoreEnd[4] = {'M', 'V', 'N', 'D'};
    inline constexpr std::uint16_t kMapStoreVersion = 1;
    inline constexpr std::uint32_t kStoredBytesMost = 640u * 1024u;
    inline constexpr const char *kRunIndexFile = "run/index.dat";
    inline constexpr char kRunIndexMagic[4] = {'H', 'L', 'R', 'I'};
    /// How much of a file is read or written at once (the caller's buffer).
    inline constexpr std::size_t kMapStorePiece = 4096;

    /// At the head of a map's file.
    struct MapStoreHead
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t flags;
        std::uint32_t map;   ///< nameHash of the map
        std::uint32_t level; ///< the cooked map's checksum (LevelHeader::crc32)
        std::uint32_t bytes; ///< of what follows, before the tail
        std::uint32_t stamp; ///< the run's count when it was left (RunIndex::generation)
        std::int16_t sentAway[kMaxVisitors]; ///< actors who went with the player; -1 none
    };
    static_assert(sizeof(MapStoreHead) == 32);
    /// ... and at its end: a CRC of all before it.
    struct MapStoreTail
    {
        std::uint32_t crc;
        char magic[4];
    };
    static_assert(sizeof(MapStoreTail) == 8);

    // (RunEntry and RunIndex are HlSave.hpp's: a save's trailer lists them.)
    static_assert(kRunVisitors == kMaxVisitors);

    /// Those of `sentAway` who `landed` (bit by bit) taken out of the map
    /// laid back: they are in the map they went to.
    inline void runSendAway(ActorWorld &actors, const std::int16_t *sentAway, std::uint8_t landed)
    {
        for (int v = 0; v < kMaxVisitors; ++v)
        {
            const int who = sentAway[v];
            if (!(landed & (1u << v)) || who < 0 || who >= actors.count)
                continue;
            actors.states[who].active = false;
            actors.states[who].flags &= static_cast<std::uint8_t>(~kActorSolid);
            if (actors.ai != nullptr)
                aiForget(actors, who);
            actorPlaceBox(actors, who);
        }
    }

    inline int runFind(const RunIndex &index, const char *map)
    {
        for (int k = 0; k < index.count; ++k)
        {
            if (std::strncmp(index.entries[k].name, map, sizeof(index.entries[k].name)) == 0)
                return k;
        }
        return -1;
    }

    inline std::uint32_t runBytes(const RunIndex &index)
    {
        std::uint32_t total = 0;
        for (int k = 0; k < index.count; ++k)
            total += index.entries[k].bytes;
        return total;
    }

    inline bool runIndexWrite(SaveCard &card, const RunIndex &index)
    {
        std::uint8_t bytes[4 + sizeof(RunIndex) + 4];
        std::memcpy(bytes, kRunIndexMagic, 4);
        std::memcpy(bytes + 4, &index, sizeof(index));
        const std::uint32_t crc = crc32(bytes, 4 + sizeof(index));
        std::memcpy(bytes + 4 + sizeof(index), &crc, 4);
        return card.write(kRunIndexFile, bytes, sizeof(bytes));
    }

    /// The index on the card; false (and `index` empty) if it is not there
    /// or not whole.
    inline bool runIndexRead(SaveCard &card, RunIndex &index)
    {
        index = RunIndex();
        std::vector<std::uint8_t> bytes;
        if (!card.read(kRunIndexFile, bytes, 4 + sizeof(RunIndex) + 4) || bytes.size() != 4 + sizeof(RunIndex) + 4 ||
            std::memcmp(bytes.data(), kRunIndexMagic, 4) != 0)
            return false;
        std::uint32_t crc = 0;
        std::memcpy(&crc, bytes.data() + 4 + sizeof(RunIndex), 4);
        if (crc != crc32(bytes.data(), 4 + sizeof(RunIndex)))
            return false;
        std::memcpy(&index, bytes.data() + 4, sizeof(index));
        if (index.count < 0 || index.count > kStoredMapsMost)
        {
            index = RunIndex();
            return false;
        }
        return true;
    }

    /// Map k let go: its file, and its place in the index (not written).
    inline void runDrop(SaveCard &card, RunIndex &index, int k)
    {
        if (k < 0 || k >= index.count)
            return;
        char file[48];
        runFileOf(index.entries[k].name, file);
        card.remove(file);
        for (int n = k; n + 1 < index.count; ++n)
            index.entries[n] = index.entries[n + 1];
        index.entries[--index.count] = RunEntry();
        ++index.generation;
    }

    /// Every map kept let go, and a run begun afresh as `runId`.
    inline void runClear(SaveCard &card, RunIndex &index, std::uint32_t runId)
    {
        while (index.count > 0)
            runDrop(card, index, index.count - 1);
        index = RunIndex();
        index.runId = runId;
        runIndexWrite(card, index);
    }

    namespace detail
    {
        struct RunSink
        {
            SaveCard *card;
            const char *file;
        };
        inline bool runSink(void *context, const std::uint8_t *bytes, std::size_t size)
        {
            auto *sink = static_cast<RunSink *>(context);
            return sink->card->append(sink->file, bytes, size, false);
        }

        struct RunSource
        {
            SaveCard *card;
            const char *file;
            std::size_t offset, left;
            std::vector<std::uint8_t> *scratch;
        };
        inline std::size_t runSource(void *context, std::uint8_t *bytes, std::size_t most)
        {
            auto *source = static_cast<RunSource *>(context);
            const std::size_t want = most < source->left ? most : source->left;
            if (want == 0 || !source->card->readAt(source->file, source->offset, want, *source->scratch) ||
                source->scratch->size() != want)
                return 0;
            std::memcpy(bytes, source->scratch->data(), want);
            source->offset += want;
            source->left -= want;
            return want;
        }
    } // namespace detail

    /// Map `map` kept, as it is now: `body` writes what is to be kept of it
    /// (exactly `bodyBytes`) through the StreamOut it is given. What was kept
    /// of it before is let go; and the oldest maps kept, to make room.
    /// `buffer` is the caller's (kMapStorePiece). False, with nothing kept of
    /// it, if the card failed.
    template <class Body>
    bool runStore(SaveCard &card, RunIndex &index, const char *map, std::uint32_t level, std::uint32_t bodyBytes,
                  const std::int16_t *sentAway, std::uint8_t *buffer, Body &&body)
    {
        runDrop(card, index, runFind(index, map));
        const std::uint32_t whole = sizeof(MapStoreHead) + bodyBytes + sizeof(MapStoreTail);
        while (index.count > 0 && (index.count >= kStoredMapsMost || runBytes(index) + whole > kStoredBytesMost))
        {
            int oldest = 0;
            for (int k = 1; k < index.count; ++k)
            {
                if (index.entries[k].stamp < index.entries[oldest].stamp)
                    oldest = k;
            }
            runDrop(card, index, oldest);
        }
        char file[48];
        runFileOf(map, file);
        MapStoreHead head{};
        std::memcpy(head.magic, kMapStoreMagic, 4);
        head.version = kMapStoreVersion;
        head.map = nameHash(map);
        head.level = level;
        head.bytes = bodyBytes;
        head.stamp = index.generation + 1;
        for (int v = 0; v < kMaxVisitors; ++v)
            head.sentAway[v] = sentAway != nullptr ? sentAway[v] : std::int16_t(-1);
        bool ok = card.append(file, reinterpret_cast<const std::uint8_t *>(&head), sizeof(head), true);
        detail::RunSink sink{&card, file};
        detail::StreamOut out;
        out.sink = detail::runSink;
        out.context = &sink;
        out.buffer = buffer;
        out.capacity = kMapStorePiece;
        out.crc = crc32Update(kCrcStart, reinterpret_cast<const std::uint8_t *>(&head), sizeof(head));
        out.ok = ok;
        if (ok)
            body(out);
        ok = out.flush() && out.written == bodyBytes;
        MapStoreTail tail{crc32Finish(out.crc), {}};
        std::memcpy(tail.magic, kMapStoreEnd, 4);
        ok = ok && card.append(file, reinterpret_cast<const std::uint8_t *>(&tail), sizeof(tail), false);
        ok = card.finish(file) && ok;
        if (!ok)
        {
            card.remove(file);
            runIndexWrite(card, index);
            return false;
        }
        RunEntry &entry = index.entries[index.count++];
        entry = RunEntry();
        std::snprintf(entry.name, sizeof(entry.name), "%s", map);
        entry.level = level;
        entry.bytes = whole;
        entry.crc = tail.crc;
        entry.stamp = head.stamp;
        for (int v = 0; v < kMaxVisitors; ++v)
            entry.sentAway[v] = head.sentAway[v];
        index.generation = head.stamp;
        return runIndexWrite(card, index);
    }

    /// Whether map k's file is whole, and of the map cooked as `level`:
    /// read through once, its CRC worked out (into the caller's `scratch`).
    inline bool runCheck(SaveCard &card, const RunEntry &entry, std::uint32_t level, std::vector<std::uint8_t> &scratch)
    {
        char file[48];
        runFileOf(entry.name, file);
        if (entry.level != level || entry.bytes < sizeof(MapStoreHead) + sizeof(MapStoreTail))
            return false;
        MapStoreHead head{};
        if (!card.readAt(file, 0, sizeof(head), scratch) || scratch.size() != sizeof(head))
            return false;
        std::memcpy(&head, scratch.data(), sizeof(head));
        if (std::memcmp(head.magic, kMapStoreMagic, 4) != 0 || head.version != kMapStoreVersion ||
            head.level != level || head.map != nameHash(entry.name) ||
            sizeof(head) + head.bytes + sizeof(MapStoreTail) != entry.bytes)
            return false;
        std::uint32_t crc = kCrcStart;
        const std::size_t body = sizeof(head) + head.bytes;
        for (std::size_t at = 0; at < body;)
        {
            const std::size_t want = body - at < kMapStorePiece ? body - at : kMapStorePiece;
            if (!card.readAt(file, at, want, scratch) || scratch.size() != want)
                return false;
            crc = crc32Update(crc, scratch.data(), want);
            at += want;
        }
        MapStoreTail tail{};
        if (!card.readAt(file, body, sizeof(tail), scratch) || scratch.size() != sizeof(tail))
            return false;
        std::memcpy(&tail, scratch.data(), sizeof(tail));
        card.finish(file);
        return std::memcmp(tail.magic, kMapStoreEnd, 4) == 0 && tail.crc == crc32Finish(crc) && tail.crc == entry.crc;
    }

    /// Map k's kept state, read back through the StreamIn `lay` is given
    /// (check it with runCheck first). What `lay` says.
    template <class Lay>
    bool runLay(SaveCard &card, const RunEntry &entry, std::vector<std::uint8_t> &scratch, std::uint8_t *buffer,
                Lay &&lay)
    {
        char file[48];
        runFileOf(entry.name, file);
        detail::RunSource source{&card, file, sizeof(MapStoreHead),
                                 entry.bytes - sizeof(MapStoreHead) - sizeof(MapStoreTail), &scratch};
        detail::StreamIn in;
        in.source = detail::runSource;
        in.context = &source;
        in.buffer = buffer;
        in.capacity = kMapStorePiece;
        const bool ok = lay(in);
        card.finish(file);
        return ok;
    }

    /// The run's folder as the save in `slot` (read: `save`) had it, before
    /// its map is loaded. A save holding its maps left: their files, copied
    /// out of it -- not those already there as they were, which is what
    /// going back after a death finds. A save naming the folder (the one made
    /// coming into a map): the folder as it is, if it is still that run's;
    /// else nothing. A save of before version 9: nothing. Whatever is kept
    /// is a run begun again, `runId`.
    inline void runFromSave(SaveCard &card, int slot, const SaveData &save, RunIndex &index, std::uint32_t runId,
                            std::vector<std::uint8_t> &scratch)
    {
        if (save.visitedByRef && index.runId == save.visitedRunId && index.generation == save.visitedGeneration)
            return;
        if (save.visitedByRef || save.visited.empty())
        {
            runClear(card, index, runId);
            return;
        }
        auto same = [](const RunEntry &a, const RunEntry &b)
        { return std::strncmp(a.name, b.name, sizeof(a.name)) == 0 && a.crc == b.crc && a.bytes == b.bytes && a.level == b.level; };
        // Let go of what the save has not, as it has it.
        for (int k = index.count - 1; k >= 0; --k)
        {
            bool kept = false;
            for (const RunEntry &entry : save.visited)
                kept = kept || same(entry, index.entries[k]);
            if (!kept)
                runDrop(card, index, k);
        }
        RunIndex fresh;
        fresh.runId = runId;
        std::size_t at = save.visitedAt;
        const std::string fromName = saveHalfFile(slot, save.half);
        const char *from = fromName.c_str();
        for (const RunEntry &entry : save.visited)
        {
            const std::size_t here = at;
            at += entry.bytes;
            if (fresh.count >= kStoredMapsMost)
                break;
            const int there = runFind(index, entry.name);
            bool ok = there >= 0 && same(index.entries[there], entry);
            if (!ok)
            {
                // Out of the save, a piece at a time; and checked.
                char file[48];
                runFileOf(entry.name, file);
                ok = true;
                for (std::size_t done = 0; ok && done < entry.bytes;)
                {
                    const std::size_t n = entry.bytes - done < kMapStorePiece ? entry.bytes - done : kMapStorePiece;
                    ok = card.readAt(from, here + done, n, scratch) && scratch.size() == n &&
                         card.append(file, scratch.data(), n, done == 0);
                    done += n;
                }
                card.finish(from);
                ok = card.finish(file) && ok;
                std::uint32_t crc = 0;
                ok = ok && entry.bytes >= 8 && cardCrc(card, file, 0, entry.bytes - 8, scratch, crc) && crc == entry.crc;
                card.finish(file);
                if (!ok)
                    card.remove(file);
            }
            if (ok)
            {
                fresh.entries[fresh.count++] = entry;
                fresh.generation = entry.stamp > fresh.generation ? entry.stamp : fresh.generation;
            }
        }
        index = fresh;
        runIndexWrite(card, index);
    }
} // namespace hldata
