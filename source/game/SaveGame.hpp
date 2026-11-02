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
    /// Read the save in `slot` (a kSlot*), if there is one and it is whole.
    bool read(hldata::SaveData &out, int slot);
    /// Write it. False if the card would not take it: the save that was
    /// there is then there still.
    bool write(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading);
    /// One of the player's saves gone from the card and the list. False if
    /// it is not theirs or would not go.
    bool remove(int slot);

    /// The same, a step at a time (hldata::SaveWriter): begun now, with the
    /// game as it is now, and then step() once a frame until it says Done or
    /// Failed. False if it cannot be begun (no card, or one is being written
    /// already). A write(), a read or leaving the map finishes it first.
    bool begin(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading);
    hldata::SaveWriter::Result step();
    bool busy();
    /// What is left of it, at once (false if it failed).
    bool finish();

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
    const hldata::RunIndex &runIndex();
} // namespace hlrt::SaveGame
