#pragma once

// The saves on the card: which files there are, how one is written so that
// switching off half-way loses nothing, and the list of them.
//
// One is the game's own (AUTOSAVE: written at a map's autosave points and on
// arriving in a map), one is QUICK (the menu's SAVE), one more, never shown,
// is the state on coming into the map (what someone who dies again and again
// is taken back to) -- and the player's own, as many as they make, up to 99
// (save1.sav, save2.sav, ...: the six of the slots there used to be are the
// first six of these). Each begins with a heading (HlSave.hpp) that says what
// it is, and an index beside them holds the headings of those there are, so
// that showing the list opens one file and not a hundred: opening a file on a
// card is the slow part. The index is only a copy -- it is made again from
// the files if it is not there or cannot be believed.
//
// Each slot has two files (save1.sav, save1.sv2), written in turn: a save goes
// into the one not holding the slot's newest, written over where it is, so
// that at every moment one whole save is on the card. Which one holds it is
// the index's to say -- the file's number (SaveHeading::serial) and CRC say it
// again if the index is lost. Nothing is renamed or removed when a save is
// done: on some cards each change to a folder costs as much as the save's
// data (some 200 ms on a DSpico, measured 2026-11-27). A card that starts
// refusing (a worn one) costs the saves it refuses, never one it had. The
// files a version before left half-way (.new, .old) are put right at the
// next start (saveRecover).
//
// All of it is over a small interface to "a folder of files" (SaveCard), so
// that the host tests run it against a folder of their own, and against one
// that fails where they tell it to. std-only and inline.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "HlSave.hpp"

namespace hldata
{
    // Which save is which: a number for each (a slot), fixed by its file.
    inline constexpr int kSlotAuto = 0;
    inline constexpr int kSlotQuick = 1;
    inline constexpr int kSlotArrival = 2;
    inline constexpr int kSlotManual = 3;   ///< the player's first (save1.sav) ...
    inline constexpr int kSlotManuals = 99; ///< ... and how many of them there may be
    inline constexpr int kSlotCount = kSlotManual + kSlotManuals;

    inline const char *saveSlotFile(int slot)
    {
        static char manual[kSlotManuals][12];
        if (slot == kSlotAuto)
            return "auto.sav";
        if (slot == kSlotQuick)
            return "quick.sav";
        if (slot == kSlotArrival)
            return "arrival.sav";
        if (slot < kSlotManual || slot >= kSlotCount)
            return "";
        char *name = manual[slot - kSlotManual];
        if (name[0] == 0)
            std::snprintf(name, sizeof(manual[0]), "save%d.sav", slot - kSlotManual + 1);
        return name;
    }
    /// One of a slot's two files: 0 the .sav (the only one, before), 1 the .sv2.
    inline std::string saveHalfFile(int slot, int half)
    {
        std::string name = saveSlotFile(slot);
        if (half != 0 && name.size() > 4)
            name.replace(name.size() - 4, 4, ".sv2");
        return name;
    }
    /// The slot a file is (-1: none of them).
    inline int saveSlotOfFile(const char *name)
    {
        if (std::strcmp(name, "auto.sav") == 0)
            return kSlotAuto;
        if (std::strcmp(name, "quick.sav") == 0)
            return kSlotQuick;
        if (std::strcmp(name, "arrival.sav") == 0)
            return kSlotArrival;
        int n = 0;
        char tail[8] = {};
        if (std::sscanf(name, "save%d%7s", &n, tail) == 2 && std::strcmp(tail, ".sav") == 0 && n >= 1 &&
            n <= kSlotManuals && std::strcmp(name, saveSlotFile(kSlotManual + n - 1)) == 0)
            return kSlotManual + n - 1;
        return -1;
    }
    inline constexpr const char *kSaveIndexFile = "index.dat";
    inline std::uint8_t saveSlotKind(int slot)
    {
        return slot == kSlotAuto ? kSaveKindAuto
                                 : (slot == kSlotQuick ? kSaveKindQuick
                                                       : (slot == kSlotArrival ? kSaveKindArrival : kSaveKindManual));
    }

    /// A folder of files, by name.
    struct SaveCard
    {
        virtual ~SaveCard() = default;
        /// The first `most` bytes of a file (all of it, if it is shorter).
        /// False if it is not there or cannot be read.
        virtual bool read(const char *name, std::vector<std::uint8_t> &out, std::size_t most) = 0;
        /// A file made (or made again) with exactly these bytes. False if
        /// not all of them were written.
        virtual bool write(const char *name, const std::uint8_t *bytes, std::size_t size) = 0;
        virtual bool remove(const char *name) = 0;
        /// (To a name that is not there: FAT will not rename over a file.)
        virtual bool rename(const char *from, const char *to) = 0;

        // A file a piece at a time (SaveWriter: a save written over many
        // frames). As they are here, made of the calls above -- a card that
        // can keep a file open between pieces does better.

