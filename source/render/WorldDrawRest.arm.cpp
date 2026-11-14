#include "render/WorldDraw.hpp"
#include "HlBrushPlan.hpp"
#include "HlDsp.hpp"
#include "HlPolygonIds.hpp"

#include <cstdlib>
#include <malloc.h>
#include <cstring>

#include <nds.h>

#include <NEAMain.h>

#include "DebugLog.hpp"
#include "render/EffectDraw.hpp"
#include "render/HudDraw.hpp"
#include "Global.hpp"
#include "HlGxBounds.hpp"
#include "HlLamp.hpp"
#include "HlPlaneMath.hpp"
#include "game/Game.hpp"
#include "data/LevelFile.hpp"
#include "data/TextureCache.hpp"
#include "data/TextureStream.hpp"
#include "render/Camera.hpp"
#include "render/ModelDraw.hpp"
#include "render/WorldDrawParts.hpp"

// The parts of the world renderer that do NOT run from fast memory. ARM code
// still (the file name), for the 64-bit multiplies; but what is here runs a
// few times a frame, or once a map, and the instruction cache is enough for
// it. The fast memory is 32 KB and is kept for the two loops that run
// thousands of times a frame: the walk and the emit (WorldDraw.itcm.arm.cpp).

namespace hlrt
{
    // (Here and not in WorldDraw.itcm.arm.cpp: the code that sets it up
    // runs once, and that file's code is all in fast memory.)
    WorldDraw worldDraw;

    using namespace hldata;
    using namespace worldparts;

    bool WorldDraw::create()
    {
        destroy();
        const LevelView &view = level.view;
        chosenWords_ = (view.surfaceCount + 31) / 32;
        // calloc: a zero stamp never matches visFrame_, which starts at 1.
        nodeFrame_ = static_cast<std::uint16_t *>(std::calloc(view.nodeCount + 1, 2));
        leafFrame_ = static_cast<std::uint16_t *>(std::calloc(view.leafCount + 1, 2));
        chosen_ = static_cast<std::uint32_t *>(std::calloc(chosenWords_ + 1, 4));
        skipped_ = static_cast<std::uint32_t *>(std::calloc(chosenWords_ + 1, 4));
        far_ = static_cast<std::uint32_t *>(std::calloc(chosenWords_ + 1, 4));
#ifdef HLDS_BOX_TEST
        out_ = static_cast<std::uint32_t *>(std::calloc(chosenWords_ + 1, 4));
        boxes_ = static_cast<std::int16_t(*)[6]>(std::malloc(view.surfaceCount * sizeof(*boxes_) + 1));
        // (No room for them: the map is played by balls alone -- an
        // experiment does not stop a map loading.)
        boxTest_ = out_ != nullptr && boxes_ != nullptr;
        for (std::uint32_t s = 0; boxTest_ && s < view.surfaceCount; ++s)
        {
            const Surface &surface = view.surfaces[s];
            std::int16_t *box = boxes_[s];
            if (!gxStreamBounds(level.gxAt(surface), surface.gxWords, box, box + 3) ||
                !gxBoxFits(box, box + 3))
            {
                box[0] = 1; // (none: judged by the ball)
                box[3] = 0;
            }
        }
#endif
        detailKept_ = static_cast<std::uint32_t *>(std::malloc((chosenWords_ + 1) * 4));
        if (nodeFrame_ == nullptr || leafFrame_ == nullptr || chosen_ == nullptr || skipped_ == nullptr || far_ == nullptr ||
            detailKept_ == nullptr)
        {
            destroy();
            return false;
        }
        std::memset(detailKept_, 0xFF, (chosenWords_ + 1) * 4); // (all kept, to begin with)
        detailWasOn_ = false;
        camera.mapFarUnits = static_cast<int>(view.header->farUnits);
        // (Not having it is not an error: the surfaces are then written one
        // word at a time, as on a DS. On cache lines of its own: it is
        // flushed in pieces.)
        if (isDSiMode())
            list_ = static_cast<std::uint32_t *>(memalign(32, kListWords * 4));
        // (The game's node rule costs surfaces a DS has no room for.)
        nodeSurfaces_ = isDSiMode();
        listStuck_ = false; // (each map tries the DMA afresh)
        visFrame_ = 0;
        visLeaf_ = -1;
        return true;
    }

    void WorldDraw::destroy()
    {
        std::free(nodeFrame_);
        std::free(leafFrame_);
        std::free(chosen_);
        std::free(skipped_);
        skipped_ = nullptr;
        std::free(far_);
        far_ = nullptr;
#ifdef HLDS_BOX_TEST
        std::free(out_);
        out_ = nullptr;
        std::free(boxes_);
        boxes_ = nullptr;
#endif
        std::free(detailKept_);
        detailKept_ = nullptr;
        std::free(list_);
        list_ = nullptr;
        brushState_ = nullptr;
        nodeFrame_ = nullptr;
        leafFrame_ = nullptr;
        chosen_ = nullptr;
        chosenWords_ = 0;
        pressure_ = 0;
        cutPasses_ = easyPasses_ = sinceRelax_ = 0;
        relaxWait_ = kRelaxWait;
        fogEnd_ = kNoFog;
        fogOn_ = false;
    }

