// The saves on the card (shared/HlSaveFiles.hpp): the game's three and as
// many of the player's as they make (up to 99), and a list of them; a save
// written so that being cut off at any point loses nothing; the list made
// again when it cannot be believed (from a listing of the folder, where the
// card has one); the most recent found; one of the player's deleted.
// Against a "card" in memory that can be told to fail at any step.

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>

#include "HlSaveFiles.hpp"
#include "TestBspBuilder.hpp"

using namespace hldata;
using namespace hldstest;

namespace
{
    /// A folder in memory. `budget`: how many more steps (a write, a remove,
    /// a rename) it will do before it "loses power": the step it stops at
    /// is half done if it is a write (half the bytes), not done otherwise,
    /// and nothing after it happens. -1: it never stops.
    struct MemoryCard : SaveCard
    {
        std::map<std::string, std::vector<std::uint8_t>> files;
        int budget = -1;
        int steps = 0;
        bool spend()
        {
            ++steps;
            if (budget < 0)
                return true;
            if (budget == 0)
                return false;
            --budget;
            return true;
        }
        bool read(const char *name, std::vector<std::uint8_t> &out, std::size_t most) override
        {
            const auto at = files.find(name);
            if (at == files.end())
                return false;
            out.assign(at->second.begin(), at->second.begin() + std::min(most, at->second.size()));
            return true;
        }
        bool write(const char *name, const std::uint8_t *bytes, std::size_t size) override
        {
            if (!spend())
            {
                if (budget == 0)
                {
                    files[name].assign(bytes, bytes + size / 2); // (cut off half-way)
                    budget = -2;                                 // (... and dead from here on)
                }
                return false;
            }
            files[name].assign(bytes, bytes + size);
            return true;
        }
        bool remove(const char *name) override
        {
            if (budget == -2 || !spend())
            {
                budget = -2;
                return false;
            }
            return files.erase(name) != 0;
        }
        bool rename(const char *from, const char *to) override
        {
            if (budget == -2 || !spend())
            {
                budget = -2;
                return false;
            }
            const auto at = files.find(from);
            if (at == files.end() || files.count(to) != 0)
                return false;
            files[to] = at->second;
            files.erase(at);
            return true;
        }
    };

    /// ... and one that can list itself, as the console's can.
    struct ListingCard : MemoryCard
    {
        int listed = 0;
        bool list(std::vector<std::string> &names) override
        {
            ++listed;
            names.clear();
            for (const auto &file : files)
                names.push_back(file.first);
            return true;
        }
    };