        /// `bytes` added to the end of a file; `first`: the file made anew
        /// with them. False if not all of them were written.
        virtual bool append(const char *name, const std::uint8_t *bytes, std::size_t size, bool first)
        {
            if (first)
                return write(name, bytes, size);
            std::vector<std::uint8_t> was;
            if (!read(name, was, static_cast<std::size_t>(-1)))
                return false;
            was.insert(was.end(), bytes, bytes + size);
            return write(name, was.data(), was.size());
        }
        /// Up to `size` bytes of a file from `offset` (fewer at its end).
        virtual bool readAt(const char *name, std::size_t offset, std::size_t size, std::vector<std::uint8_t> &out)
        {
            if (!read(name, out, offset + size))
                return false;
            out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(offset < out.size() ? offset : out.size()));
            return true;
        }
        /// Done with a file read or written a piece at a time. False if what
        /// was written did not all reach the card.
        virtual bool finish(const char *name)
        {
            (void)name;
            return true;
        }
        /// The names of the files there are, if the card can say (false: it
        /// cannot, and each one is asked for by name instead).
        virtual bool list(std::vector<std::string> &names)
        {
            (void)names;
            return false;
        }
    };

    struct SaveSlot
    {
        int status = kSaveNone; ///< kSave*
        SaveHeading heading;
        /// For kSaveOther: the numbers its header had (0: not known), to say
        /// why it is not read (saveRefusal). Not in the index: found again.
        std::uint16_t version = 0, minReader = 0, levelVersion = 0;
    };
    struct SaveIndex
    {
        SaveSlot slots[kSlotCount];
        std::uint32_t nextSerial = 1;
        /// Which of each slot's two files holds its save (saveHalfFile): the
        /// next is written into the other.
        std::uint8_t half[kSlotCount] = {};

        /// As new, in place. (Not `= SaveIndex()`: that is ten kilobytes made
        /// on the stack first, more than a DS's functions may take.)
        void clear()
        {
            for (SaveSlot &slot : slots)
                slot = SaveSlot();
            for (std::uint8_t &h : half)
                h = 0;
            nextSerial = 1;
        }
    };

    namespace detail
    {
        // The index: a heading, then one entry for each save there is (the
        // slots that have nothing are not in it). ('HLSI', the nine slots of
        // before, is not read: it is made again from the files.)
        inline constexpr char kIndexMagic[4] = {'H', 'L', 'S', '2'};
        struct IndexHead
        {
            char magic[4];
            std::uint16_t version;
            std::uint16_t levelVersion;
            std::uint32_t nextSerial;
            std::uint32_t count;
            std::uint32_t crc; ///< of the entries
        };
        struct IndexEntry
        {
            std::uint16_t slot;
            std::uint16_t half; ///< (0 before there were two: the .sav, as then)
            std::uint32_t status;
            SaveHeading heading;
        };

        // What a version before left beside a save: one being written (.new),
        // or the one before set aside while it took its place (.old).
        inline std::string legacyNew(int slot)
        {
            return std::string(saveSlotFile(slot)) + ".new";
        }
        inline std::string legacyOld(int slot)
        {
            return std::string(saveSlotFile(slot)) + ".old";
        }

        /// Which files each slot has, if the card can list itself (false: it
        /// cannot): bit 0 its .sav, bit 1 its .sv2, kOnLegacy a .new or .old.
        inline constexpr std::uint8_t kOnLegacy = 4;
        inline bool slotsOnCard(SaveCard &card, std::vector<std::uint8_t> &on)
        {
            std::vector<std::string> names;
            if (!card.list(names))
                return false;
            on.assign(kSlotCount, 0);
            auto ends = [](const std::string &name, const char *tail)
            { return name.size() > 4 && name.compare(name.size() - 4, 4, tail) == 0; };
            for (std::string &name : names)
            {
                const bool legacy = ends(name, ".new") || ends(name, ".old");
                if (legacy)
                    name.resize(name.size() - 4);
                int half = 0;
                if (!legacy && ends(name, ".sv2"))
                {
                    name.replace(name.size() - 4, 4, ".sav");
                    half = 1;
                }
                const int slot = saveSlotOfFile(name.c_str());
                if (slot >= 0)
                    on[slot] |= legacy ? kOnLegacy : static_cast<std::uint8_t>(1 << half);
            }
            return true;
        }

        inline bool exists(SaveCard &card, const char *name)
        {
            std::vector<std::uint8_t> probe;
            return card.read(name, probe, 1);
        }

        inline void noteHeader(const std::vector<std::uint8_t> &bytes, SaveSlot &slot)
        {
            SaveHeader header;
            if (slot.status != kSaveOther || bytes.size() < sizeof(header))
                return;
            std::memcpy(&header, bytes.data(), sizeof(header));
            slot.version = header.version;
            slot.minReader = header.minReader;
            slot.levelVersion = header.levelVersion;
        }

        /// What a file says it is, by its heading (status None: not there,
        /// or not a save).
        inline SaveSlot fileHeading(SaveCard &card, const char *name)
        {
            SaveSlot out;
            std::vector<std::uint8_t> bytes;
            if (card.read(name, bytes, kSaveHeadBytes))
            {
                out.status = readSaveHeading(bytes.data(), bytes.size(), out.heading);
                noteHeader(bytes, out);
            }
            return out;
        }
    } // namespace detail

    /// A run's map file's name ("run/c1a1.mst").
    inline void runFileOf(const char *map, char (&out)[48]) { std::snprintf(out, sizeof(out), "run/%.31s.mst", map); }

    /// The CRC of `size` bytes of a file from `offset`, read a piece at a
    /// time into `scratch`; false if they cannot all be read.
    inline bool cardCrc(SaveCard &card, const char *name, std::size_t offset, std::size_t size,
                        std::vector<std::uint8_t> &scratch, std::uint32_t &crc)
    {
        crc = kCrcStart;
        for (std::size_t at = 0; at < size;)
        {
            const std::size_t want = size - at < 4096 ? size - at : 4096;
            if (!card.readAt(name, offset + at, want, scratch) || scratch.size() != want)
                return false;
            crc = crc32Update(crc, scratch.data(), want);
            at += want;
        }
        crc = crc32Finish(crc);
        return true;
    }

    /// A save file's trailer (the maps left it holds): its heading and
    /// table, read from where the payload ends (`at`). False if there is
    /// none, or it is not whole; the files themselves are not read.
    inline bool saveTrailer(SaveCard &card, const char *name, std::size_t at, VisitedHead &head,
                            std::vector<RunEntry> &table, std::vector<std::uint8_t> &scratch)
    {
        table.clear();
        if (!card.readAt(name, at, sizeof(head), scratch) || scratch.size() != sizeof(head))
            return false;
        std::memcpy(&head, scratch.data(), sizeof(head));
        if (std::memcmp(head.magic, kVisitedMagic, 4) != 0 || head.count > kStoredMapsMost ||
            head.bytes < head.count * sizeof(RunEntry))
            return false;
        const std::size_t tableBytes = head.count * sizeof(RunEntry);
        if (tableBytes != 0 && (!card.readAt(name, at + sizeof(head), tableBytes, scratch) || scratch.size() != tableBytes))
            return false;
        if (crc32(scratch.data(), static_cast<std::uint32_t>(tableBytes)) != head.tableCrc)
            return false;
        table.resize(head.count);
        if (tableBytes != 0)
            std::memcpy(table.data(), scratch.data(), tableBytes);
        std::size_t files = 0;
        for (const RunEntry &entry : table)
            files += entry.bytes;
        return (head.flags & kVisitedByRef) ? head.bytes == tableBytes : head.bytes == tableBytes + files;
    }

    /// Whether a save file is whole, all of it: its payload, and the maps
    /// left that follow it, each file its own CRC.
    inline bool saveFileWhole(SaveCard &card, const char *name)
    {
        std::vector<std::uint8_t> bytes;
        if (!card.read(name, bytes, kSaveHeadBytes) || bytes.size() != kSaveHeadBytes)
            return false;
        SaveHeader header;
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.payloadBytes > kSaveMaxBytes || !card.read(name, bytes, kSaveHeadBytes + header.payloadBytes) ||
            !saveWhole(bytes.data(), bytes.size()))
            return false;
        // (Before version 9, nothing after it.)
        std::vector<std::uint8_t> scratch;
        VisitedHead head;
        std::vector<RunEntry> table;
        const std::size_t at = kSaveHeadBytes + header.payloadBytes;
        if (!saveTrailer(card, name, at, head, table, scratch))
        {
            std::vector<std::uint8_t> more;
            return header.version < 9 && card.readAt(name, at, 1, more) && more.empty();
        }
        std::size_t file = at + sizeof(head) + table.size() * sizeof(RunEntry);
        for (const RunEntry &entry : table)
        {
            if ((head.flags & kVisitedByRef) || entry.bytes < 8)
                break;
            std::uint32_t crc = 0;
            if (!cardCrc(card, name, file, entry.bytes - 8, scratch, crc) || crc != entry.crc)
                return false;
            file += entry.bytes;
        }
        return true;
    }

    namespace detail
    {
        /// A file a version before left beside a slot's save (legacyNew,
        /// legacyOld) made one of its two, if it is whole: into one not there,
        /// or over one older or not whole -- else, or if it is not whole and
        /// the slot has a save that is, gone.
        inline void takeLegacy(SaveCard &card, int slot, const char *left)
        {
            if (!exists(card, left))
                return;
            const std::string halves[2] = {saveHalfFile(slot, 0), saveHalfFile(slot, 1)};
            if (!saveFileWhole(card, left))
            {
                if (saveFileWhole(card, halves[0].c_str()) || saveFileWhole(card, halves[1].c_str()))
                    card.remove(left);
                return;
            }
            const SaveSlot was = fileHeading(card, left);
            for (const std::string &half : halves)
            {
                if (!exists(card, half.c_str()))
                {
                    card.rename(left, half.c_str());
                    return;
                }
            }
            for (const std::string &half : halves)
            {
                const SaveSlot there = fileHeading(card, half.c_str());
                const bool older = there.status == kSaveGood && (there.heading.serial < was.heading.serial ||
                                                                 !saveFileWhole(card, half.c_str()));
                if (there.status == kSaveNone || there.status == kSaveBad || older)
                {
                    if (card.remove(half.c_str()))
                        card.rename(left, half.c_str());
                    return;
                }
            }
            card.remove(left);
        }
    } // namespace detail

    /// A slot as its files say: the newer of its two that is whole, and
    /// which one (`half`); with none whole, what one of them says it is (a
    /// save of another version; or damaged). What a version before left
    /// half-way is put right first. `newest`, if asked: the highest number
    /// any of its files has (whole or not), for the next save's to be higher.
    inline SaveSlot saveRecover(SaveCard &card, int slot, std::uint8_t &half, std::uint32_t *newest = nullptr)
    {
        detail::takeLegacy(card, slot, detail::legacyNew(slot).c_str());
        detail::takeLegacy(card, slot, detail::legacyOld(slot).c_str());
        const std::string names[2] = {saveHalfFile(slot, 0), saveHalfFile(slot, 1)};
        SaveSlot halves[2] = {detail::fileHeading(card, names[0].c_str()), detail::fileHeading(card, names[1].c_str())};
        for (const SaveSlot &h : halves)
        {
            if (newest != nullptr && h.status == kSaveGood && h.heading.serial > *newest)
                *newest = h.heading.serial;
        }
        const bool secondFirst = halves[1].status == kSaveGood &&
                                 (halves[0].status != kSaveGood || halves[1].heading.serial > halves[0].heading.serial);
        const int order[2] = {secondFirst ? 1 : 0, secondFirst ? 0 : 1};
        for (int h : order)
        {
            if (halves[h].status == kSaveGood && saveFileWhole(card, names[h].c_str()))
            {
                half = static_cast<std::uint8_t>(h);
                return halves[h];
            }
        }
        for (int h : order)
        {
            if (halves[h].status == kSaveOther)
            {
                half = static_cast<std::uint8_t>(h);
                return halves[h];
            }
        }
        for (int h : order)
        {
            if (halves[h].status != kSaveNone)
            {
                // (A heading that reads, on a file that is not whole: damaged,
                // or cut off half-way. Kept: a card that failed to read it once
                // may not the next time.)
                half = static_cast<std::uint8_t>(h);
                halves[h].status = kSaveBad;
                return halves[h];
            }
        }
        half = 0;
        return SaveSlot();
    }

    inline bool saveIndexWrite(SaveCard &card, const SaveIndex &index)
    {
        // (On the heap, not the stack: a hundred entries are ten kilobytes.)
        std::vector<detail::IndexEntry> entries;
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (index.slots[i].status == kSaveNone)
                continue;
            detail::IndexEntry entry{};
            entry.slot = static_cast<std::uint16_t>(i);
            entry.half = index.half[i];
            entry.status = static_cast<std::uint32_t>(index.slots[i].status);
            entry.heading = index.slots[i].heading;
            entries.push_back(entry);
        }
        detail::IndexHead head{};
        std::memcpy(head.magic, detail::kIndexMagic, sizeof(head.magic));
        head.version = kSaveVersion;
        head.levelVersion = kLevelVersion;
        head.nextSerial = index.nextSerial;
        head.count = static_cast<std::uint32_t>(entries.size());
        const std::size_t entryBytes = entries.size() * sizeof(detail::IndexEntry);
        head.crc = crc32(reinterpret_cast<const std::uint8_t *>(entries.data()), static_cast<std::uint32_t>(entryBytes));
        std::vector<std::uint8_t> bytes(sizeof(head) + entryBytes);
        std::memcpy(bytes.data(), &head, sizeof(head));
        if (entryBytes != 0)
            std::memcpy(bytes.data() + sizeof(head), entries.data(), entryBytes);
        if (card.write(kSaveIndexFile, bytes.data(), bytes.size()))
            return true;
        // (Not written: an index that is whole but behind is worse than
        // none -- it would be believed at the next start. Gone, the list is
        // made again from the files then.)
        card.remove(kSaveIndexFile);
        return false;
    }

    namespace detail
    {
        /// One slot made again from its files, in a list.
        inline void relist(SaveCard &card, SaveIndex &index, int slot)
        {
            std::uint32_t newest = 0;
            index.slots[slot] = saveRecover(card, slot, index.half[slot], &newest);
            if (newest >= index.nextSerial)
                index.nextSerial = newest + 1;
        }
    } // namespace detail

    /// The list made again from the files themselves (each put right
    /// first), and written. (Only the files there are, if the card can say
    /// which.)
    inline void saveIndexRebuild(SaveCard &card, SaveIndex &index)
    {
        index.clear();
        std::vector<std::uint8_t> on;
        const bool listed = detail::slotsOnCard(card, on);
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (!listed || on[i] != 0)
                detail::relist(card, index, i);
        }
        saveIndexWrite(card, index);
    }

    /// The list: from the index if it is there and whole, else from the
    /// files. (Once, at the start: whatever was left half-way is put right
    /// before the index is believed -- `recover` false to skip that, for a
    /// list asked for again.)
    inline void saveIndexLoad(SaveCard &card, SaveIndex &index, bool recover = true)
    {
        std::vector<std::uint8_t> bytes;
        detail::IndexHead head{};
        constexpr std::size_t kMost = sizeof(detail::IndexHead) + kSlotCount * sizeof(detail::IndexEntry);
        bool good = card.read(kSaveIndexFile, bytes, kMost + 1) && bytes.size() >= sizeof(head);
        if (good)
        {
            std::memcpy(&head, bytes.data(), sizeof(head));
            const std::size_t entryBytes = static_cast<std::size_t>(head.count) * sizeof(detail::IndexEntry);
            good = std::memcmp(head.magic, detail::kIndexMagic, sizeof(head.magic)) == 0 && head.version == kSaveVersion &&
                   head.levelVersion == kLevelVersion && head.count <= static_cast<std::uint32_t>(kSlotCount) &&
                   bytes.size() == sizeof(head) + entryBytes &&
                   head.crc == crc32(bytes.data() + sizeof(head), static_cast<std::uint32_t>(entryBytes));
        }
        if (!good)
        {
            saveIndexRebuild(card, index);
            return;
        }
        index.clear();
        index.nextSerial = head.nextSerial;
        for (std::uint32_t e = 0; e < head.count; ++e)
        {
            detail::IndexEntry entry;
            std::memcpy(&entry, bytes.data() + sizeof(head) + e * sizeof(entry), sizeof(entry));
            if (entry.slot >= kSlotCount || entry.half > 1)
                continue;
            SaveSlot &slot = index.slots[entry.slot];
            slot.status = static_cast<int>(entry.status);
            slot.heading = entry.heading;
            slot.heading.map[sizeof(slot.heading.map) - 1] = 0;
            slot.heading.chapter[sizeof(slot.heading.chapter) - 1] = 0;
            index.half[entry.slot] = static_cast<std::uint8_t>(entry.half);
        }
        // A slot is believed only if the card agrees with the index: its file
        // there, and none it does not list; nothing left by a version before;
        // and its other file not a save written after the index was (cut off
        // before the index could say so: the newest there is, if it is whole).
        // Any other is made again from its files.
        bool changed = false;
        if (recover)
        {
            std::vector<std::uint8_t> on;
            const bool canList = detail::slotsOnCard(card, on);
            for (int i = 0; i < kSlotCount; ++i)
            {
                const bool listed = index.slots[i].status != kSaveNone;
                const int mine = index.half[i], other = 1 - mine;
                bool fine = true;
                if (canList)
                    fine = !(on[i] & detail::kOnLegacy) && (listed ? (on[i] & (1 << mine)) != 0 : (on[i] & 3) == 0);
                else
                    fine = !detail::exists(card, detail::legacyNew(i).c_str()) &&
                           !detail::exists(card, detail::legacyOld(i).c_str()) &&
                           (!listed || detail::exists(card, saveHalfFile(i, mine).c_str()));
                if (fine && listed && (!canList || (on[i] & (1 << other)) != 0))
                {
                    const SaveSlot after = detail::fileHeading(card, saveHalfFile(i, other).c_str());
                    fine = after.status != kSaveGood || after.heading.serial < head.nextSerial;
                }
                if (!fine)
                {
                    detail::relist(card, index, i);
                    changed = true;
                }
            }
        }
        for (int i = 0; i < kSlotCount; ++i)
        {
            SaveSlot &slot = index.slots[i];
            if (slot.status == kSaveOther && slot.version == 0)
            {
                std::vector<std::uint8_t> headBytes;
                if (card.read(saveHalfFile(i, index.half[i]).c_str(), headBytes, kSaveHeadBytes))
                    detail::noteHeader(headBytes, slot);
            }
        }
        if (changed)
            saveIndexWrite(card, index);
    }

    /// What a save's card work is done in, a step at a time: small, as a
    /// step is done in the frame, the game waiting on the card meanwhile.
    inline constexpr std::size_t kSaveStepBytes = 1024;

    /// A part of a save's state that is not held in memory, but made as it is
    /// written (writeSaveSplit): the map's state, straight to the card.
    struct SaveMiddle
    {
        using Sink = bool (*)(void *context, const std::uint8_t *bytes, std::size_t size);
        std::uint32_t bytes = 0;
        void *context = nullptr;
        /// All of its bytes, in order, through `sink`; the same each time it
        /// is asked. False: it could not be made.
        bool (*write)(void *context, Sink sink, void *sinkContext) = nullptr;
    };

    /// A save written into a slot: into the file of its two not holding its
    /// save, which stays the slot's until the new one is whole on the card
    /// (and read back to be the same), and the index says so. The heading is
    /// given its kind and its number here. One step() does one piece of the
    /// card's work -- a kilobyte written or read -- so a game can write a save
    /// over a few frames and go on being played. Cut off anywhere, or failed,
    /// the save there was is still there.
    class SaveWriter
    {
    public:
        enum class Result
        {
            Busy,
            Done,
            Failed
        };

        /// False (and nothing begun) if the save cannot be made at all.
        /// `run`: the maps left, kept on the card, to be in the save after
        /// the rest -- copied in from their files, or (`byRef`) only named.
        bool begin(SaveCard &card, SaveIndex &index, int slot, const SaveData &data, SaveHeading heading,
                   const RunIndex *run = nullptr, bool byRef = false)
        {
            if (!settle(card, index, slot, heading))
                return false;
            bytes_ = writeSave(data, heading);
            if (bytes_.size() > kSaveMaxBytes + kSaveHeadBytes)
            {
                failedAt_ = "too big";
                letGo();
                return false;
            }
            middle_ = SaveMiddle();
            tail_.clear();
            return start(card, index, slot, heading, run, byRef, bytes_);
        }

        /// The same, with `middle` -- the state's part not in `data.state`,
        /// at `middleAt` in it -- made as it is written, a piece at a time:
        /// the save is never in memory whole (writeSaveSplit). Its bytes are
        /// the same. To be finished at once (finish()): `middle` is made
        /// from the game as it is, which must not change in between.
        bool beginSplit(SaveCard &card, SaveIndex &index, int slot, const SaveData &data, std::size_t middleAt,
                        const SaveMiddle &middle, SaveHeading heading, const RunIndex *run = nullptr,
                        bool byRef = false)
        {
            if (!settle(card, index, slot, heading))
                return false;
            SaveSplit split = writeSaveSplit(data, heading, middleAt, middle.bytes);
            if (split.front.empty() || middle.write == nullptr ||
                split.front.size() - kSaveHeadBytes + middle.bytes + split.back.size() > kSaveMaxBytes)
            {
                failedAt_ = split.front.empty() || middle.write == nullptr ? "split" : "too big";
                letGo();
                return false;
            }
            // Its CRC, and the payload's, once through.
            Counted counted{saveSplitCrcStart(split), kCrcStart, 0};
            if (!middle.write(middle.context, &Counted::take, &counted) || counted.bytes != middle.bytes)
            {
                failedAt_ = "state";
                letGo();
                return false;
            }
            saveSplitSeal(split, counted.chain);
            middleCrc_ = crc32Finish(counted.alone);
            middle_ = middle;
            bytes_ = std::move(split.front);
            tail_ = std::move(split.back);
            return start(card, index, slot, heading, run, byRef, tail_);
        }

    private:
        /// What may stop a save of `slot` before anything is written; the
        /// heading made this save's.
        bool settle(SaveCard &card, SaveIndex &index, int slot, SaveHeading &heading)
        {
            stage_ = Stage::Idle;
            failedAt_ = "begin";
            if (slot < 0 || slot >= kSlotCount)
                return false;
            // (One of the player's the list has as empty, with a save there
            // after all -- a list the card would not let be brought up to
            // date: that save is not written over. The list learns of it.)
            if (slot >= kSlotManual && index.slots[slot].status == kSaveNone &&
                (detail::exists(card, saveHalfFile(slot, 0).c_str()) || detail::exists(card, saveHalfFile(slot, 1).c_str())))
            {
                index.slots[slot] = saveRecover(card, slot, index.half[slot]);
                if (index.slots[slot].status != kSaveNone)
                {
                    failedAt_ = "taken";
                    return false;
                }
            }
            heading.kind = saveSlotKind(slot);
            heading.serial = index.nextSerial;
            return true;
        }

        /// The save's last part (`last`) followed by the maps left: a heading
        /// and their table; their files after, copied a piece at a time
        /// (Copy). Then begun.
        bool start(SaveCard &card, SaveIndex &index, int slot, const SaveHeading &heading, const RunIndex *run,
                   bool byRef, std::vector<std::uint8_t> &last)
        {
            files_.clear();
            VisitedHead head{};
            std::memcpy(head.magic, kVisitedMagic, 4);
            head.flags = byRef ? kVisitedByRef : 0;
            if (run != nullptr)
            {
                head.count = static_cast<std::uint16_t>(run->count);
                head.runId = run->runId;
                head.generation = run->generation;
                for (int k = 0; k < run->count; ++k)
                    files_.push_back(run->entries[k]);
            }
            const std::size_t tableBytes = files_.size() * sizeof(RunEntry);
            std::size_t fileBytes = 0;
            for (const RunEntry &entry : files_)
                fileBytes += entry.bytes;
            head.bytes = static_cast<std::uint32_t>(tableBytes + (byRef ? 0 : fileBytes));
            head.tableCrc = crc32(reinterpret_cast<const std::uint8_t *>(files_.data()), static_cast<std::uint32_t>(tableBytes));
            const std::size_t at = last.size();
            last.resize(at + sizeof(head) + tableBytes);
            std::memcpy(last.data() + at, &head, sizeof(head));
            if (tableBytes != 0)
                std::memcpy(last.data() + at + sizeof(head), files_.data(), tableBytes);
            if (byRef)
                files_.clear();
            card_ = &card;
            index_ = &index;
            slot_ = slot;
            heading_ = heading;
            had_ = index.slots[slot].status != kSaveNone;
            half_ = had_ ? static_cast<std::uint8_t>(1 - index.half[slot]) : 0;
            fresh_ = saveHalfFile(slot, half_);
            at_ = 0;
            stage_ = Stage::Write;
            return true;
        }

        /// The middle's bytes counted: its CRC alone, and the payload's
        /// going on through it.
        struct Counted
        {
            std::uint32_t chain, alone, bytes;
            static bool take(void *context, const std::uint8_t *data, std::size_t size)
            {
                auto *c = static_cast<Counted *>(context);
                c->chain = crc32Update(c->chain, data, size);
                c->alone = crc32Update(c->alone, data, size);
                c->bytes += static_cast<std::uint32_t>(size);
                return true;
            }
        };
        /// ... and written to the save's file.
        struct ToCard
        {
            SaveCard *card;
            const char *name;
            std::uint32_t bytes;
            static bool take(void *context, const std::uint8_t *data, std::size_t size)
            {
                auto *c = static_cast<ToCard *>(context);
                c->bytes += static_cast<std::uint32_t>(size);
                return c->card->append(c->name, data, size, false);
            }
        };

    public:
        bool busy() const { return stage_ != Stage::Idle; }
        int slot() const { return slot_; }
        /// Where the last write that failed stopped ("write", "read back" ...).
        const char *failedAt() const { return failedAt_; }

        Result step()
        {
            switch (stage_)
            {
            case Stage::Idle:
                return Result::Failed;
            case Stage::Write:
            {
                const std::size_t n = piece();
                if (!card_->append(fresh_.c_str(), bytes_.data() + at_, n, at_ == 0))
                    return fail("write");
                at_ += n;
                if (at_ == bytes_.size())
                {
                    stage_ = middle_.write != nullptr ? Stage::WriteMiddle : (files_.empty() ? Stage::Close : Stage::Copy);
                    file_ = 0;
                    fileAt_ = 0;
                }
                return Result::Busy;
            }
            case Stage::WriteMiddle:
            {
                // (All of it in one step: it is made from the game as it is.)
                ToCard to{card_, fresh_.c_str(), 0};
                if (!middle_.write(middle_.context, &ToCard::take, &to) || to.bytes != middle_.bytes)
                    return fail("write state");
                at_ = 0;
                stage_ = Stage::WriteTail;
                return Result::Busy;
            }
            case Stage::WriteTail:
            {
                const std::size_t left = tail_.size() - at_;
                const std::size_t n = left < kSaveStepBytes ? left : kSaveStepBytes;
                if (!card_->append(fresh_.c_str(), tail_.data() + at_, n, false))
                    return fail("write");
                at_ += n;
                if (at_ == tail_.size())
                {
                    stage_ = files_.empty() ? Stage::Close : Stage::Copy;
                    file_ = 0;
                    fileAt_ = 0;
                }
                return Result::Busy;
            }
            case Stage::Copy:
            {
                // A piece of a map left's file, from the run's folder into
                // the save.
                const RunEntry &entry = files_[file_];
                char name[48];
                runFileOf(entry.name, name);
                const std::size_t left = entry.bytes - fileAt_;
                const std::size_t n = left < kSaveStepBytes ? left : kSaveStepBytes;
                if (!card_->readAt(name, fileAt_, n, piece_) || piece_.size() != n)
                    return fail("read map");
                if (!card_->append(fresh_.c_str(), piece_.data(), n, false))
                    return fail("write map");
                fileAt_ += n;
                if (fileAt_ == entry.bytes)
                {
                    card_->finish(name);
                    fileAt_ = 0;
                    if (++file_ == files_.size())
                        stage_ = Stage::Close;
                }
                return Result::Busy;
            }
            case Stage::Close:
                if (!card_->finish(fresh_.c_str()))
                    return fail("close");
                at_ = 0;
                stage_ = Stage::Check;
                return Result::Busy;
            case Stage::Check:
            {
                // (Read back: what is on the card is what was meant to be --
                // and no longer.)
                const std::size_t n = piece();
                std::vector<std::uint8_t> back;
                const bool last = at_ + n == bytes_.size() && files_.empty() && middle_.write == nullptr;
                if (!card_->readAt(fresh_.c_str(), at_, n + (last ? 1 : 0), back))
                    return fail("read back");
                if (back.size() != n || std::memcmp(back.data(), bytes_.data() + at_, n) != 0)
                    return fail("not as written");
                at_ += n;
                if (at_ == bytes_.size())
                {
                    if (middle_.write != nullptr)
                    {
                        stage_ = Stage::CheckMiddle;
                        return Result::Busy;
                    }
                    stage_ = files_.empty() ? Stage::Commit : Stage::CheckFiles;
                    file_ = 0;
                    fileAt_ = at_;
                    if (files_.empty())
                        card_->finish(fresh_.c_str());
                }
                return Result::Busy;
            }
            case Stage::CheckMiddle:
            {
                // (By its CRC: it is not kept to be compared with.)
                std::uint32_t crc = 0;
                if (!cardCrc(*card_, fresh_.c_str(), bytes_.size(), middle_.bytes, piece_, crc) || crc != middleCrc_)
                    return fail("state read back");
                at_ = 0;
                stage_ = Stage::CheckTail;
                return Result::Busy;
            }
            case Stage::CheckTail:
            {
                const std::size_t left = tail_.size() - at_;
                const std::size_t n = left < kSaveStepBytes ? left : kSaveStepBytes;
                const std::size_t from = bytes_.size() + middle_.bytes + at_;
                std::vector<std::uint8_t> back;
                const bool last = at_ + n == tail_.size() && files_.empty();
                if (!card_->readAt(fresh_.c_str(), from, n + (last ? 1 : 0), back))
                    return fail("read back");
                if (back.size() != n || std::memcmp(back.data(), tail_.data() + at_, n) != 0)
                    return fail("not as written");
                at_ += n;
                if (at_ == tail_.size())
                {
                    stage_ = files_.empty() ? Stage::Commit : Stage::CheckFiles;
                    file_ = 0;
                    fileAt_ = bytes_.size() + middle_.bytes + tail_.size();
                    if (files_.empty())
                        card_->finish(fresh_.c_str());
                }
                return Result::Busy;
            }
            case Stage::CheckFiles:
            {
                // (Each file copied in is as it was: its own CRC.)
                const RunEntry &entry = files_[file_];
                std::uint32_t crc = 0;
                if (entry.bytes < 8 || !cardCrc(*card_, fresh_.c_str(), fileAt_, entry.bytes - 8, piece_, crc) ||
                    crc != entry.crc)
                    return fail("map read back");
                fileAt_ += entry.bytes;
                if (++file_ == files_.size())
                {
                    // (... and nothing after the last.)
                    std::vector<std::uint8_t> more;
                    if (!card_->readAt(fresh_.c_str(), fileAt_, 1, more) || !more.empty())
                        return fail("map read back");
                    card_->finish(fresh_.c_str());
                    stage_ = Stage::Commit;
                }
                return Result::Busy;
            }
            case Stage::Commit:
                // (Nothing renamed or removed: the index says which file is
                // the slot's. Cut off before it is written, the start after
                // finds the newer file by its number.)
                stage_ = Stage::Idle;
                index_->slots[slot_].status = kSaveGood;
                index_->slots[slot_].heading = heading_;
                index_->half[slot_] = half_;
                ++index_->nextSerial;
                saveIndexWrite(*card_, *index_); // (failed, the list is made again at the next start)
                letGo();
                return Result::Done;
            }
            return Result::Failed;
        }

        /// Every step that is left, now.
        bool finish()
        {
            Result r = Result::Busy;
            while (busy() && (r = step()) == Result::Busy)
            {
            }
            return r == Result::Done;
        }

    private:
        enum class Stage
        {
            Idle,
            Write,
            WriteMiddle,
            WriteTail,
            Copy,
            Close,
            Check,
            CheckMiddle,
            CheckTail,
            CheckFiles,
            Commit
        };

        std::size_t piece() const
        {
            const std::size_t left = bytes_.size() - at_;
            return left < kSaveStepBytes ? left : kSaveStepBytes;
        }
        /// (Before the commit: the file written is not the slot's, and is
        /// written over by its next save -- gone only if the slot has none, not
        /// to look like one.)
        Result fail(const char *where)
        {
            failedAt_ = where;
            card_->finish(fresh_.c_str());
            if (!had_)
                card_->remove(fresh_.c_str());
            stage_ = Stage::Idle;
            letGo();
            return Result::Failed;
        }
        /// Every block it took, given back: none of it is to stay in the
        /// middle of a DS's heap.
        void letGo()
        {
            std::vector<std::uint8_t>().swap(bytes_);
            std::vector<std::uint8_t>().swap(tail_);
            middle_ = SaveMiddle();
            std::vector<RunEntry>().swap(files_);
            std::vector<std::uint8_t>().swap(piece_);
            std::string().swap(fresh_);
        }

        Stage stage_ = Stage::Idle;
        const char *failedAt_ = "";
        SaveCard *card_ = nullptr;
        SaveIndex *index_ = nullptr;
        int slot_ = -1;
        SaveHeading heading_;
        bool had_ = false;       ///< the slot had a save (in the other file)
        std::uint8_t half_ = 0;  ///< the file of the slot's two written into
        std::string fresh_;      ///< its name
        std::vector<std::uint8_t> bytes_; ///< all of the save; or, split, its front
        std::vector<std::uint8_t> tail_;  ///< split: what is after the middle
        SaveMiddle middle_;               ///< split: the part made as it is written
        std::uint32_t middleCrc_ = 0;
        std::size_t at_ = 0;
        std::vector<RunEntry> files_; ///< the maps left whose files are copied in
        std::size_t file_ = 0, fileAt_ = 0;
        std::vector<std::uint8_t> piece_;
    };

    /// The same, all at once. False if the card would not take it -- the
    /// old save is then still there.
    inline bool saveSlotWrite(SaveCard &card, SaveIndex &index, int slot, const SaveData &data, SaveHeading heading,
                              const RunIndex *run = nullptr, bool byRef = false)
    {
        SaveWriter writer;
        return writer.begin(card, index, slot, data, heading, run, byRef) && writer.finish();
    }

    namespace detail
    {
        inline bool readSaveFile(SaveCard &card, const char *name, SaveData &out, SaveHeading *heading)
        {
            std::vector<std::uint8_t> bytes;
            if (!card.read(name, bytes, kSaveHeadBytes) || bytes.size() != kSaveHeadBytes)
                return false;
            SaveHeader header;
            std::memcpy(&header, bytes.data(), sizeof(header));
            if (header.payloadBytes > kSaveMaxBytes || !card.read(name, bytes, kSaveHeadBytes + header.payloadBytes) ||
                !readSave(bytes.data(), bytes.size(), out, heading))
                return false;
            VisitedHead head;
            std::vector<std::uint8_t> scratch;
            if (saveTrailer(card, name, kSaveHeadBytes + header.payloadBytes, head, out.visited, scratch))
            {
                out.visitedAt = static_cast<std::uint32_t>(kSaveHeadBytes + header.payloadBytes + sizeof(head) +
                                                           out.visited.size() * sizeof(RunEntry));
                out.visitedByRef = (head.flags & kVisitedByRef) != 0;
                out.visitedRunId = head.runId;
                out.visitedGeneration = head.generation;
            }
            card.finish(name);
            return true;
        }
    } // namespace detail

    /// The save in `slot`: its payload read and checked; and of its trailer,
    /// the table of the maps left it holds (their files stay on the card,
    /// for runFromSave: HlMapStore.hpp). From the file of its two `half` (the
    /// list's: SaveIndex::half); -1, the newer of them that reads.
    inline bool saveSlotRead(SaveCard &card, int slot, SaveData &out, SaveHeading *heading = nullptr, int half = -1)
    {
        if (slot < 0 || slot >= kSlotCount)
            return false;
        if (half < 0)
        {
            const SaveSlot first = detail::fileHeading(card, saveHalfFile(slot, 0).c_str());
            const SaveSlot second = detail::fileHeading(card, saveHalfFile(slot, 1).c_str());
            half = second.status == kSaveGood && (first.status != kSaveGood || second.heading.serial > first.heading.serial)
                       ? 1
                       : 0;
            if (saveSlotRead(card, slot, out, heading, half))
                return true;
            half = 1 - half;
        }
        if (!detail::readSaveFile(card, saveHalfFile(slot, half).c_str(), out, heading))
            return false;
        out.half = static_cast<std::uint8_t>(half);
        return true;
    }

    /// The most recent save that can be loaded: of those shown, or with
    /// `arrival` of all of them. -1: none.
    inline int saveMostRecent(const SaveIndex &index, bool arrival = false)
    {
        int best = -1;
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (i == kSlotArrival && !arrival)
                continue;
            if (index.slots[i].status == kSaveGood &&
                (best < 0 || index.slots[i].heading.serial > index.slots[best].heading.serial))
                best = i;
        }
        return best;
    }

    /// The saves the player is shown, the most recent first (those that
    /// cannot be read after the rest): to load from, every one but the
    /// arrival; to save into, only their own.
    inline void saveListed(const SaveIndex &index, bool loading, std::vector<int> &out)
    {
        out.clear();
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (i == kSlotArrival || index.slots[i].status == kSaveNone || (!loading && i < kSlotManual))
                continue;
            out.push_back(i);
        }
        auto later = [&](int a, int b)
        {
            const bool goodA = index.slots[a].status == kSaveGood, goodB = index.slots[b].status == kSaveGood;
            if (goodA != goodB)
                return goodA;
            return index.slots[a].heading.serial != index.slots[b].heading.serial
                       ? index.slots[a].heading.serial > index.slots[b].heading.serial
                       : a < b;
        };
        for (std::size_t i = 1; i < out.size(); ++i)
        {
            for (std::size_t k = i; k > 0 && later(out[k], out[k - 1]); --k)
                std::swap(out[k], out[k - 1]);
        }
    }

    /// Where a new save of the player's goes: the first of theirs with
    /// nothing in it. -1: all 99 are taken.
    inline int saveFreeManual(const SaveIndex &index)
    {
        for (int i = kSlotManual; i < kSlotCount; ++i)
        {
            if (index.slots[i].status == kSaveNone)
                return i;
        }
        return -1;
    }

    /// One of the player's saves gone: its files off the card, and out of the
    /// list. False if it is not theirs, or its file would not go.
    inline bool saveSlotForget(SaveCard &card, SaveIndex &index, int slot)
    {
        card.remove(detail::legacyNew(slot).c_str());
        card.remove(detail::legacyOld(slot).c_str());
        // (The file that is not the slot's first: refused half-way, the
        // slot's save is still there.)
        const int mine = index.half[slot];
        for (int half : {1 - mine, mine})
        {
            const std::string name = saveHalfFile(slot, half);
            if (detail::exists(card, name.c_str()) && !card.remove(name.c_str()))
                return false;
        }
        index.slots[slot] = SaveSlot();
        index.half[slot] = 0;
        saveIndexWrite(card, index);
        return true;
    }

    /// One of the player's saves gone (the game's own are not theirs to delete).
    inline bool saveSlotDelete(SaveCard &card, SaveIndex &index, int slot)
    {
        return slot >= kSlotManual && slot < kSlotCount && saveSlotForget(card, index, slot);
    }

    /// A line for the list: how long was played, the map, when (if the
    /// console's clock said), the chapter -- "1:02 c1a1 12/10 14:05
    /// UNFORESEEN CONSEQUENCES" -- as much of it as there is.
    inline std::string saveLine(const SaveSlot &slot)
    {
        if (slot.status == kSaveNone)
            return "EMPTY";
        if (slot.status == kSaveOther)
            return slot.version != 0 ? saveRefusal(slot.version, slot.minReader, slot.levelVersion)
                                     : "FROM ANOTHER VERSION";
        if (slot.status == kSaveBad)
            return "DAMAGED";
        const SaveHeading &h = slot.heading;
        char text[112];
        const unsigned hours = h.playSeconds / 3600, minutes = h.playSeconds / 60 % 60;
        if (h.year != 0)
            std::snprintf(text, sizeof(text), "%u:%02u %s %02u/%02u %02u:%02u %s", hours, minutes, h.map, h.day, h.month,
                          h.hour, h.minute, h.chapter);
        else
            std::snprintf(text, sizeof(text), "%u:%02u %s %s", hours, minutes, h.map, h.chapter);
        return text;
    }
} // namespace hldata