    void WorldDraw::markVisible(int leaf)
    {
        const LevelView &view = level.view;
        visLeaf_ = leaf;
        if (++visFrame_ == 0)
        {
            // The stamp wrapped: old stamps could now match by accident.
            std::memset(nodeFrame_, 0, view.nodeCount * 2);
            std::memset(leafFrame_, 0, view.leafCount * 2);
            visFrame_ = 1;
        }
        const std::uint16_t frame = visFrame_;

        auto stamp = [&](int visible)
        {
            leafFrame_[visible] = frame;
            // Up the tree until a node some earlier leaf already stamped.
            std::uint16_t node = view.leaves[visible].parent;
            while (node != kNoParent && nodeFrame_[node] != frame)
            {
                nodeFrame_[node] = frame;
                node = view.nodes[node].parent;
            }
        };

        const int count = view.header->visLeafCount;
        const std::int32_t ofs = view.leaves[leaf].visOfs;
        // (On a DS that reads them from the card, the row is read now.)
        const std::uint8_t *end = nullptr;
        const std::uint8_t *in = ofs < 0 ? nullptr : level.visRow(leaf, end);
        if (in == nullptr)
        {
            // No row: outside the world (or an unvised map, or a card that
            // would not give it). Everything shows, and the budget does the rest.
            for (int i = 1; i <= count; ++i)
                stamp(i);
            return;
        }

        // The BSP's own run-length rows: a non-zero byte is eight leaves, a
        // zero byte is followed by how many zero bytes it stands for.
        int index = 1; // bit 0 of the first byte is leaf 1
        while (index <= count && in < end)
        {
            const std::uint8_t bits = *in++;
            if (bits == 0)
            {
                if (in >= end)
                    break;
                index += 8 * *in++;
                continue;
            }
            for (int b = 0; b < 8 && index + b <= count; ++b)
            {
                if (bits & (1 << b))
                    stamp(index + b);
            }
            index += 8;
        }
    }

    bool WorldDraw::brushEye(std::uint32_t index, fx *eye, std::int32_t *axes) const
    {
        const Brush &brush = level.view.brushes[index];
        const fx *pos = brushState_ != nullptr ? brushState_[index].pos : brush.origin;
        // The eye in the brush's own space: its surfaces and their planes
        // never move, the brush does.
        static const std::uint16_t kNotTurned[3] = {0, 0, 0};
        const std::uint16_t *angles = brushState_ != nullptr ? brushState_[index].angles : kNotTurned;
        const bool turned = angles[0] != 0 || angles[1] != 0 || angles[2] != 0;
        for (int a = 0; a < 3; ++a)
            eye[a] = camera.pos[a] - pos[a];
        if (turned)
        {
            // ... and turned back the way the brush is turned: by the
            // transpose of its matrix.
            anglesToAxes(angles, axes);
            const fx d[3] = {eye[0], eye[1], eye[2]};
            for (int j = 0; j < 3; ++j)
            {
                eye[j] = static_cast<fx>((static_cast<std::int64_t>(d[0]) * axes[j] +
                                          static_cast<std::int64_t>(d[1]) * axes[3 + j] +
                                          static_cast<std::int64_t>(d[2]) * axes[6 + j]) >>
                                         kNormalShift);
            }
        }
        return turned;
    }

    void WorldDraw::brushCost(std::uint32_t index, int &polygons, int &vertices) const
    {
        const LevelView &view = level.view;
        const Model &model = view.models[view.brushes[index].model];
        fx eye[3];
        std::int32_t axes[9];
        brushEye(index, eye, axes);
        polygons = vertices = 0;
        const Surface *surface = view.surfaces + model.firstSurface;
        for (int n = model.surfaceCount; n > 0; --n, ++surface)
        {
            const std::uint32_t planeIndex = surface->planeRef & kSurfacePlaneMask;
            const fx side = planeDistance(view.planes[planeIndex], view.planeTypes[planeIndex], eye);
            if ((surface->planeRef & kSurfacePlaneBack) ? side >= 0 : side <= 0)
                continue;
            polygons += surface->polygons;
            vertices += surface->vertices;
        }
    }

