#include "data/LevelFile.hpp"
#include "data/CardRead.hpp"

#include "DebugLog.hpp"
#include "Global.hpp"
#include "global_header.hpp"
#include "net/GuestLoad.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <utility>

#include "platform/Slot2.hpp"

namespace hlrt
{
    Level level;

    void Level::unload()
    {
        view = hldata::LevelView();
        std::free(block_);
        block_ = nullptr;
        size_ = 0;
#ifndef HLDS_CHILD
        // (What is in the Pak is let go with the map: slot2::reset.)
        for (const std::uint8_t *&at : inPak_)
            at = nullptr;
        pakBytes_ = 0;
#endif
    }

#ifndef HLDS_CHILD
    std::size_t Level::keepInPak()
    {
        using hldata::Section;
        if (slot2::pakBytes() == 0 || block_ == nullptr)
            return 0;
        // Read least: the visibility (a leaf changed, a monster looking),
        // the light patches (a light switched, a tenth of a second of a
        // flicker), what is said and written on the screen, the sounds of
        // the models' moves, the map's names. (The geometry stream is drawn
        // from every frame, and written to: it stays.)
        constexpr Section kAway[] = {Section::Vis,       Section::LightPatch,    Section::Messages,
                                     Section::Sentences, Section::SentenceWords, Section::Speech,
                                     Section::ModelSounds, Section::Landmarks,   Section::Strings};
        auto *dir = reinterpret_cast<hldata::SectionDir *>(block_ + sizeof(hldata::LevelHeader));
        std::size_t moved = 0;
        for (Section s : kAway)
        {
            const hldata::SectionDir &d = dir[static_cast<int>(s)];
            if (d.size < 256)
                continue; // (not worth it)
            std::uint8_t *at = slot2::take(d.size);
            if (at == nullptr)
                break;
            slot2::write(at, block_ + d.offset, d.size);
            inPak_[static_cast<int>(s)] = at;
            moved += d.size;
        }
        if (moved == 0)
            return 0;
        // The rest packed together, in the order it was (each section after
        // the last one kept, on a word): only ever moved down.
        int order[hldata::kSectionCount];
        for (int i = 0; i < hldata::kSectionCount; ++i)
            order[i] = i;
        for (int i = 1; i < hldata::kSectionCount; ++i)
        {
            for (int k = i; k > 0 && dir[order[k]].offset < dir[order[k - 1]].offset; --k)
                std::swap(order[k], order[k - 1]);
        }
        std::uint32_t end = hldata::kLevelDataStart;
        for (int i : order)
        {
            if (inPak_[i] != nullptr)
                continue;
            hldata::SectionDir &d = dir[i];
            if (d.offset != end)
                std::memmove(block_ + end, block_ + d.offset, d.size);
            d.offset = end;
            end += (d.size + 3) & ~3u;
        }
        // (Shrunk where it is: it is the last thing on the heap, and what it
        // gives back goes back to the top.)
        if (auto *smaller = static_cast<std::uint8_t *>(std::realloc(block_, end)))
            block_ = smaller;
        malloc_trim(0);
        size_ = end;
        return moved;
    }
#endif

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

    bool Level::readFiles(hldata::LevelHeader &header, bool &got, std::string &error)
    {
        // (Not in an archive -- sent over the air, or packed in a way that
        // can't be undone as it is read: as files, a piece at a time.)
        std::FILE *file = std::fopen(global.mapPath("level.0").c_str(), "rb");
        got = file != nullptr && std::fread(&header, sizeof(header), 1, file) == 1;
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
            return true;
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
        return true;
    }

    bool Level::load(std::string &error)
    {
        unload();

        // The first piece says how long the level is: the card's word, so
        // it is checked before it is trusted with an allocation. No cooked
        // map is anywhere near 8 MB. (The piece is read once, header first:
        // a map sent over the air comes once, and is not read again.)
        hldata::LevelHeader header{};
        bool got = false;
#ifndef HLDS_CHILD
        // In the map's archive: each piece read straight to its place in the
        // level's block (undone as it is read, if packed), with no file
        // opened and no copy of it beside the block. (A packed piece opened
        // as a file is undone whole into a block of its own first: as big as
        // a piece, it left a hole behind it every map.)
        NEA_NpacMember member;
        if (NEA_NpacMemberFind(global.mapPath("level.0").c_str(), &member) == 0 && cardMemberReadable(member))
        {
            // (Only the first piece's size is known before its header is
            // read: the whole of it, here. Small: the header and the
            // sections' list.)
            alignas(4) static std::uint8_t first[hldata::kLevelDataStart];
            got = member.size == sizeof(first) && cardReadMember(member, 0, first, sizeof(first));
            std::memcpy(&header, first, sizeof(header));
            const std::uint32_t size = header.fileSize;
            if (got && (size < hldata::kLevelDataStart || size > 8 * 1024 * 1024))
            {
                error = "Map file is damaged. Convert again.";
                return false;
            }
            if (got)
            {
                block_ = static_cast<std::uint8_t *>(std::malloc(size));
                if (block_ == nullptr)
                {
                    error = "Not enough memory for this map.";
                    return false;
                }
                std::memcpy(block_, first, sizeof(first));
                for (std::uint32_t piece = 1; got && piece < hldata::levelPieces(size); ++piece)
                {
                    char name[16];
                    std::snprintf(name, sizeof(name), "level.%u", static_cast<unsigned>(piece));
                    const std::uint32_t bytes = hldata::levelPieceBytes(size, piece);
                    got = NEA_NpacMemberFind(global.mapPath(name).c_str(), &member) == 0 &&
                          cardMemberReadable(member) && member.size == bytes &&
                          cardReadMember(member, 0, block_ + hldata::levelPieceStart(piece), bytes);
                }
                if (!got)
                {
                    unload();
                    error = "Map could not be read from the card.";
                    return false;
                }
            }
        }
        else
#endif
        if (!readFiles(header, got, error))
            return false;
        if (!got)
        {
            unload();
            error = "Map not found on the card.";
            return false;
        }
        const std::uint32_t size = header.fileSize;

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
#else
        // An Expansion Pak: what is read least, there, and the room it took
        // here given back. (Looked at again where it now is: the checksum
        // was of the file as read.)
        if ((pakBytes_ = keepInPak()) != 0 &&
            view.open(block_, size, false, inPak_) != hldata::LevelError::Ok)
        {
            unload();
            error = "Map file is damaged. Convert again.";
            return false;
        }
#endif
        return true;
    }
} // namespace hlrt
