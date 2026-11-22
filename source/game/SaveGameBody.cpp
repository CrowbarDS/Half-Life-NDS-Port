// The saves' work: in the save overlay (build.py's OVERLAYS), with the
// shared files' code it brings in (HlSaveFiles.hpp, HlMapStore.hpp), read in
// by the front end and single play -- a match between consoles keeps no saves,
// and the region is its screens' then. The program calls it only through
// SaveGame.cpp's gates.

#include "global_header.hpp"

#ifndef HLDS_CHILD

#include "game/SaveGame.hpp"

#include <cstring>

#include "Global.hpp"
#include "Guard.hpp"
#include "audio/Sound.hpp"
#include "game/Game.hpp"
#include "game/SaveGameCard.hpp"

namespace hlrt::SaveGame::work
{
    using namespace detail;

    namespace
    {
        void need()
        {
            if (listed || !global.hasFat)
                return;
            hldata::saveIndexLoad(card, list);
            listed = true;
        }

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
        const bool stored = hldata::runStore(card, run, name.c_str(), level, bytes, game.leftWith(), piece,
                                             [](hldata::detail::StreamOut &out) { game.storeMap(out); });
        // (Its file shut now: opened while the map was still in memory, its
        // blocks went into the map's last holes, and would part the heap the
        // next map's level is read into, on a DS.)
        const bool shut = card.close();
        return stored && shut;
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
        // and each makes those again.) The card's writing holds up the ARM7
        // on a DSi: the sound plays straight from its channels for that long
        // (Sound::holdPipeline).
        sound.holdPipeline(true);
        const bool wrote = writer.begin(card, list, slot, save, heading, &run, namesFolder(slot)) && writer.finish();
        sound.holdPipeline(false);
        return wrote;
    }

    bool writeGame(const std::string &map, int slot, const hldata::SaveHeading &heading)
    {
        if (!global.hasFat)
            return false;
        guard::check("save");
        need();
        finish();
        needRun();
        std::size_t middleAt = 0;
        std::uint32_t middleBytes = 0;
        const hldata::SaveData save = game.snapshotSplit(map, middleAt, middleBytes);
        hldata::SaveMiddle middle;
        middle.bytes = middleBytes;
        middle.context = &game;
        middle.write = [](void *g, hldata::SaveMiddle::Sink sink, void *sinkContext) {
            return static_cast<Game *>(g)->streamState(sink, sinkContext);
        };
        sound.holdPipeline(true);
        const bool wrote = writer.beginSplit(card, list, slot, save, middleAt, middle, heading, &run, namesFolder(slot)) &&
                           writer.finish();
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
} // namespace hlrt::SaveGame::work

#endif