    void WorldDraw::drawBrush(std::uint32_t index, int polygonId)
    {
        const std::uint8_t look =
            brushState_ != nullptr && brushState_[index].look < kBrushUnseen ? brushState_[index].look : 0;
        const LevelView &view = level.view;
        const Brush &brush = view.brushes[index];
        const Model &model = view.models[brush.model];
        const fx *pos = brushState_ != nullptr ? brushState_[index].pos : brush.origin;
        fx eye[3];
        std::int32_t axes[9]; // Q30
        const bool turned = brushEye(index, eye, axes);

        switch (static_cast<RenderClass>(brush.renderClass))
        {
        case RenderClass::Cutout:
            // Full alpha, but the A3I5 texture makes the hardware treat the
            // polygon as see-through: without the depth-update bit the solid
            // parts of a grate would not hide what is drawn behind it later.
            NEA_PolyFormat(31, polygonId, static_cast<NEA_LightEnum>(0), kCull,
                           static_cast<NEA_OtherFormatEnum>(polygonMode() |
                                                            NEA_TRANS_DEPTH_UPDATE));
            break;
        case RenderClass::Blend:
        case RenderClass::Colour:
            NEA_PolyFormat(look != 0 ? look : brush.alpha, polygonId, static_cast<NEA_LightEnum>(0), kCull,
                           polygonMode());
            break;
        default:
            // (Solid as made; see-through if something has since said so.)
            NEA_PolyFormat(look != 0 ? look : 31, look != 0 ? polygonId : 0, static_cast<NEA_LightEnum>(0),
                           kCull, polygonMode());
            break;
        }

        // Vertices are quarter units and positions are Q12 units: >> 10.
        const bool moved = pos[0] != 0 || pos[1] != 0 || pos[2] != 0 || turned;
        if (moved)
        {
            glPushMatrix();
            glTranslatef32(pos[0] >> 10, pos[1] >> 10, pos[2] >> 10);
            if (turned)
            {
                // The same matrix the collision uses, so that what is seen is
                // what is walked into. The hardware multiplies a ROW vector
                // by its matrices, which makes the one to load the transpose.
                // In Half-Life's axes: the swap to the hardware's is in the
                // view matrix, above this.
                m3x3 turn;
                for (int row = 0; row < 3; ++row)
                {
                    for (int column = 0; column < 3; ++column)
                        turn.m[row * 3 + column] = axes[column * 3 + row] >> (kNormalShift - 12);
                }
                glMultMatrix3x3(&turn);
            }
        }

        // Which face its '+' textures show, and -- a conveyor -- how far its
        // belt has slid.
        const bool switched = brushState_ != nullptr && brushState_[index].frame != 0;
        const fx slid = brushState_ != nullptr ? brushState_[index].scroll : 0;
        bool sliding = false;
        std::uint32_t bound = 0xFFFFFFFEu;
        const Surface *surface = view.surfaces + model.firstSurface;
        for (int n = model.surfaceCount; n > 0; --n, ++surface)
        {
            const std::uint32_t planeIndex = surface->planeRef & kSurfacePlaneMask;
            const fx side = planeDistance(view.planes[planeIndex], view.planeTypes[planeIndex], eye);
            if ((surface->planeRef & kSurfacePlaneBack) ? side >= 0 : side <= 0)
                continue;
            if (stats_.polygons + surface->polygons > polygonBudget() ||
                stats_.vertices + surface->vertices > vertexBudget())
            {
                stats_.brushCut = true;
                break;
            }
            if (surface->texture != bound)
            {
                bound = surface->texture;
                if (bound == kNoTexture)
                    GFX_TEX_FORMAT = 0;
                else
                {
                    const unsigned picture = textureCache.shown(bound, switched);
                    noteTextureDrawn(picture);
                    textureCache.use(picture);
                }
                if (bound != kNoTexture && (view.textures[bound].flags & kTextureScroll))
                {
                    // The belt: its picture goes the way the belt goes, so
                    // its coordinates go the other. (In the texture as it
                    // was cooked: that may be smaller than the game's.)
                    const TextureRef &ref = view.textures[bound];
                    const fx texels = ref.sourceWidth != 0
                                          ? static_cast<fx>((static_cast<std::int64_t>(slid) * ref.width) / ref.sourceWidth)
                                          : slid;
                    glMatrixMode(GL_TEXTURE);
                    glLoadIdentity();
                    glTranslatef32(-(texels << 4), 0, 0);
                    glMatrixMode(GL_MODELVIEW);
                    sliding = true;
                }
                else if (sliding)
                {
                    sway();
                    sliding = false;
                }
            }
            const std::uint32_t *word = level.gxAt(*surface);
            for (int k = surface->gxWords; k > 0; --k)
                GFX_FIFO = *word++;
            stats_.polygons += surface->polygons;
            stats_.vertices += surface->vertices;
            ++stats_.surfaces;
        }

        if (sliding)
            sway();
        if (moved)
            glPopMatrix(1);
    }

    namespace
    {
        // This pass's brushes, as planBrushes chose them (HlBrushPlan.hpp):
        // the solid ones near to far, the see-through ones far to near; a
        // room with more of either in view than a list holds leaves out the
        // farthest. Each with what it costs (polygons, vertices).
        constexpr int kMaxSolid = 128;
        constexpr int kMaxLater = 64;
        BrushList<kMaxSolid, true> solid;  ///< nearest first: drawn so, each that fits
        BrushList<kMaxLater, false> later; ///< farthest first: drawn so, among the effects
    } // namespace

