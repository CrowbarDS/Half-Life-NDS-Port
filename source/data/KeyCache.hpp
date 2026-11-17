#pragma once

// A map's animation keys, read from the card as a clip is played, for the
// models that have theirs apart (HlModel.hpp's kModelKeysApart): a DS short
// of memory for a map's models whole has each file without its keys, and
// the keys beside it, mdl/<name>.hmk, never compressed.
//
// A pose needs its clip's keys only while it is worked out (poseModel asks
// through hldata::modelKeysOf, and is done with them when it returns), so
// one block holds what has been read lately, a clip's keys together, taken
// in turn round the block: what the next clip needs is let go of first. A
// clip not here is read at once, in the frame that wants it (what a DS
// gives for the room), as the level's geometry pages are.
//
// Its block is the size HlDsRoom.hpp's dsKeyCacheBytes says, which the
// converter counted on: at least the map's largest clip, so any one clip
// always fits. Made with the map's models, gone with them; nothing taken
// from the heap in play. Only the main thread poses models (the thinking
// worker never does), so nothing is let go of while it is read.

#include <cstdint>

#include "HlModel.hpp"

#ifndef HLDS_CHILD
#include <NEAMain.h>
#endif

namespace hlrt
{
    class KeyCache
    {
    public:
        /// The tables and the block for those of the map's models
        /// (`views`, `count` of them, ModelCache's, by ModelRef) whose keys
        /// are apart. False if there was no memory for them: those models
        /// are then not posed (poseModel leaves them as they were).
        bool create(const hldata::ModelView *views, int count);
        void destroy();

        /// What poseModel asks: clip `clip`'s keys, indexed as the file's
        /// own; null if they cannot be had.
        const hldata::ModelKey *keysOf(const hldata::ModelView &model, int clip);

        /// What it holds of the heap: the block and its tables.
        std::uint32_t bytes() const { return bytes_; }
        /// The models with their keys apart.
        int models() const { return modelCount_; }
        /// Clips read from the card since the map began, and their bytes;
        /// reads that failed (the clip was not posed).
        std::uint32_t reads() const { return reads_; }
        std::uint32_t readBytes() const { return readBytes_; }
        std::uint32_t failed() const { return failed_; }

    private:
#ifndef HLDS_CHILD
        struct Model
        {
            const hldata::ModelHeader *header = nullptr; ///< null: its keys are not to be had
            NEA_NpacMember member{};
            std::uint32_t firstClip = 0; ///< into clips_
        };
        struct Clip
        {
            std::uint32_t firstKey = 0;
            std::uint32_t keyCount = 0;
            std::int32_t at = -1; ///< where in the block, in bytes; -1: not here
        };
        bool load(Model &model, Clip &clip);

        Model *models_ = nullptr;
        Clip *clips_ = nullptr;
        std::uint8_t *block_ = nullptr;
        std::uint32_t clipCount_ = 0;
        std::uint32_t blockBytes_ = 0;
        std::uint32_t head_ = 0; ///< where the next clip read goes
        int last_ = -1;          ///< the model asked for last
#endif
        int modelCount_ = 0;
        std::uint32_t bytes_ = 0;
        std::uint32_t reads_ = 0;
        std::uint32_t readBytes_ = 0;
        std::uint32_t failed_ = 0;
    };

    extern KeyCache keyCache;
} // namespace hlrt
