#pragma once

// The "air:" drive, read-only, for a console that was sent the game and has
// no card (the Download Play child, HLDS_CHILD). The game opens its files on
// it as it opens the card's (Global's paths are "air:/..." there), and a
// file is one of two things:
//
//  - a member of the guest stream being received (HlGuest.hpp): the map's.
//    Opened, it is the next member to come, by its name, and a read undoes
//    the stream's next bytes straight into the reader's own buffer -- the
//    level's block, a model's, a sound's. Nothing of the stream is kept: a
//    member is read once, in the order the stream has them, which is the
//    order the game loads them in. One asked for out of turn is not there;
//    those of a kind no longer asked for are passed over.
//  - a file put here whole (put()): what was received at the start, which
//    the game reads in its own order (the letters, the display's pictures),
//    let go of once it has (dropOwned()); and a test's debug_start.txt.

#include <cstdint>

#include "HlGuest.hpp"

namespace hlrt::air
{
    /// The guest stream the drive's files now come out of (its header read
    /// already: GuestCursor::start()); null for none.
    void stream(hldata::GuestCursor *cursor);
    /// The files put here that own their memory (putOwned), let go of.
    void dropOwned();
    /// The rest of the map's stream gone through, what is kept packed of it
    /// (kGuestKeepPacked: the weapons lying about) kept as files of the map's.
    void drain();
    /// The map's files (drain()'s) let go of: a new map is coming.
    void dropMapFiles();
    /// A file of the map's, as drain()'s (who another player chose to be),
    /// and those let go of once read.
    bool putMapFile(const char *name, std::uint8_t *data, std::uint32_t size);
    void dropCharacters();
    /// `name`'s whole bytes, owned by the drive (freed by dropOwned()).
    bool putOwned(const char *name, std::uint8_t *data, std::uint32_t size);

    /// The drive, once (a second call does nothing).
    bool install();
    /// `name` (no drive, no leading slash) is the `size` bytes at `data`,
    /// which must stay where they are for as long as it is put. Again, the
    /// same name: in the place of the last. False if there is no room.
    bool put(const char *name, const std::uint8_t *data, std::uint32_t size);
    /// Whether `name` is there.
    bool has(const char *name);
    /// A demo's map's bank (HlAirBank.hpp), whole and checked, at `data`
    /// (which stays put while it is mounted): its members are files too, after
    /// those put here. Null: none.
    void mountBank(const std::uint8_t *data, std::uint32_t size);
    bool bankMounted();
    /// A member of it stored as it is, on a 4-byte boundary: where it lies.
    bool bankMember(const char *name, const std::uint8_t *&data, std::uint32_t &size);
    /// Where `name`, put here as it is (not packed), lies in memory.
    bool memoryOf(const char *name, const std::uint8_t *&data, std::uint32_t &size);
    /// `name`, put here, is no longer.
    void forget(const char *name);
    /// The files there, by number (0 .. count()-1).
    int count();
    const char *name(int index);
} // namespace hlrt::air