    void WorldDraw::planBrushes()
    {
        const LevelView &view = level.view;
        const std::uint16_t frame = visFrame_;
        solid.clear();
        later.clear();
        int polygons = 0, vertices = 0;

        for (std::uint32_t i = 0; i < view.brushCount; ++i)
        {
            const Brush &brush = view.brushes[i];
            const bool active = brushState_ != nullptr ? brushState_[i].active
                                                       : (brush.flags & kBrushStartsOff) == 0;
            const fx *pos = brushState_ != nullptr ? brushState_[i].pos : brush.origin;
            if (!active || brush.renderClass == static_cast<std::uint8_t>(RenderClass::Hidden))
                continue;

            // In a leaf the eye can see? A brush that roams is not asked: it
            // is wherever its track has taken it, and the view test below is
            // the only one it gets.
            const std::uint16_t *leaf = view.brushLeaves + brush.firstLeaf;
            bool visible = (brush.flags & kBrushRoams) != 0;
            for (int n = brush.leafCount; n > 0 && !visible; --n, ++leaf)
                visible = leafFrame_[*leaf] == frame;
            if (!visible)
                continue;

            // Its box, moved by however far the brush is from where it was
            // built, against the view. A brush that turns is given the box of
            // the whole circle it sweeps.
            std::int16_t mins[3], maxs[3];
            std::int32_t centre[3];
            std::int32_t reach = 0;
            const bool pivots = (brush.flags & kBrushHasPivot) != 0;
            const bool tumbles = pivots && (brush.flags & kBrushTumbles) != 0;
            if (pivots)
            {
                for (int corner = 0; corner < 8; ++corner)
                {
                    const std::int32_t x =
                        ((corner & 1) ? brush.maxs[0] : brush.mins[0]) - fxToInt(brush.origin[0]);
                    const std::int32_t y =
                        ((corner & 2) ? brush.maxs[1] : brush.mins[1]) - fxToInt(brush.origin[1]);
                    const std::int32_t z =
                        tumbles ? ((corner & 4) ? brush.maxs[2] : brush.mins[2]) -
                                      fxToInt(brush.origin[2])
                                : 0;
                    // An over-estimate, and no square root.
                    const std::int32_t far =
                        (x < 0 ? -x : x) + (y < 0 ? -y : y) + (z < 0 ? -z : z);
                    if (far > reach)
                        reach = far;
                }
            }
            for (int a = 0; a < 3; ++a)
            {
                const std::int32_t shift = fxToInt(pos[a] - brush.origin[a]);
                mins[a] = static_cast<std::int16_t>(brush.mins[a] + shift);
                maxs[a] = static_cast<std::int16_t>(brush.maxs[a] + shift);
                if (pivots && (a < 2 || tumbles))
                {
                    mins[a] = static_cast<std::int16_t>(fxToInt(pos[a]) - reach);
                    maxs[a] = static_cast<std::int16_t>(fxToInt(pos[a]) + reach);
                }
                centre[a] = (mins[a] + maxs[a]) / 2 - fxToInt(camera.pos[a]);
            }
            if (boxOutside(camera.planes, mins, maxs))
                continue;
            // (An env_render may have made it unseen, or see-through.)
            const std::uint8_t look = brushState_ != nullptr ? brushState_[i].look : 0;
            if (look == kBrushUnseen)
                continue;
            const bool opaque = brush.renderClass == static_cast<std::uint8_t>(RenderClass::Opaque) && look == 0;
            // Under pressure, the small and far are left out, as with the
            // world's surfaces (WorldDraw.hpp) -- of what is see-through only:
            // what is solid (a door, a lift) goes by its distance and the
            // room there is, and nothing else.
            if (const int detail = opaque ? 0 : detailRatio())
            {
                // (As the world's surfaces: by how far, the same however the
                // eye is turned, and never what is near.)
                std::int32_t size = 0, most = 0, sum = 0;
                for (int a = 0; a < 3; ++a)
                {
                    size = size > maxs[a] - mins[a] ? size : maxs[a] - mins[a];
                    const std::int32_t d = centre[a] < 0 ? -centre[a] : centre[a];
                    most = d > most ? d : most;
                    sum += d;
                }
                if (!detailKeeps(most + ((sum - most) >> 1), size >> 1, detail, true))
                {
                    ++stats_.dropped;
                    continue;
                }
            }

            int costPolygons = 0, costVertices = 0;
            brushCost(i, costPolygons, costVertices);
            if (costPolygons == 0)
                continue; // (nothing of it faces the eye)
            // Halved coordinates keep the squares inside 32 bits across the
            // whole map.
            const std::int32_t distance = (centre[0] >> 1) * (centre[0] >> 1) +
                                          (centre[1] >> 1) * (centre[1] >> 1) +
                                          (centre[2] >> 1) * (centre[2] >> 1);
            polygons += costPolygons;
            vertices += costVertices;
            // In its list, by distance. When the list is full, the farthest
            // is what is left out (and counted).
            const auto out = opaque ? solid.add(static_cast<std::uint16_t>(i), distance, costPolygons, costVertices)
                                    : later.add(static_cast<std::uint16_t>(i), distance, costPolygons, costVertices);
            if (out.index != 0xFFFF)
            {
                polygons -= out.polygons;
                vertices -= out.vertices;
                ++stats_.brushesOverflow;
                ++stats_.brushesDropped;
            }
        }

        // Set aside for them, out of what the world would have: all they
        // cost, up to a third of the pass -- the world is never left with
        // nothing (what does not fit of them is left out, nearest first kept:
        // drawBrushes).
        brushPolygons_ = polygons < polygonBudget() / 3 ? polygons : polygonBudget() / 3;
        brushVertices_ = vertices < vertexBudget() / 3 ? vertices : vertexBudget() / 3;
    }

    bool WorldDraw::brushSeeThrough(int i) const
    {
        if (!level.loaded() || i < 0 || static_cast<std::uint32_t>(i) >= level.view.brushCount)
            return false;
        const std::uint8_t look = brushState_ != nullptr ? brushState_[i].look : 0;
        return level.view.brushes[i].renderClass != static_cast<std::uint8_t>(RenderClass::Opaque) || look != 0;
    }

