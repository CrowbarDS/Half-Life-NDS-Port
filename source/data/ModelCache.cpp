#include <cstring>
#include "data/ModelCache.hpp"
#include "data/TextureStream.hpp"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

#include "Global.hpp"
#include "audio/Sound.hpp"
#include "data/CardRead.hpp"
#include "data/LevelFile.hpp"
#include "data/TextureCache.hpp"
#include "data/VramWriter.hpp"
#include "game/Game.hpp"
#include "game/Player.hpp"
#include "net/GuestLoad.hpp"
#include "net/NetGame.hpp"
#include "HlNetGame.hpp"
#include "HlRemap.hpp"

namespace hlrt
{
    ModelCache modelCache;

    namespace
    {
        /// A model is tens of kilobytes; one that claims to be megabytes is
        /// not one.
        constexpr long kMaxModelBytes = 1024 * 1024;

        /// A model file, read whole into a block of its own; null if it is
        /// not there, too big, or `spare` bytes would not be left after it.
        std::uint8_t *readModel(const std::string &path, std::uint32_t &size, std::size_t spare = 0)
        {
#ifndef HLDS_CHILD
            // In an archive: its size from the index, and read (undone, if
            // packed) straight into its block, with no file opened. (Opened as
            // a file, a packed model is undone whole into a block of its own
            // first, beside this one: a hole left behind each time.)
            NEA_NpacMember member;
            if (NEA_NpacMemberFind(path.c_str(), &member) == 0 && cardMemberReadable(member))
            {
                std::uint8_t *block = nullptr;
                if (member.size > 0 && member.size <= static_cast<std::uint32_t>(kMaxModelBytes) &&
                    global.heapHeadroom() >= member.size + spare)
                    block = static_cast<std::uint8_t *>(std::malloc(member.size));
                if (block == nullptr || !cardReadMember(member, 0, block, member.size))
                {
                    std::free(block);
                    return nullptr;
                }
                size = member.size;
                return block;
            }
#endif
            std::FILE *file = std::fopen(path.c_str(), "rb");
            if (file == nullptr)
                return nullptr;
            std::fseek(file, 0, SEEK_END);
            const long length = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            // The size is the card's word: bounded before it is trusted with
            // an allocation. (malloc's blocks are aligned for the words the
            // file is made of.)
            std::uint8_t *block = nullptr;
            if (length > 0 && length <= kMaxModelBytes &&
                global.heapHeadroom() >= static_cast<std::size_t>(length) + spare)
                block = static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(length)));
            const bool got = block != nullptr && cardRead(block, static_cast<std::size_t>(length), file) ==
                                                     static_cast<std::size_t>(length);
            std::fclose(file);
            if (!got)
            {
                std::free(block);
                return nullptr;
            }
            size = static_cast<std::uint32_t>(length);
            return block;
        }
    } // namespace

    namespace
    {
        /// Each player's colours until they say their own (their profile): eight
        /// hues apart, top and bottom unlike.
        constexpr std::uint8_t kDefaultColours[ModelCache::kPlayerColours][2] = {
            {30, 6}, {160, 100}, {80, 200}, {230, 40}, {120, 10}, {190, 150}, {0, 70}, {50, 250}};
        int playerColours[ModelCache::kPlayerColours][2];
        bool playerColoursSet = false;
    } // namespace

    void ModelCache::makePlayerPalettes(int model)
    {
        if (!playerColoursSet)
        {
            for (int s = 0; s < kPlayerColours; ++s)
            {
                playerColours[s][0] = kDefaultColours[s][0];
                playerColours[s][1] = kDefaultColours[s][1];
            }
            playerColoursSet = true;
        }
        playerModel_ = model;
        for (int s = 0; s < kPlayerColours; ++s)
            setPlayerColours(s, playerColours[s][0], playerColours[s][1]);
    }

    void ModelCache::setPlayerColours(int slot, int top, int bottom)
    {
        if (slot < 0 || slot >= kPlayerColours)
            return;
        playerColours[slot][0] = top;
        playerColours[slot][1] = bottom;
        if (playerModel_ < 0)
            return;
        // (The skin's colours as 8 bits each, the runs given the player's
        // hues, and back.)
        std::uint8_t base[256][3];
        for (int i = 0; i < 256; ++i)
        {
            const int model = playerCharacter_[slot];
            const std::uint16_t colour = model >= count_ && characterBases_ != nullptr
                                             ? characterBases_[(model - count_) * 256 + i]
                                             : playerBase_[i];
            base[i][0] = static_cast<std::uint8_t>((colour & 31) << 3);
            base[i][1] = static_cast<std::uint8_t>(((colour >> 5) & 31) << 3);
            base[i][2] = static_cast<std::uint8_t>(((colour >> 10) & 31) << 3);
        }
        std::uint16_t colours[256];
        hldata::playerPalette(base, hldata::remapRunsOf("DM_Base"), top, bottom, colours);
        if (playerPalettes_[slot] == nullptr)
            playerPalettes_[slot] = NEA_PaletteCreate();
        if (playerPalettes_[slot] != nullptr && NEA_PaletteLoad(playerPalettes_[slot], colours, 256, NEA_PAL256) == 1)
            playerPaletteWords_[slot] = NEA_PaletteGetBase(playerPalettes_[slot]);
        else
            playerPaletteWords_[slot] = 0;
    }

    int ModelCache::characterSlot(int id) const
    {
        for (int s = 0; id > 0 && s < hldata::kCharacterSlots; ++s)
        {
            if (characterIds_[s] == id && views_ != nullptr && views_[count_ + s].header != nullptr)
                return count_ + s;
        }
        return -1;
    }

    void ModelCache::setPlayerCharacter(int slot, int model)
    {
        if (slot >= 0 && slot < kPlayerColours)
            playerCharacter_[slot] = model;
    }

    void ModelCache::loadCharacters()
    {
        const hldata::NetMatch &match = netGame.match();
        if (!netGame.on() || views_ == nullptr)
            return;
#ifdef HLDS_CHILD
        constexpr std::size_t kSpare = hldata::kGuestPlayBytes / 2;
#else
        constexpr std::size_t kSpare = 200 * 1024;
#endif
        // (Those read already stay: more may come.)
        int slot = 0;
        while (slot < hldata::kCharacterSlots && characterIds_[slot] != 0)
            ++slot;
        for (int p = 0; p < hldata::kNetPlayers && slot < hldata::kCharacterSlots; ++p)
        {
            const int id = match.profiles[p].model;
            if (!hldata::netHere(match, p) || !match.profiles[p].known || id <= 0 || id >= hldata::kCharacterCount ||
                characterSlot(id) >= 0)
                continue;
            if (characterBases_ == nullptr)
                characterBases_ = static_cast<std::uint16_t *>(std::malloc(hldata::kCharacterSlots * 256 * 2));
            if (characterBases_ == nullptr)
                return;
            const std::string name = hldata::kCharacterFiles[id];
            std::uint32_t size = 0;
            // (At its finest on a DSi with the game. Sent it, it came so, or not.)
            std::string kind;
            std::uint8_t *block = global.isDSi ? readModel(global.commonPath("chr/" + name + ".dsi.hmd"), size, kSpare) : nullptr;
            if (block != nullptr)
                kind = ".dsi";
            else
                block = readModel(global.commonPath("chr/" + name + ".hmd"), size, kSpare);
            const int at = count_ + slot;
            if (block == nullptr || !views_[at].open(block, size))
            {
                std::free(block);
                views_[at] = hldata::ModelView();
                continue;
            }
            if (!TextureCache::loadFile(global.commonPath("chr/" + name + kind + ".htx").c_str(), false,
                                        views_[at].header->atlasWidth, views_[at].header->atlasHeight, 0, materials_[at],
                                        palettes_[at]) ||
                !TextureCache::takeRemapBase(characterBases_ + slot * 256))
            {
                // (No room for its skin: the suit, for those who chose it.)
                if (materials_[at] != nullptr)
                    NEA_MaterialDelete(materials_[at]);
                if (palettes_[at] != nullptr)
                    NEA_PaletteDelete(palettes_[at]);
                materials_[at] = nullptr;
                palettes_[at] = nullptr;
                views_[at] = hldata::ModelView();
                std::free(block);
                continue;
            }
            blocks_[at] = block;
            characterIds_[slot++] = id;
        }
    }

    std::uint32_t ModelCache::charactersAwaited() const
    {
        const hldata::NetMatch &match = netGame.match();
        if (!netGame.on())
            return 0;
        int slot = 0;
        while (slot < hldata::kCharacterSlots && characterIds_[slot] != 0)
            ++slot;
        std::uint32_t bytes = 0;
        for (int p = 0; p < hldata::kNetPlayers && slot < hldata::kCharacterSlots; ++p)
        {
            const int id = match.profiles[p].model;
            if (!hldata::netHere(match, p) || !match.profiles[p].known || id <= 0 || id >= hldata::kCharacterCount || characterSlot(id) >= 0)
                continue;
            bytes += global.twlChild ? hldata::kFinePlayerSkinTexels : 128 * 128;
            ++slot;
        }
        return bytes;
    }

    void ModelCache::release(bool engineGone)
    {
        for (int i = 0; i < count_ + hldata::kCharacterSlots && views_ != nullptr; ++i)
        {
            if (blocks_ != nullptr)
                std::free(blocks_[i]);
            if (!engineGone && materials_ != nullptr && materials_[i] != nullptr)
                NEA_MaterialDelete(materials_[i]);
            if (!engineGone && palettes_ != nullptr && palettes_[i] != nullptr)
                NEA_PaletteDelete(palettes_[i]);
        }
        for (int s = 0; s < kPlayerColours; ++s)
        {
            if (!engineGone && playerPalettes_[s] != nullptr)
                NEA_PaletteDelete(playerPalettes_[s]);
            playerPalettes_[s] = nullptr;
            playerPaletteWords_[s] = 0;
        }
        playerModel_ = -1;
        std::free(views_);
        std::free(blocks_);
        std::free(materials_);
        std::free(palettes_);
        std::free(kept_);
        std::free(lazy_);
        std::free(heldRefs_);
        std::free(characterBases_);
        characterBases_ = nullptr;
        for (int &id : characterIds_)
            id = 0;
        for (int &model : playerCharacter_)
            model = -1;
        heldRefs_ = nullptr;
        kept_ = nullptr;
        lazy_ = nullptr;
        lazyMask_ = haveSeen_ = 0;
        haveKnown_ = false;
        wanted_ = resident_ = residentSlot_ = writing_ = writingSlot_ = -1;
        slotCount_ = 0;
        views_ = nullptr;
        blocks_ = nullptr;
        materials_ = nullptr;
        palettes_ = nullptr;
        count_ = 0;
        missing_ = 0;
        anyLazy_ = false;
    }

    void ModelCache::destroy() { release(false); }
    void ModelCache::forget() { release(true); }

    void ModelCache::forgetSkins()
    {
        for (int i = 0; i < count_ + hldata::kCharacterSlots && materials_ != nullptr; ++i)
        {
            materials_[i] = nullptr;
            palettes_[i] = nullptr;
        }
        for (int s = 0; s < kPlayerColours; ++s)
        {
            playerPalettes_[s] = nullptr;
            playerPaletteWords_[s] = 0;
        }
        playerModel_ = -1;
        // (Gone with the old engine, and the slots with it; wanted_ stays, and
        // is put back once there are slots again.)
        resident_ = residentSlot_ = writing_ = writingSlot_ = -1;
        slotCount_ = 0;
    }

    std::uint32_t ModelCache::largestSkin() const
    {
        std::uint32_t most = 0;
        for (int i = 0; kept_ != nullptr && i < count_; ++i)
        {
            if (kept_[i] == 0 || materials_[i] == nullptr)
                continue;
            const NEA_Material *m = materials_[i];
            const std::uint32_t bytes = static_cast<std::uint32_t>(m->ram_sizex * m->ram_sizey / 4);
            most = bytes > most ? bytes : most;
        }
        // (... and of one the map has not, which may be taken up: all of
        // those are made at one size.)
        if (anyLazy_)
            most = most > hldata::kLazySkinTexels / 4 ? most : hldata::kLazySkinTexels / 4;
        return (most + 31u) & ~31u;
    }

    void ModelCache::dropSlots()
    {
        if (resident_ >= 0 && materials_ != nullptr && materials_[resident_] != nullptr)
            NEA_MaterialTexVramUnload(materials_[resident_]);
        resident_ = residentSlot_ = writing_ = writingSlot_ = -1;
        slotCount_ = 0;
    }

    void ModelCache::setSlots(void *const *texels, int count, std::uint32_t bytes)
    {
        // (Whatever was in the old ones is gone with them.)
        dropSlots();
        slotCount_ = count < 0 ? 0 : (count > 2 ? 2 : count);
        slotBytes_ = bytes;
        for (int k = 0; k < 2; ++k)
            slots_[k] = Slot{k < slotCount_ ? texels[k] : nullptr, 0, false};
        if (slotCount_ == 0)
        {
            // No room for any: drawn bare, as a skin that did not fit always was.
            for (int i = 0; kept_ != nullptr && i < count_; ++i)
            {
                if (kept_[i] != 0)
                    ++bare_;
                kept_[i] = 0;
            }
        }
    }

    void ModelCache::putAway(std::uint32_t now)
    {
        if (resident_ < 0)
            return;
        if (materials_[resident_] != nullptr)
            NEA_MaterialTexVramUnload(materials_[resident_]);
        // (Written over no sooner than two frames on: the pictures until
        // then were drawn with it.)
        slots_[residentSlot_].used = false;
        slots_[residentSlot_].freeSince = now;
        resident_ = residentSlot_ = -1;
    }

    void ModelCache::apply()
    {
        lazyTick();
        if (kept_ == nullptr || slotCount_ == 0)
            return;
        const std::uint32_t now = textureStream.blanks();
        // A skin written: it is the one in hand now.
        if (writing_ >= 0 && vramWriter.done(ticket_))
        {
            const NEA_Material *m = materials_[writing_];
            putAway(now);
            if (NEA_MaterialTexBindFixed(materials_[writing_], NEA_TEX4X4, m->ram_sizex, m->ram_sizey, m->ram_flags,
                                         slots_[writingSlot_].texels) == 1)
            {
                resident_ = writing_;
                residentSlot_ = writingSlot_;
            }
            else
            {
                slots_[writingSlot_].used = false;
                slots_[writingSlot_].freeSince = now;
            }
            writing_ = writingSlot_ = -1;
        }
        if (writing_ >= 0 || resident_ == wanted_)
            return;
        // (Its skin on its way: not given up on.)
        if (wanted_ >= 0 && wanted_ < count_ && lazy_ != nullptr && lazy_[wanted_].skinWait && materials_[wanted_] == nullptr)
            return;
        if (wanted_ < 0)
        {
            putAway(now);
            return;
        }
        NEA_Material *m = wanted_ < count_ && kept_[wanted_] != 0 ? materials_[wanted_] : nullptr;
        const std::uint32_t texels = m != nullptr ? static_cast<std::uint32_t>(m->ram_sizex * m->ram_sizey / 4) : 0;
        if (m == nullptr || m->ram_data == nullptr || m->ram_format != NEA_TEX4X4 ||
            m->ram_size < texels + texels / 2 || texels > slotBytes_)
        {
            if (m != nullptr && kept_[wanted_] != 0)
            {
                kept_[wanted_] = 0; // (not one this can write: drawn bare)
                ++bare_;
            }
            wanted_ = -1; // not one that is kept in main memory: it is drawn as it is
            return;
        }
        // A slot that nothing on the screen draws from; with one slot only,
        // the weapon in hand is put away first.
        int slot = -1;
        for (int k = 0; k < slotCount_ && slot < 0; ++k)
        {
            if (!slots_[k].used && now - slots_[k].freeSince >= 2)
                slot = k;
        }
        if (slot < 0)
        {
            if (slotCount_ == 1)
                putAway(now);
            return; // (a frame or two on)
        }
        // [texels][index], as NEA keeps it.
        const auto *data = static_cast<const std::uint8_t *>(m->ram_data);
        void *at = slots_[slot].texels;
        const std::uint32_t first = vramWriter.write(at, data, texels);
        const std::uint32_t second =
            first != 0 ? vramWriter.write(NEA_Texture4x4IndexAddress(at), data + texels, texels / 2) : 0;
        if (second == 0)
            return; // (the queue is full: next frame)
        slots_[slot].used = true;
        writing_ = wanted_;
        writingSlot_ = slot;
        ticket_ = second;
    }

    bool ModelCache::lazy(int index) const
    {
        return index >= 0 && index < count_ && hldata::modelRefLazy(level.view.modelRefs[index]);
    }

    std::string ModelCache::skinPath(int i) const
    {
        const std::string skin = hldata::modelSkinFile(level.view.modelRefs[i].name, views_[i].header->atlasWidth,
                                                       views_[i].header->atlasHeight);
        return lazy(i) ? global.commonPath("wpn/" + skin) : global.mapPath("mdl/") + skin;
    }

    void ModelCache::wield(int index)
    {
        wanted_ = index;
        if (index >= 0 && index < count_)
            sound.askGun(level.view.modelRefs[index].name);
        if (!lazy(index))
            return;
        // (Now, not a frame on: the game looks into a weapon's clips as it
        // takes it up. The rest of what goes with it, lazyTick.)
        lazy_[index].usedAt = textureStream.blanks();
        if (views_[index].header == nullptr)
            loadLazy(index, true);
    }

    bool ModelCache::lazyInUse(int index) const
    {
        if (index == wanted_ || index == resident_ || index == writing_)
            return true;
        const auto &actors = game.actors;
        for (int i = 0; i < hldata::kMaxProjectiles; ++i)
        {
            if (actors.shots[i].active && game.thrownModel(actors.shots[i].kind) == index)
                return true;
        }
        for (int i = 0; i < hldata::kMaxPieces; ++i)
        {
            if (actors.pieces[i].kind != 0 && game.shellModel(actors.pieces[i].kind) == index)
                return true;
        }
        return heldByAnother(index);
    }

    bool ModelCache::heldByAnother(int index) const
    {
        const hldata::NetMatch &match = netGame.match();
        for (int p = 0; match.playing && p < hldata::kNetPlayers; ++p)
        {
            const hldata::NetPlayer &other = hldata::netPlayer(match, p);
            if (p != match.me && (other.flags & hldata::kNetHere) != 0 && (other.flags & hldata::kNetInGame) != 0 &&
                heldModel(other.weapon) == index)
                return true;
        }
        return false;
    }

    int ModelCache::lazyToDrop() const
    {
        const std::uint16_t inHand = wanted_ >= 0 && lazy(wanted_) ? lazy_[wanted_].bundles : 0;
        int best = -1;
        for (int i = 0; i < count_; ++i)
        {
            if (!lazy(i) || views_[i].header == nullptr || (lazy_[i].bundles & inHand) != 0 || lazyInUse(i))
                continue;
            if (best < 0 || lazy_[i].usedAt < lazy_[best].usedAt)
                best = i;
        }
        return best;
    }

    /// A model the map has not, read when it is wanted.
    bool ModelCache::loadLazy(int index, bool evict)
    {
        const std::uint32_t now = textureStream.blanks();
        const char *name = level.view.modelRefs[index].name;
        // (The map's own memory comes first: room left beside it, or not at
        // all. A console sent the game was promised less: what it was sent
        // was reckoned up with this in it.)
#ifdef HLDS_CHILD
        constexpr std::size_t kSpare = hldata::kGuestPlayBytes / 2;
#else
        constexpr std::size_t kSpare = 200 * 1024;
#endif
        // A place among those in memory, and the room it takes: the longest
        // unused let go until there is. (Not for one that is not there.)
        std::string path = global.commonPath(std::string("wpn/") + name + ".hmd");
        if (global.isDSi)
        {
            const std::string finer = global.commonPath(std::string("wpn/") + name + ".dsi.hmd");
            if (std::FILE *file = std::fopen(finer.c_str(), "rb"))
            {
                std::fclose(file);
                path = finer;
            }
        }
        long length = 0;
        if (std::FILE *file = std::fopen(path.c_str(), "rb"))
        {
            std::fseek(file, 0, SEEK_END);
            length = std::ftell(file);
            std::fclose(file);
        }
        // (Sent the game in DSi mode: not here is not yet. Asked for; tried again.)
        if (length == 0)
            netGame.fetchFile((std::string("wpn/") + name + ".hmd").c_str(), index == wanted_);
        int resident = 0;
        for (int i = 0; i < count_; ++i)
            resident += lazy(i) && views_[i].header != nullptr ? 1 : 0;
        while (length > 0 && (resident >= hldata::kLazyResident ||
                              global.heapHeadroom() < static_cast<std::size_t>(length) + kSpare))
        {
            const int gone = evict ? lazyToDrop() : -1;
            if (gone < 0)
                break;
            dropLazy(gone);
            --resident;
        }
        std::uint32_t size = 0;
        std::uint8_t *block = nullptr;
        if (length > 0 && (evict || resident < hldata::kLazyResident))
            block = readModel(path, size, kSpare);
        if (block == nullptr || !views_[index].open(block, size))
        {
            std::free(block);
            views_[index] = hldata::ModelView();
            if (!lazy_[index].failed)
                ++missing_;
            lazy_[index].failed = true;
            lazy_[index].triedAt = now;
            return false;
        }
        if (lazy_[index].failed)
            --missing_;
        lazy_[index].failed = false;
        lazy_[index].usedAt = now;
        blocks_[index] = block;
        if (views_[index].header->atlasWidth == 0)
            return true;
        loadLazySkin(index);
        return true;
    }

    bool ModelCache::loadLazySkin(int index)
    {
        const char *name = level.view.modelRefs[index].name;
        LazyState &state = lazy_[index];
        const std::string path = skinPath(index);
        // Its skin kept in main memory, as a weapon's is, while there are
        // slots to write it to; else (the tripmine's: it is on walls too --
        // and what is not held) straight into video memory, where there is
        // room.
        const bool kept = slotCount_ > 0 && std::strncmp(name, "v_", 2) == 0 && std::strcmp(name, "v_tripmine") != 0;
        kept_[index] = kept ? 1 : 0;
        if (TextureCache::loadFile(path.c_str(), false, views_[index].header->atlasWidth,
                                   views_[index].header->atlasHeight, 0, materials_[index], palettes_[index],
                                   nullptr, kept))
        {
            if (state.skinWait && state.failed)
                state.failed = false;
            state.skinWait = false;
            return true;
        }
        state.triedAt = textureStream.blanks();
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (file != nullptr)
        {
            // Here, and no room: what is not in use makes way, and it is
            // drawn bare until there is.
            std::fclose(file);
            if (!state.skinWait)
                ++bare_;
            state.skinWait = true;
            kept_[index] = 0;
            const int gone = lazyToDrop();
            if (gone >= 0 && gone != index)
                dropLazy(gone);
            return false;
        }
        // Not here: asked for (and not drawn without it, for the moment it takes).
        const std::string asked = "wpn/" + hldata::modelSkinFile(name, views_[index].header->atlasWidth, views_[index].header->atlasHeight);
        if (netGame.fetchFile(asked.c_str(), index == wanted_))
        {
            state.skinWait = true;
            kept_[index] = 1;
            return false;
        }
        ++bare_;
        kept_[index] = 0;
        state.skinWait = false;
        return false;
    }

    bool ModelCache::fetchAhead()
    {
        for (; lazy_ != nullptr && aheadNext_ < count_; ++aheadNext_)
        {
            if (!lazy(aheadNext_))
                continue;
            const char *name = level.view.modelRefs[aheadNext_].name;
            const std::string model = std::string("wpn/") + name + ".hmd";
            std::FILE *file = std::fopen(global.commonPath(model).c_str(), "rb");
            if (file == nullptr)
            {
                if (netGame.fetchFile(model.c_str()))
                    return true;
                continue;
            }
            hldata::ModelHeader header;
            const bool read = std::fread(&header, sizeof(header), 1, file) == 1;
            std::fclose(file);
            if (!read || header.atlasWidth == 0)
                continue;
            const std::string skin = "wpn/" + hldata::modelSkinFile(name, header.atlasWidth, header.atlasHeight);
            file = std::fopen(global.commonPath(skin).c_str(), "rb");
            if (file != nullptr)
            {
                std::fclose(file);
                continue;
            }
            if (netGame.fetchFile(skin.c_str()))
                return true;
        }
        return false;
    }

    void ModelCache::lazyTick()
    {
        if (lazy_ == nullptr || !anyLazy_)
            return;
        const std::uint32_t now = textureStream.blanks();
        const auto &actors = game.actors;
        // What was just picked up is likely taken in hand: read ahead, if
        // there is room for it without anything going.
        std::uint16_t have = 0;
        for (int weapon = hldata::kWeaponCrowbar; weapon < hldata::kWeaponCount; ++weapon)
            have = static_cast<std::uint16_t>(have | (hldata::hasWeapon(player.state, weapon) ? 1u << weapon : 0u));
        if (haveKnown_)
            lazyMask_ = static_cast<std::uint16_t>(lazyMask_ | (have & ~haveSeen_));
        haveSeen_ = have;
        haveKnown_ = true;
        // What is heard with a gun, once it is anyone's: asked for ahead.
        {
            std::uint16_t guns = have;
            const hldata::NetMatch &match = netGame.match();
            for (int p = 0; match.playing && p < hldata::kNetPlayers; ++p)
            {
                const int weapon = hldata::netPlayer(match, p).weapon;
                if (p != match.me && hldata::netHere(match, p) && weapon > 0 && weapon < hldata::kWeaponCount)
                    guns = static_cast<std::uint16_t>(guns | 1u << weapon);
            }
            for (int weapon = hldata::kWeaponCrowbar; weapon < hldata::kWeaponCount; ++weapon)
            {
                if (((guns & ~gunsAsked_) >> weapon) & 1)
                    sound.askGun(hldata::kWeaponBundles[weapon].models[0]);
            }
            gunsAsked_ = guns | gunsAsked_;
        }
        const std::uint16_t inHand = wanted_ >= 0 && lazy(wanted_) ? lazy_[wanted_].bundles : 0;
        for (int i = 0; i < count_; ++i)
        {
            if (!lazy(i))
                continue;
            LazyState &state = lazy_[i];
            const bool needed = (state.bundles & inHand) != 0;
            if (needed)
                state.usedAt = now;
            // A skin that was not here, or had no room: again, while it is in use.
            if (views_[i].header != nullptr && state.skinWait && materials_[i] == nullptr &&
                now - state.triedAt >= 30 && (needed || lazyInUse(i)))
            {
                loadLazySkin(i);
                return;
            }
            if (views_[i].header != nullptr || (state.failed && now - state.triedAt < 60))
                continue;
            bool flying = false;
            for (int s = 0; s < hldata::kMaxProjectiles && !flying; ++s)
                flying = actors.shots[s].active && game.thrownModel(actors.shots[s].kind) == i;
            for (int p = 0; p < hldata::kMaxPieces && !flying; ++p)
                flying = actors.pieces[p].kind != 0 && game.shellModel(actors.pieces[p].kind) == i;
            flying = flying || heldByAnother(i);
            if (needed || flying)
            {
                loadLazy(i, true);
                return;
            }
            if ((state.bundles & lazyMask_) != 0)
            {
                loadLazy(i, false);
                lazyMask_ = static_cast<std::uint16_t>(lazyMask_ & ~state.bundles);
                return;
            }
        }
        lazyMask_ = 0;
    }

    void ModelCache::dropLazy(int index)
    {
        if (resident_ == index)
            putAway(textureStream.blanks());
        if (materials_[index] != nullptr)
            NEA_MaterialDelete(materials_[index]);
        if (palettes_[index] != nullptr)
            NEA_PaletteDelete(palettes_[index]);
        materials_[index] = nullptr;
        palettes_[index] = nullptr;
        kept_[index] = 0;
        lazy_[index].skinWait = false;
        views_[index] = hldata::ModelView();
        std::free(blocks_[index]);
        blocks_[index] = nullptr;
    }

    void ModelCache::loadSkins()
    {
        bare_ = 0;
        for (int i = 0; i < count_; ++i)
        {
            if (views_[i].header == nullptr || views_[i].header->atlasWidth == 0 ||
                materials_[i] != nullptr)
                continue;
            const std::string skin = skinPath(i);
            // The weapons' are kept in main memory until one is taken up:
            // of the six, one is in hand. (Not the tripmine's: it is also
            // the mine on the wall, the map's own and the player's -- and
            // small.)
            const char *name = level.view.modelRefs[i].name;
            const bool weapon = name[0] == 'v' && name[1] == '_' && std::strcmp(name, "v_tripmine") != 0;
            if (kept_ != nullptr)
                kept_[i] = weapon ? 1 : 0;
            if (!TextureCache::loadFile(skin.c_str(), false, views_[i].header->atlasWidth,
                                        views_[i].header->atlasHeight, 0, materials_[i], palettes_[i], nullptr,
                                        weapon))
            {
                ++bare_;
                if (kept_ != nullptr)
                    kept_[i] = 0;
            }
            // (A skin with a player's colours to lay over it: each player's
            // palette, made from its own.)
            else if (TextureCache::takeRemapBase(playerBase_))
            {
                makePlayerPalettes(i);
            }
        }
    }

    bool ModelCache::create()
    {
        destroy();
        const int count = static_cast<int>(level.view.modelRefCount);
        views_ = static_cast<hldata::ModelView *>(std::calloc(count + hldata::kCharacterSlots + 1, sizeof(hldata::ModelView)));
        blocks_ = static_cast<std::uint8_t **>(std::calloc(count + hldata::kCharacterSlots + 1, sizeof(std::uint8_t *)));
        materials_ = static_cast<NEA_Material **>(std::calloc(count + hldata::kCharacterSlots + 1, sizeof(NEA_Material *)));
        palettes_ = static_cast<NEA_Palette **>(std::calloc(count + hldata::kCharacterSlots + 1, sizeof(NEA_Palette *)));
        kept_ = static_cast<std::uint8_t *>(std::calloc(count + hldata::kCharacterSlots + 1, 1));
        lazy_ = static_cast<LazyState *>(std::calloc(count + 1, sizeof(LazyState)));
        if (views_ == nullptr || blocks_ == nullptr || materials_ == nullptr || palettes_ == nullptr ||
            kept_ == nullptr || lazy_ == nullptr)
        {
            destroy();
            return false;
        }
        count_ = count;
        aheadNext_ = 0;
        gunsAsked_ = 0;
        heldRefs_ = static_cast<std::int16_t *>(std::malloc(hldata::kWeaponCount * sizeof(std::int16_t)));
        for (int w = 0; heldRefs_ != nullptr && w < hldata::kWeaponCount; ++w)
        {
            heldRefs_[w] = -1;
            for (int i = 0; hldata::kHeldModels[w] != nullptr && i < count; ++i)
            {
                if (std::strcmp(level.view.modelRefs[i].name, hldata::kHeldModels[w]) == 0)
                    heldRefs_[w] = static_cast<std::int16_t>(i);
            }
        }
        for (int i = count; i < count + hldata::kCharacterSlots; ++i)
            new (&views_[i]) hldata::ModelView();
        for (int i = 0; i < count; ++i)
        {
            new (&views_[i]) hldata::ModelView();
            // (A weapon the map has not: when it is taken up.)
            const char *name = level.view.modelRefs[i].name;
            if (lazy(i))
            {
                anyLazy_ = true;
                lazy_[i].bundles = hldata::bundlesOf(name);
                continue;
            }
            // (Worn only by what a DS leaves out of the map: consoleActors.)
            if (!global.isDSi && hldata::modelRefDsiOnly(level.view.modelRefs[i]))
                continue;
#ifdef HLDS_CHILD
            // (Sent the game: a model of the group's base was kept when it
            // came, and is borrowed -- never this cache's to free.)
            {
                const std::uint8_t *shared = nullptr;
                std::uint32_t sharedSize = 0;
                if (guest::borrow((std::string("mdl/") + name + ".hmd").c_str(), shared, sharedSize) &&
                    views_[i].open(shared, sharedSize))
                    continue;
            }
#endif
            // A DSi has the memory for the file with the game's own mesh as
            // its finest level, where the converter made one.
            std::uint32_t size = 0;
            std::uint8_t *block = nullptr;
            if (global.isDSi)
                block = readModel(global.mapPath("mdl/") + name + ".dsi.hmd", size);
            if (block == nullptr)
                block = readModel(global.mapPath("mdl/") + name + ".hmd", size);
            if (block == nullptr || !views_[i].open(block, size))
            {
                std::free(block);
                views_[i] = hldata::ModelView();
                ++missing_;
                continue;
            }
            blocks_[i] = block;
#ifdef HLDS_CHILD
            guest::count(hldata::GuestKind::Model, size);
#endif
        }
        // Their skins. Without one (not on the card, or no room in video
        // memory) a model is drawn in its lighting alone.
        loadSkins();
        return true;
    }
} // namespace hlrt
