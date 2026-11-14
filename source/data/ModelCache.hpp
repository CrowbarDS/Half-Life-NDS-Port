#pragma once

#include "EngineObject.hpp"

// The models of the loaded map: each one's cooked file (mdl/<map>/<name>.hmd)
// held in memory and opened, and its skin (mdl/<name>.htx) in video memory,
// except weapons' (v_*), of which only the one in hand is in video memory.
//
// One entry per ModelRef of the level, in the level's order. A model that is
// not on the card, or does not fit, is simply not there: its view's header is
// null and the actors made of it are not drawn.
//
// A weapon the map was not cooked with (kModelRefLazy: one a cheat gives) is
// named but not loaded: wield() reads it then, out of the common archive
// (wpn/), one such at a time.

#include <NEAMain.h>

#include <string>

#include "HlModel.hpp"
#include "HlWeaponBundle.hpp"
#include "HlCharacters.hpp"

namespace hlrt
{
    class ModelCache
    {
    public:
        /// Load the models of the level that is loaded, out of its archive.
        bool create();
        void destroy();
        /// As destroy(), for when the 3D engine has been shut down under it:
        /// the skins went with the engine.
        void forget();

        /// For when the 3D engine is brought up again under a map that
        /// stays loaded: the skins are gone with the old engine and are
        /// loaded into the new one. The models themselves are not touched.
        void forgetSkins();
        void loadSkins();

        /// What another player holding `weapon` is seen with: its ModelRef
        /// (-1: none on this map). In memory if views()[it].header is.
        int heldModel(int weapon) const
        {
            return heldRefs_ != nullptr && weapon > 0 && weapon < hldata::kWeaponCount ? heldRefs_[weapon] : -1;
        }
        const hldata::ModelView *views() const { return views_; }
        /// (The map's models, then the slots of those who chose to be another.)
        int count() const { return count_ + hldata::kCharacterSlots; }
        /// Who the players here chose to be, read (a console with the game;
        /// before the map's textures stream, for the room for their skins).
        void loadCharacters();
        /// The video memory of the skins of those not here yet (they come
        /// after the map's textures have taken theirs).
        std::uint32_t charactersAwaited() const;
        /// The ModelRef-like index of character `id`, if it is in memory; -1
        /// (the suit, or not here): the map's own player.
        int characterSlot(int id) const;
        /// Player `slot` is drawn as that (from characterSlot): their colours
        /// go over its skin.
        void setPlayerCharacter(int slot, int model);
        NEA_Material *skin(unsigned index) const { return materials_[index]; }
        /// Drawing the model `index` as player `slot` of a game between
        /// consoles: the palette word of that player's colours, for a skin
        /// that has them (a 256-colour one: a console sent the game); 0 for
        /// the skin's own.
        std::uint32_t playerPalette(unsigned index, int slot) const
        {
            return (static_cast<int>(index) == playerModel_ || static_cast<int>(index) >= count_) && slot >= 0 &&
                           slot < kPlayerColours
                       ? playerPaletteWords_[slot]
                       : 0;
        }
        /// Player `slot`'s colours (0..255 each, the game's top and bottom):
        /// their palette made again.
        void setPlayerColours(int slot, int top, int bottom);
        static constexpr int kPlayerColours = 8;
        int missing() const { return missing_; }

        /// `index`: weapon in hand; -1: none. A weapon the map has not is
        /// loaded there and then (too little memory: it is not drawn).
        void wield(int index);
        /// The weapon in hand read again if it could not be when it was
        /// taken up (a console sent the game: its weapons come at the end
        /// of the map's stream, after the player is in).
        /// The model of the weapon wanted is here (it was not when wanted).
        bool wantedHere() const { return wanted_ >= 0 && views_ != nullptr && views_[wanted_].header != nullptr; }
        /// A console in DSi mode, nothing else to ask for: the next of these
        /// it has not. False: it has all there is to ask for.
        bool fetchAhead();
        void rewield()
        {
            if (wanted_ >= 0)
                wield(wanted_);
        }
        /// Do what wield() asked, once a frame: the skin is written into a
        /// slot of video memory by VramWriter, and the weapon is drawn with
        /// it once it is all there.
        void apply();
        /// Whether the model can be drawn: not a weapon whose skin is not
        /// in video memory yet.
        bool drawable(unsigned index) const
        {
            return kept_ == nullptr || index >= static_cast<unsigned>(count_) || kept_[index] == 0 ||
                   resident_ == static_cast<int>(index);
        }
        /// The texel bytes of the largest weapon skin (a 4x4 texture: its
        /// index is half as many again), for the slots it is written to.
        std::uint32_t largestSkin() const;
        /// The palette memory the models the map has not need when they are
        /// taken up (their skins' colours), to be left free; 0 if none.
        /// (And of their skins: all but a weapon's are in video memory.)
        int lazyPaletteBytes() const { return anyLazy_ ? hldata::kLazyResident * 512 * 2 : 0; }
        int lazyTexelBytes() const { return anyLazy_ ? (hldata::kLazyResident - 1) * hldata::kLazyPieceSkinTexels / 4 : 0; }
        /// The places a weapon's skin is written to (TextureStream's region;
        /// `count` of them, 0 to 2, of `bytes` texel bytes each -- largestSkin(),
        /// or what was set by before the map's textures): one for the weapon
        /// in hand, one for the next. No slots: weapons are drawn bare.
        void setSlots(void *const *texels, int count, std::uint32_t bytes);
        /// The slots are going (the map is left): nothing more is written to
        /// them, and nothing is drawn from them.
        void dropSlots();
        /// Models whose skin did not load (not on the card, or no room).
        int bare() const { return bare_; }

