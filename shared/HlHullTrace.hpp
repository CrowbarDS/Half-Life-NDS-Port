#pragma once

// Collision: tracing a point through a clip hull.
//
// A GoldSrc map carries its collision as "hulls": BSP trees whose solid space
// has already been grown outwards by the size of the thing moving through it.
// Moving the player's box through the world is then moving a single POINT --
// the player's origin -- through the hull made for that box, which is a walk
// down a tree. There are two hulls in a cooked level, standing and crouched.
//
// The algorithm is the Quake engine's recursive hull check, as GoldSrc and
// xash3d (engine/common/pm_trace.c) use it, in fixed point:
//
//  * positions and plane distances are Q12, normals Q30 (HlFixed.hpp);
//  * the fraction along the segment is Q30. That is the one place this differs
//    in kind from a float port: the fraction decides where the point stops,
//    the segment can be thousands of units long, and the stop has to be
//    within DIST_EPSILON (1/32 unit) of the wall, on the near side. Q12 would
//    leave it up to a whole unit out -- inside the wall.
//
// std-only and inline, so the host tests run the same code as the console. On
// the DS it is instantiated in an ITCM translation unit (game/HullTrace).

#include "HlPlaneMath.hpp"

namespace hldata
{
    /// How far short of a plane a trace stops, so that the end point is
    /// unambiguously on the open side.
    inline constexpr fx kDistEpsilon = kFxOne / 32;

    /// One clip hull: a tree in a level's ClipNodes section.
    struct Hull
    {
        const ClipNode *nodes = nullptr;
        const Plane *planes = nullptr;
        const std::uint8_t *planeTypes = nullptr;
        /// A node, or a negative kContents* value when the hull is one thing.
        std::int32_t root = kContentsEmpty;
    };

    struct Trace
    {
        bool allSolid = true;    ///< never left solid space
        bool startSolid = false; ///< the start point is in solid
        bool inOpen = false;
        bool inWater = false;
        bool hit = false;        ///< stopped by a plane; `normal` and `dist` are set
        std::int32_t fraction = kFractionOne; ///< Q30: how much of the move was made
        fx endPos[3] = {0, 0, 0};
        std::int32_t normal[3] = {0, 0, 0}; ///< Q30, facing the side the move came from
        fx dist = 0;
        /// Which brush entity stopped it, as an index into the mover list; -1 for
        /// the world (or nothing). Filled in by traceAll.
        std::int16_t mover = -1;
        /// Which actor's box stopped it (its index in the box list), or -1.
        std::int16_t box = -1;
    };

    /// What `p` is inside of: a kContents* value.
    HL_HOT int hullPointContents(const Hull &hull, std::int32_t num, const fx *p)
    {
        while (num >= 0)
        {
            const ClipNode &node = hull.nodes[num];
            const fx d = planeDistance(hull.planes[node.plane], hull.planeTypes[node.plane], p);
            num = node.children[d < 0 ? 1 : 0];
        }
        return num;
    }

    namespace detail
    {
        // Deeper than any retail hull's worst path. Reaching it is treated as
        // hitting something, which is the safe direction to be wrong in.
        inline constexpr int kMaxTraceDepth = 128;

        HL_HOT void lerp(const fx *a, const fx *b, std::int32_t frac, fx *out)
        {
            for (int i = 0; i < 3; ++i)
            {
                out[i] = a[i] + static_cast<fx>((static_cast<std::int64_t>(b[i] - a[i]) * frac) >>
                                                kFractionShift);
            }
        }

