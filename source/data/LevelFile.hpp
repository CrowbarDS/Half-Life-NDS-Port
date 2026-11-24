#pragma once

#include "EngineObject.hpp"

// The cooked level in memory.
//
// A level is one heap block: load() reads its pieces out of the map's archive
// (HlLevel.hpp: "level.0", "level.1"...), one after another into their places,
// and hands the bytes to LevelView::open (shared/HlLevelReader.hpp), which
// checks them and points into them. Nothing is parsed or copied after that,
// and unload() is one free.
//
// Only one level is ever loaded; `level` is it.

#include <string>

#include "HlDsCut.hpp"
#include "HlLevelReader.hpp"

#ifndef HLDS_CHILD
#include <NEAMain.h>
#endif

namespace hlrt
{
    class Level
    {
    public:
        hldata::LevelView view;

        /// Replace the loaded level with the one in the archive that is
        /// mounted (Global::mountMap). On failure nothing is loaded and
        /// `error` holds a line fit to show the player.
        bool load(std::string &error);
        void unload();

        /// Read just a level's header out of map `name`'s archive, without
        /// loading it or disturbing the map that is mounted. False if the
        /// archive is not there or is not of a level of this version; the
        /// full load is what reports why.
        static bool peekHeader(const std::string &name, hldata::LevelHeader &header);
        bool loaded() const { return block_ != nullptr; }
        /// How long the last load spent checking what it read (a build that
        /// measures only: HLDS_PROFILE).
        std::uint32_t checkMs = 0;
        std::size_t sizeBytes() const { return size_; }

        Level() = default;
        Level(const Level &) = delete;
        Level &operator=(const Level &) = delete;

    private:
        /// The level read as files (load's way when it is not in an
        /// archive): false, with `error`, if it was found but could not be
        /// had; `got`, whether it was found.
        bool readFiles(hldata::LevelHeader &header, bool &got, std::string &error);
#ifndef HLDS_CHILD
        /// With an Expansion Pak: the sections read least -- a few times a
        /// second at most -- copied into it and the rest of the block packed
        /// together and shrunk, its tail given back to the heap; not the
        /// light patches when the cut (`cutFlags`: dsCutHeader's) pages the
        /// geometry, which lets them go. How many bytes went (0 with no Pak).
        std::size_t keepInPak(std::uint16_t cutFlags);
        /// Where each section went in the Pak, or null.
        const std::uint8_t *inPak_[hldata::kSectionCount] = {};
        /// The sections marked away in inPak_ gone, the rest packed together
        /// and the block shrunk.
        void packTogether();
        /// A DS's cut of this level (ds.cut): its header and member, false
        /// if there is none (a map that fits) or it is not of this level.
        bool dsCutHeader(NEA_NpacMember &member, hldata::DsCutHeader &cut) const;
        /// A DS, on a map too big for its heap (HlDsCut.hpp): what the
        /// converter said it may leave out, out -- with a Pak too, which
        /// keeps instead what it took (the visibility, the light patches
        /// while the geometry stays whole). False if nothing is (a DSi, a
        /// map that fits, or a cut not of this level).
        bool applyDsCut();
        /// Whether what applyDsCut set out to read from the card has its room.
        bool dsCutReady() const;
        NEA_NpacMember visMember_{};
        std::uint8_t *visRow_ = nullptr;
        std::uint32_t visRowCap_ = 0;
        std::uint32_t visRowBytes_ = 0;
        int visRowLeaf_ = -1;
        std::uint16_t cutFlags_ = 0;
        // The geometry read a page at a time (kDsCutGx): where each page
        // starts, which slot holds it (kNoSlot: none), and each slot's page,
        // its words and when it was last drawn from.
        static constexpr std::uint16_t kNoSlot = 0xFFFF;
        NEA_NpacMember gxMember_{};
        std::uint32_t *pageStart_ = nullptr;
        std::uint16_t *pageSlot_ = nullptr;
        std::uint16_t *slotPage_ = nullptr;
        std::uint32_t *slotUsed_ = nullptr;
        std::uint32_t *slotWords_ = nullptr;
        std::uint32_t pageCount_ = 0, slotCount_ = 0, slotSize_ = 0, gxUse_ = 0;
        std::uint32_t pagesRead_ = 0;
        /// The words from `ofs` on, their page read in if it was not.
        const std::uint32_t *pagedGx(std::uint32_t ofs);
        void freeDsCut();
        /// A DSi's finer runs of some surfaces (HlDsiGx.hpp): read with the
        /// level, and those surfaces made to name them -- a gxOfs of
        /// `fineFrom_` and on is a word of `fine_`, not of the level's.
        void readDsiFine();
        std::uint8_t *fineBlock_ = nullptr;
        const std::uint32_t *fine_ = nullptr;
        std::uint32_t fineFrom_ = 0xFFFFFFFFu;
        std::uint16_t fineCount_ = 0;
    public:
        /// A surface's geometry words: in the level, or a DSi's finer run of
        /// it, or (a DS reading them from the card) in the slot its page was
        /// read into -- valid until the next call.
        const std::uint32_t *gxAt(const hldata::Surface &surface)
        {
            if (surface.gxOfs >= fineFrom_)
                return fine_ + (surface.gxOfs - fineFrom_);
            return view.gxStream != nullptr ? view.gxStream + surface.gxOfs : pagedGx(surface.gxOfs);
        }
        /// How many surfaces a DSi draws from its finer runs on this map.
        std::uint16_t fineSurfaces() const { return fineCount_; }
        /// Pages of geometry read from the card since the map began.
        std::uint32_t gxPagesRead() const { return pagesRead_; }
        /// Bytes of the level kept in the Expansion Pak, not in main memory.
        std::size_t pakBytes() const { return pakBytes_; }
        /// What of the level a DS left out (HlDsCut.hpp's flags).
        std::uint16_t dsCut() const { return cutFlags_; }
        /// The visibility row of `leaf` (its leaves seen, run-length as the
        /// BSP has them), and in `end` where it may be read to: in the level,
        /// or read from the card on a DS that left them there (the last
        /// leaf's kept). Null if there is none to be had.
        const std::uint8_t *visRow(int leaf, const std::uint8_t *&end);
    private:
        std::size_t pakBytes_ = 0;
#else
    public:
        std::size_t pakBytes() const { return 0; }
        std::uint16_t dsCut() const { return 0; }
        const std::uint32_t *gxAt(const hldata::Surface &surface) { return view.gxStream + surface.gxOfs; }
        std::uint32_t gxPagesRead() const { return 0; }
        const std::uint8_t *visRow(int leaf, const std::uint8_t *&end)
        {
            end = view.vis + view.visSize;
            return view.vis + view.leaves[leaf].visOfs;
        }
    private:
#endif

        std::uint8_t *block_ = nullptr;
        std::size_t size_ = 0;
    };

    HL_ENGINE_OBJECT(Level, level);
} // namespace hlrt
