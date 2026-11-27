// The saves' work: in the save overlay (build.py's OVERLAYS), with the
// shared files' code it brings in (HlSaveFiles.hpp, HlMapStore.hpp), read in
// by the front end and single play -- a match between consoles keeps no saves,
// and the region is its screens' then. The program calls it only through
// SaveGame.cpp's gates.

#include "global_header.hpp"

#ifndef HLDS_CHILD

#include "game/SaveGame.hpp"

#include <cstring>
#include <vector>

#include <fcntl.h>
#include <nds/arm9/dldi.h>
#include <unistd.h>

#include "Global.hpp"
#include "Settings.hpp"
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

    namespace
    {
        /// A file written and read back in the DLDI mode just set: 4 KB, the
        /// same coming back, and gone after. (Read back from the card
        /// itself: libnds keeps only the card's tables, not what is written.)
        bool dldiWorks(const std::string &dir)
        {
            const std::string path = dir + "dldi.tst";
            constexpr std::size_t kBytes = 4096;
            std::vector<std::uint8_t> out(kBytes), back(kBytes, 0);
            for (std::size_t i = 0; i < kBytes; ++i)
                out[i] = static_cast<std::uint8_t>(i * 37 + (i >> 8) * 101 + 5);
            int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0)
                return false;
            bool ok = ::write(fd, out.data(), kBytes) == static_cast<ssize_t>(kBytes);
            ok = ::close(fd) == 0 && ok;
            if (ok)
            {
                fd = ::open(path.c_str(), O_RDONLY);
                ok = fd >= 0 && ::read(fd, back.data(), kBytes) == static_cast<ssize_t>(kBytes);
                if (fd >= 0)
                    ::close(fd);
            }
            ::unlink(path.c_str());
            return ok && back == out;
        }
    } // namespace

    // The OPTIONS' DLDI mode (dldi.dat, beside the settings): the driver moved to the
    // CPU asked for (libnds's fatDldiSetMode, our patch: files open stay open,
    // the card is the same), and tested there the first time. SELECT held at
    // the start, or a mode that does not work: AUTO, as the driver says, and
    // the setting put back to it. (A card that is not a flashcart's: nothing.
    // Here, in the saves' overlay, as the card's work: it costs a DS's heap
    // nothing.)
    void chooseDldiMode()
    {
        if (!global.hasFat || global.fatDevice == nullptr || std::strncmp(global.fatDevice, "fat", 3) != 0)
            return;
        {
            std::uint8_t dldi[4] = {};
            const int fd = ::open(global.dataPath("dldi.dat").c_str(), O_RDONLY);
            if (fd < 0)
                return; // (none: AUTO)
            const bool read = ::read(fd, dldi, sizeof(dldi)) == static_cast<ssize_t>(sizeof(dldi));
            ::close(fd);
            if (!read || dldi[0] != 'D' || dldi[1] != 'L' || dldi[2] >= Settings::kDldiChoices ||
                dldi[3] >= Settings::kDldiChoices)
                return;
            settings.dldiMode = dldi[2];
            settings.dldiTested = dldi[3];
            Settings::dldiOnCard(dldi[2], dldi[3]);
        }
        if (settings.dldiMode == Settings::kDldiAuto)
            return;
        scanKeys();
        if (keysHeld() & KEY_SELECT)
            global.dldiStart = Global::DldiStart::Select;
        else
        {
            const int mode = settings.dldiMode == Settings::kDldiArm9 ? DLDI_MODE_ARM9 : DLDI_MODE_ARM7;
            // (Where AUTO put it already, it has just read the settings: it works.)
            if (dldiGetMode() != mode)
            {
                if (!fatDldiSetMode(mode))
                    global.dldiStart = Global::DldiStart::Refused;
                else if (settings.dldiTested != settings.dldiMode && !dldiWorks(global.dataDir()))
                {
                    fatDldiSetMode(DLDI_MODE_AUTODETECT);
                    global.dldiStart = Global::DldiStart::TestFailed;
                }
            }
        }
        if (global.dldiStart != Global::DldiStart::AsAsked)
        {
            settings.dldiMode = Settings::kDldiAuto;
            settings.dldiTested = Settings::kDldiAuto;
            settings.save();
        }
        else if (settings.dldiTested != settings.dldiMode)
        {
            settings.dldiTested = settings.dldiMode;
            settings.save();
        }
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
        if (hldata::saveSlotRead(card, hldata::kSlotAuto, held, nullptr, list.half[hldata::kSlotAuto]) &&
            held.visitedByRef)
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
        // (The file the list has as the slot's: the other may hold a save
        // that failed half-way, with a higher number.)
        const int half = slot >= 0 && slot < hldata::kSlotCount && list.slots[slot].status != hldata::kSaveNone
                             ? list.half[slot]
                             : -1;
        return hldata::saveSlotRead(card, slot, out, nullptr, half);
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

    hldata::SaveWriter::Result step(bool quiet)
    {
        writer.hold(!quiet && global.cardOnArm9());
        return writer.step();
    }

    bool finish()
    {
        // (One on the save thread: waited for, never stepped here as well.)
        const bool threadDone = waitThread();
        if (!writer.busy())
            return threadDone;
        // (All that is left in one go: as write's.)
        sound.holdPipeline(true);
        const bool done = writer.finish();
        sound.holdPipeline(false);
        return done;
    }
} // namespace hlrt::SaveGame::work

#endif
