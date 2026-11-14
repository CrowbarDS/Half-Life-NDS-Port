#include "data/LevelFile.hpp"
#include "data/CardRead.hpp"

#include "DebugLog.hpp"
#include "Global.hpp"
#include "global_header.hpp"
#include "net/GuestLoad.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <utility>

#include "platform/Slot2.hpp"
#include "HlDsCut.hpp"

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
        freeDsCut();
#endif
    }

#ifndef HLDS_CHILD
    void Level::freeDsCut()
    {
        std::free(visRow_);
        visRow_ = nullptr;
        visRowLeaf_ = -1;
        visRowBytes_ = visRowCap_ = 0;
        for (void *block : {static_cast<void *>(pageStart_), static_cast<void *>(pageSlot_),
                            static_cast<void *>(slotPage_), static_cast<void *>(slotUsed_),
                            static_cast<void *>(slotWords_)})
            std::free(block);
        pageStart_ = slotUsed_ = slotWords_ = nullptr;
        pageSlot_ = slotPage_ = nullptr;
        pageCount_ = slotCount_ = slotSize_ = gxUse_ = pagesRead_ = 0;
        cutFlags_ = 0;
    }

    const std::uint32_t *Level::pagedGx(std::uint32_t ofs)
    {
        // The page: the last that starts at or before `ofs` (no surface
        // runs over the end of its page).
        std::uint32_t lo = 0, hi = pageCount_;
        while (hi - lo > 1)
        {
            const std::uint32_t mid = (lo + hi) / 2;
            (pageStart_[mid] <= ofs ? lo : hi) = mid;
        }
        const std::uint32_t page = lo;
        ++gxUse_;
        std::uint16_t slot = pageSlot_[page];
        if (slot == kNoSlot)
        {
            // Read now, into the slot drawn from longest ago. (The frame
            // waits for the card: what a DS gives for the room.)
            slot = 0;
            for (std::uint16_t k = 1; k < slotCount_; ++k)
            {
                if (slotUsed_[k] < slotUsed_[slot])
                    slot = k;
            }
            if (slotPage_[slot] != kNoSlot)
                pageSlot_[slotPage_[slot]] = kNoSlot;
            const std::uint32_t words = pageStart_[page + 1] - pageStart_[page];
            std::uint32_t *to = slotWords_ + static_cast<std::size_t>(slot) * slotSize_;
            if (!cardReadMember(gxMember_, pageStart_[page] * 4u, to, words * 4u))
            {
                // (Not given: drawn as nothing, a no-op, and asked for again.)
                std::memset(to, 0, words * 4u);
                slotPage_[slot] = kNoSlot;
                slotUsed_[slot] = 0;
                return to + (ofs - pageStart_[page]);
            }
            pageSlot_[page] = slot;
            slotPage_[slot] = static_cast<std::uint16_t>(page);
            ++pagesRead_;
        }
        slotUsed_[slot] = gxUse_;
        return slotWords_ + static_cast<std::size_t>(slot) * slotSize_ + (ofs - pageStart_[page]);
    }
