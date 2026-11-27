#pragma once

// The saves on the card: the files themselves. Which files there are, how
// one is written safely and the list of them are shared/HlSaveFiles.hpp's
// business (and run in the host tests); what is IN a save is HlSave.hpp's,
// HlSaveState.hpp's and Game's. This is only "a folder on the card".

#include <string>

#include "HlMapStore.hpp"
#include "HlSaveFiles.hpp"

namespace hlrt::SaveGame
{
    /// The list of saves: read once (what a write left half-way is put
    /// right then), and kept up as saves are made.
    const hldata::SaveIndex &index();
#ifndef HLDS_CHILD
    /// Another game chosen: its saves are in another folder, listed afresh.
    void forget();
    /// A flashcart's driver set up as the OPTIONS' DLDI mode asks: call once,
    /// after the settings are read; they are told what came of it
    /// (Global::dldiStart says it).
    void chooseDldiMode();
#endif
    /// The save thread's stack (a step-at-a-time save is written on a
    /// thread of its own where the card's work is not the ARM9's): where it
    /// is broken, if its guard words are (nullptr: whole); how deep it has
    /// been (profiling builds; else 0).
    const void *stackOverflowed();
    std::uint32_t stackPeak();
    /// Read the save in `slot` (a kSlot*), if there is one and it is whole.
    bool read(hldata::SaveData &out, int slot);
    /// Write it. False if the card would not take it: the save that was
    /// there is then there still.
    bool write(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading);
    /// The game as it is now, saved as the map `map`, at once. On a card,
    /// the map's own state is made as it is written (Game::snapshotSplit):
    /// a save never holds 100 KB and more of the heap at once, which a DS's,
    /// full after a map's load, has not got in one piece (2026-11-16: the
    /// heap trace of c1a3a).
    bool writeGame(const std::string &map, int slot, const hldata::SaveHeading &heading);
    /// One of the player's saves gone from the card and the list. False if
    /// it is not theirs or would not go.
    bool remove(int slot);

    /// The same, a step at a time (hldata::SaveWriter): begun now, with the
    /// game as it is now, and then step() once a frame until it says Done or
    /// Failed. False if it cannot be begun (no card, or one is being written
    /// already). A write(), a read or leaving the map finishes it first.
    bool begin(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading);
    /// `quiet`: the game is at a moment a stop would not spoil (paused, no
    /// fighting for a while). Where the card's writes stop the game
    /// (Global::cardOnArm9), the last of the save waits for one.
    hldata::SaveWriter::Result step(bool quiet = true);
    bool busy();
    /// The save thread is in one of the card's calls: what the frame reads of
    /// the card now waits for it (the music leaves it alone while it can).
    bool cardInUse();
    /// What is left of it, at once (false if it failed).
    bool finish();
    /// Why the last write failed, in a few words: where it stopped, and the
    /// card's last refusal ("swap, rename 5"). Asked once: forgotten after.
    const char *whyFailed();

    // The maps left, kept to come back to (HlMapStore.hpp), in the saves'
    // folder (run/).

    /// The map being left (`name`, cooked as `level`) kept, as the game has
    /// it now: call after the game's makeArrival, before the map is let go.
    bool leaveMap(const std::string &name, std::uint32_t level);
    /// The map just begun, laid back as it was left, if it was kept (and it
    /// is no longer, after): 1 laid; 0 not kept, or not of this map as it is
    /// cooked (nothing touched); -1 the card failed part of the way, and the
    /// map is to be begun again.
    int arriveMap(const std::string &name, std::uint32_t level);
    /// Which of those who went along with the player from `left` came into
    /// the map just entered (Game::placed): they are taken out of it when
    /// it is come back to.
    void landed(const std::string &left, std::uint8_t mask);
    /// No maps kept: a run of the game begun (a new game).
    void newRun(std::uint32_t runId);
    /// The maps kept as the save in `slot` (read: `save`) had them: call
    /// before loading its map (HlMapStore.hpp's runFromSave). True if that
    /// changed the folder: the saves that only name it name what has gone.
    bool fromSave(int slot, const hldata::SaveData &save, std::uint32_t runId);
    /// The game's own save off the card, if it only names the folder: for
    /// when the folder changes and it is not to be made again.
    void dropNamedAuto();
    const hldata::RunIndex &runIndex();
} // namespace hlrt::SaveGame
