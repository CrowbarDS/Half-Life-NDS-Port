#include "global_header.hpp"

#include "render/ScreenFade.hpp"

#include "Global.hpp"

#include "HlPolygonIds.hpp"
#include "render/Camera.hpp"
#include "render/FrameFreeze.hpp"
#include "render/TopText.hpp"

namespace hlrt
{
    ScreenFade screenFade;

    namespace
    {
        constexpr std::uint16_t kBlack = 0, kWhite = 0x7FFF;
        // What the HUD and the top screen's writing set: nothing blended but
        // their own sprites, added to what is behind them.
        constexpr std::uint16_t kPlainBlend = 0x3F00, kPlainAlpha = 16 | (16 << 8);

        // The colour's quad: a flat picture (as HudDraw's), through a
        // projection whose near plane is an eighth as far as the world's --
        // the weapon in hand has its own at a quarter (ModelDraw::drawHeld),
        // and the quad is in front of that too: at 0.54 units, its near
        // plane at 0.5. (The pixels it covers are the flat pictures', so
        // the screen's, and a margin.)
        constexpr int kQuadShrink = 3;
        constexpr int kQuadScale = 23;   ///< of 4096: 3058 of the camera's hudPixel depth -> 17 (0.54 units)
        constexpr int kQuadDepth = 3058; ///< HudDraw's kDepth
        constexpr int kQuadMargin = 32;  ///< pixels past each edge

        void corner(int sx, int sy)
        {
            GFX_VERTEX16 = (static_cast<std::uint32_t>((96 - sy) * camera.hudPixel) << 16) |
                           (static_cast<std::uint32_t>((sx - 128) * camera.hudPixelX) & 0xFFFF);
            GFX_VERTEX16 = static_cast<std::uint32_t>(-kQuadDepth) & 0xFFFF;
        }
    } // namespace

    bool ScreenFade::layerUsable() const
    {
        // (While the picture is frozen there is no 3D picture to put it
        // over: FrameFreeze.)
        return !frameFreeze.frozen();
    }

    void ScreenFade::showLayer(int step)
    {
        if (!layer_)
        {
            layer_ = true;
            // (The top screen's writing in front of it, read on the colour as
            // the game shows it: not added to what is behind, while it is up.)
            topText.blendChanged();
        }
        // The quad's alpha of 31, and how much of the sprites (HUD and
        // glows, added to the picture) is left: they are under the fade.
        // Both shown with the picture they go with (onVblank).
        const int alpha = (step * 31 + 8) >> 4;
        const int state = enterCriticalSection();
        quadAlpha_ = alpha;
        quadColour_ = colour_;
        eva_ = 16 - step;
        leaveCriticalSection(state);
    }

    void ScreenFade::hideLayer()
    {
        if (!layer_)
            return;
        layer_ = false;
        const int state = enterCriticalSection();
        quadAlpha_ = 0;
        eva_ = -1;
        leaveCriticalSection(state);
        topText.blendChanged();
    }

    void ScreenFade::layerLost()
    {
        hideLayer();
    }

    void ScreenFade::drawQuad()
    {
        // (A quad's alpha of 0 is drawn as its outline: not drawn at all.)
        if (quadAlpha_ <= 0)
            return;
        drawnAlpha_ = quadAlpha_;
        drawnEva_ = eva_;

        int projection[16];
        glGetFixed(GL_GET_MATRIX_PROJECTION, projection);
        m4x4 nearer;
        for (int k = 0; k < 16; ++k)
            nearer.m[k] = projection[k];
        nearer.m[14] = projection[14] >> kQuadShrink;
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadMatrix4x4(&nearer);

        MATRIX_CONTROL = GL_MODELVIEW;
        MATRIX_IDENTITY = 0;
        MATRIX_SCALE = kQuadScale;
        MATRIX_SCALE = kQuadScale;
        MATRIX_SCALE = kQuadScale;
        GFX_POLY_FORMAT = POLY_ALPHA(static_cast<unsigned>(quadAlpha_)) | POLY_ID(hldata::kIdScreenFade) |
                          POLY_CULL_NONE;
        GFX_TEX_FORMAT = 0;
        GFX_COLOR = quadColour_;
        GFX_BEGIN = GL_QUADS;
        corner(-kQuadMargin, -kQuadMargin);
        corner(-kQuadMargin, 192 + kQuadMargin);
        corner(256 + kQuadMargin, 192 + kQuadMargin);
        corner(256 + kQuadMargin, -kQuadMargin);

        glMatrixMode(GL_PROJECTION);
        glPopMatrix(1);
        glMatrixMode(GL_MODELVIEW);
    }

    // (ITCM round three, clean counts: missed often for its size.)
    HL_ITCM void ScreenFade::frameDrawn()
    {
        // The picture just handed over shows from the next blank: the
        // sprites' share goes with it. (Nothing drawn: the sprites as they
        // are with no fade.)
        const int state = enterCriticalSection();
        pendingEva_ = drawnAlpha_ > 0 ? drawnEva_ : -1;
        drawnAlpha_ = 0;
        pending_ = true;
        leaveCriticalSection(state);
    }

    HL_ITCM void ScreenFade::onVblank()
    {
        if (!pending_)
            return;
        pending_ = false;
        if (pendingEva_ == shownEva_)
            return;
        shownEva_ = pendingEva_;
        REG_BLDCNT = kPlainBlend;
        REG_BLDALPHA = static_cast<std::uint16_t>(shownEva_ < 0 ? kPlainAlpha : (shownEva_ | (16 << 8)));
    }

    HL_ITCM void ScreenFade::apply(bool advance)
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
        // Into any colour but white, the quad -- black too, so that the
        // writing over it is still read (TopText: in front of it); without
        // the quad, black is the brightness, and the writing goes with it.
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

        if (layer && step > 0)
            showLayer(step);
        else
            hideLayer();

        if (level == shown_)
            return;
        shown_ = level;
        setBrightness(1, level);
    }

    HL_ITCM void ScreenFade::setFade(int alpha, std::uint16_t colour)
    {
        fade_ = alpha < 0 ? 0 : (alpha > 255 ? 255 : alpha);
        colour_ = static_cast<std::uint16_t>(colour & 0x7FFF);
    }

    HL_ITCM void ScreenFade::setDim(bool under)
    {
        dim_ = under;
        apply(false);
    }

    void ScreenFade::curtainDown()
    {
        // Between two pictures, not part of the way down one: the top of
        // the old picture over black is a tear, if only for a frame.
        if (!curtain_)
            Global::waitVBlank();
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
                Global::waitVBlank(); // (as it came down: between two pictures)
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
