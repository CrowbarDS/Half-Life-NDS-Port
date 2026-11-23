#pragma once

#include "EngineObject.hpp"

// The head-up display, over the 3D picture as the game has it: health and
// armour at the bottom left, what is in the gun and what is carried for it
// at the bottom right, a crosshair in the middle. And the picture shown
// while a map loads.
//
// Both are the game's own pictures, cooked to the card (shared/HlUi.hpp); a
// card without them has neither. The display is not 3D at all: it is put on
// sprites of the top screen (the main engine's, in video bank F, after
// TopText's in G), added to the picture as the game adds its display -- so
// it costs the 3D engine nothing, and is always in front. A pane redrawn when
// what it shows changes: the bottom row (health, armour, ammunition), the
// lamp, the crosshair; and the red of being hurt, a sheet of sprites over all
// of it, as bright as the hurt is. The loading picture is still a quad.
//
// A card that has the game's 640 set (HlUi.hpp, kHud640File) has it drawn as
// the game draws it at 640 by 480 (HlHudLayout.hpp): its sizes, its places,
// its marks of where a hurt came from and its pictures of the kinds of hurt.
// One cooked before that has the 320 set, in the places this first had.

#include <cstdint>

namespace hlrt
{
    class HudDraw
    {
    public:
        /// What the lamp's light may cost a frame, while it is alight: the
        /// walls it lights, drawn again on the game's grid (WorldDraw::
        /// lampPass) -- every 16 texels on a DSi, 32 on a DS: about 70 and
        /// 20 polygons for one wall's light.
        static constexpr int kLampPolygonsDsi = 200;
        static constexpr int kLampVerticesDsi = 600;
        static constexpr int kLampPolygonsDs = 80;
        static constexpr int kLampVerticesDs = 240;
        bool lampAlight() const { return lampOn_; }

        /// Load what the display keeps. After the 3D engine is brought up
        /// (and again each time it is: forget() first).
        void load();
        /// The display's pictures (the digits, the suit, the train's): read
        /// as each map is loaded, its archive mounted -- they are the map's
        /// overlay's (HudDraw.arm.cpp). Nothing if they are there already.
        void loadPictures();
        /// ... let go of: the map's overlay leaving (GameDll.arm.cpp).
        void forgetPictures();
        /// The marks' sheet, if it is not there yet: as a map loads, in
        /// Bank D with the map's textures that are not 4x4 (BankD).
        void loadDecals();
        void forget();
#ifndef HLDS_CHILD
        /// Another game chosen: what load() read of the last one's common
        /// archive let go of, and its colours with it (load() again after).
        void forgetGame();
#endif

        /// What to show. `suit`: the player has one (without it, nothing).
        /// `weapon` is a kWeapon*; `extra` what it has besides, or -1.
        void setStatus(bool suit, int health, int armor, int weapon, int clip, int spare, int extra);
        /// The lamp: alight or not, and how much is in it (0..100).
        void setLamp(bool on, int percent, bool suit = true)
        {
            lampOn_ = on;
            lampPercent_ = percent;
            lampSuit_ = suit;
        }
        /// Being hurt: how red the screen is (0..256) and how strongly each
        /// of the four marks round its middle shows where from (front,
        /// right, back, left).
        void setHurt(int flash, const int *pain)
        {
            flash_ = flash;
            for (int i = 0; i < 4; ++i)
                pain_[i] = pain[i];
        }
        /// The light of an explosion the player is in (RGB555, added to the
        /// whole screen this frame; 0: none). GlowLayer's.
        void setWarm(std::uint16_t colour) { warm_ = colour; }
        /// Hurt by these kinds (HlPlayerMove.hpp's kHurt* bits): each one's
        /// picture shown at the left for a while, as the game shows them.
        void addHurt(unsigned kinds) { hurtKinds_ |= kinds; }
        /// Once a frame, after the pass: what the sprites are to show.
        void draw();
        /// Nothing of the display shown (no game: the title).
        void clear();
        /// What the last game said is forgotten (no suit, nothing in hand,
        /// no lamp): a new game's first frames show nothing of it.
        void forgetStatus()
        {
            suit_ = false;
            health_ = 100;
            armor_ = weapon_ = clip_ = spare_ = 0;
            extra_ = -1;
            lampOn_ = lampSuit_ = false;
            lampPercent_ = 100;
        }
        /// From the vertical blank interrupt: what draw() said, to the
        /// sprites' table.
        void onVblank();
        /// Right after the wait for the vertical blank, before anything is
        /// drawn: the sprites' pictures draw() changed, into video memory.
        void present();

