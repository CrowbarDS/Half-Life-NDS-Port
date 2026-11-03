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
// A save is written to a file of another name beside the old one (.new), and
// takes its place only when it is whole: the old one is renamed aside (.old)
// first and removed last, so that at every moment one whole save is on the
// card under a name the game knows. Nothing is removed before what replaces
// it is in its place -- a card that starts refusing (a worn one) costs the
// saves it refuses, never one it had. Whatever is found half-way at the next
// start is put right then (saveRecover).
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
        /// (Not on the card.) A slot whose last write could not be put in its
        /// place: its files are put right before it is written again.
        bool unsettled[kSlotCount] = {};

        /// As new, in place. (Not `= SaveIndex()`: that is ten kilobytes made
        /// on the stack first, more than a DS's functions may take.)
        void clear()
        {
            for (SaveSlot &slot : slots)
                slot = SaveSlot();
            for (bool &u : unsettled)
                u = false;
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
            std::uint16_t unused;
            std::uint32_t status;
            SaveHeading heading;
        };

        inline std::string freshName(int slot)
        {
            return std::string(saveSlotFile(slot)) + ".new";
        }
        /// The save a slot had, set aside while a new one takes its place.
        inline std::string oldName(int slot)
        {
            return std::string(saveSlotFile(slot)) + ".old";
        }

        /// Which slots have a file, which a new one beside it (half-written,
        /// or not yet in its place), and which an old one set aside, if the
        /// card can list itself; false if it cannot.
        inline bool slotsOnCard(SaveCard &card, std::vector<bool> &has, std::vector<bool> &fresh,
                                std::vector<bool> &aside)
        {
            std::vector<std::string> names;
            if (!card.list(names))
                return false;
            has.assign(kSlotCount, false);
            fresh.assign(kSlotCount, false);
            aside.assign(kSlotCount, false);
            for (std::string &name : names)
            {
                const bool isFresh = name.size() > 4 && name.compare(name.size() - 4, 4, ".new") == 0;
                const bool isOld = name.size() > 4 && name.compare(name.size() - 4, 4, ".old") == 0;
                if (isFresh || isOld)
                    name.resize(name.size() - 4);
                const int slot = saveSlotOfFile(name.c_str());
                if (slot >= 0)
                    (isFresh ? fresh : (isOld ? aside : has))[slot] = true;
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

    /// A slot's new file (whole: checked) put in its place. The save there
    /// is renamed aside first and removed last; if the new one cannot be put
    /// in, the old one is put back. False if the new one is not in its place
    /// -- the old one is then the slot's still, or, if the card would not even
    /// put it back, aside where saveRecover finds it. Never is one removed
    /// before another whole one has its name.
    inline bool savePlaceFresh(SaveCard &card, int slot)
    {
        const char *file = saveSlotFile(slot);
        const std::string fresh = detail::freshName(slot), old = detail::oldName(slot);
        const bool had = detail::exists(card, file);
        if (had)
        {
            // (An old one left aside before, with the slot's own file there:
            // the slot's is the newer. FAT renames over nothing.)
            if (detail::exists(card, old.c_str()) && !card.remove(old.c_str()))
                return false;
            if (!card.rename(file, old.c_str()))
                return false;
        }
        if (!card.rename(fresh.c_str(), file))
        {
            if (had)
                card.rename(old.c_str(), file);
            return false;
        }
        if (had)
            card.remove(old.c_str()); // (failed, it is found and removed at the next start)
        return true;
    }

    /// One slot put right after a write that was cut off or refused, and what
    /// it is now: an old save left aside with nothing in its place put back;
    /// a whole new one put in its place; what is left beside a whole save
    /// removed. Nothing is removed unless the slot has a whole save.
    inline SaveSlot saveRecover(SaveCard &card, int slot)
    {
        SaveSlot out;
        const char *file = saveSlotFile(slot);
        const std::string fresh = detail::freshName(slot), old = detail::oldName(slot);
        std::vector<std::uint8_t> bytes;
        if (!detail::exists(card, file) && detail::exists(card, old.c_str()))
            card.rename(old.c_str(), file);
        if (detail::exists(card, fresh.c_str()))
        {
            if (saveFileWhole(card, fresh.c_str()))
                savePlaceFresh(card, slot);
            else if (saveFileWhole(card, file))
                card.remove(fresh.c_str()); // (half-written: the slot's own is whole)
        }
        if (detail::exists(card, old.c_str()) && saveFileWhole(card, file))
            card.remove(old.c_str());
        bytes.clear();
        if (card.read(file, bytes, kSaveHeadBytes))
        {
            out.status = readSaveHeading(bytes.data(), bytes.size(), out.heading);
            detail::noteHeader(bytes, out);
        }
        return out;
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

    /// The list made again from the files themselves (each put right
    /// first), and written. (Only the files there are, if the card can say
    /// which.)
    inline void saveIndexRebuild(SaveCard &card, SaveIndex &index)
    {
        index.clear();
        std::vector<bool> has, fresh, aside;
        const bool listed = detail::slotsOnCard(card, has, fresh, aside);
        for (int i = 0; i < kSlotCount; ++i)
        {
            if (listed && !has[i] && !fresh[i] && !aside[i])
                continue;
            index.slots[i] = saveRecover(card, i);
            // (A new one the card would not put in its place: kept, and not
            // written over by the next save of the slot.)
            index.unsettled[i] = (!listed || fresh[i]) && detail::exists(card, detail::freshName(i).c_str());
            if (index.slots[i].status == kSaveGood && index.slots[i].heading.serial >= index.nextSerial)
                index.nextSerial = index.slots[i].heading.serial + 1;
        }
        saveIndexWrite(card, index);
    }

    /// The list: from the index if it is there and whole, else from the
    /// files. (Once, at the start: whatever a write left half-way is put
    /// right before the index is believed -- `recover` false to skip that,
    /// for a list asked for again.)
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
        // The index is believed only if the card agrees with it: a file
        // left beside a save (new, or old set aside) means a write was cut
        // off; a save it lists that is not there, or one there it does not
        // list, that it is behind (a write of it the card refused).
        if (good && recover)
        {
            std::vector<bool> listedSlot(kSlotCount, false);
            for (std::uint32_t e = 0; e < head.count; ++e)
            {
                detail::IndexEntry entry;
                std::memcpy(&entry, bytes.data() + sizeof(head) + e * sizeof(entry), sizeof(entry));
                if (entry.slot < kSlotCount && entry.status != static_cast<std::uint32_t>(kSaveNone))
                    listedSlot[entry.slot] = true;
            }
            std::vector<bool> has, fresh, aside;
            if (detail::slotsOnCard(card, has, fresh, aside))
            {
                for (int i = 0; good && i < kSlotCount; ++i)
                    good = !fresh[i] && !aside[i] && has[i] == listedSlot[i];
            }
            else
            {
                for (int i = 0; good && i < kSlotCount; ++i)
                {
                    if (detail::exists(card, detail::freshName(i).c_str()) ||
                        detail::exists(card, detail::oldName(i).c_str()) ||
                        (listedSlot[i] && !detail::exists(card, saveSlotFile(i))))
                        good = false;
                }
            }
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
            if (entry.slot >= kSlotCount)
                continue;
            SaveSlot &slot = index.slots[entry.slot];
            slot.status = static_cast<int>(entry.status);
            slot.heading = entry.heading;
            slot.heading.map[sizeof(slot.heading.map) - 1] = 0;
            slot.heading.chapter[sizeof(slot.heading.chapter) - 1] = 0;
            if (slot.status == kSaveOther)
            {
                std::vector<std::uint8_t> headBytes;
                if (card.read(saveSlotFile(entry.slot), headBytes, kSaveHeadBytes))
                    detail::noteHeader(headBytes, slot);
            }
        }
    }

    /// What a save's card work is done in, a step at a time.
    inline constexpr std::size_t kSaveStepBytes = 4096;

    /// A save written into a slot: beside the old one first, which goes
    /// only when the new one is whole on the card (and read back to be the
    /// same). The heading is given its kind and its number here. One step()
    /// does one piece of the card's work -- a few kilobytes written or read,
    /// a file removed or renamed -- so a game can write a save over a few
    /// frames and go on being played. Cut off anywhere, it leaves what
    /// saveRecover puts right; failed, the old save is still there.
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
            stage_ = Stage::Idle;
            failedAt_ = "begin";
            if (slot < 0 || slot >= kSlotCount)
                return false;
            // A write of this slot the card would not put in its place: put
            // right first. Its new file may be the newest save there is; if it
            // is still there after, it is not written over -- this save is
            // refused instead.
            if (index.unsettled[slot])
            {
                const SaveSlot now = saveRecover(card, slot);
                if (detail::exists(card, detail::freshName(slot).c_str()))
                {
                    failedAt_ = "unsettled";
                    return false;
                }
                index.slots[slot] = now;
                index.unsettled[slot] = false;
            }
            // (One of the player's the list has as empty, with a file there
            // after all -- a list the card would not let be brought up to
            // date: that save is not written over. The list learns of it.)
            if (slot >= kSlotManual && index.slots[slot].status == kSaveNone && detail::exists(card, saveSlotFile(slot)))
            {
                index.slots[slot] = saveRecover(card, slot);
                failedAt_ = "taken";
                return false;
            }
            heading.kind = saveSlotKind(slot);
            heading.serial = index.nextSerial;
            bytes_ = writeSave(data, heading);
            if (bytes_.size() > kSaveMaxBytes + kSaveHeadBytes)
            {
                failedAt_ = "too big";
                letGo();
                return false;
            }
            // The maps left: a heading and their table, here; their files
            // after, copied a piece at a time (Copy).
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
            const std::size_t at = bytes_.size();
            bytes_.resize(at + sizeof(head) + tableBytes);
            std::memcpy(bytes_.data() + at, &head, sizeof(head));
            if (tableBytes != 0)
                std::memcpy(bytes_.data() + at + sizeof(head), files_.data(), tableBytes);
            if (byRef)
                files_.clear();
            card_ = &card;
            index_ = &index;
            slot_ = slot;
            heading_ = heading;
            fresh_ = detail::freshName(slot);
            at_ = 0;
            stage_ = Stage::Write;
            return true;
        }

        bool busy() const { return stage_ != Stage::Idle; }
        int slot() const { return slot_; }
        /// Where the last write that failed stopped ("write", "swap" ...).
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
                const bool last = at_ + n == bytes_.size() && files_.empty();
                if (!card_->readAt(fresh_.c_str(), at_, n + (last ? 1 : 0), back))
                    return fail("read back");
                if (back.size() != n || std::memcmp(back.data(), bytes_.data() + at_, n) != 0)
                    return fail("not as written");
                at_ += n;
                if (at_ == bytes_.size())
                {
                    stage_ = files_.empty() ? Stage::Swap : Stage::CheckFiles;
                    file_ = 0;
                    fileAt_ = at_;
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
                    stage_ = Stage::Swap;
                }
                return Result::Busy;
            }
            case Stage::Swap:
                stage_ = Stage::Idle;
                if (!savePlaceFresh(*card_, slot_))
                {
                    // (The new one is whole, beside the old: neither is
                    // removed. Put right before this slot is written again,
                    // or at the next start.)
                    index_->unsettled[slot_] = true;
                    failedAt_ = "swap";
                    letGo();
                    return Result::Failed;
                }
                index_->slots[slot_].status = kSaveGood;
                index_->slots[slot_].heading = heading_;
                ++index_->nextSerial;
                saveIndexWrite(*card_, *index_); // (only a copy: a failure here costs a rebuild)
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
            Copy,
            Close,
            Check,
            CheckFiles,
            Swap
        };

        std::size_t piece() const
        {
            const std::size_t left = bytes_.size() - at_;
            return left < kSaveStepBytes ? left : kSaveStepBytes;
        }
        /// (Before the swap: the new file is not whole, and goes.)
        Result fail(const char *where)
        {
            failedAt_ = where;
            card_->finish(fresh_.c_str());
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
        std::string fresh_;
        std::vector<std::uint8_t> bytes_;
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

    /// The save in `slot`: its payload read and checked; and of its trailer,
    /// the table of the maps left it holds (their files stay on the card,
    /// for runFromSave: HlMapStore.hpp).
    inline bool saveSlotRead(SaveCard &card, int slot, SaveData &out, SaveHeading *heading = nullptr)
    {
        if (slot < 0 || slot >= kSlotCount)
            return false;
        std::vector<std::uint8_t> bytes;
        if (!card.read(saveSlotFile(slot), bytes, kSaveHeadBytes) || bytes.size() != kSaveHeadBytes)
            return false;
        SaveHeader header;
        std::memcpy(&header, bytes.data(), sizeof(header));
        if (header.payloadBytes > kSaveMaxBytes ||
            !card.read(saveSlotFile(slot), bytes, kSaveHeadBytes + header.payloadBytes) ||
            !readSave(bytes.data(), bytes.size(), out, heading))
            return false;
        VisitedHead head;
        std::vector<std::uint8_t> scratch;
        if (saveTrailer(card, saveSlotFile(slot), kSaveHeadBytes + header.payloadBytes, head, out.visited, scratch))
        {
            out.visitedAt = static_cast<std::uint32_t>(kSaveHeadBytes + header.payloadBytes + sizeof(head) +
                                                       out.visited.size() * sizeof(RunEntry));
            out.visitedByRef = (head.flags & kVisitedByRef) != 0;
            out.visitedRunId = head.runId;
            out.visitedGeneration = head.generation;
        }
        card.finish(saveSlotFile(slot));
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

    /// One of the player's saves gone: its file (and any left half-written)
    /// off the card, and out of the list. False if it is not theirs, or its
    /// file would not go.
    inline bool saveSlotForget(SaveCard &card, SaveIndex &index, int slot)
    {
        std::vector<std::uint8_t> probe;
        card.remove(detail::freshName(slot).c_str());
        card.remove(detail::oldName(slot).c_str());
        if (card.read(saveSlotFile(slot), probe, 1) && !card.remove(saveSlotFile(slot)))
            return false;
        index.slots[slot] = SaveSlot();
        index.unsettled[slot] = false;
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
