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

#include "HlLevelReader.hpp"

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

        std::uint8_t *block_ = nullptr;
        std::size_t size_ = 0;
    };

    HL_ENGINE_OBJECT(Level, level);
} // namespace hlrt
