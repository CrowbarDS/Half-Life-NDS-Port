// The world's draw list sent to the geometry engine, and the frame's world
// drawn: what WorldDraw::walk (WorldDraw.itcm.arm.cpp, in ITCM) has chosen.
// In main memory: the instruction cache profiler (tools/icache_rank.py) found
// these miss little for what ITCM they would take -- about 30 us a frame out
// of it, where the AI's and the traces' own misses cost several times that.
#include "render/HudDraw.hpp"
#include "render/WorldDraw.hpp"

#include "render/EffectDraw.hpp"
#include "render/ModelDraw.hpp"

#include "Global.hpp"

#include <cstdlib>
#include <cstring>

#include <nds.h>

#include <NEAMain.h>

#include "DebugLog.hpp"
#include "HlPlaneMath.hpp"
#include "data/LevelFile.hpp"
#include "data/TextureCache.hpp"
#include "data/TextureStream.hpp"
#include "render/Camera.hpp"
#include "render/WorldDrawParts.hpp"

namespace hlrt
{

    using namespace hldata;
    using namespace worldparts;

    namespace
    {
        /// Start the DSi's own DMA feeding `words` words to the geometry
        /// engine, as fast as it asks for them, and come straight back. (As
        /// NEA's NEA_DisplayListDrawNDMA_GFX_FIFO does it, which waits.)
        inline void feed(const std::uint32_t *from, std::uint32_t words)
        {
            DC_FlushRange(from, words * 4);
            REG_NDMA_SRC(0) = reinterpret_cast<std::uint32_t>(from);
            REG_NDMA_DEST(0) = reinterpret_cast<std::uint32_t>(&GFX_FIFO);
            REG_NDMA_LENGTH(0) = words;
            REG_NDMA_BLENGTH(0) = 112;
            REG_NDMA_CR(0) = NDMA_ENABLE | NDMA_START_FIFO | NDMA_SRC_INC | NDMA_DST_FIX;
        }

        /// Wait for the DMA to have given the geometry engine all it was
        /// handed. False -- the DMA stopped -- if it has not within two
        /// frames: on a DSi it has been seen never to (in melonDS, c0a0d's
        /// tram stopped by the window, now and then), and a wait for ever is
        /// the whole game stopped. A frame drawn short is the price instead.
        inline bool listDone()
        {
            const std::uint32_t from = Global::vblanks();
            while (ndmaBusy(0))
            {
                if (Global::vblanks() - from >= 2)
                {
                    REG_NDMA_CR(0) = 0;
                    return false;
                }
            }
            return true;
        }
    } // namespace

    void WorldDraw::emitList()
    {
        const LevelView &view = level.view;
        std::uint32_t bound = 0xFFFFFFFFu;
        // `sent` .. `at` is put together but not yet handed over. It is
        // handed over whenever the DMA has finished with the piece before:
        // the geometry engine is being fed while the rest is put together.
        std::uint32_t *at = list_;
        const std::uint32_t *sent = list_;
        const std::uint32_t *const end = list_ + kListWords;
        constexpr int kPiece = 512; // words worth starting the DMA for

        // The near surfaces, then the far (THE ORDER, WorldDraw.hpp).
        for (int tier = 0; tier < 2; ++tier)
        for (std::uint32_t w = 0; w < chosenWords_; ++w)
        {
            std::uint32_t bits = tier == 0 ? chosen_[w] & ~far_[w] : far_[w];
            while (bits != 0)
            {
                const int b = __builtin_ctz(bits);
                bits &= bits - 1;
                const Surface &surface = view.surfaces[(w << 5) + b];
                if (at + surface.gxWords + 4 > end)
                {
                    // No room left (it does not happen: see kListWords): all
                    // of it goes, and the list is begun again.
                    if (!listDone())
                    {
                        listStuck_ = true;
                        return;
                    }
                    if (at != sent)
                        feed(sent, static_cast<std::uint32_t>(at - sent));
                    if (!listDone())
                    {
                        listStuck_ = true;
                        return;
                    }
                    at = list_;
                    sent = list_;
                    if (surface.gxWords + 4 > kListWords)
                        continue;
                }
                if (surface.texture != bound)
                {
                    // What NEA_MaterialUse writes to registers, as commands.
                    bound = surface.texture;
                    const unsigned picture = textureCache.shown(bound);
                    noteTextureDrawn(picture);
                    const std::uint32_t *bind = textureCache.bind(picture);
                    *at++ = FIFO_COMMAND_PACK(FIFO_PAL_FORMAT, FIFO_COLOR, FIFO_TEX_FORMAT, FIFO_NOP);
                    *at++ = bind[1];
                    *at++ = 0x7FFF;
                    *at++ = bind[0];
                }
                std::memcpy(at, level.gxAt(surface), surface.gxWords * 4u);
                at += surface.gxWords;
                if (at - sent >= kPiece && !ndmaBusy(0))
                {
                    feed(sent, static_cast<std::uint32_t>(at - sent));
                    sent = at;
                }
            }
        }
        if (at != sent)
        {
            if (!listDone())
            {
                listStuck_ = true;
                return;
            }
            feed(sent, static_cast<std::uint32_t>(at - sent));
        }
        listSent_ = true;
    }