        /// Returns false once the trace has been stopped (the Quake convention).
        HL_HOT bool hullCheck(const Hull &hull, std::int32_t num, std::int32_t p1f,
                              std::int32_t p2f, const fx *p1, const fx *p2, Trace &trace,
                              int depth)
        {
            fx t1 = 0;
            fx t2 = 0;
            const ClipNode *node = nullptr;
            // Both ends on one side of a plane: go down that side. This is the
            // common case by far and needs no recursion.
            for (;;)
            {
                if (num < 0)
                {
                    if (num != kContentsSolid)
                    {
                        trace.allSolid = false;
                        if (num == kContentsEmpty)
                            trace.inOpen = true;
                        else
                            trace.inWater = true;
                    }
                    else
                    {
                        trace.startSolid = true;
                    }
                    return true;
                }
                node = &hull.nodes[num];
                const Plane &plane = hull.planes[node->plane];
                const std::uint8_t type = hull.planeTypes[node->plane];
                t1 = planeDistance(plane, type, p1);
                t2 = planeDistance(plane, type, p2);
                if (t1 >= 0 && t2 >= 0)
                    num = node->children[0];
                else if (t1 < 0 && t2 < 0)
                    num = node->children[1];
                else
                    break;
            }

            if (depth >= kMaxTraceDepth)
            {
                trace.fraction = p1f;
                for (int i = 0; i < 3; ++i)
                    trace.endPos[i] = p1[i];
                return false;
            }

            // The segment crosses the plane. Split it DIST_EPSILON short of the
            // crossing, on the side it starts from. t1 and t2 have opposite
            // signs here, so the divisor is never zero.
            const int side = t1 < 0 ? 1 : 0;
            const std::int64_t numer =
                static_cast<std::int64_t>(side ? t1 + kDistEpsilon : t1 - kDistEpsilon)
                << kFractionShift;
            std::int64_t frac64 = numer / (static_cast<std::int64_t>(t1) - t2);
            if (frac64 < 0)
                frac64 = 0;
            if (frac64 > kFractionOne)
                frac64 = kFractionOne;
            std::int32_t frac = static_cast<std::int32_t>(frac64);

            fx mid[3];
            std::int32_t midf = p1f + static_cast<std::int32_t>(
                                          (static_cast<std::int64_t>(p2f - p1f) * frac) >>
                                          kFractionShift);
            lerp(p1, p2, frac, mid);

            // The near half first.
            if (!hullCheck(hull, node->children[side], p1f, midf, p1, mid, trace, depth + 1))
                return false;

            // If the far side is not solid at the crossing, carry on through it.
            if (hullPointContents(hull, node->children[side ^ 1], mid) != kContentsSolid)
                return hullCheck(hull, node->children[side ^ 1], midf, p2f, mid, p2, trace,
                                 depth + 1);

            if (trace.allSolid)
                return false; // never got out of the solid area

            // The far side is solid: this plane is what stops the move.
            const Plane &plane = hull.planes[node->plane];
            trace.hit = true;
            for (int i = 0; i < 3; ++i)
                trace.normal[i] = side ? -plane.normal[i] : plane.normal[i];
            trace.dist = side ? -plane.dist : plane.dist;

            // Rounding can leave the split point just inside solid. Back up the
            // segment a tenth at a time until it is not.
            while (hullPointContents(hull, hull.root, mid) == kContentsSolid)
            {
                frac -= kFractionOne / 10;
                if (frac < 0)
                {
                    trace.fraction = midf;
                    for (int i = 0; i < 3; ++i)
                        trace.endPos[i] = mid[i];
                    return false;
                }
                midf = p1f + static_cast<std::int32_t>(
                                 (static_cast<std::int64_t>(p2f - p1f) * frac) >> kFractionShift);
                lerp(p1, p2, frac, mid);
            }

            trace.fraction = midf;
            for (int i = 0; i < 3; ++i)
                trace.endPos[i] = mid[i];
            return false;
        }
    } // namespace detail

#if !defined(__arm__)
    /// (The host's count of traces, for the tests' measures of a think.)
    inline std::uint32_t hostTraceCount = 0;
#endif

    /// Move a point from `start` towards `end` through `hull`.
#ifdef HL_DSL
    // (In the game library, the engine's own, in ITCM: game/GameApi.hpp.)
    Trace traceHull(const Hull &hull, const fx *start, const fx *end);
#else
    HL_HOT Trace traceHull(const Hull &hull, const fx *start, const fx *end)
    {
#if !defined(__arm__)
        ++hostTraceCount;
#endif
        Trace trace;
        for (int i = 0; i < 3; ++i)
            trace.endPos[i] = end[i];
        detail::hullCheck(hull, hull.root, 0, kFractionOne, start, end, trace, 0);
        if (trace.allSolid)
            trace.startSolid = true;
        if (trace.startSolid)
        {
            trace.fraction = 0;
            for (int i = 0; i < 3; ++i)
                trace.endPos[i] = start[i];
        }
        return trace;
    }
#endif

    /// True when a point may stand at `p` in `hull`.
    HL_HOT bool hullPositionFree(const Hull &hull, const fx *p)
    {
        return hullPointContents(hull, hull.root, p) != kContentsSolid;
    }
} // namespace hldata
