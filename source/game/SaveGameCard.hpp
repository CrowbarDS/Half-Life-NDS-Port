#pragma once

// SaveGame's own (not the child's): what its gates, in the program
// (SaveGame.cpp), and its work, in the save overlay (SaveGameBody.cpp, by
// build.py's OVERLAYS), share. The card and the lists stay in the program:
// they outlast the overlay's leaving, and the card's table of functions
// (its vtable) may not point into one (tools/ovl.py, R1).

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "HlMapStore.hpp"
#include "HlSave.hpp"
#include "HlSaveFiles.hpp"

namespace hlrt::SaveGame::detail
{
    /// The saves' folder on the card.
    struct FatCard : hldata::SaveCard
    {
        std::string path(const char *name) const;

        /// The last call the card refused, and what it said (errno): for a
        /// save that failed to say why.
        const char *refused = "";
        int refusedErrno = 0;
        bool no(const char *call);

        // The files being written and read a piece at a time (SaveWriter,
        // the maps left: HlMapStore.hpp), kept open from one piece to the
        // next -- opening one is the slow part: one being written, and one
        // being read (a map left's file copied into a save, a save's out into
        // the run's folder: both at once).
        struct Open
        {
            std::FILE *file = nullptr;
            std::string name;
            bool failed = false;
            /// (Being written:) how long it was before (overwrite).
            long was = 0;
        };
        Open writing_, reading_;
        static bool shut(Open &open);
        /// What is written is kept here and goes to the card in big pieces: a
        /// write costs about the same whatever its size -- tens of ms on a
        /// DSi's SD, some 200, measured 2026-11-27 -- and a save in pieces of
        /// a kilobyte was a hundred and fifty of them, a stall each. Written
        /// when full, and when the file is let go of or read back
        /// (shutWriting); let go of with the file (a block left in the heap is
        /// room a DS's next map may not have). None if there is no room for
        /// it: each piece as it comes, as before.
        static constexpr std::size_t kWriteBufferDs = 8 * 1024;
        static constexpr std::size_t kWriteBufferDsi = 64 * 1024;
        std::uint8_t *buffer_ = nullptr;
        std::size_t buffered_ = 0, bufferBytes_ = 0;
        bool flushWriting();
        bool shutWriting();
        /// A file written over where it is, not made anew: the card's table
        /// of where a file's pieces are is left as it is unless the file grows
        /// or shrinks -- every change to it a write more, as slow as any. Cut
        /// to its new length when let go of (shutWriting).
        std::FILE *overwrite(const char *name, long &was);
        bool cut(std::FILE *file, long was);
        bool close();
        /// The folder `name` is to be in, made if it is not there (it usually
        /// is): the saves', or a folder in it (run/).
        void makeFolder(const char *name) const;

        bool append(const char *name, const std::uint8_t *bytes, std::size_t size, bool first) override;
        bool readAt(const char *name, std::size_t offset, std::size_t size, std::vector<std::uint8_t> &out) override;
        bool finish(const char *name) override;
        bool read(const char *name, std::vector<std::uint8_t> &out, std::size_t most) override;
        bool write(const char *name, const std::uint8_t *bytes, std::size_t size) override;
        bool remove(const char *name) override;
        bool rename(const char *from, const char *to) override;
        /// The files in the saves' folder (one look at the folder, not a
        /// hundred files asked for one by one).
        bool list(std::vector<std::string> &names) override;
    };
    extern FatCard card;
    extern hldata::SaveIndex list;
    extern hldata::SaveWriter writer;
    extern bool listed;

    // The maps left, kept to come back to (HlMapStore.hpp): the index of them,
    // read once; a piece of a file at a time.
    extern hldata::RunIndex run;
    extern bool runKnown;
    extern std::uint8_t piece[hldata::kMapStorePiece];
    /// What a piece is read into: let go of after each use -- a block left in
    /// the middle of the heap is one the next map's level may not have room
    /// for, on a DS.
    extern std::vector<std::uint8_t> scratch;
    /// dropNamedAuto's save, not on the stack: it holds vectors of vectors.
    extern hldata::SaveData held;
} // namespace hlrt::SaveGame::detail

namespace hlrt::SaveGame::work
{
    // The work of SaveGame.hpp's calls of the same names, in the save
    // overlay: reached only through their gates (SaveGame.cpp).
    const hldata::SaveIndex &index();
    bool read(hldata::SaveData &out, int slot);
    bool write(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading);
    bool writeGame(const std::string &map, int slot, const hldata::SaveHeading &heading);
    bool remove(int slot);
    bool begin(const hldata::SaveData &save, int slot, const hldata::SaveHeading &heading);
    hldata::SaveWriter::Result step(bool quiet);
    bool finish();
    bool leaveMap(const std::string &name, std::uint32_t level);
    int arriveMap(const std::string &name, std::uint32_t level);
    void landed(const std::string &left, std::uint8_t mask);
    void newRun(std::uint32_t runId);
    bool fromSave(int slot, const hldata::SaveData &save, std::uint32_t runId);
    void dropNamedAuto();
    const hldata::RunIndex &runIndex();
} // namespace hlrt::SaveGame::work
