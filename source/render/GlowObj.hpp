#pragma once

// The map's glows as the game draws them: light ADDED to the picture.
//
// The 3D engine cannot add: it blends, src * a + dst * (1 - a), and a faint
// glow so drawn is a flat disc that greys what is behind it. The 2D engine
// can: a semi-transparent sprite over the 3D layer, with the HUD's blend
// (EVA = EVB = 16), is min(31, sprite + picture) -- black adds nothing. And a
// glow in the game is drawn with nothing in front of it hidden (no depth
// test; the line from the eye says whether it is seen: sightBlockedBeyond),
// which a sprite over the picture is too.
//
// Room is short: the sprites' tiles are all TopText's and the HUD's, but for
// one 32x32 cell of the HUD's sheet. So one picture serves: a cone made at
// map load from the map's most used glow sprite, made round and smooth (16
// levels) from its rings. Each glow drawn so is an affine sprite scaled to
// its size on the screen, in one of five palettes -- its colour times how
// bright it is, a ramp of 15 steps. A glow of another picture, one too big
// for the cell's double-size box, or one past the palettes, stays a 3D quad.

#include <cstdint>

#include "HlFixed.hpp"

namespace hlrt
{
    class GlowObj
    {
    public:
        /// Whether glows are drawn so at all (a prototype: false draws them
        /// all as before).
        static constexpr bool kOn = true;

        /// At map load: the cone, from the map's most used glow sprite.
        void load();
        void forget();
        /// Each pass, before the effects are drawn.
        void beginFrame();
        /// A glow of sprite `sprite` at `at`, `half` units across from its
        /// middle, `alpha` (1..31) of its colour `colour` (RGB15): true if it
        /// is drawn as a sprite (the caller draws nothing), false if it is
        /// to be drawn as a 3D quad.
        bool place(unsigned sprite, const hldata::fx *at, int half, int alpha, std::uint16_t colour);
        /// After the pass: what the vertical blank is to put in.
        void publish(bool shown);
        /// In the vertical blank (HudDraw::onVblank): stores only.
        void onVblank();
        /// The cell's tiles, 32x32 at 4 bits (HudDraw puts them in its
        /// sheet); null when there are none.
        const std::uint32_t *cell() const { return sprite_ >= 0 ? cell_ : nullptr; }
        /// The cell's place in the HUD's sheet, in pixels.
        static constexpr int kCellX = 224, kCellY = 96;
        /// How much of a glow's light is taken off, of 31 (the screen's own
        /// steps): the game's light fainter than that is a gradient the eye
        /// does not see, here a step of one -- a disc with an edge. 2 hides
        /// c2a1's lamp glow at 459 units, as the game shows it; 1 keeps more
        /// of a near lamp's halo. (To be judged on a DS Lite's and a DSi's
        /// screens, which differ from the emulator's near black.)
        static constexpr int kFloor = 2;

    private:
        static constexpr int kMax = 24;       ///< sprites at once (affine groups 0..23)
        static constexpr int kFirstOam = 48;  ///< after TopText's 16 and the HUD's 32
        static constexpr int kBanks = 5;      ///< palettes 11..15
        static constexpr int kFirstBank = 11;
        int sprite_ = -1;
        std::uint32_t cell_[32 * 32 / 8] = {};
        int count_ = 0, banks_ = 0;
        std::uint32_t bankKey_[kBanks] = {};
        std::uint16_t attr_[kMax][3] = {};
        std::int16_t scale_[kMax] = {};
        std::uint16_t palette_[kBanks][15] = {};
        std::int32_t pixelsUp_ = 0, pixelsAcross_ = 0; ///< Q12: pixels per unit across at a unit's depth
        /// Where the gun in hand is on the screen this pass: the outline
        /// (convex, the turn of its points one way) of its box's corners --
        /// a glow over it is drawn as a 3D quad, which the gun, drawn after
        /// it, covers, as the game's gun covers its glows. And each side's
        /// length, for how far a glow is from it. (No points: no gun.)
        int gunPoints_ = 0;
        std::int32_t gunX_[8] = {}, gunY_[8] = {}, gunSide_[8] = {};
        bool overGun(int x, int y, int r) const;
        /// The ramps made (a glow's colour times its brightness, 15 steps,
        /// less kFloor), kept from pass to pass: made again only for a
        /// colour or a brightness not among them. Key 0: none.
        static constexpr int kRamps = 8;
        std::uint32_t rampKey_[kRamps] = {};
        bool rampLit_[kRamps] = {};
        std::uint16_t ramp_[kRamps][15] = {};
        int rampNext_ = 0;
        int rampOf(std::uint32_t key, std::uint16_t colour, int alpha);
        // What the vertical blank puts in.
        int shownCount_ = 0, shownBanks_ = 0;
        std::uint16_t shownAttr_[kMax][3] = {};
        std::int16_t shownScale_[kMax] = {};
        std::uint16_t shownPalette_[kBanks][15] = {};
    };

    extern GlowObj glowObj;
} // namespace hlrt