#endif

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
        packTogether();
        return moved;
    }

    void Level::packTogether()
    {
        auto *dir = reinterpret_cast<hldata::SectionDir *>(block_ + sizeof(hldata::LevelHeader));
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
    }

    /// (Where a section a DS reads from the card is said to be: anywhere but
    /// in the block. Nothing reads through it: the view's pointer is put
    /// back to null.)
    static const std::uint8_t kOnCard[4] = {};

    bool Level::applyDsCut()
    {
        // A DS, with no Pak to keep what is read least: a map the converter
        // found too big for its heap says what it may leave out (HlDsCut.hpp).
        if (global.isDSi || slot2::pakBytes() != 0 || block_ == nullptr)
            return false;
        NEA_NpacMember member;
        alignas(4) hldata::DsCutHeader cut;
        if (NEA_NpacMemberFind(global.mapPath(hldata::kDsCutMember).c_str(), &member) != 0 ||
            member.method != NEA_NPAC_STORED || member.size < sizeof(cut) ||
            member.size > sizeof(cut) + (hldata::kDsGxMostPages + 1) * 4u ||
            !cardReadMember(member, 0, &cut, sizeof(cut)) || member.size != hldata::dsCutBytes(cut) ||
            cut.levelCrc != view.header->crc32)
            return false;
        auto *dir = reinterpret_cast<hldata::SectionDir *>(block_ + sizeof(hldata::LevelHeader));
        const hldata::SectionDir &vis = dir[static_cast<int>(hldata::Section::Vis)];
        const hldata::SectionDir &gx = dir[static_cast<int>(hldata::Section::GxStream)];
        const bool wantVis = (cut.flags & hldata::kDsCutVis) != 0 && vis.size == cut.visBytes &&
                             NEA_NpacMemberFind(global.mapPath(hldata::kDsVisMember).c_str(), &visMember_) == 0 &&
                             visMember_.method == NEA_NPAC_STORED && visMember_.size == vis.size;
        const bool wantGx = (cut.flags & hldata::kDsCutGx) != 0 && gx.count == cut.gxWords &&
                            NEA_NpacMemberFind(global.mapPath(hldata::kDsGxMember).c_str(), &gxMember_) == 0 &&
                            gxMember_.method == NEA_NPAC_STORED && gxMember_.size == gx.size;
        // (The page table, checked whole, read before the block shrinks: it
        // is let go after, and the hole it leaves joins the room given back.)
        static constexpr std::uint32_t kMostCut = sizeof(hldata::DsCutHeader) + (hldata::kDsGxMostPages + 1) * 4u;
        std::uint8_t *table = nullptr;
        if (wantGx)
        {
            table = static_cast<std::uint8_t *>(std::malloc(member.size));
            if (table != nullptr && (!cardReadMember(member, 0, table, member.size) ||
                                     !hldata::dsCutValid(table, member.size) || member.size > kMostCut))
            {
                std::free(table);
                table = nullptr;
            }
        }
        const bool gxOut = table != nullptr;

        // Out of the block: what is read from the card, and the lights that
        // change (as they were lit, for good: a page read again would not
        // have its patches made).
        if (wantVis)
            inPak_[static_cast<int>(hldata::Section::Vis)] = kOnCard;
        if (gxOut)
            inPak_[static_cast<int>(hldata::Section::GxStream)] = kOnCard;
        if ((cut.flags & hldata::kDsCutLightPatch) != 0 || gxOut)
        {
            hldata::SectionDir &patches = dir[static_cast<int>(hldata::Section::LightPatch)];
            patches.count = patches.size = 0;
            cutFlags_ |= hldata::kDsCutLightPatch;
        }
        if (!wantVis && !gxOut && cutFlags_ == 0)
            return false;
        packTogether();

        // Then what reads them, in the room given back.
        if (wantVis)
        {
            visRowCap_ = (view.header->visLeafCount + 7) / 8 + 16;
            visRow_ = static_cast<std::uint8_t *>(std::malloc(visRowCap_));
            cutFlags_ |= hldata::kDsCutVis;
        }
        if (gxOut)
        {
            std::memcpy(&cut, table, sizeof(cut));
            pageCount_ = cut.pageCount;
            slotCount_ = cut.slotCount;
            slotSize_ = cut.slotWords;
            pageStart_ = static_cast<std::uint32_t *>(std::malloc((pageCount_ + 1) * 4u));
            pageSlot_ = static_cast<std::uint16_t *>(std::malloc(pageCount_ * 2u));
            slotPage_ = static_cast<std::uint16_t *>(std::malloc(slotCount_ * 2u));
            slotUsed_ = static_cast<std::uint32_t *>(std::calloc(slotCount_, 4u));
            slotWords_ = static_cast<std::uint32_t *>(std::malloc(static_cast<std::size_t>(slotCount_) * slotSize_ * 4u));
            if (pageStart_ != nullptr)
                std::memcpy(pageStart_, table + sizeof(cut), (pageCount_ + 1) * 4u);
            if (pageSlot_ != nullptr)
                std::memset(pageSlot_, 0xFF, pageCount_ * 2u);
            if (slotPage_ != nullptr)
                std::memset(slotPage_, 0xFF, slotCount_ * 2u);
            cutFlags_ |= hldata::kDsCutGx;
            std::free(table);
        }
        // (No room even for those: the map cannot be played on this DS.)
        return true;
    }

    bool Level::dsCutReady() const
    {
        return ((cutFlags_ & hldata::kDsCutVis) == 0 || visRow_ != nullptr) &&
               ((cutFlags_ & hldata::kDsCutGx) == 0 ||
                (pageStart_ != nullptr && pageSlot_ != nullptr && slotPage_ != nullptr && slotUsed_ != nullptr &&
                 slotWords_ != nullptr));
    }

    const std::uint8_t *Level::visRow(int leaf, const std::uint8_t *&end)
    {
        const std::int32_t ofs = view.leaves[leaf].visOfs;
        if (view.vis != nullptr)
        {
            end = view.vis + view.visSize;
            return view.vis + ofs;
        }
        // A DS reading them from the card: this leaf's, unless it is the one
        // already read. (Rows are packed: what follows the row is read too,
        // and never looked at.)
        if (visRow_ == nullptr || ofs < 0 || static_cast<std::uint32_t>(ofs) >= visMember_.size)
            return nullptr;
        if (leaf != visRowLeaf_)
        {
            const std::uint32_t left = visMember_.size - static_cast<std::uint32_t>(ofs);
            visRowBytes_ = left < visRowCap_ ? left : visRowCap_;
            visRowLeaf_ = cardReadMember(visMember_, static_cast<std::uint32_t>(ofs), visRow_, visRowBytes_) ? leaf : -1;
            if (visRowLeaf_ < 0)
                return nullptr;
        }
        end = visRow_ + visRowBytes_;
        return visRow_;
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

#ifndef HLDS_CHILD
    /// Said when the card would not give a piece of the level (after the
    /// archive's own tries): which piece, and the card's error, for a report.
    static void cardReadFailed(std::uint32_t piece, std::string &error)
    {
        // (The archive's words: EIO, the card refused a read; EILSEQ, it
        // gave bytes that were not the ones written.)
        const int code = errno;
        const char *why = code == EIO ? "the card refused a read" : code == EILSEQ ? "the card gave wrong data" : "error";
        char text[112];
        std::snprintf(text, sizeof(text), "Map could not be read from the card (piece %u: %s, %d).",
                      static_cast<unsigned>(piece), why, code);
        error = text;
    }
#endif

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
            if (member.size != sizeof(first))
            {
                error = "Map file is damaged. Convert again.";
                return false;
            }
            if (!cardReadMember(member, 0, first, sizeof(first)))
            {
                cardReadFailed(0, error);
                return false;
            }
            got = true;
            std::memcpy(&header, first, sizeof(header));
            const std::uint32_t size = header.fileSize;
            if (size < hldata::kLevelDataStart || size > 8 * 1024 * 1024)
            {
                error = "Map file is damaged. Convert again.";
                return false;
            }
            block_ = static_cast<std::uint8_t *>(std::malloc(size));
            if (block_ == nullptr)
            {
                error = "Not enough memory for this map.";
                return false;
            }
            std::memcpy(block_, first, sizeof(first));
            for (std::uint32_t piece = 1; piece < hldata::levelPieces(size); ++piece)
            {
                char name[16];
                std::snprintf(name, sizeof(name), "level.%u", static_cast<unsigned>(piece));
                const std::uint32_t bytes = hldata::levelPieceBytes(size, piece);
                // (A piece missing from the archive, or not the length the
                // header says, is the file's fault; one there that the card
                // will not give is the card's.)
                if (NEA_NpacMemberFind(global.mapPath(name).c_str(), &member) != 0 ||
                    !cardMemberReadable(member) || member.size != bytes)
                {
                    unload();
                    error = "Map file is damaged. Convert again.";
                    return false;
                }
                if (!cardReadMember(member, 0, block_ + hldata::levelPieceStart(piece), bytes))
                {
                    unload();
                    cardReadFailed(piece, error);
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
        // A DS short of room: what it reads from the card instead, or goes
        // without, out of the block. (The size the header says is the file's:
        // the view is told it, not the block's.)
        if (pakBytes_ == 0 && applyDsCut())
        {
            if (!dsCutReady())
            {
                unload();
                error = "Not enough memory for this map.";
                return false;
            }
            const std::uint32_t whole = size;
            // (Looked at as the file was, the sections now where they are.)
            if (view.open(block_, whole, false, inPak_) != hldata::LevelError::Ok)
            {
                unload();
                error = "Map file is damaged. Convert again.";
                return false;
            }
            if ((cutFlags_ & hldata::kDsCutVis) != 0)
                view.vis = nullptr;
            if ((cutFlags_ & hldata::kDsCutGx) != 0)
                view.gxStream = nullptr;
        }
#endif
        return true;
    }
} // namespace hlrt
