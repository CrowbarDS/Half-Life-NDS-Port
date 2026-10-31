#pragma once

// The touch screen as something to draw on: one layer of the sub engine,
// 256 by 192 pixels in sixteen colours, every cell with a tile of its own
// (shared/HlFont.hpp draws on it); and the fonts it is written in.
//
// Video memory: bank H (32 KB, sub background) and nothing else, so that the
// four big banks stay free for 3D textures and capture.
//
//   0 ..  2 KB   the layer's map: cell (x, y) shows tile 64 + y * 32 + x
//   2 .. 26 KB   those 768 tiles
//
// The fonts are the game's own, cooked to the card (ui/font*.hfn). On a card
// that has not got them everything is written in the 8x8 letters that are in
// the ROM anyway, by the same code.

#include "HlFont.hpp"

namespace hlrt
{
    class TouchCanvas
    {
    public:
        /// Set the sub engine up. Idempotent, until reset().
        void create();
        /// The video mode was changed under it: the next create() starts over.
        void reset() { created_ = false; }

        hldata::Canvas &canvas() { return canvas_; }
        /// Drawn off the screen, into `tiles` (laid out as the screen's 768),
        /// until onScreen(); the screen's own tiles, to copy to and from.
        void offScreen(std::uint32_t *tiles) { canvas_.tiles = tiles; }
        void onScreen() { canvas_.tiles = screen_; }
        std::uint32_t *screenTiles() const { return screen_; }
        /// The one copy of the screen (24 KB, kept: a console sent the game
        /// has no room for two): the pad as it slides, a page being drawn.
        std::uint32_t *copyTiles();
        /// A page drawn off the screen, starting from what is on it: wiped
        /// and drawn again where it is seen, it is seen wiped, and blinks.
        /// Until pageOn(): the canvas, and tint()'s cells, are the copy's.
        /// Then what differs goes on, each word from the old to the new.
        /// (Within another: it is that one's.)
        void pageOff();
        void pageOn();
        /// kFontSmall, kFontMedium or kFontTitle -- or the plain letters,
        /// where the card has not got that one.
        const hldata::FontView &font(int which);
        /// The ROM's own 8x8 letters.
        const hldata::FontView &plain();
        /// The game's letters read in, if they are not yet (and the card --
        /// or what a host sent -- has them): once for good.
        void loadFonts();
        /// A block of cells, `w` by `h` from cell (`x`, `y`), in colours of
        /// its own -- palette `bank` (1..15) of the sub engine: the sixteen
        /// as ever but 1, 2 and 3, which are `three` (a picture in a colour
        /// of the player's: PROFILE's spray, a player's swatch in a lobby).
        /// Only what is drawn there in those three is changed, so the block
        /// is for that alone. Again with `three` null: as every cell is.
        void tint(int x, int y, int w, int h, const std::uint16_t *three, int bank = 1);
        /// A block of cells in sixteen colours altogether of its own, palette
        /// `bank`: a picture.
        void tintWhole(int x, int y, int w, int h, const std::uint16_t *sixteen, int bank);
        /// Every cell as every cell is (the game's pad, which has no tints).
        void untintAll() { tint(0, 0, 32, 24, nullptr); }

    private:
        bool created_ = false;
        bool loaded_ = false;
        hldata::Canvas canvas_;
        std::uint32_t *screen_ = nullptr;
        int pageDepth_ = 0;
        hldata::FontView fonts_[hldata::kFontCount];
        hldata::FontView plain_;
    };

    extern TouchCanvas touchCanvas;
    /// A player's colour (0..255, the game's hue), to show.
    std::uint16_t hueColour(int hue);
} // namespace hlrt
