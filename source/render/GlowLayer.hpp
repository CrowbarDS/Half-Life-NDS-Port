#pragma once

// The map's glows (render mode 3: env_glow, and a sprite drawn as one) as
// sprites of the top screen's 2D engine, added to the 3D picture.
//
// The DS's 3D engine cannot add one colour to another; its 2D engine can.
// The display's sprites are already blended with the 3D picture so that
// they add to it (HudDraw.arm.cpp: BLDALPHA 16 and 16, which sums and stops
// at full). A glow put there brightens the wall behind it as the game's
// does, where a 3D quad can only cover it with its own colour.
//
// The game draws a glow the same way: no depth test, a line from the eye
// that must reach it (glass and models do not stop it), its size kept on the
// screen and its brightness falling with distance (HlSprite.hpp,
// glowFalloff), and nothing of it when its middle is off the screen.
//
// What there is room for: one picture, 32 by 32 in sixteen levels, in a
// corner of the display's sheet (HudDraw) -- made from the map's own glow
// sprite as it loads, the one most of its glows use; eight sprites, each
// scaled by a matrix of its own and shown up to 64 pixels across; five of
// the sprites' palettes, each a glow's colour at one brightness (glows near
// enough alike share one). A glow that does not fit -- another sprite's, too
// big, over the gun, past the eight or the five -- is drawn as before, a
// quad in the 3D picture.

#include <cstdint>

#include "HlTexture.hpp"

namespace hlrt
{
    class GlowLayer
    {
    public:
        /// The map has loaded: which of its sprites the glows are to be
        /// made from (-1: none of them), for SpriteCache to hand its file
        /// to takePicture() as it reads it.
        int chooseSprite();
        /// The picture of sprite chooseSprite() named, from its file as read
        /// (a texture: HlTexture.hpp; `body` is what follows the header).
        static void takePicture(const hldata::TextureHeader &header, const std::uint8_t *body, void *context);
        /// The map is gone: no picture, no glows.
        void forget();

        /// Before a pass is drawn, once the camera is where it will be: which
        /// glows the 2D engine shows this pass (the rest are left to the 3D
        /// picture), and their sprites made.
        void choose();
        /// Whether record `index`'s glow is the 2D engine's this pass (shown,
        /// or hidden behind something): not to be drawn in 3D.
        bool taken(int index) const;
        /// ... and whether effect `n`'s (a kEffectGlow) is.
        bool takenEffect(int n) const { return (takenEffects_ >> n) & 1; }

        /// From HudDraw::draw() and clear(), once the pass is over: what
        /// choose() made, for the vertical blank to show with the rest of
        /// the frame. (`shown` false: none.)
        void publish(bool shown);
        /// From the vertical blank: the sprites, their matrices and colours.
        void onVblank();

        static constexpr int kSprites = 8;
        static constexpr int kFirstSprite = 48; ///< after the display's (HudDraw: 16 + 32)
        static constexpr int kFirstMatrix = kFirstSprite / 4;
        static constexpr int kBanks = 5;
        static constexpr int kFirstBank = 11; ///< after the display's (HudDraw: 1..10)
        static constexpr int kSide = 32;      ///< the picture's

    private:
        int sprite_ = -1;         ///< the map's sprite the picture is of
        bool picture_ = false;    ///< ... and it was made
        std::uint16_t tint_ = 0x7FFF; ///< its colour (RGB555), at its brightest

        static constexpr int kTaken = 16;
        std::int16_t taken_[kTaken];
        int takenCount_ = 0;
        std::uint64_t takenEffects_ = 0; ///< a bit each (kMaxEffects: 48)

        // What choose() made, and what the vertical blank shows: each
        // sprite's three words and its matrix's first number (8.8), its
        // matrix's last; the palettes' colours.
        std::uint16_t shadow_[kSprites][4];
        std::uint16_t matrixY_[kSprites];
        std::uint16_t shadowColour_[kBanks][15];
        std::uint16_t shown_[kSprites][4];
        std::uint16_t shownMatrixY_[kSprites];
        std::uint16_t shownColour_[kBanks][15];
        int shadowCount_ = 0, shownCount_ = 0;
        int banks_ = 0, shownBanks_ = 0;
    };

    extern GlowLayer glowLayer;
} // namespace hlrt