    SaveData game(const char *map, int health)
    {
        SaveData data;
        data.map = map;
        data.health = health;
        data.state.assign(3000, static_cast<std::uint8_t>(health)); // (as a map's state would fill it)
        return data;
    }
    SaveHeading heading(const char *map, const char *chapter, unsigned seconds)
    {
        SaveHeading h;
        std::snprintf(h.map, sizeof(h.map), "%s", map);
        std::snprintf(h.chapter, sizeof(h.chapter), "%s", chapter);
        h.playSeconds = seconds;
        return h;
    }
    /// The player's saves, as many as they make: their files, the list
    /// newest first, the hundredth refused, one deleted, the list read
    /// back from the index and made again from a listing.
    void testMany()
    {
        check(std::string(saveSlotFile(kSlotManual)) == "save1.sav" &&
                  std::string(saveSlotFile(kSlotManual + 5)) == "save6.sav" &&
                  std::string(saveSlotFile(kSlotCount - 1)) == "save99.sav" && saveSlotFile(kSlotCount)[0] == 0,
              "the player's saves are save1.sav to save99.sav (the six of before among them)");
        check(saveSlotOfFile("save12.sav") == kSlotManual + 11 && saveSlotOfFile("arrival.sav") == kSlotArrival &&
                  saveSlotOfFile("save0.sav") < 0 && saveSlotOfFile("save100.sav") < 0 &&
                  saveSlotOfFile("save01.sav") < 0 && saveSlotOfFile("index.dat") < 0,
              "a file's name says which save it is, and nothing else is one");

        ListingCard card;
        SaveIndex index;
        saveIndexLoad(card, index);
        int made = 0;
        for (int n = 0; n < kSlotManuals; ++n)
        {
            const int slot = saveFreeManual(index);
            char map[8];
            std::snprintf(map, sizeof(map), "m%d", n);
            if (slot >= 0 && saveSlotWrite(card, index, slot, game(map, 50), heading(map, "CHAPTER", 60 + n)))
                ++made;
        }
        check(made == kSlotManuals && saveFreeManual(index) < 0, "99 saves of the player's, and no room for a hundredth");
        std::vector<int> listed;
        saveListed(index, false, listed);
        check(listed.size() == static_cast<std::size_t>(kSlotManuals) && listed.front() == kSlotCount - 1 &&
                  listed.back() == kSlotManual,
              "listed the most recent first");
        check(saveMostRecent(index) == kSlotCount - 1, "... the most recent is the last made");

        // One deleted: gone from the card and the list, and its place is
        // the next one's.
        check(saveSlotDelete(card, index, kSlotManual + 40) && card.files.count("save41.sav") == 0 &&
                  index.slots[kSlotManual + 40].status == kSaveNone && saveFreeManual(index) == kSlotManual + 40,
              "a save deleted: its file gone, and the first free place is its");
        check(!saveSlotDelete(card, index, kSlotAuto) && card.files.count("auto.sav") == 0,
              "the game's own saves are not the player's to delete");
        saveListed(index, true, listed);
        check(listed.size() == static_cast<std::size_t>(kSlotManuals - 1), "... and it is not in the list");

        // The index read back, as it is; and made again from a listing.
        SaveIndex again;
        saveIndexLoad(card, again);
        bool same = again.nextSerial == index.nextSerial;
        for (int i = 0; i < kSlotCount; ++i)
            same = same && again.slots[i].status == index.slots[i].status &&
                   again.slots[i].heading.serial == index.slots[i].heading.serial;
        check(same, "the list is read back from the index as it was");
        card.files.erase(kSaveIndexFile);
        SaveIndex rebuilt;
        card.listed = 0;
        saveIndexLoad(card, rebuilt);
        same = card.listed > 0;
        for (int i = 0; i < kSlotCount; ++i)
            same = same && rebuilt.slots[i].status == index.slots[i].status;
        check(same && saveMostRecent(rebuilt) == kSlotCount - 1, "... and made again from the files, listed");

        // A save cut off half-way in a high place: put right at the next start.
        SaveIndex during = index;
        card.budget = 1;
        saveSlotWrite(card, during, kSlotManual + 40, game("late", 20), heading("late", "CHAPTER", 9999));
        card.budget = -1;
        SaveIndex after;
        saveIndexLoad(card, after);
        check(card.files.count("save41.sav.new") == 0, "a half-written save41 is put right at the next start");
    }

    /// A card from before: the old index (the nine slots of 'HLSI'), the
    /// player's save1.sav and save6.sav, the arrival. All found, in their
    /// places.
    void testOldCard()
    {
        MemoryCard old;
        SaveIndex was;
        saveIndexLoad(old, was);
        check(saveSlotWrite(old, was, kSlotManual, game("c1a1", 80), heading("c1a1", "UNFORESEEN", 100)) &&
                  saveSlotWrite(old, was, kSlotManual + 5, game("c1a2", 70), heading("c1a2", "OFFICE", 200)) &&
                  saveSlotWrite(old, was, kSlotArrival, game("c1a2", 70), heading("c1a2", "OFFICE", 200)),
              "an old card's saves");
        std::vector<std::uint8_t> oldIndex(880, 0);
        std::memcpy(oldIndex.data(), "HLSI", 4);
        old.files[kSaveIndexFile] = oldIndex;
        SaveIndex now;
        saveIndexLoad(old, now);
        check(now.slots[kSlotManual].status == kSaveGood && now.slots[kSlotManual + 5].status == kSaveGood &&
                  now.slots[kSlotArrival].status == kSaveGood && std::string(now.slots[kSlotManual + 5].heading.map) == "c1a2",
              "an old card's index is made again, and its saves are where they were");
        check(saveMostRecent(now) == kSlotManual + 5 && saveMostRecent(now, true) == kSlotArrival,
              "... the most recent known (the arrival only when asked for)");
    }
} // namespace