    void WorldDraw::finishList()
    {
        if (!listSent_)
            return;
        // Nothing else may be written to the geometry engine until the last
        // of the list has gone in.
        if (!listDone())
            listStuck_ = true;
        listSent_ = false;
    }

    void WorldDraw::emit()
    {
        // On a DSi, by display list -- with its DSP in use too: what jams a
        // DMA to the geometry engine is the DSP fetching through its own bus
        // (NEA's notes on its display lists), and the game's DSP is only
        // ever given words through the ARM9's port, which does not
        // (tools/hwtest/dsp_bus: none in thousands of frames, even during
        // the DMA).)
        if (list_ != nullptr && !listStuck_)
        {
            emitList();
            return;
        }
        const LevelView &view = level.view;
        // Surfaces are stored sorted by texture, so walking the bits in order
        // changes texture as seldom as it can: the near ones, then the far
        // (THE ORDER, WorldDraw.hpp).
        std::uint32_t bound = 0xFFFFFFFFu;

        for (int tier = 0; tier < 2; ++tier)
        for (std::uint32_t w = 0; w < chosenWords_; ++w)
        {
            std::uint32_t bits = tier == 0 ? chosen_[w] & ~far_[w] : far_[w];
            while (bits != 0)
            {
                const int b = __builtin_ctz(bits);
                bits &= bits - 1;
                const Surface &surface = view.surfaces[(w << 5) + b];
                if (surface.texture != bound)
                {
                    bound = surface.texture;
                    // Textures not on the card are baked light alone,
                    // textures streamed and not in memory yet are flattened.
                    const unsigned picture = textureCache.shown(bound);
                    noteTextureDrawn(picture);
                    textureCache.use(picture);
                }
                const std::uint32_t *word = level.gxAt(surface);
                // The FIFO takes packed command words as they are; a write
                // simply waits while it is full.
                for (int n = surface.gxWords; n > 0; --n)
                    GFX_FIFO = *word++;
            }
        }
    }

    void WorldDraw::draw()
    {
        if (!level.loaded() || chosen_ == nullptr)
            return;
        stats_.budgetHit = false;
        stats_.brushCut = false;
        stats_.surfacesCut = 0;
        stats_.brushesDropped = 0;
        stats_.brushesOverflow = 0;
        const int leaf = findLeaf(level.view, camera.pos);
        stats_.leaf = leaf;
        if (leaf != visLeaf_)
            markVisible(leaf);

        // The sky, behind everything, where any of it can be seen from here.
        if (level.view.header->skyTexture != 0 && leaf >= 0 &&
            (level.view.leaves[leaf].flags & kLeafSeesSky))
            drawSky();

        textureCache.frame();
        sway();
        {
            const DebugLog::Perf::Scope scope(DebugLog::Section::Walk);
            modelDraw.plan(modelPolygons_, modelVertices_);
            // (What is only seen -- sprites, beams, bursts -- is costed
            // with the models: the world has what both leave.)
            effectDraw.gather(modelPolygons_, modelVertices_);
            // (... and room for the lamp's light: as much as it lit the pass
            // before and a little more -- all it may have, the pass it comes on.)
            lampPolygons_ = lampVertices_ = 0;
            if (hudDraw.lampAlight())
            {
                const int mostPolygons = HudDraw::kLampPolygons >> (modelDraw.strained() ? 1 : 0);
                lampPolygons_ = lampLastPolygons_ < 0 ? mostPolygons : lampLastPolygons_ + 24;
                lampVertices_ = lampLastPolygons_ < 0 ? HudDraw::kLampVertices : lampLastVertices_ + 96;
                lampPolygons_ = lampPolygons_ > mostPolygons ? mostPolygons : lampPolygons_;
                lampVertices_ = lampVertices_ > HudDraw::kLampVertices ? HudDraw::kLampVertices : lampVertices_;
            }
            // (... and the marks and small things there are: nothing, with
            // none.)
            effectDraw.reserve(modelPolygons_, modelVertices_);
            planBrushes();
            walk();
            // ... and the models' share is counted with the world's from here
            // on, so that the brushes are held to what is really left.
            stats_.polygons += modelPolygons_;
            stats_.vertices += modelVertices_;
        }
        {
            const DebugLog::Perf::Scope scope(DebugLog::Section::Emit);
            emit();
            // While a display list is on its way, what does not need the
            // geometry engine: where the models' bones are.
            modelDraw.prepare();
            finishList();
            // The actors, between the world and its see-through brushes: they
            // are opaque, and glass is drawn over them.
            stats_.models = 0;
            stats_.modelsCut = 0;
            modelDraw.draw(stats_.models, stats_.modelsCut);
            drawBrushes(); // (and the effects, in among the glass)
            modelDraw.drawHeld();
        }

        // The usage registers are only meaningful once the geometry engine has
        // worked through everything sent. It keeps pace with the loop above, so
        // this wait is a few cycles.
        while (GFX_STATUS & BIT(27))
        {
        }
        stats_.hwPolygons = GFX_POLYGON_RAM_USAGE;
        stats_.hwVertices = GFX_VERTEX_RAM_USAGE;
    }
} // namespace hlrt
