#include "data/LevelFile.hpp"
#include "data/CardRead.hpp"

#include "DebugLog.hpp"
#include "Global.hpp"
#include "global_header.hpp"
#include "net/GuestLoad.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hlrt
{
    Level level;

    void Level::unload()
    {
        view = hldata::LevelView();
        std::free(block_);
        block_ = nullptr;
        size_ = 0;
    }

    bool Level::peekHeader(const std::string &name, hldata::LevelHeader &header)
    {
#ifdef HLDS_CHILD
        // (Sent the game, the level is read once, as it comes: there is no
        // looking at it first. Whether it was sent at all is what is known.)
        header = hldata::LevelHeader();
        std::memcpy(header.magic, hldata::kLevelMagic, sizeof(hldata::kLevelMagic));
        header.version = hldata::kLevelVersion;
        header.minReader = hldata::kLevelMinReader;
        return guest::offered(name);
#endif
        // (Read from its archive without mounting it: the name looked up a
        // little at a time, nothing of the index kept. The map being played
        // stays mounted, and may be this very one.)
        const bool got = global.hasData && NEA_NpacPeek(global.mapArchive(name).c_str(), "level.0", 0, &header,
                                                        sizeof(header)) == 0;
        return got &&
               std::memcmp(header.magic, hldata::kLevelMagic, sizeof(hldata::kLevelMagic)) == 0 &&
               hldata::levelVersionOk(header.version, header.minReader);
    }

    bool Level::load(std::string &error)
    {
        unload();

        // The first piece says how long the level is: the card's word, so
        // it is checked before it is trusted with an allocation. No cooked
        // map is anywhere near 8 MB. (The piece is read once, header first:
        // a map sent over the air comes once, and is not read again.)
        hldata::LevelHeader header{};
        std::FILE *file = std::fopen(global.mapPath("level.0").c_str(), "rb");
        bool got = file != nullptr && std::fread(&header, sizeof(header), 1, file) == 1;
        const std::uint32_t size = header.fileSize;
        if (got && (size < hldata::kLevelDataStart || size > 8 * 1024 * 1024))
        {
            std::fclose(file);
            error = "Map file is damaged. Convert again.";
            return false;
        }
        if (got)
        {
            block_ = static_cast<std::uint8_t *>(std::malloc(size));
            if (block_ == nullptr)
            {
                std::fclose(file);
                error = "Not enough memory for this map.";
                return false;
            }
            std::memcpy(block_, &header, sizeof(header));
            const std::uint32_t rest = hldata::levelPieceBytes(size, 0) - sizeof(header);
            got = cardRead(block_ + sizeof(header), rest, file) == rest;
        }
        if (file != nullptr)
            std::fclose(file);
        if (!got)
        {
            unload();
            error = "Map not found on the card.";
            return false;
        }
        // Piece by piece, each to its place. (A compressed piece is inflated
        // by the archive as it is opened, beside this block, and let go
        // when it is closed: that is why a level is in pieces.)
        for (std::uint32_t piece = 1; piece < hldata::levelPieces(size); ++piece)
        {
            char name[16];
            std::snprintf(name, sizeof(name), "level.%u", static_cast<unsigned>(piece));
            const std::uint32_t bytes = hldata::levelPieceBytes(size, piece);
            file = std::fopen(global.mapPath(name).c_str(), "rb");
            const bool read = file != nullptr &&
                              cardRead(block_ + hldata::levelPieceStart(piece), bytes, file) == bytes;
            if (file != nullptr)
                std::fclose(file);
            if (!read)
            {
                unload();
                error = "Map could not be read from the card.";
                return false;
            }
        }

#ifdef HLDS_PROFILE
        const std::uint32_t before = DebugLog::Perf::now();
#endif
        const hldata::LevelError result = view.open(block_, size);
#ifdef HLDS_PROFILE
        checkMs = static_cast<std::uint32_t>((std::uint64_t(DebugLog::Perf::now() - before) * 2u) / 67000u);
#endif
        if (result != hldata::LevelError::Ok)
        {
            unload();
            if (result == hldata::LevelError::BadVersion)
            {
                char text[96];
                std::snprintf(text, sizeof(text), "Map is format %u (needs %u to %u). Convert again.",
                              static_cast<unsigned>(header.version), static_cast<unsigned>(hldata::kLevelMinVersion),
                              static_cast<unsigned>(hldata::kLevelVersion));
                error = text;
            }
            else
            {
                error = "Map file is damaged. Convert again.";
            }
            return false;
        }
        size_ = size;
#ifdef HLDS_CHILD
        guest::count(hldata::GuestKind::Level, size);
#endif
        return true;
    }
} // namespace hlrt
