#include "global_header.hpp"

#include "render/ScreenFade.hpp"

#include "Global.hpp"

#include "render/FrameFreeze.hpp"

namespace hlrt
{
    ScreenFade screenFade;

    namespace
    {
        constexpr std::uint16_t kBlack = 0, kWhite = 0x7FFF;
        // The colour's layer, in the first 16 KB of Bank D: tile 0 all of
        // colour 1, and a map of tile 0 at 4 KB.
        constexpr int kLayerMapBase = 2;
        // What the HUD and the top screen's writing set: nothing blended but
        // their own sprites, added to what is behind them.
        constexpr std::uint16_t kPlainBlend = 0x3F00, kPlainAlpha = 16 | (16 << 8);
    } // namespace

    bool ScreenFade::layerUsable() const
    {
#ifdef HLDS_CHILD
        // (Sent the game, on a DS, there is no layer: a coloured fade is not
        // shown, a black one is the brightness. In DSi mode, as on a card.)
        if (!global.twlChild)
            return false;
#endif
        // (Bank D is the frozen picture's while there is one.)
        return !frameFreeze.frozen();
    }

    void ScreenFade::showLayer(int eva)
    {
        if (!layer_)
        {
            vramSetBankD(VRAM_D_MAIN_BG_0x06000000);
            auto *tile = reinterpret_cast<volatile std::uint32_t *>(0x06000000);
            for (int i = 0; i < 8; ++i)
                tile[i] = 0x11111111u;
            auto *map = reinterpret_cast<volatile std::uint32_t *>(0x06000000 + kLayerMapBase * 0x800);
            for (int i = 0; i < 32 * 32 / 2; ++i)
                map[i] = 0;
            REG_BG1CNT = BG_32x32 | BG_COLOR_16 | BG_TILE_BASE(0) | BG_MAP_BASE(kLayerMapBase) | BG_PRIORITY(0);
            REG_BG1HOFS = 0;
            REG_BG1VOFS = 0;
            REG_DISPCNT |= DISPLAY_BG1_ACTIVE;
            layer_ = true;
            blend_ = -1;
        }
        BG_PALETTE[1] = colour_;
        // (Every time, not only when it changes: the HUD and the writing set
        // their own when they are set up.)
        REG_BLDCNT = BLEND_ALPHA | BLEND_SRC_BG1 | BLEND_DST_BG0 | BLEND_DST_SPRITE | BLEND_DST_BACKDROP;
        REG_BLDALPHA = static_cast<std::uint16_t>(eva | ((16 - eva) << 8));
        blend_ = eva;
    }

    void ScreenFade::hideLayer()
    {
        if (!layer_)
            return;
        layer_ = false;
        blend_ = -1;
        REG_DISPCNT &= ~DISPLAY_BG1_ACTIVE;
        REG_BLDCNT = kPlainBlend;
        REG_BLDALPHA = kPlainAlpha;
        if (!frameFreeze.frozen())
            vramSetBankD(VRAM_D_LCD);
    }

    void ScreenFade::layerLost()
    {
        if (!layer_)
            return;
        layer_ = false;
        blend_ = -1;
        REG_DISPCNT &= ~DISPLAY_BG1_ACTIVE;
        REG_BLDCNT = kPlainBlend;
        REG_BLDALPHA = kPlainAlpha;
    }

    void ScreenFade::apply(bool advance)
    {
        // The fade in 1/256ths of the hardware's steps; shown as the step
        // below it, or the one above as often as it is owed.
        const int wanted = fade_ * 16 * 256 / 255;
        int step = wanted >> 8;
        if (advance)
        {
            carried_ += wanted & 255;
            if (carried_ >= 256)
            {
                carried_ -= 256;
                ++step;
            }
        }
        else if ((wanted & 255) >= 128)
        {
            ++step;
        }
        if (wanted == 0)
            carried_ = 0;

        const int dim = dim_ ? 7 : 0;
        // Into any colour but white, the layer -- black too, so that the
        // writing over it is still read (TopText: above it); without the
        // layer, black is the brightness, and the writing goes with it.
        const bool layer = !curtain_ && colour_ != kWhite && fade_ > 0 && layerUsable();
        int level; // the hardware's brightness: -16 black .. 16 white
        if (curtain_)
            level = -16;
        else if (colour_ == kWhite && step > 0)
            level = step;
        else if (colour_ == kBlack && !layer)
            level = -(step > dim ? step : dim);
        else
            level = -dim;

        if (layer)
            showLayer(step);
        else
            hideLayer();

        if (level == shown_)
            return;
        shown_ = level;
        setBrightness(1, level);
    }

    void ScreenFade::setFade(int alpha, std::uint16_t colour)
    {
        fade_ = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
        colour_ = static_cast<std::uint16_t>(colour & 0x7FFF);
    }

    void ScreenFade::setDim(bool under)
    {
        dim_ = under;
        apply(false);
    }

    void ScreenFade::curtainDown()
    {
        // Between two pictures, not part of the way down one: the top of
        // the old picture over black is a tear, if only for a frame.
        if (!curtain_)
            swiWaitForVBlank();
        curtain_ = true;
        wait_ = 0;
        fade_ = 0;
        apply(false);
    }

    void ScreenFade::curtainUp(int frames)
    {
        wait_ = frames;
        if (frames <= 0)
        {
            if (curtain_)
                swiWaitForVBlank(); // (as it came down: between two pictures)
            curtain_ = false;
            apply(false);
        }
    }

    void ScreenFade::tick()
    {
        if (curtain_ && wait_ > 0 && --wait_ == 0)
            curtain_ = false;
        apply(true);
    }
} // namespace hlrt