    void WorldDraw::drawBrushes()
    {
        int drawn = 0;
        lampBrushCount_ = 0;
        // Whole, or (when even their share has run out) not at all.
        auto fits = [&](const std::int16_t *cost)
        {
            if (stats_.polygons + cost[0] <= polygonBudget() && stats_.vertices + cost[1] <= vertexBudget())
                return true;
            stats_.brushCut = true;
            ++stats_.brushesDropped;
            return false;
        };
        // The solid ones nearest first, each that fits (HlBrushPlan.hpp).
        bool take[kMaxSolid];
        brushesThatFit(solid, polygonBudget() - stats_.polygons, vertexBudget() - stats_.vertices, take);
        for (int n = 0; n < solid.count; ++n)
        {
            if (!take[n])
            {
                stats_.brushCut = true;
                ++stats_.brushesDropped;
                continue;
            }
            drawBrush(solid.index[n], 0);
            if (lampBrushCount_ < kMaxLampBrushes)
                lampBrushes_[lampBrushCount_++] = solid.index[n];
            ++drawn;
        }
        const int laterCount = later.count;

        // The see-through ones and the effects (EffectDraw) together, far
        // to near: both lists are in that order already. Each thing gets a
        // polygon ID of its own (HlPolygonIds.hpp: round and round; marks on
        // walls, fading models, the lamp's light and the head-up display have
        // theirs apart): the hardware will
        // not blend two polygons of the same ID over each other, which is
        // what keeps the back of a glass box from showing through its front
        // as a darker patch -- and lets a glow show through a pane.
        // (Their room was set aside before the walk -- EffectDraw::reserve
        // -- and is counted in already.)
        effectDraw.drawDecals();
        effectDraw.drawSprays();
        effectDraw.drawSolid();
        lampPass();
        const int effects = effectDraw.begin();
        int k = 0, e = 0, id = 0;
        while (k < laterCount || e < effects)
        {
            id = id % kIdSeeThroughCount + kIdSeeThroughFirst;
            if (e >= effects || (k < laterCount && later.distance[k] >= effectDraw.distance(e)))
            {
                if (fits(later.cost[k]))
                {
                    drawBrush(later.index[k], id);
                    ++drawn;
                }
                ++k;
            }
            else
            {
                effectDraw.drawItem(e++, id);
            }
        }
        effectDraw.end();
        stats_.brushes = drawn;
    }