        ModelCache() = default;
        ModelCache(const ModelCache &) = delete;
        ModelCache &operator=(const ModelCache &) = delete;

    private:
        // A skin a player's colours are laid over (TextureCache::takeRemapBase):
        // its model, its palette as it came, and each player's.
        int playerModel_ = -1;
        std::uint16_t playerBase_[256] = {};
        NEA_Palette *playerPalettes_[kPlayerColours] = {};
        std::uint32_t playerPaletteWords_[kPlayerColours] = {};
        void makePlayerPalettes(int model);
        void release(bool engineGone);
        void putAway(std::uint32_t now);
        bool lazy(int index) const;
        std::string skinPath(int index) const;
        /// A model the map has not read in, what it takes made room for
        /// (`evict`: by letting others go). False if it could not be.
        bool loadLazy(int index, bool evict);
        void dropLazy(int index);
        /// Its skin: false if it is not here yet, or there is no room for it.
        bool loadLazySkin(int index);
        /// The one of the lazy models in memory to go, the longest unused:
        /// never one of the weapon in hand's, nor one being drawn or
        /// written, nor what a shot or a casing in the air is. -1: none can.
        int lazyToDrop() const;
        /// What is wanted and not yet in memory: the rest of the weapon in
        /// hand's, what is flying about, what was just picked up. One a frame.
        void lazyTick();
        bool lazyInUse(int index) const;
        bool heldByAnother(int index) const;

        /// By ModelRef, of the lazy ones.
        struct LazyState
        {
            std::uint32_t usedAt = 0;  ///< the frame it was last wanted (TextureStream::blanks)
            std::uint32_t triedAt = 0; ///< ... and last failed to load, not to try every frame
            bool failed = false;
            bool skinWait = false; ///< its skin is not read yet (on its way, or no room): tried again
            std::uint16_t bundles = 0; ///< hldata::bundlesOf
        };
        std::int16_t *heldRefs_ = nullptr;
        std::uint16_t *characterBases_ = nullptr; ///< 256 colours a slot, as its skin came
        int characterIds_[hldata::kCharacterSlots] = {};
        int playerCharacter_[kPlayerColours] = {-1, -1, -1, -1, -1, -1, -1, -1};
        LazyState *lazy_ = nullptr;
        int aheadNext_ = 0;
        std::uint16_t gunsAsked_ = 0; ///< whose sounds have been asked for
        std::uint16_t lazyMask_ = 0;     ///< the weapons (bits) picked up just now, to load ahead
        std::uint16_t haveSeen_ = 0;     ///< what the player had when last looked
        bool haveKnown_ = false;

        hldata::ModelView *views_ = nullptr;
        std::uint8_t **blocks_ = nullptr;
        NEA_Material **materials_ = nullptr;
        NEA_Palette **palettes_ = nullptr;
        std::uint8_t *kept_ = nullptr;
        int wanted_ = -1;
        int resident_ = -1;     ///< the skin in hand: written, and drawn
        int residentSlot_ = -1;
        int writing_ = -1;      ///< the skin being written ...
        int writingSlot_ = -1;
        std::uint32_t ticket_ = 0; ///< ... done with this VramWriter ticket
        struct Slot
        {
            void *texels = nullptr;
            std::uint32_t freeSince = 0; ///< the frame it was let go (TextureStream::blanks)
            bool used = false;
        };
        Slot slots_[2];
        int slotCount_ = 0;
        std::uint32_t slotBytes_ = 0; ///< how big each slot is (a skin larger is drawn bare)
        int count_ = 0;
        int missing_ = 0;
        std::uint32_t fileBytes_ = 0;
        std::uint32_t skinBytes_ = 0;
    public:
        /// What loading the skins took of the heap (perf.txt).
        std::uint32_t skinBytes() const { return skinBytes_; }
        /// The bytes of the map's model files read (perf.txt).
        std::uint32_t fileBytes() const { return fileBytes_; }
    private:
        int bare_ = 0;
        bool anyLazy_ = false; ///< the level names a weapon it has not
    };

    HL_ENGINE_OBJECT(ModelCache, modelCache);
} // namespace hlrt
