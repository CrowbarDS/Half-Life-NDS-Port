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
        /// What the lamp's light may cost a frame, while it is
        /// alight: the surfaces in its beam are drawn twice.
        static constexpr int kLampPolygons = 120;
        static constexpr int kLampVertices = 480;
        bool lampAlight() const { return lampOn_; }

        /// Load the display's texture. After the 3D engine is brought up
        /// (and again each time it is: forget() first).
        void load();
        void forget();

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

    private:
        void compose(); ///< draw()'s work, into the sprites' copy
        // ... with the 640 set, where the game has each piece (HlHudLayout.hpp).
        void compose640();

    public:
        /// The marks' texture (an NEA_Material: HlUi.hpp, kDecalAtlasFile),
        /// or null on a card that has not got it.
        void *decalMaterial() const { return decalMaterial_; }
        /// The lamp's picture of light (an NEA_Material, kLampTexels a
        /// side), which the world's lamp pass projects; null if it could
        /// not be made.
        void *lampMaterial() const { return lampMaterial_; }
        static constexpr int kLampTexels = 32;

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

    private:
        bool atlas_ = false; ///< the display's pictures are read (HudDraw.arm.cpp)
        bool atlas640_ = false; ///< ... and they are the 640 set's (kHud640File)
        void *lampMaterial_ = nullptr;
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
        int pain_[4] = {0, 0, 0, 0};
        unsigned hurtKinds_ = 0;
        int lampPercent_ = 100;
        int health_ = 100, armor_ = 0, weapon_ = 0, clip_ = 0, spare_ = 0, extra_ = -1;
    };

    HL_ENGINE_OBJECT(HudDraw, hudDraw);
} // namespace hlrt