    void WorldDraw::lampPass()
    {
        NEA_Material *light = static_cast<NEA_Material *>(hudDraw.lampMaterial());
        if (!game.lampLit || light == nullptr)
        {
            lampLastPolygons_ = -1;
            return;
        }
        const LevelView &view = level.view;
        const LampBeam &beam = game.lampBeam;
        // The hardware's "draw where the depth is the same" (a polygon
        // attribute NEA has no name for): the light is drawn on the very
        // polygons it lights -- the same vertices through the same
        // matrices -- and nowhere else.
        constexpr int kDepthEqual = 1 << 14;
        constexpr int kLampId = kIdLamp;
        int polygons = 0, vertices = 0;

        NEA_MaterialUse(light);
        GFX_COLOR = 0x7FFF;
        // One surface: lit by how squarely it faces the lamp, its geometry
        // sent again with the projection's constant for a texture
        // coordinate. `from` and `axis` are the lamp's in the surface's own
        // space. False when there is no more room.
        auto lightSurface = [&](const Surface &surface, const fx *from, const fx *axis, std::uint32_t texCoord) -> bool
        {
            const std::uint32_t planeIndex = surface.planeRef & kSurfacePlaneMask;
            const Plane &plane = view.planes[planeIndex];
            const bool back = (surface.planeRef & kSurfacePlaneBack) != 0;
            const fx side = planeDistance(plane, view.planeTypes[planeIndex], from);
            if (back ? side >= 0 : side <= 0)
                return true; // it faces away
            // (Two unit vectors in Q12: 16-bit parts.)
            const std::int32_t normal[3] = {plane.normal[0] >> 18, plane.normal[1] >> 18, plane.normal[2] >> 18};
            std::int64_t facing = dotShort(normal, 1, axis, 1);
            facing = (back ? facing : -facing) >> 12; // Q12: 4096 met head-on
            if (facing <= 0)
                return true;
            if (polygons + surface.polygons > (HudDraw::kLampPolygons >> (modelDraw.strained() ? 1 : 0)) ||
                stats_.polygons + surface.polygons > polygonBudget() ||
                stats_.vertices + surface.vertices > vertexBudget())
                return false;
            // (A wall skimmed still has a quarter of what one met head-on
            // has: the picture's own thinning does the rest.)
            int alpha = static_cast<int>(beam.strength * (1024 + facing * 3 / 4) >> 12);
            alpha = alpha < 1 ? 1 : (alpha > 30 ? 30 : alpha);
            NEA_PolyFormat(alpha, kLampId, static_cast<NEA_LightEnum>(0), kCull,
                           static_cast<NEA_OtherFormatEnum>(static_cast<int>(polygonMode()) | kDepthEqual));
            // In the wall's own colour at full light, not in a colour of
            // the lamp's: what is seen is the wall, brighter.
            lampStream(level.gxAt(surface), surface.gxWords, texCoord,
                       [](std::uint32_t word) { GFX_FIFO = word; }, textureCache.tint(surface.texture));
            polygons += surface.polygons;
            vertices += surface.vertices;
            stats_.polygons += surface.polygons;
            stats_.vertices += surface.vertices;
            return true;
        };
        auto project = [&](const fx *from, const fx *u, const fx *v) -> std::uint32_t
        {
            const LampProjection p = lampProjection(from, u, v, beam.radius, HudDraw::kLampTexels);
            MATRIX_CONTROL = GL_TEXTURE;
            const std::int32_t m[16] = {p.s[0], p.t[0], 0, 0, p.s[1], p.t[1], 0, 0,
                                        p.s[2], p.t[2], 0, 0, 0,      0,      0, 4096};
            for (std::int32_t value : m)
                MATRIX_LOAD4x4 = value;
            MATRIX_CONTROL = GL_MODELVIEW;
            return p.texCoord;
        };

        // The world's surfaces that were drawn this pass.
        camera.apply();
        const std::uint32_t worldCoord = project(beam.from, beam.u, beam.v);
        bool room = true;
        for (std::uint32_t w = 0; w < chosenWords_ && room; ++w)
        {
            std::uint32_t bits = chosen_[w];
            while (bits != 0 && room)
            {
                const int b = __builtin_ctz(bits);
                bits &= bits - 1;
                const Surface &surface = view.surfaces[(w << 5) + b];
                const std::int32_t centre[3] = {surface.center[0], surface.center[1], surface.center[2]};
                if (!lampTouches(beam, centre, surface.radius))
                    continue;
                room = lightSurface(surface, beam.from, beam.axis, worldCoord);
            }
        }

        // ... and the solid brushes: the lamp brought into each one's own
        // space, as drawBrush brings the eye.
        for (int n = 0; n < lampBrushCount_ && room; ++n)
        {
            const std::uint32_t index = lampBrushes_[n];
            const Brush &brush = view.brushes[index];
            const Model &model = view.models[brush.model];
            const fx *pos = brushState_ != nullptr ? brushState_[index].pos : brush.origin;
            std::int32_t centre[3];
            std::int32_t reach = 0;
            for (int a = 0; a < 3; ++a)
            {
                centre[a] = (brush.mins[a] + brush.maxs[a]) / 2 + fxToInt(pos[a] - brush.origin[a]);
                reach += (brush.maxs[a] - brush.mins[a]) / 2;
            }
            if (!lampTouches(beam, centre, reach))
                continue;
            static const std::uint16_t kNotTurned[3] = {0, 0, 0};
            const std::uint16_t *angles = brushState_ != nullptr ? brushState_[index].angles : kNotTurned;
            const bool turned = angles[0] != 0 || angles[1] != 0 || angles[2] != 0;
            fx from[3] = {beam.from[0] - pos[0], beam.from[1] - pos[1], beam.from[2] - pos[2]};
            fx axis[3] = {beam.axis[0], beam.axis[1], beam.axis[2]};
            fx u[3] = {beam.u[0], beam.u[1], beam.u[2]}, v[3] = {beam.v[0], beam.v[1], beam.v[2]};
            std::int32_t axes[9]; // Q30
            if (turned)
            {
                anglesToAxes(angles, axes);
                for (fx *vector : {from, axis, u, v})
                {
                    const fx d[3] = {vector[0], vector[1], vector[2]};
                    for (int j = 0; j < 3; ++j)
                    {
                        vector[j] = static_cast<fx>((static_cast<std::int64_t>(d[0]) * axes[j] +
                                                     static_cast<std::int64_t>(d[1]) * axes[3 + j] +
                                                     static_cast<std::int64_t>(d[2]) * axes[6 + j]) >>
                                                    kNormalShift);
                    }
                }
            }
            const std::uint32_t texCoord = project(from, u, v);
            const bool moved = pos[0] != 0 || pos[1] != 0 || pos[2] != 0 || turned;
            if (moved)
            {
                glPushMatrix();
                glTranslatef32(pos[0] >> 10, pos[1] >> 10, pos[2] >> 10);
                if (turned)
                {
                    m3x3 turn;
                    for (int row = 0; row < 3; ++row)
                    {
                        for (int column = 0; column < 3; ++column)
                            turn.m[row * 3 + column] = axes[column * 3 + row] >> (kNormalShift - 12);
                    }
                    glMultMatrix3x3(&turn);
                }
            }
            const Surface *surface = view.surfaces + model.firstSurface;
            for (int k = model.surfaceCount; k > 0 && room; --k, ++surface)
                room = lightSurface(*surface, from, axis, texCoord);
            if (moved)
                glPopMatrix(1);
        }

        lampLastPolygons_ = polygons;
        lampLastVertices_ = vertices;
        GFX_TEX_FORMAT = 0;
        NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), kCull, polygonMode());
        // The texture matrix back to what the rest of the frame is drawn
        // with: the beam's projection left in it would put the textures of
        // whatever is drawn after (see-through brushes, liquids, effects)
        // anywhere.
        sway();
    }

    bool WorldDraw::ballVisible(const std::int16_t *centre, int radius, bool anyDistance) const
    {
        if (!level.loaded() || leafFrame_ == nullptr)
            return false;
        const fx at[3] = {fxFromInt(centre[0]), fxFromInt(centre[1]), fxFromInt(centre[2])};
        const int leaf = findLeaf(level.view, at);
        // In solid (leaf 0) it cannot be judged by leaf: an actor standing
        // half in a wall is still drawn if the view test lets it be.
        if (leaf != 0 && leafFrame_[leaf] != visFrame_)
            return false;
        // (`anyDistance`: past the far end of the view too -- an enemy is
        // drawn there, and the fog has it.)
        return !sphereOutside(camera.planes, centre, radius, anyDistance ? Camera::kPlaneCount - 1 : Camera::kPlaneCount);
    }

    bool WorldDraw::spend(int polygons, int vertices)
    {
        if (stats_.polygons + polygons > polygonBudget() || stats_.vertices + vertices > vertexBudget())
            return false;
        stats_.polygons += polygons;
        stats_.vertices += vertices;
        return true;
    }

    void WorldDraw::drawSky()
    {
        // A box round the eye, one picture a side, that has to come out
        // BEHIND the whole world. Depth here is distance (W-buffering), so
        // the box is simply made big: its sides are further away than
        // anything the walk lets through (Camera::kFarUnits). That puts it
        // beyond the hardware's far plane too, which would clip it away; so
        // it is drawn through a projection whose z row is the w row's
        // values, a hair short -- every point of it then sits just inside
        // the far plane as far as clipping goes, while its depth stays its
        // distance. The projection is read back because it is NEA's.
        const LevelView &view = level.view;
        const unsigned first = view.header->skyTexture - 1u;

        int projection[16];
        glGetFixed(GL_GET_MATRIX_PROJECTION, projection);
        m4x4 flat;
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
                flat.m[row * 4 + column] = projection[row * 4 + column];
            flat.m[row * 4 + 2] = static_cast<int>(
                (static_cast<std::int64_t>(projection[row * 4 + 3]) * 4095) >> 12);
        }
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadMatrix4x4(&flat);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glTranslatef32(camera.pos[0] >> 10, camera.pos[1] >> 10, camera.pos[2] >> 10);
        // The box is built 64 units to a side from the eye (what fits a
        // vertex) and scaled to half as far again as the cull distance.
        constexpr int kSkyScale = ((Camera::kFarUnits * 3 / 2) / 64) << 12;
        glScalef32(kSkyScale, kSkyScale, kSkyScale);
        NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, kPolygonMode);

        // The game's own table of which way each side faces: for a point
        // (s, t) of a side's picture, each of x, y, z is plus or minus one of
        // s, t and 1 (as 1, 2, 3).
        static const std::int8_t kAxes[6][3] = {{3, -1, 2}, {-3, 1, 2},  {1, 3, 2},
                                                {-1, -3, 2}, {-2, -1, 3}, {2, -1, -3}};
        static const std::int8_t kCorner[4][2] = {{-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
        constexpr int kHalf = 64 << 2;                    // 64 units, in vertex units
        constexpr int kTexels = hldata::kSkySide << 4;    // the picture's side, 12.4
        for (int side = 0; side < 6; ++side)
        {
            // ... and its table of which picture goes on which side: the
            // second and third are the other way round from their names.
            static const std::uint8_t kPicture[6] = {0, 2, 1, 3, 4, 5};
            const NEA_Material *material = textureCache.material(first + kPicture[side]);
            if (material == nullptr)
                continue;
            NEA_MaterialUse(material);
            GFX_COLOR = 0x7FFF;
            glBegin(GL_QUADS);
            for (int corner = 0; corner < 4; ++corner)
            {
                const int b[3] = {kCorner[corner][0], kCorner[corner][1], 1};
                int v[3];
                for (int a = 0; a < 3; ++a)
                {
                    const int k = kAxes[side][a];
                    v[a] = (k < 0 ? -b[-k - 1] : b[k - 1]) * kHalf;
                }
                // s runs across the picture, t up it: the picture's own
                // rows run down.
                const int s = (kCorner[corner][0] + 1) * kTexels / 2;
                const int t = (1 - kCorner[corner][1]) * kTexels / 2;
                GFX_TEX_COORD = TEXTURE_PACK(s, t);
                glVertex3v16(static_cast<v16>(v[0]), static_cast<v16>(v[1]),
                             static_cast<v16>(v[2]));
            }
            glEnd();
        }

        glPopMatrix(1);
        glMatrixMode(GL_PROJECTION);
        glPopMatrix(1);
        glMatrixMode(GL_MODELVIEW);
        NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), kCull, polygonMode());
    }


    void WorldDraw::applyFog()
    {
        if (!fogOn_)
        {
            NEA_FogDisable();
            return;
        }
        // In the player's colour (which what is behind everything is too:
        // govern()).
        const std::uint16_t colour = kFogColour[settings.graphics.fogColour < kFogColours ? settings.graphics.fogColour : 0];
        // The hardware's fog goes by depth, in steps: with the depth buffer
        // holding distance (W), one of its units is kFogUnit of the map's.
        // Its table of 32 densities covers a band of 512 of those units, or
        // 1024, 2048, 4096; before the band there is none, past it the
        // last of the table. A straight ramp over the widest band that is
        // not more than the far half of what can be seen, ending where the
        // fog is to be whole.
        constexpr int kFogUnit = 16;
        int shift = 10;
        int band = 512;
        while (shift > 7 && band * 2 <= fogEnd_ / 2)
        {
            band *= 2;
            --shift;
        }
        int offset = (fogEnd_ - band) / kFogUnit;
        offset = offset < 0 ? 0 : (offset > 0x7FFF ? 0x7FFF : offset);
        GFX_CONTROL = (GFX_CONTROL & ~(15 << 8)) | GL_FOG | (shift << 8);
        GFX_FOG_COLOR = colour | (31 << 16);
        GFX_FOG_OFFSET = offset;
        // (The first entry has to be none: the hardware's rule.)
        // As solid at its far end as the player has it, coming on slowly
        // (shared/HlFog.hpp).
        std::uint8_t table[32];
        fogTable(settings.graphics.fogStrength, table, fogSolid_);
        for (int i = 0; i < 32; ++i)
            GFX_FOG_TABLE[i] = table[i];
    }

    void WorldDraw::govern(bool overflow, bool underflow, int lines, bool slow)
    {
        if (slow)
        {
            quickPasses_ = 0;
            if (spareQuarters_ > 0)
                --spareQuarters_;
        }
        else if (++quickPasses_ >= 60)
        {
            quickPasses_ = 0;
            if (spareQuarters_ < 4)
                ++spareQuarters_;
        }

        // Pressure: up after a few passes on end that ran out of room, down
        // after a couple of seconds of passes that had room for everything.
        // (If it has to go straight back up, it is let down less readily
        // the next time: a view on the edge is not to flicker.)
        ++sinceRelax_;
        // (... and, a long while with none going back up, readily again: a
        // view that was on the edge once does not hold the pressure up for
        // the rest of the map.)
        if (++sinceRise_ >= 1800)
        {
            sinceRise_ = 0;
            if (relaxWait_ > kRelaxWait)
                relaxWait_ /= 2;
        }
        if (stats_.budgetHit || stats_.brushCut)
        {
            easyPasses_ = 0;
            if (++cutPasses_ >= 3)
            {
                cutPasses_ = 0;
                if (pressure_ < hldata::kPressureMost)
                {
                    ++pressure_;
                    sinceRise_ = 0;
                    if (sinceRelax_ < 600 && relaxWait_ < kRelaxWaitMost)
                        relaxWait_ *= 2;
                }
            }
        }
        else
        {
            cutPasses_ = 0;
            // (Room for what is being left out as well: otherwise letting
            // it back in would only cut the view again.)
            const bool room =
                stats_.polygons + stats_.droppedPolygons < polygonBudget() * 15 / 16 &&
                stats_.vertices + stats_.droppedVertices < vertexBudget() * 15 / 16;
            if (pressure_ > 0 && room)
            {
                if (++easyPasses_ >= relaxWait_)
                {
                    easyPasses_ = 0;
                    sinceRelax_ = 0;
                    --pressure_;
                }
            }
            else
            {
                easyPasses_ = 0;
            }
        }
        // The fog: whole a little short of where the view is cut off. In at
        // once -- all the way, the pass after a cut, so the cut's edge shows
        // for one pass, not for several -- out slowly; with nothing cut off
        // it goes out until it is no fog at all.
        {
            // (As the player has it: none; only where a view is cut; or
            // always there, at their distance -- and nearer for a cut.
            // For heavy maps, the middle one is the last: most views are cut,
            // and fog that comes and goes with the cut would pump.)
            const GraphicsSettings &say = settings.graphics;
            const bool heavy = level.loaded() &&
                               levelIsHeavy(level.view.header->worstPolygons, level.view.header->worstVertices);
            const int mapFar = level.loaded() ? static_cast<int>(level.view.header->farUnits) : 0;
            const FogWant wanted =
                fogWanted(say, say.fog == kFogWhenNeeded && heavy, mapFar, stats_.budgetHit, stats_.cutDistance);
            const int want = wanted.end > kNoFog ? kNoFog : wanted.end;
            fogSolid_ = wanted.hidesEdge;
            if (say.fog == kFogOff || say.fogStrength == 0)
                fogEnd_ = kNoFog; // (switched off: gone at once)
            if (want < fogEnd_ && stats_.budgetHit)
                fogEnd_ = want;
            else if (want < fogEnd_)
                fogEnd_ -= (fogEnd_ - want + 3) / 4;
            else if (want > fogEnd_)
                fogEnd_ += (fogEnd_ >> 7) + 1;
            if (fogEnd_ > kNoFog)
                fogEnd_ = kNoFog;
            fogOn_ = fogEnd_ < kNoFog;
            // What is behind everything is the fog's colour while there is
            // fog, or the far end of a cut view would show against it; and
            // black otherwise. (Set here, between passes.)
            const std::uint16_t behind =
                fogOn_ ? kFogColour[say.fogColour < kFogColours ? say.fogColour : 0] : std::uint16_t(0);
            if (behind != behind_)
            {
                behind_ = behind;
                NEA_ClearColorSet(behind, 31, 63);
            }
        }

        // The budget the hardware's word leaves the next pass (budgetStep).
        BudgetReport report;
        report.overflow = overflow;
        report.underflow = underflow;
        report.lines = lines;
        report.hwPolygons = stats_.hwPolygons;
        report.hwVertices = stats_.hwVertices;
        report.polygons = stats_.polygons;
        report.vertices = stats_.vertices;
        report.spareQuarters = spareQuarters_;
        budgetStep(budget_, report);
    }
} // namespace hlrt
