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

// THIS FILE IS IN ITCM (the name says so to the linker). Keep it to the frame's
// inner loops: no standard-library containers, no strings, nothing that would
// drag template code into the 32 KB.

namespace hlrt
{

    using namespace hldata;
    using namespace worldparts;

#ifdef HLDS_BOX_TEST
    namespace
    {
        /// The geometry engine's BOX_TEST: whether any of the box (mins,
        /// maxs: vertex numbers) is in the view as the hardware clips it.
        /// Waits for the answer (GBATEK: about a hundred of its cycles).
        inline bool boxInView(const std::int16_t *box)
        {
            auto pair = [](int lo, int hi) -> std::int32_t
            { return static_cast<std::int32_t>((lo & 0xFFFF) | (static_cast<std::uint32_t>(hi) << 16)); };
            GFX_BOX_TEST = pair(box[0], box[1]);
            GFX_BOX_TEST = pair(box[2], box[3] - box[0]);
            GFX_BOX_TEST = pair(box[4] - box[1], box[5] - box[2]);
            while (GFX_STATUS & GFX_STATUS_TEST_BUSY)
            {
            }
            return (GFX_STATUS & BIT(1)) != 0;
        }
    } // namespace
#endif

    void WorldDraw::walk()
    {
        const LevelView &view = level.view;
        const std::uint16_t frame = visFrame_;
        const CullPlane *planes = camera.planes;
        const fx *eye = camera.pos;

        std::memset(chosen_, 0, chosenWords_ * 4);
        std::memset(far_, 0, chosenWords_ * 4);
        stats_.sphereOut = 0;
        stats_.boxOut = 0;
        stats_.boxSkipped = false;
#ifdef HLDS_BOX_TEST
        int sphereOut = 0, boxOut = 0;
        std::memset(out_, 0, chosenWords_ * 4);
        // (Not while the DSi's DMA may still be feeding the geometry engine:
        // THE BOX TEST, WorldDraw.hpp.)
        const bool boxes = boxTest_ && !(list_ != nullptr && ndmaBusy(0));
        stats_.boxSkipped = boxTest_ && !boxes;
        if (boxes)
        {
            // What the test needs of the polygon format (GBATEK): far-plane
            // crossing and one-dot polygons kept, set by a begin and end
            // with nothing in them. The world's own is put back after.
            NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), kCull,
                           static_cast<NEA_OtherFormatEnum>(polygonMode() | NEA_RENDER_FAR_CLIPPED |
                                                            NEA_RENDER_ONEA_DOT_POLYS));
            GFX_BEGIN = 0;
            GFX_END = 0;
        }