        /// The glows' picture (GlowLayer): 32 by 32 levels of 0..15, in a
        /// corner of the sheet neither layout uses (x 224, y 96); null: none.
        void setGlowShape(const std::uint8_t *levels);
        static constexpr int kGlowTile = 512 + (96 / 8) * 32 + 224 / 8;
        /// The sprites were shown, added to the picture, the last frame
        /// (not under a fade's own blend, nor a view from a trigger_camera).
        bool spritesLive() const { return spritesLive_; }

    private:
        void compose(); ///< draw()'s work, into the sprites' copy
        // ... with the 640 set, where the game has each piece (HlHudLayout.hpp).
        void compose640();

    public:
        /// The marks' texture (an NEA_Material: HlUi.hpp, kDecalAtlasFile),
        /// or null on a card that has not got it.
        void *decalMaterial() const { return decalMaterial_; }

        /// Put the loading picture on the top screen and leave it there:
        /// returns once it is what the screen shows. False if the card has
        /// not got it (nothing is changed). hideLoading() when the map is in.
        bool showLoading();
        void hideLoading();

        /// The Valve intro (HlUi.hpp, kIntroFile) on the top screen, with its
        /// sound, until it is over or a button or the stylus ends it. False
        /// (and nothing shown) if the card has not got it.
        bool playIntro();
        /// The title screen's top: the game's menu background with its
        /// animated title over it, drawn by menuFrame() -- once a frame,
        /// straight after the wait for the screen -- until hideMenu(), which
        /// leaves what is on the screen as a picture. False if the card has
        /// not got the background.
        bool showMenu();
        void menuFrame();
        void hideMenu();
        /// The animated title drawn with the picture, or not (a lobby's
        /// top screen has other things to say).
        void setMenuLogo(bool on) { menuLogoOn_ = on; }
        /// Whether the title screen's top is up (showMenu, not yet hideMenu).
        bool menuShowing() const { return menuMaterial_ != nullptr; }

    private:
        bool atlas_ = false; ///< the display's pictures are read (HudDraw.arm.cpp)
        bool spritesLive_ = false;
        bool atlas640_ = false; ///< ... and they are the 640 set's (kHud640File)
        bool train_ = false; ///< the pictures of a driven train's speed are read (kHudTrainFile)
        void *decalMaterial_ = nullptr;
        void *decalPalette_ = nullptr;
        void *loadingMaterial_ = nullptr;
        void *loadingPalette_ = nullptr;
        bool menuLogoOn_ = true;
        // The title screen's (showMenu): its background, its animated title
        // (a texture rewritten as it moves, the file it moves through), and
        // which of its pictures is shown, since when.
        void *menuMaterial_ = nullptr;
        void *menuPalette_ = nullptr;
        void *logoMaterial_ = nullptr;
        void *logoPalette_ = nullptr;
        void *logoFile_ = nullptr;
        int logoFrames_ = 0;
        int logoFrame_ = -1;
        std::uint32_t logoMicros_ = 0;
        std::uint32_t menuSince_ = 0;
        bool suit_ = false;
        bool lampOn_ = false;
        bool lampSuit_ = false; ///< the suit is worn: its lamp is shown, alight or not
        int flash_ = 0;
        std::uint16_t warm_ = 0;
        int pain_[4] = {0, 0, 0, 0};
        unsigned hurtKinds_ = 0;
        int lampPercent_ = 100;
        int health_ = 100, armor_ = 0, weapon_ = 0, clip_ = 0, spare_ = 0, extra_ = -1;
    };

    HL_ENGINE_OBJECT(HudDraw, hudDraw);
} // namespace hlrt