int main()
{
    testMany();
    testOldCard();
    // An empty card: an empty list, nothing most recent.
    {
        MemoryCard card;
        SaveIndex index;
        saveIndexLoad(card, index);
        bool empty = true;
        for (const SaveSlot &slot : index.slots)
            empty = empty && slot.status == kSaveNone;
        check(empty && saveMostRecent(index) < 0 && saveLine(index.slots[0]) == "EMPTY", "a new card has no saves");
        check(card.files.count(kSaveIndexFile) == 1, "... and is given its list");
    }

    // Saves made, listed, read back; the most recent known; each kind in
    // its own file.
    MemoryCard card;
    SaveIndex index;
    saveIndexLoad(card, index);
    check(saveSlotWrite(card, index, kSlotAuto, game("c1a0", 90), heading("c1a0", "ANOMALOUS MATERIALS", 300)),
          "the game's own save is written");
    check(saveSlotWrite(card, index, kSlotManual + 2, game("c1a1", 55), heading("c1a1", "UNFORESEEN CONSEQUENCES", 3725)),
          "one of the player's is written");
    check(saveSlotWrite(card, index, kSlotQuick, game("c1a1a", 40), heading("c1a1a", "UNFORESEEN CONSEQUENCES", 4000)),
          "the quick one is written");
    check(card.files.count("auto.sav") && card.files.count("save3.sav") && card.files.count("quick.sav") &&
              !card.files.count("save3.sav.new"),
          "each in its own file, with nothing left beside it");
    check(index.slots[kSlotAuto].heading.kind == kSaveKindAuto && index.slots[kSlotManual + 2].heading.kind == kSaveKindManual &&
              index.slots[kSlotQuick].heading.kind == kSaveKindQuick,
          "each knows what kind it is");
    check(saveMostRecent(index) == kSlotQuick, "the most recent is the last one made");
    check(saveLine(index.slots[kSlotManual + 2]) == "1:02 c1a1 UNFORESEEN CONSEQUENCES", "a line of the list says how long, where, and the chapter");
    {
        SaveSlot dated = index.slots[kSlotManual + 2];
        dated.heading.year = 2026;
        dated.heading.month = 10;
        dated.heading.day = 9;
        dated.heading.hour = 14;
        dated.heading.minute = 5;
        check(saveLine(dated) == "1:02 c1a1 09/10 14:05 UNFORESEEN CONSEQUENCES", "... and when, if the console's clock said");
    }
    {
        SaveData back;
        SaveHeading h;
        check(saveSlotRead(card, kSlotManual + 2, back, &h) && back.map == "c1a1" && back.health == 55 &&
                  back.state.size() == 3000 && h.playSeconds == 3725,
              "a save read back is what was written, its map's state with it");
        check(!saveSlotRead(card, kSlotManual, back), "an empty slot reads as nothing");
    }
    // The save on coming into a map is there to go back to, and not shown.
    check(saveSlotWrite(card, index, kSlotArrival, game("c1a2", 77), heading("c1a2", "OFFICE COMPLEX", 4100)) &&
              saveMostRecent(index) == kSlotQuick && saveMostRecent(index, true) == kSlotArrival,
          "the save made on arriving is the most recent of all, and not among those shown");

    // The list is only a copy: gone, or damaged, it is made again.
    {
        const SaveIndex before = index;
        card.files.erase(kSaveIndexFile);
        SaveIndex again;
        saveIndexLoad(card, again);
        bool same = again.nextSerial == before.nextSerial;
        for (int i = 0; i < kSlotCount; ++i)
            same = same && again.slots[i].status == before.slots[i].status &&
                   again.slots[i].heading.serial == before.slots[i].heading.serial;
        check(same && card.files.count(kSaveIndexFile) == 1, "the list is made again from the files when it is gone");
        card.files[kSaveIndexFile][40] ^= 0x55;
        SaveIndex third;
        saveIndexLoad(card, third);
        check(third.slots[kSlotQuick].status == kSaveGood && third.nextSerial == before.nextSerial,
              "... and when it is damaged");
    }

    // A file that is not a save of this version is said to be what it is.
    {
        MemoryCard other = card;
        other.files["save1.sav"] = other.files["quick.sav"];
        other.files["save1.sav"][4] = static_cast<std::uint8_t>(kSaveMinVersion - 1); // (its version, currently unread)
        other.files["save1.sav"][5] = 0;
        other.files["save2.sav"] = other.files["quick.sav"];
        other.files["save2.sav"][sizeof(SaveHeader) + 9] ^= 1; // (its heading)
        other.files["save4.sav"] = {'n', 'o', 't', ' ', 'o', 'n', 'e'};
        other.files.erase(kSaveIndexFile);
        SaveIndex listed;
        saveIndexLoad(other, listed);
        check(listed.slots[kSlotManual].status == kSaveOther &&
                  saveLine(listed.slots[kSlotManual]) ==
                      "SAVE V" + std::to_string(kSaveMinVersion - 1) + ", NEED V" + std::to_string(kSaveMinVersion) + "+",
              "a save of another version is listed as that, with the numbers");
        // ... and found again from the files when the list is read back from its index.
        SaveIndex again;
        saveIndexLoad(other, again, false);
        check(saveLine(again.slots[kSlotManual]) == saveLine(listed.slots[kSlotManual]),
              "... also from the saved list");
        check(saveRefusal(kSaveVersion, kSaveVersion + 1, kLevelVersion) ==
                  "SAVE NEEDS V" + std::to_string(kSaveVersion + 1) + ", HAVE V" + std::to_string(kSaveVersion),
              "a save that needs a newer game says which");
        check(saveRefusal(kSaveVersion, kSaveMinReader, kLevelSaveSince - 1) ==
                  "MAPS L" + std::to_string(kLevelSaveSince - 1) + ", NEED L" + std::to_string(kLevelSaveSince) + "+",
              "one of maps too old says which");
        check(saveRefusal(kSaveVersion, kSaveMinReader, kLevelVersion + 1) ==
                  "MAPS L" + std::to_string(kLevelVersion + 1) + ", HAVE L" + std::to_string(kLevelVersion),
              "and one of maps too new");
        check(listed.slots[kSlotManual + 1].status == kSaveBad && listed.slots[kSlotManual + 3].status == kSaveNone,
              "a damaged one as damaged; what is no save at all as nothing");
        SaveData back;
        check(!saveSlotRead(other, kSlotManual, back) && !saveSlotRead(other, kSlotManual + 1, back),
              "... and neither is loaded");
    }

    // Cut off at every step of a write: afterwards there is a whole save in
    // the slot -- the old one or the new -- and never neither.
    {
        MemoryCard fresh = card;
        fresh.budget = -1;
        fresh.steps = 0;
        SaveIndex scratch = index;
        saveSlotWrite(fresh, scratch, kSlotQuick, game("c1a3", 12), heading("c1a3", "WE'VE GOT HOSTILES", 5000));
        const int steps = fresh.steps;
        check(steps >= 4, "a write is several steps");
        bool always = true, sometimesOld = false, sometimesNew = false;
        for (int cut = 0; cut <= steps; ++cut)
        {
            MemoryCard torn = card;
            torn.budget = cut;
            SaveIndex during = index;
            saveSlotWrite(torn, during, kSlotQuick, game("c1a3", 12), heading("c1a3", "WE'VE GOT HOSTILES", 5000));
            // The power comes back.
            torn.budget = -1;
            SaveIndex after;
            saveIndexLoad(torn, after);
            SaveData back;
            const bool whole = after.slots[kSlotQuick].status == kSaveGood && saveSlotRead(torn, kSlotQuick, back) &&
                               (back.map == "c1a1a" || back.map == "c1a3") &&
                               std::string(after.slots[kSlotQuick].heading.map) == back.map;
            always = always && whole && torn.files.count("quick.sav.new") == 0;
            sometimesOld = sometimesOld || back.map == "c1a1a";
            sometimesNew = sometimesNew || back.map == "c1a3";
            // (And the others are as they were.)
            always = always && saveSlotRead(torn, kSlotAuto, back) && back.map == "c1a0";
        }
        check(always, "cut off at any step, the slot holds a whole save and the list agrees with it");
        check(sometimesOld && sometimesNew, "... the old one if it was cut off early, the new one if late");
    }

    // A card that will not take it: said, and the old save still there.
    {
        MemoryCard full = card;
        full.budget = 0;
        SaveIndex during = index;
        check(!saveSlotWrite(full, during, kSlotAuto, game("c1a4", 5), heading("c1a4", "BLAST PIT", 6000)),
              "a write the card refuses is said to have failed");
        full.budget = -1;
        SaveIndex after;
        saveIndexLoad(full, after);
        SaveData back;
        check(saveSlotRead(full, kSlotAuto, back) && back.map == "c1a0", "... and the save that was there is there still");
    }

    // A save written a step at a time (as the game's autosave is, over
    // frames): the same files and list as one written at once, in pieces.
    {
        SaveData big = game("c2a1", 61);
        big.state.assign(20000, 7); // (several pieces)
        MemoryCard once = card, stepped = card;
        SaveIndex onceIndex = index, steppedIndex = index;
        check(saveSlotWrite(once, onceIndex, kSlotAuto, big, heading("c2a1", "POWER UP", 7000)), "written at once");
        SaveWriter writer;
        check(writer.begin(stepped, steppedIndex, kSlotAuto, big, heading("c2a1", "POWER UP", 7000)) && writer.busy(),
              "a stepped write begins");
        int steps = 0;
        SaveWriter::Result r = SaveWriter::Result::Busy;
        while ((r = writer.step()) == SaveWriter::Result::Busy)
            ++steps;
        check(r == SaveWriter::Result::Done && !writer.busy(), "... and ends");
        check(steps >= 2 * static_cast<int>(20000 / kSaveStepBytes), "... a piece at a time, written and read back");
        check(once.files == stepped.files, "the card is the same as after a write made at once");
        check(onceIndex.nextSerial == steppedIndex.nextSerial &&
                  std::string(steppedIndex.slots[kSlotAuto].heading.map) == "c2a1",
              "... and so is the list");

        // Stopped between any two steps (the console switched off): the
        // slot holds a whole save, the old or the new.
        bool always = true;
        for (int stop = 0; stop <= steps; ++stop)
        {
            MemoryCard cut = card;
            SaveIndex during = index;
            SaveWriter w;
            w.begin(cut, during, kSlotAuto, big, heading("c2a1", "POWER UP", 7000));
            for (int k = 0; k < stop && w.busy(); ++k)
                w.step();
            SaveIndex after;
            saveIndexLoad(cut, after);
            SaveData back;
            always = always && saveSlotRead(cut, kSlotAuto, back) && (back.map == "c1a0" || back.map == "c2a1") &&
                     cut.files.count("auto.sav.new") == 0;
        }
        check(always, "stopped between any two steps, the slot holds a whole save");
    }

    if (g_failures == 0)
        std::printf("test_savefiles: ok\n");
    return g_failures == 0 ? 0 : 1;
}
