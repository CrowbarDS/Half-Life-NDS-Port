// (In a build to measure ITCM with, HLDS_ITCM_OFF, this file is empty and
// WorldDrawMain.arm.cpp has its code, in main memory: the linker puts a file
// named .itcm in ITCM whatever it holds.)
#if !defined(HLDS_ITCM_OFF) || defined(HLDS_WORLDDRAW_IN_MAIN)
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
            boxTestFormat(true);
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
            boxTestFormat(false);
#endif
    }
} // namespace hlrt
#endif