#endif
        int polygons = 0;
        int farPolygons = 0;
        int vertices = 0;
        int surfaces = 0;
        int dropped = 0, droppedPolygons = 0, droppedVertices = 0;
        // Under pressure, what is small and far is left out (WorldDraw.hpp):
        // how far in front of the eye a surface is, against its size.
        const int detail = detailRatio();
        if (detail != 0)
            std::memset(skipped_, 0, chosenWords_ * 4);
        // (Detail off: everything is kept, and is so when it comes on again.)
        if (detail == 0 && detailWasOn_)
            std::memset(detailKept_, 0xFF, chosenWords_ * 4);
        detailWasOn_ = detail != 0;
        const std::int32_t *forward = camera.forward;
        const std::int32_t eyeAt[3] = {fxToInt(eye[0]), fxToInt(eye[1]), fxToInt(eye[2])};
        stats_.cutDistance = 0;
        // How far along the view a surface's nearest point may be and still
        // be sent with the near ones: half of how far the view reaches --
        // where the fog was whole last pass, or with no fog, the map's own
        // farthest (THE ORDER, WorldDraw.hpp).
        const std::int32_t nearLimit = (fogEnd_ < kNoFog ? fogEnd_ : camera.mapFarUnits) >> kNearShift;
        // How far in front of the eye a surface's middle is (whole units).
        auto depthOf = [&](const Surface &surface) -> std::int32_t
        {
            return ((surface.center[0] - eyeAt[0]) * forward[0] + (surface.center[1] - eyeAt[1]) * forward[1] +
                    (surface.center[2] - eyeAt[2]) * forward[2]) >>
                   12;
        };
        // The models have been costed already (ModelDraw::plan): the world
        // has what they leave.
        // (... and the brushes, whose share is set aside too: planBrushes.)
        const int polygonBudget = this->polygonBudget() - modelPolygons_ - brushPolygons_ - lampPolygons_;
        const int vertexBudget = this->vertexBudget() - modelVertices_ - brushVertices_ - lampVertices_;

        // Once out of room, a surface that does not fit is left out and the
        // walk goes on for smaller ones -- farther, all of them -- until
        // there is almost no room left or this many in a row have not fitted
        // (what the walk itself costs is held to that).
        constexpr int kMissesMost = 48;
        constexpr int kRoomLeast = 8;
        int misses = 0;
        int sp = 0;
        // One surface met, through a leaf that lists it or a node it lies on:
        // taken, if it faces the eye and is in view and there is room for it.
        // False when there is no more room at all: the walk ends.
        auto take = [&](const std::uint32_t s) -> bool
        {
            // (`s`: the surface; false: the walk is over -- out of room.)
            std::uint32_t &word = chosen_[s >> 5];
            const std::uint32_t bit = 1u << (s & 31);
            if (word & bit)
                return true; // already taken through another leaf
#ifdef HLDS_BOX_TEST
            std::uint32_t &outWord = out_[s >> 5];
            if (outWord & bit)
                return true; // already left out through another leaf
#endif

            const Surface &surface = view.surfaces[s];
            const std::uint32_t planeIndex = surface.planeRef & kSurfacePlaneMask;
            const fx side =
                planeDistance(view.planes[planeIndex], view.planeTypes[planeIndex], eye);
            // A face on the plane's front is seen from in front; the back
            // flag turns that round.
            if ((surface.planeRef & kSurfacePlaneBack) ? side >= 0 : side <= 0)
                return true;
            if (sphereOutside(planes, surface.center, surface.radius))
            {
#ifdef HLDS_BOX_TEST
                outWord |= bit;
                ++sphereOut;
#endif
                return true;
            }
#ifdef HLDS_BOX_TEST
            if (boxes)
            {
                const std::int16_t *box = boxes_[s];
                if (box[0] <= box[3] && !boxInView(box))
                {
                    outWord |= bit;
                    ++boxOut;
                    return true;
                }
            }
#endif
            if (detail != 0)
            {
                // How far it is, the same however the eye is turned (the
                // largest of the three, and half the other two: within a
                // tenth of the length), so that turning on the spot does not
                // change what is left out. (Nothing near ever is: detailKeeps.)
                std::int32_t d[3];
                for (int a = 0; a < 3; ++a)
                {
                    const std::int32_t v = surface.center[a] - eyeAt[a];
                    d[a] = v < 0 ? -v : v;
                }
                const std::int32_t most = d[0] > d[1] ? (d[0] > d[2] ? d[0] : d[2]) : (d[1] > d[2] ? d[1] : d[2]);
                const std::int32_t distance = most + ((d[0] + d[1] + d[2] - most) >> 1);
                std::uint32_t &kept = detailKept_[s >> 5];
                const bool keep = detailKeeps(distance, surface.radius, detail, (kept & bit) != 0);
                kept = keep ? kept | bit : kept & ~bit;
                if (!keep)
                {
                    // (Counted once, though it is met in every leaf it
                    // is part of.)
                    std::uint32_t &seen = skipped_[s >> 5];
                    if (!(seen & bit))
                    {
                        seen |= bit;
                        ++dropped;
                        droppedPolygons += surface.polygons;
                        droppedVertices += surface.vertices;
                    }
                    return true;
                }
            }

            if (polygons + surface.polygons > polygonBudget ||
                vertices + surface.vertices > vertexBudget)
            {
                // Out of room: everything still on the stack is farther
                // away than this. The first one marks where the view is
                // cut (where the fog has to be whole); after it, only what
                // still fits.
                if (!stats_.budgetHit)
                {
                    stats_.budgetHit = true;
                    stats_.cutDistance = depthOf(surface) - surface.radius;
                }
                ++stats_.surfacesCut;
                if (++misses >= kMissesMost || polygonBudget - polygons < kRoomLeast ||
                    vertexBudget - vertices < kRoomLeast * 3)
                {
                    return false;
                }
                return true;
            }
            misses = 0;
            word |= bit;
            if (depthOf(surface) - surface.radius >= nearLimit)
            {
                far_[s >> 5] |= bit;
                farPolygons += surface.polygons;
            }
            polygons += surface.polygons;
            vertices += surface.vertices;
            ++surfaces;
            return true;
        };
        // (On a DSi, the game's own rule as well: a node gone through has its
        // surfaces drawn, whether or not a leaf that can be seen lists them --
        // what is seen through the gap of c1a1's broken sign.)
        const bool nodeSurfaces = nodeSurfaces_ && view.nodeSpans != nullptr;

        walkStack[sp++] = static_cast<std::int16_t>(view.models[0].headNode);
        while (sp > 0)
        {
            const std::int32_t index = walkStack[--sp];
            if (index >= 0)
            {
                if (nodeFrame_[index] != frame)
                    continue;
                const Node &node = view.nodes[index];
                if (boxOutside(planes, node.mins, node.maxs))
                    continue;
                if (nodeSurfaces)
                {
                    const NodeSpan &span = view.nodeSpans[index];
                    const std::uint16_t *mark = view.nodeMarks + span.first;
                    bool room = true;
                    for (int m = span.count; m > 0 && room; --m, ++mark)
                        room = take(*mark);
                    if (!room)
                    {
                        sp = 0;
                        continue;
                    }
                }
                const fx d = planeDistance(view.planes[node.plane], view.planeTypes[node.plane], eye);
                const int nearSide = d >= 0 ? 0 : 1;
                // Far first, so the near side is popped first.
                if (sp < kWalkStack - 2)
                    walkStack[sp++] = node.children[nearSide ^ 1];
                walkStack[sp++] = node.children[nearSide];
                continue;
            }

            const int leafIndex = -1 - index;
            if (leafFrame_[leafIndex] != frame)
                continue;
            const Leaf &leaf = view.leaves[leafIndex];
            if (boxOutside(planes, leaf.mins, leaf.maxs))
                continue;

            const std::uint16_t *mark = view.marks + leaf.firstMark;
            for (int m = leaf.markCount; m > 0; --m, ++mark)
            {
                if (!take(*mark))
                {
                    sp = 0;
                    break;
                }
            }
        }
        stats_.surfaces = surfaces;
        stats_.dropped = dropped;
        stats_.droppedPolygons = droppedPolygons;
        stats_.droppedVertices = droppedVertices;
        stats_.polygons = polygons;
        stats_.vertices = vertices;
        stats_.farPolygons = farPolygons;
#ifdef HLDS_BOX_TEST
        stats_.sphereOut = sphereOut;
        stats_.boxOut = boxOut;
        if (boxes)
            NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), kCull, polygonMode());
#endif
    }

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

    // Liquids sway: their textures' coordinates go through the texture
    // matrix (TextureCache asks for that), and the matrix slides them round
    // a small circle, a couple of texels across, every four seconds. In the
    // matrix a texel is 16: the hardware feeds it a sixteenth where a vertex
    // has a one.
    void WorldDraw::sway()
    {
        const auto phase = static_cast<std::uint16_t>(Global::drawClock() * 273u);
        fx sine, cosine;
        fxSinCos(phase, sine, cosine);
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glTranslatef32(sine << 5, cosine << 5, 0);
        glMatrixMode(GL_MODELVIEW);
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
