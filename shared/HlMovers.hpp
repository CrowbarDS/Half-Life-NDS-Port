#pragma once

// Collision against brush entities as well as the world.
//
// A door, a grate or an invisible wall is its own little level: it has clip
// hulls of its own, in its own space, and that space sits somewhere in the
// world -- somewhere that changes, for a door. So a move is traced against the
// world's hull, then against each solid brush's hull with the segment carried
// into that brush's space, and the nearest thing hit wins. That is how the PC
// engine does it (xash3d pm_trace.c, PM_PlayerTraceExt).
//
// TWO DELIBERATE CHOICES:
//
//  * A brush the move STARTS inside is ignored. When a door closes on the
//    player there is a moment when they overlap; if that counted as "in
//    solid" the player would be frozen in the door. Ignoring it lets them walk
//    out, and pushing them out is the door's job, not the trace's.
//
//  * Each brush has a box in world space that is tested first. A map has up to
//    a couple of hundred brushes and a frame of movement makes a dozen traces;
//    nearly all of those pairs are nowhere near each other.
//
// std-only and inline, like HlHullTrace.hpp: the host tests run this code.

#include <cstdio>
#include <cstdlib>

#include "HlFields.hpp"
#include "HlHullTrace.hpp"
#include "HlPlaneMath.hpp"

namespace hldata
{
    /// A brush entity as the collision sees it, right now.
    struct MoverHull
    {
        Hull standing;
        Hull crouched;
        fx origin[3] = {0, 0, 0}; ///< where the brush's own space sits in the world
        /// How the brush is turned about its origin: the matrix that takes a
        /// direction in its own space to the world's, row by row, in Q30
        /// (anglesToAxes). `turned` is false for a brush that is not turned at
        /// all, which is nearly all of them and costs them nothing.
        std::int32_t axes[9] = {1 << 30, 0, 0, 0, 1 << 30, 0, 0, 0, 1 << 30};
        bool turned = false;
        /// Its bounds in the world at this position. Not grown by the player's
        /// size: the test below does that.
        fx mins[3] = {0, 0, 0};
        fx maxs[3] = {0, 0, 0};
        bool solid = false;       ///< present and in the way
        /// A brush that is a body of liquid (func_water): what it is full of
        /// (kContents*), or 0 for a brush that is not one. Whatever is inside
        /// its box is in that liquid while it is `present`.
        std::int8_t contents = 0;
        bool present = false;     ///< there at all, solid or not
        /// How far its clip hulls reach past `mins`..`maxs` (whole units): a
        /// brush entity's collision is not always its drawn box (a clip brush
        /// in front of a pane of glass, c1a0d's suit case). Whatever passes
        /// over a brush by its box alone (nearMover, MoverReach) leaves this
        /// much more room. The map's (Brush::pad, worked out by the
        /// converter), so no part of a save (one written before it was there
        /// reads the same).
        Unsaved<std::uint16_t> pad;
    };

    /// A brush entity that is solid as a box (the game's SOLID_BBOX: of the
    /// brush entities, only func_pushable is): the engine meets it, not by
    /// the brush's own clip hulls, but by a box hull it makes of the
    /// entity's bounds grown by the size of what moves (SV_HullForBox) --
    /// six planes, two to an axis. Here such a mover's two hulls are those,
    /// in its own space centred on the box, so that every box of a size can
    /// share one set of planes: the x and y planes for both hulls (a person
    /// crouched is as wide as one standing), then z for the standing one,
    /// then z for the crouched one. A mover is a box when its hulls' nodes
    /// are these: known by the mark in their first (a map's clip nodes have
    /// 0 there), not by where they are -- a family's library has its own
    /// copy of them.
    inline constexpr int kBoxPlanes = 8;
    inline constexpr std::uint16_t kBoxMark = 0xB0C5;
    inline constexpr ClipNode kBoxNodes[2][6] = {
        // Each axis: past the far plane, outside; short of the near one,
        // outside; between all six, inside.
        {{0, {kContentsEmpty, 1}, kBoxMark}, {1, {2, kContentsEmpty}, 0}, {2, {kContentsEmpty, 3}, 0},
         {3, {4, kContentsEmpty}, 0}, {4, {kContentsEmpty, 5}, 0}, {5, {kContentsSolid, kContentsEmpty}, 0}},
        {{0, {kContentsEmpty, 1}, kBoxMark}, {1, {2, kContentsEmpty}, 0}, {2, {kContentsEmpty, 3}, 0},
         {3, {4, kContentsEmpty}, 0}, {6, {kContentsEmpty, 5}, 0}, {7, {kContentsSolid, kContentsEmpty}, 0}}};
    inline constexpr std::uint8_t kBoxPlaneTypes[kBoxPlanes] = {kPlaneAxisX, kPlaneAxisX, kPlaneAxisY, kPlaneAxisY,
                                                                kPlaneAxisZ, kPlaneAxisZ, kPlaneAxisZ, kPlaneAxisZ};

    /// Whether a mover is met as a box (above).
    inline bool moverIsBox(const MoverHull &mover)
    {
        return mover.standing.nodes != nullptr && mover.standing.nodes[0].pad == kBoxMark;
    }

    /// The planes of a box `half` (Q12, each way from its middle) for both
    /// hulls: kBoxPlanes of them into `out`.
    inline void boxHullPlanes(const fx *half, Plane *out)
    {
        // How far each hull's box reaches past the box: the player's
        // half-size, standing and crouched (16 across either way; 36 and 18
        // up and down).
        const fx grow[4] = {fxFromInt(16), fxFromInt(16), fxFromInt(36), fxFromInt(18)};
        for (int p = 0; p < kBoxPlanes; ++p)
        {
            const int axis = p < 4 ? p >> 1 : 2;
            Plane &plane = out[p];
            plane.normal[0] = plane.normal[1] = plane.normal[2] = 0;
            plane.normal[axis] = 1 << 30;
            const fx reach = half[axis] + grow[p < 4 ? axis : (p < 6 ? 2 : 3)];
            plane.dist = (p & 1) ? -reach : reach;
        }
    }

    /// Make `mover` a box whose planes are `planes` (boxHullPlanes').
    inline void moverMakeBox(MoverHull &mover, const Plane *planes)
    {
        mover.standing = Hull{kBoxNodes[0], planes, kBoxPlaneTypes, 0};
        mover.crouched = Hull{kBoxNodes[1], planes, kBoxPlaneTypes, 0};
    }

    /// A volume the player climbs in: a func_ladder. Not solid, never drawn.
    struct LadderVolume
    {
        fx mins[3] = {0, 0, 0};
        fx maxs[3] = {0, 0, 0};
        bool active = false;
    };

    /// Something solid that is just a box: an actor standing there. Its bounds
    /// are in the world; like a brush's, they are not grown by the player's
    /// size.
    struct SolidBox
    {
        fx mins[3] = {0, 0, 0};
        fx maxs[3] = {0, 0, 0};
        bool active = false;
    };

    /// Where a solid brush is, in six short numbers: whole units, rounded
    /// outwards. A map has some hundreds of brushes and a trace asks each
    /// "could this move come near you?" -- of the brush's own record that is
    /// a hundred bytes fetched to say no; of this table it is twelve, one
    /// after another. A brush that is not solid has bounds nothing is
    /// inside.
    struct MoverReach
    {
        std::int16_t lo[3] = {32767, 32767, 32767};
        std::int16_t hi[3] = {-32768, -32768, -32768};
    };

    /// How many movers lie near each other in a group (MoveWorld::groupReach).
    inline constexpr int kMoverGroup = 8;
    inline constexpr int moverGroups(int movers) { return (movers + kMoverGroup - 1) / kMoverGroup; }

    /// Everything the player can collide with, and climb.
    struct MoveWorld
    {
        Hull standing;
        Hull crouched;
        /// Where something 64 units across has room (the world alone: see
        /// walkStep). With no nodes -- a world not given one -- everywhere.
        Hull large;
        const MoverHull *movers = nullptr;
        int moverCount = 0;
        /// One per mover, if given: kept by whoever moves a brush or makes
        /// it solid (moverReachSet), and read before the mover itself is.
        /// A world given none asks every mover, as before.
        MoverReach *reach = nullptr;
        /// The movers by where they are, if given (with `reach`), set out by
        /// moverReachAll: `moverOrder` lists them so that each kMoverGroup of
        /// it, one after another, lie near each other; `moverSlot` is where
        /// each is in that list; `groupReach` is each group's reach, all its
        /// movers' together, kept by moverReachSet. A trace asks a group
        /// before its movers: an eighth as many questions, on a map of a
        /// hundred and more (detail::moversNear). None: every mover asked.
        std::uint16_t *moverOrder = nullptr;
        std::uint16_t *moverSlot = nullptr;
        MoverReach *groupReach = nullptr;
        const LadderVolume *ladders = nullptr;
        int ladderCount = 0;
        /// Parallel to `ladders`: a ladder that is not one plain box (a
        /// leaning or a turned one), its brush model (into `models`) -- whose
        /// hull for the player's size says whether they are on it, and whose
        /// own faces which way it faces, as the game's PM_Ladder and
        /// PM_LadderMove have it. 0 (or no table): a box, all its bounds.
        const std::uint16_t *ladderModels = nullptr;
        /// The world's render tree, which is where water is: the clip hulls
        /// the player collides with know only solid and empty. Null on a
        /// world given no tree (some tests): then nothing is water.
        const Node *nodes = nullptr;
        const Leaf *leaves = nullptr;
        std::int32_t headNode = 0;
        /// What each leaf of that tree can see (the level's Vis section and
        /// how many leaves have a row in it). May be none: then every leaf
        /// sees every other.
        const std::uint8_t *vis = nullptr;
        std::uint32_t visSize = 0;
        int visLeafCount = 0;
        /// The actors' boxes.
        const SolidBox *boxes = nullptr;
        int boxCount = 0;
        /// The level's brushes (one per mover, in order) and models: what a
        /// shot meets of a mover is its model's own box. None: its hull.
        const Brush *brushes = nullptr;
        const Model *models = nullptr;
    };

    /// The leaf of the world's tree `p` is in; 0 (the solid leaf) on a world
    /// given no tree.
    inline int worldLeaf(const MoveWorld &world, const fx *p)
    {
        if (world.nodes == nullptr)
            return 0;
        std::int32_t index = world.headNode;
        while (index >= 0)
        {
            const Node &node = world.nodes[index];
            const fx d = planeDistance(world.standing.planes[node.plane],
                                       world.standing.planeTypes[node.plane], p);
            index = node.children[d >= 0 ? 0 : 1];
        }
        return -1 - index;
    }

    /// Whether leaf `to` can be seen from leaf `from`, by the map's own
    /// visibility. A leaf with no row (the solid one, or a map that was
    /// never vised) sees everything.
    inline bool leafSees(const MoveWorld &world, int from, int to)
    {
        if (world.vis == nullptr || world.leaves == nullptr || from <= 0 || from == to)
            return true;
        const std::int32_t ofs = world.leaves[from].visOfs;
        if (ofs < 0 || to <= 0 || to > world.visLeafCount)
            return ofs < 0;
        // The BSP's run-length rows: a non-zero byte is eight leaves, a zero
        // byte is followed by how many zero bytes it stands for. Bit 0 of
        // the first byte is leaf 1.
        const std::uint8_t *in = world.vis + ofs;
        const std::uint8_t *end = world.vis + world.visSize;
        int index = 1;
        while (index <= to && in < end)
        {
            const std::uint8_t bits = *in++;
            if (bits == 0)
            {
                if (in >= end)
                    break;
                index += 8 * *in++;
                continue;
            }
            if (to < index + 8)
                return (bits >> (to - index)) & 1;
            index += 8;
        }
        return false;
    }

    /// What is at `p`: kContentsEmpty, Solid, Water, Slime or Lava. The
    /// world's own leaf first; a body of liquid that is a brush entity
    /// overrides it, as in the game.
    HL_HOT int pointContents(const MoveWorld &world, const fx *p)
    {
        int contents = kContentsEmpty;
        if (world.nodes != nullptr)
        {
            std::int32_t index = world.headNode;
            while (index >= 0)
            {
                const Node &node = world.nodes[index];
                const fx d = planeDistance(world.standing.planes[node.plane],
                                           world.standing.planeTypes[node.plane], p);
                index = node.children[d >= 0 ? 0 : 1];
            }
            contents = world.leaves[-1 - index].contents;
        }
        for (int m = 0; m < world.moverCount; ++m)
        {
            const MoverHull &mover = world.movers[m];
            if (mover.contents == 0 || !mover.present)
                continue;
            if (p[0] >= mover.mins[0] && p[0] <= mover.maxs[0] && p[1] >= mover.mins[1] &&
                p[1] <= mover.maxs[1] && p[2] >= mover.mins[2] && p[2] <= mover.maxs[2])
                contents = mover.contents;
        }
        return contents;
    }

    /// Water, slime or lava: something to swim in.
    constexpr bool isLiquid(int contents)
    {
        return contents <= kContentsWater && contents >= kContentsLava;
    }

    /// The matrix for a brush turned by `angles` (turns, 65536 to the circle):
    /// world = m * local, m[row * 3 + column], in Q30 (fxSinCosFine says why
    /// not Q12). It is the game's own (yaw, then pitch, then roll), so a fan
    /// cooked from a map turns the way it does on the PC.
    inline void anglesToAxes(const std::uint16_t *angles, std::int32_t *m)
    {
        std::int32_t sp, cp, sy, cy, sr, cr;
        fxSinCosFine(angles[kPitch], sp, cp);
        fxSinCosFine(angles[kYaw], sy, cy);
        fxSinCosFine(angles[kRoll], sr, cr);
        const auto mul = [](std::int64_t a, std::int64_t b)
        {
            return static_cast<std::int32_t>((a * b) >> kNormalShift);
        };
        const std::int32_t srsp = mul(sr, sp);
        const std::int32_t crsp = mul(cr, sp);
        m[0] = mul(cp, cy);
        m[1] = mul(srsp, cy) - mul(cr, sy);
        m[2] = mul(crsp, cy) + mul(sr, sy);
        m[3] = mul(cp, sy);
        m[4] = mul(srsp, sy) + mul(cr, cy);
        m[5] = mul(crsp, sy) - mul(sr, cy);
        m[6] = -sp;
        m[7] = mul(sr, cp);
        m[8] = mul(cr, cp);
    }

    namespace detail
    {
        // The player's half-size: 16 x 16, and 36 standing or 18 crouched.
        // The clip hulls are already grown by it; the brushes' boxes are not.
        inline constexpr fx kPlayerHalfWidth = fxFromInt(16);
        inline constexpr fx kPlayerHalfHeight[2] = {fxFromInt(36), fxFromInt(18)};
        /// A little more than the trace's own stand-off distance, so a move
        /// that would stop just short of a brush still gets tested against it.
        inline constexpr fx kMoverSlack = fxFromInt(1);

        /// Can the segment a..b, as the centre of the player's box, come near
        /// the brush at all?
        HL_HOT bool nearMover(const MoverHull &mover, bool crouched, const fx *a, const fx *b)
        {
            for (int i = 0; i < 3; ++i)
            {
                const fx grow = (i == 2 ? kPlayerHalfHeight[crouched ? 1 : 0] : kPlayerHalfWidth) +
                                kMoverSlack + fxFromInt(mover.pad.value);
                const fx lo = a[i] < b[i] ? a[i] : b[i];
                const fx hi = a[i] < b[i] ? b[i] : a[i];
                if (hi < mover.mins[i] - grow || lo > mover.maxs[i] + grow)
                    return false;
            }
            return true;
        }

        /// What MoverReach says of a brush as it now is.
        inline MoverReach moverReachOf(const MoverHull &mover)
        {
            MoverReach reach;
            if (!mover.solid)
                return reach;
            for (int i = 0; i < 3; ++i)
            {
                const std::int32_t lo = (mover.mins[i] >> kFxShift) - mover.pad.value;
                const std::int32_t hi = ((mover.maxs[i] + kFxOne - 1) >> kFxShift) + mover.pad.value;
                reach.lo[i] = static_cast<std::int16_t>(lo < -32767 ? -32767 : (lo > 32766 ? 32766 : lo));
                reach.hi[i] = static_cast<std::int16_t>(hi < -32767 ? -32767 : (hi > 32766 ? 32766 : hi));
                // (Further out than the table can say: as far as it can.)
                if (lo > 32766)
                    reach.lo[i] = 32766;
                if (hi < -32767)
                    reach.hi[i] = -32767;
            }
            return reach;
        }

        /// A move, as the same kind of box: where the player's middle goes,
        /// grown by their size.
        struct SegmentReach
        {
            std::int32_t lo[3], hi[3];
        };
        HL_HOT SegmentReach segmentReach(bool crouched, const fx *a, const fx *b)
        {
            SegmentReach reach;
            for (int i = 0; i < 3; ++i)
            {
                const fx grow = (i == 2 ? kPlayerHalfHeight[crouched ? 1 : 0] : kPlayerHalfWidth) +
                                kMoverSlack;
                const fx lo = (a[i] < b[i] ? a[i] : b[i]) - grow;
                const fx hi = (a[i] < b[i] ? b[i] : a[i]) + grow;
                reach.lo[i] = lo >> kFxShift;
                reach.hi[i] = (hi + kFxOne - 1) >> kFxShift;
            }
            return reach;
        }

        /// Whether mover `m` has to be asked about a move at all. (Never
        /// "no" for one nearMover would say yes to: both boxes are rounded
        /// outwards.)
        inline bool moverMayTouch(const MoveWorld &world, int m, const SegmentReach &move)
        {
            if (world.reach == nullptr)
                return true;
            const MoverReach &reach = world.reach[m];
#ifndef __arm__
            // (Off the console, where there is time: the table is what the
            // mover says it is, or whoever changed the mover forgot it.)
            const MoverReach now = moverReachOf(world.movers[m]);
            for (int i = 0; i < 3; ++i)
            {
                if (now.lo[i] != reach.lo[i] || now.hi[i] != reach.hi[i])
                {
                    std::fprintf(stderr, "moverMayTouch: mover %d was changed without moverReachSet\n", m);
                    std::abort();
                }
            }
#endif
            return !(move.hi[0] < reach.lo[0] || move.lo[0] > reach.hi[0] || move.hi[1] < reach.lo[1] ||
                     move.lo[1] > reach.hi[1] || move.hi[2] < reach.lo[2] || move.lo[2] > reach.hi[2]);
        }

        /// The movers a move may touch, by their groups (MoveWorld::groupReach):
        /// in their own order, as asking every one of them would meet them --
        /// so that of two a trace meets at once, the same one is said. Each
        /// is still asked (moverMayTouch). `count` -1: every mover -- a world
        /// with no groups, or more of them near than there is room to say.
        inline constexpr int kMoversNearMax = 48;
        /// (A map of more movers than this asks each: none of the game's.)
        inline constexpr int kMoversNearBits = 1024;
        struct MoversNear
        {
            int count = -1;
            std::uint16_t list[kMoversNearMax];
            int size(const MoveWorld &world) const { return count < 0 ? world.moverCount : count; }
            int at(int k) const { return count < 0 ? k : list[k]; }
        };
        // (In main memory, and one copy of it, not one in each file that
        // traces -- once a trace, not in its inner loop: ITCM has no room.)
        [[gnu::noinline, gnu::noclone]] HL_WARM void moversNear(const MoveWorld &world, const SegmentReach &move,
                                                               MoversNear &near)
        {
            near.count = -1;
            if (world.groupReach == nullptr || world.moverCount > kMoversNearBits)
                return;
            std::uint32_t bits[kMoversNearBits / 32];
            const int words = (world.moverCount + 31) / 32;
            for (int w = 0; w < words; ++w)
                bits[w] = 0;
            const int groups = moverGroups(world.moverCount);
            for (int g = 0; g < groups; ++g)
            {
                const MoverReach &reach = world.groupReach[g];
                if (move.hi[0] < reach.lo[0] || move.lo[0] > reach.hi[0] || move.hi[1] < reach.lo[1] ||
                    move.lo[1] > reach.hi[1] || move.hi[2] < reach.lo[2] || move.lo[2] > reach.hi[2])
                    continue;
                const int end = (g + 1) * kMoverGroup < world.moverCount ? (g + 1) * kMoverGroup : world.moverCount;
                for (int i = g * kMoverGroup; i < end; ++i)
                {
                    const int m = world.moverOrder[i];
                    bits[m >> 5] |= 1u << (m & 31);
                }
            }
            int count = 0;
            for (int w = 0; w < words; ++w)
            {
                for (std::uint32_t left = bits[w]; left != 0; left &= left - 1)
                {
                    if (count == kMoversNearMax)
                        return;
                    near.list[count++] = static_cast<std::uint16_t>(w * 32 + __builtin_ctz(left));
                }
            }
            near.count = count;
#ifndef __arm__
            // (Off the console: none that may touch the move is left out, or a
            // group's reach is not all of its movers'.)
            for (int m = 0, k = 0; m < world.moverCount; ++m)
            {
                while (k < count && near.list[k] < m)
                    ++k;
                if (moverMayTouch(world, m, move) && !(k < count && near.list[k] == m))
                {
                    std::fprintf(stderr, "moversNear: mover %d may touch the move, and its group says not\n", m);
                    std::abort();
                }
            }
#endif
        }

        /// A world point in the brush's own space: back along its origin,
        /// then turned back.
        /// A brush's model box (its own space) in a mover's space: the same,
        /// except for a box, whose space is centred on it (moverMakeBox).
        /// (What a shot meets is the model's box, not grown by the engine's
        /// unit as the game's SOLID_BBOX is: TECHNICAL.md, Boxes.)
        [[gnu::noinline]] inline void boxCentred(const MoverHull &mover, fx *mins, fx *maxs)
        {
            if (!moverIsBox(mover))
                return;
            for (int a = 0; a < 3; ++a)
            {
                const fx middle = (mins[a] + maxs[a]) / 2;
                mins[a] -= middle;
                maxs[a] -= middle;
            }
        }

        HL_HOT void toMoverSpace(const MoverHull &mover, const fx *world, fx *local)
        {
            const fx d[3] = {world[0] - mover.origin[0], world[1] - mover.origin[1],
                             world[2] - mover.origin[2]};
            if (!mover.turned)
            {
                local[0] = d[0];
                local[1] = d[1];
                local[2] = d[2];
                return;
            }
            // The inverse of a rotation is its transpose.
            for (int j = 0; j < 3; ++j)
            {
                local[j] = static_cast<fx>((static_cast<std::int64_t>(d[0]) * mover.axes[j] +
                                            static_cast<std::int64_t>(d[1]) * mover.axes[3 + j] +
                                            static_cast<std::int64_t>(d[2]) * mover.axes[6 + j]) >>
                                           kNormalShift);
            }
        }

        /// A direction (or an offset) from the brush's space to the world's.
        /// Works on Q12 and Q30 values alike.
        HL_HOT void turnToWorld(const MoverHull &mover, const std::int32_t *local,
                                std::int32_t *world)
        {
            if (!mover.turned)
            {
                world[0] = local[0];
                world[1] = local[1];
                world[2] = local[2];
                return;
            }
            for (int i = 0; i < 3; ++i)
            {
                world[i] = static_cast<std::int32_t>(
                    (static_cast<std::int64_t>(local[0]) * mover.axes[i * 3] +
                     static_cast<std::int64_t>(local[1]) * mover.axes[i * 3 + 1] +
                     static_cast<std::int64_t>(local[2]) * mover.axes[i * 3 + 2]) >>
                    kNormalShift);
            }
        }
    } // namespace detail

    namespace detail
    {
        /// Group `g`'s reach: all its movers' together. (This and what sets
        /// the groups out: Thumb, once a mover moves or a map is set up.)
        [[gnu::noinline]] HL_COLD void groupReachSet(const MoveWorld &world, int g)
        {
            MoverReach all;
            const int end = (g + 1) * kMoverGroup < world.moverCount ? (g + 1) * kMoverGroup : world.moverCount;
            for (int i = g * kMoverGroup; i < end; ++i)
            {
                const MoverReach &one = world.reach[world.moverOrder[i]];
                for (int k = 0; k < 3; ++k)
                {
                    all.lo[k] = one.lo[k] < all.lo[k] ? one.lo[k] : all.lo[k];
                    all.hi[k] = one.hi[k] > all.hi[k] ? one.hi[k] : all.hi[k];
                }
            }
            world.groupReach[g] = all;
        }

        /// Where a mover is, for its place in moverOrder: its middle's three
        /// whole numbers, by 64 units, their bits taken in turn (a Morton
        /// order -- what is near in it is near in the map, mostly), and its
        /// number after, so that no two are the same.
        [[gnu::noinline]] HL_COLD std::uint64_t moverPlaceKey(const MoverHull &mover, int m)
        {
            std::uint64_t key = 0;
            for (int k = 0; k < 3; ++k)
            {
                const std::int32_t middle = ((mover.mins[k] >> 1) + (mover.maxs[k] >> 1)) >> kFxShift;
                const std::uint32_t at = static_cast<std::uint32_t>((middle + 32768) >> 6) & 1023;
                for (int bit = 0; bit < 10; ++bit)
                    key |= static_cast<std::uint64_t>((at >> bit) & 1) << (bit * 3 + k);
            }
            return (key << 16) | static_cast<std::uint64_t>(m);
        }
    } // namespace detail

    /// Mover `m` has moved, or become solid or stopped being: its line of
    /// the table, again, and its group's. Whoever writes a MoverHull's bounds
    /// or its `solid` calls this after. (One copy, not one in each place that
    /// moves a brush: it is not what a frame spends its time on.)
    [[gnu::noinline]] inline void moverReachSet(const MoveWorld &world, int m)
    {
        if (world.reach == nullptr)
            return;
        world.reach[m] = detail::moverReachOf(world.movers[m]);
        if (world.groupReach != nullptr)
            detail::groupReachSet(world, world.moverSlot[m] / kMoverGroup);
    }
    /// ... and all of them (a map set up; a saved game laid over it): the
    /// movers put in groups by where they are now, too. (Which group one is
    /// in says nothing of what a trace finds -- only how soon it is asked.)
    [[gnu::noinline]] HL_COLD void moverReachAll(const MoveWorld &world)
    {
        if (world.reach == nullptr)
            return;
        for (int m = 0; m < world.moverCount; ++m)
            world.reach[m] = detail::moverReachOf(world.movers[m]);
        if (world.groupReach == nullptr)
            return;
        std::uint16_t *order = world.moverOrder;
        for (int m = 0; m < world.moverCount; ++m)
            order[m] = static_cast<std::uint16_t>(m);
        // (Shell sort, the gaps halved: no memory, a few thousand keys made
        // for some hundreds of movers, once a map.)
        for (int gap = world.moverCount / 2; gap > 0; gap /= 2)
        {
            for (int i = gap; i < world.moverCount; ++i)
            {
                const std::uint16_t m = order[i];
                const std::uint64_t key = detail::moverPlaceKey(world.movers[m], m);
                int j = i;
                for (; j >= gap && detail::moverPlaceKey(world.movers[order[j - gap]], order[j - gap]) > key; j -= gap)
                    order[j] = order[j - gap];
                order[j] = m;
            }
        }
        for (int i = 0; i < world.moverCount; ++i)
            world.moverSlot[order[i]] = static_cast<std::uint16_t>(i);
        for (int g = 0; g < moverGroups(world.moverCount); ++g)
            detail::groupReachSet(world, g);
    }

    namespace detail
    {
        /// Whether the line from `a` to `b` lies wholly beyond one of the
        /// faces of the box `mins`..`maxs` -- both ends past it, an end on it
        /// not -- and so cannot come into it. What lineEntersBox's slab test
        /// says of such a line anyway, before its divides:
        /// tools/hlds-convert/tests/test_divfree.cpp holds it to its old self,
        /// bit for bit.
        inline bool lineBeyondBox(const fx *a, const fx *b, const fx *mins, const fx *maxs)
        {
            for (int k = 0; k < 3; ++k)
            {
                const fx low = a[k] < b[k] ? a[k] : b[k];
                const fx high = a[k] < b[k] ? b[k] : a[k];
                if (high < mins[k] || low > maxs[k])
                    return true;
            }
            return false;
        }

        /// The boxes (the actors') in the way of a move from `start` to
        /// `end`: each grown by `growXY` across and `growZ` up and down, one
        /// pair of faces at a time; the nearer hit put into `best`.
        HL_HOT void traceBoxes(const MoveWorld &world, const fx *start, const fx *end, fx growXY, fx growZ, Trace &best)
        {
            // (A move that starts inside one passes through it, as with a
            // brush: nobody is ever frozen inside an actor who walked up to
            // them.)
            for (int n = 0; n < world.boxCount; ++n)
            {
                const SolidBox &box = world.boxes[n];
                if (!box.active)
                    continue;
                // A move wholly beyond a face of it never comes into it: what
                // the slab test below finds -- a miss, or an entry at its very
                // end or before its start, which a trace never takes (its
                // fraction is at most kFractionOne) -- without its divides.
                // Its end may lie on the face; not its start, from which the
                // slab test can find it met at once, at a corner.
                // (Below a face: the start under it and the end at or under
                // it. Above, the same the other way. A line along the face
                // itself is the slab test's, which needs no divide for it.)
                bool beyond = false;
                for (int i = 0; i < 3 && !beyond; ++i)
                {
                    const fx grow = i == 2 ? growZ : growXY;
                    const fx lo = box.mins[i] - grow, hi = box.maxs[i] + grow;
                    const fx s = start[i], e = end[i];
                    beyond = (s < lo && e <= lo) || (s > hi && e >= hi);
                }
                if (beyond)
                    continue;
                std::int64_t enter = -1; // Q30; -1 = from before the start
                std::int64_t leave = kFractionOne;
                int axis = -1;
                bool inside = true, miss = false;
                for (int i = 0; i < 3 && !miss; ++i)
                {
                    const fx grow = i == 2 ? growZ : growXY;
                    const fx lo = box.mins[i] - grow, hi = box.maxs[i] + grow;
                    const fx d = end[i] - start[i];
                    if (start[i] <= lo || start[i] >= hi)
                        inside = false;
                    if (d == 0)
                    {
                        miss = start[i] <= lo || start[i] >= hi;
                        continue;
                    }
                    std::int64_t t0 = (static_cast<std::int64_t>(lo - start[i]) << kFractionShift) / d;
                    std::int64_t t1 = (static_cast<std::int64_t>(hi - start[i]) << kFractionShift) / d;
                    if (t0 > t1)
                    {
                        const std::int64_t swap = t0;
                        t0 = t1;
                        t1 = swap;
                    }
                    if (t0 > enter)
                    {
                        enter = t0;
                        axis = i;
                    }
                    if (t1 < leave)
                        leave = t1;
                    miss = enter > leave;
                }
                if (miss || inside || axis < 0 || enter < 0 || enter >= best.fraction)
                    continue;
                // Stop a little short, as the hull trace does.
                const fx d = end[axis] - start[axis];
                const std::int64_t back =
                    (static_cast<std::int64_t>(kFxOne / 32) << kFractionShift) / (d < 0 ? -d : d);
                const std::int64_t fraction = enter > back ? enter - back : 0;
                best.fraction = static_cast<std::int32_t>(fraction);
                for (int i = 0; i < 3; ++i)
                {
                    best.endPos[i] = start[i] + static_cast<fx>(
                                                    (static_cast<std::int64_t>(end[i] - start[i]) * fraction) >>
                                                    kFractionShift);
                    best.normal[i] = 0;
                }
                best.normal[axis] = d > 0 ? -(1 << kNormalShift) : (1 << kNormalShift);
                {
                    const fx grow = axis == 2 ? growZ : growXY;
                    best.dist = d > 0 ? -(box.mins[axis] - grow) : box.maxs[axis] + grow;
                }
                best.mover = -1;
                best.box = static_cast<std::int16_t>(n);
                best.hit = true;
            }
        }
    } // namespace detail

    /// Move the player's origin from `start` towards `end` through the world
    /// and every solid brush.
#ifdef HL_DSL
    // (In the game library, the engine's own, in ITCM: game/GameApi.hpp.)
    Trace traceAll(const MoveWorld &world, bool crouched, const fx *start, const fx *end);
#else
    HL_HOT HL_TRACE_NOINLINE Trace traceAll(const MoveWorld &world, bool crouched, const fx *start, const fx *end)
    {
        HL_TRACE_SPENT(kTraceAll);
        Trace best = traceHull(crouched ? world.crouched : world.standing, start, end);
        const detail::SegmentReach reach = detail::segmentReach(crouched, start, end);
        detail::MoversNear near;
        detail::moversNear(world, reach, near);
        for (int k = 0, nearCount = near.size(world); k < nearCount; ++k)
        {
            const int m = near.at(k);
            if (!detail::moverMayTouch(world, m, reach))
                continue;
            const MoverHull &mover = world.movers[m];
            if (!mover.solid || !detail::nearMover(mover, crouched, start, end))
                continue;
            fx a[3], b[3];
            detail::toMoverSpace(mover, start, a);
            detail::toMoverSpace(mover, end, b);
            const Trace t = traceHull(crouched ? mover.crouched : mover.standing, a, b);
            if (t.startSolid || t.allSolid || t.fraction >= best.fraction)
                continue;
            // The nearer hit. Whether the move as a whole started in solid is
            // still the world's answer.
            const bool startSolid = best.startSolid;
            const bool allSolid = best.allSolid;
            best = t;
            best.startSolid = startSolid;
            best.allSolid = allSolid;
            // Back into the world: the plane it stopped on, and the end point.
            // For a brush that is turned the end point is taken along the
            // move as it was asked for, in the world -- not brought back from
            // the brush's space. There and back through a rotation is never
            // quite where it started, and a position that is put through it
            // every frame (the ground check does that) walks.
            detail::turnToWorld(mover, t.normal, best.normal);
            std::int64_t shift = 0;
            for (int i = 0; i < 3; ++i)
            {
                // (Rounded towards the start: never a hair further than the
                // trace allowed.)
                const std::int64_t along = static_cast<std::int64_t>(end[i] - start[i]) * t.fraction;
                best.endPos[i] =
                    mover.turned
                        ? start[i] + static_cast<fx>(along >= 0 ? along >> kFractionShift
                                                                : -((-along) >> kFractionShift))
                        : t.endPos[i] + mover.origin[i];
                shift += static_cast<std::int64_t>(best.normal[i]) * mover.origin[i];
            }
            best.dist = t.dist + static_cast<fx>(shift >> kNormalShift);
            best.mover = static_cast<std::int16_t>(m);
        }
        // The boxes: the move against each, grown by the player's size.
        detail::traceBoxes(world, start, end, detail::kPlayerHalfWidth, detail::kPlayerHalfHeight[crouched ? 1 : 0], best);
        return best;
    }
#endif

    /// May the player's origin be at `p`: not in the world's solid, and not
    /// inside any solid brush? This is the question for "is there room to
    /// stand up here" -- NOT for "am I stuck", which asks the world alone (see
    /// the first choice above).
    inline bool positionFree(const MoveWorld &world, bool crouched, const fx *p)
    {
        if (!hullPositionFree(crouched ? world.crouched : world.standing, p))
            return false;
        const detail::SegmentReach reach = detail::segmentReach(crouched, p, p);
        detail::MoversNear near;
        detail::moversNear(world, reach, near);
        for (int k = 0, nearCount = near.size(world); k < nearCount; ++k)
        {
            const int m = near.at(k);
            if (!detail::moverMayTouch(world, m, reach))
                continue;
            const MoverHull &mover = world.movers[m];
            if (!mover.solid || !detail::nearMover(mover, crouched, p, p))
                continue;
            fx local[3];
            detail::toMoverSpace(mover, p, local);
            if (!hullPositionFree(crouched ? mover.crouched : mover.standing, local))
                return false;
        }
        return true;
    }

    // ------------------------------------------------------------- lines
    //
    // A line through the world as it is drawn (not the clip hulls, which are
    // the world grown by the player's size), and through the brushes. What
    // is seen, what is shot, and what the player's ledge checks look at.

    namespace detail
    {
#if !defined(__arm__)
        inline std::uint32_t hostLineCount = 0; ///< (the host's count, as hostTraceCount)
#endif

        /// A line through the world's own tree: false, with `hit` how far
        /// along it got (Q30), if it runs into something solid.
        /// `wall`, if it is asked for, is then what it ran into: the plane
        /// the line last crossed, twice its number, plus one if it was
        /// crossed from behind (-1: none, it began in the solid).
        /// (Not in ITCM: measured there, it gained nothing -- 2026-11-16.)
        HL_HOT bool lineThrough(const MoveWorld &world, std::int32_t num, std::int32_t f1,
                                std::int32_t f2, const fx *from, const fx *to, std::int32_t &hit,
                                int depth, int crossed = -1, int *wall = nullptr)
        {
#if !defined(__arm__)
            hostLineCount += depth == 0 ? 1u : 0u;
#endif
            fx p1[3] = {from[0], from[1], from[2]};
            while (true)
            {
                if (num < 0)
                {
                    const std::int8_t contents = world.leaves[-1 - num].contents;
                    if (contents == kContentsSolid || contents == kContentsSky)
                    {
                        hit = f1;
                        if (wall != nullptr)
                            *wall = crossed;
                        return false;
                    }
                    return true;
                }
                if (depth > 96)
                {
                    hit = f1;
                    if (wall != nullptr)
                        *wall = crossed;
                    return false;
                }
                const Node &node = world.nodes[num];
                const Plane &plane = world.standing.planes[node.plane];
                const std::uint8_t type = world.standing.planeTypes[node.plane];
                const fx t1 = planeDistance(plane, type, p1);
                const fx t2 = planeDistance(plane, type, to);
                if (t1 >= 0 && t2 >= 0)
                {
                    num = node.children[0];
                    continue;
                }
                if (t1 < 0 && t2 < 0)
                {
                    num = node.children[1];
                    continue;
                }
                std::int64_t part = (static_cast<std::int64_t>(t1) << kFractionShift) / (t1 - t2);
                part = part < 0 ? 0 : (part > kFractionOne ? kFractionOne : part);
                fx mid[3];
                for (int i = 0; i < 3; ++i)
                    mid[i] = p1[i] + static_cast<fx>((static_cast<std::int64_t>(to[i] - p1[i]) * part) >>
                                                     kFractionShift);
                const std::int32_t fm =
                    f1 + static_cast<std::int32_t>((static_cast<std::int64_t>(f2 - f1) * part) >>
                                                   kFractionShift);
                const int side = t1 < 0 ? 1 : 0;
                if (!lineThrough(world, node.children[side], f1, fm, p1, mid, hit, depth + 1, crossed, wall))
                    return false;
                crossed = static_cast<int>(node.plane) * 2 + side;
                num = node.children[side ^ 1];
                f1 = fm;
                for (int i = 0; i < 3; ++i)
                    p1[i] = mid[i];
                ++depth;
            }
        }
    } // namespace detail

    /// How far a line gets from `start` towards `end`: against the world as
    /// it is drawn, and against the doors and boxes (as their clip hulls: a
    /// hand's breadth fatter than they look).
    struct Shot
    {
        std::int32_t fraction = kFractionOne; ///< Q30
        std::int16_t mover = -1;              ///< the brush entity that stopped it, if one did
    };

    namespace detail
    {
        /// Where a line from `start` to `end` stops `fraction` of the way:
        /// the end of what is left of it for the movers to be looked for on.
        inline void shotStop(const fx *start, const fx *end, std::int32_t fraction, fx *stop)
        {
            for (int i = 0; i < 3; ++i)
                stop[i] = fraction >= kFractionOne
                              ? end[i]
                              : start[i] + static_cast<fx>((static_cast<std::int64_t>(end[i] - start[i]) * fraction) >>
                                                           kFractionShift);
        }
        /// Where the line from `a` to `b` comes into the box `mins`..`maxs`
        /// (a fraction of it, kFractionOne the whole), or -1 if it misses
        /// it.
        inline std::int64_t lineIntoBox(const fx *a, const fx *b, const fx *mins, const fx *maxs)
        {
            std::int64_t enter = 0, leave = kFractionOne;
            for (int k = 0; k < 3; ++k)
            {
                const fx d = b[k] - a[k];
                if (d == 0)
                {
                    if (a[k] < mins[k] || a[k] > maxs[k])
                        return -1;
                    continue;
                }
                std::int64_t t0 = (static_cast<std::int64_t>(mins[k] - a[k]) << kFractionShift) / d;
                std::int64_t t1 = (static_cast<std::int64_t>(maxs[k] - a[k]) << kFractionShift) / d;
                if (t0 > t1)
                {
                    const std::int64_t swap = t0;
                    t0 = t1;
                    t1 = swap;
                }
                enter = t0 > enter ? t0 : enter;
                leave = t1 < leave ? t1 : leave;
                if (enter > leave)
                    return -1;
            }
            return enter;
        }
        /// How far a line goes through the world alone, its movers left out
        /// (kFractionOne the whole way): traceShot's first part.
        inline std::int32_t shotWorld(const MoveWorld &world, const fx *start, const fx *end)
        {
            if (world.nodes != nullptr)
            {
                std::int32_t hit = kFractionOne;
                return detail::lineThrough(world, world.headNode, 0, kFractionOne, start, end, hit, 0) ? kFractionOne
                                                                                                       : hit;
            }
            // A world given no tree (some tests): its clip hull must do.
            const Trace t = traceHull(world.crouched, start, end);
            return t.startSolid ? 0 : t.fraction;
        }
    } // namespace detail

    /// `worldFraction`, if not -1: how far the line goes through the world
    /// alone, known already (detail::shotWorld) -- a line that is the same
    /// each time, from something that never moves, as a barnacle's tongue's
    /// way down. The world does not change; only its movers are looked at.
    HL_HOT HL_TRACE_NOINLINE Shot traceShot(const MoveWorld &world, const fx *start, const fx *end,
                                            std::int32_t worldFraction = -1)
    {
        HL_TRACE_SPENT(kTraceShot);
        Shot shot;
        shot.fraction = worldFraction >= 0 ? worldFraction : detail::shotWorld(world, start, end);
        // (Only a brush the line reaches before the world stops it can stop
        // it sooner: the others are passed over by their boxes.)
        fx stop[3];
        detail::shotStop(start, end, shot.fraction, stop);
        const detail::SegmentReach reach = detail::segmentReach(true, start, stop);
        detail::MoversNear near;
        detail::moversNear(world, reach, near);
        for (int k = 0, nearCount = near.size(world); k < nearCount; ++k)
        {
            const int m = near.at(k);
            if (!detail::moverMayTouch(world, m, reach))
                continue;
            const MoverHull &mover = world.movers[m];
            if (!mover.solid || !detail::nearMover(mover, true, start, stop))
                continue;
            fx a[3], b[3];
            detail::toMoverSpace(mover, start, a);
            detail::toMoverSpace(mover, end, b);
            const Trace t = traceHull(mover.crouched, a, b);
            const bool boxed = world.brushes != nullptr && world.models != nullptr;
            // (That hull is the brush grown by a crouched person's size: a
            // shot is a point. Where it comes into the model's own box, in
            // the brush's space, is nearer the truth -- exactly so for a
            // box, a target, a door -- and a line that passes beside it
            // misses it. So is one that starts within that size of it,
            // which the hull would have it start inside, and let through.)
            if (t.fraction >= shot.fraction || (!boxed && (t.startSolid || t.allSolid)))
                continue;
            std::int32_t at = t.startSolid || t.allSolid ? 0 : t.fraction;
            if (boxed)
            {
                const Model &model = world.models[world.brushes[m].model];
                fx mins[3] = {fxFromInt(model.mins[0]), fxFromInt(model.mins[1]), fxFromInt(model.mins[2])};
                fx maxs[3] = {fxFromInt(model.maxs[0]), fxFromInt(model.maxs[1]), fxFromInt(model.maxs[2])};
                detail::boxCentred(mover, mins, maxs);
                // (One that starts inside it, or on it -- a gun's own shot -- leaves
                // it: not stopped by it, as the hull never did.)
                bool inside = true;
                for (int k = 0; k < 3; ++k)
                    inside = inside && a[k] >= mins[k] && a[k] <= maxs[k];
                const std::int64_t into = inside ? -1 : detail::lineIntoBox(a, b, mins, maxs);
                if (into < 0)
                    continue;
                at = into > at ? static_cast<std::int32_t>(into) : at;
            }
            if (at >= shot.fraction)
                continue;
            shot.fraction = at;
            shot.mover = static_cast<std::int16_t>(m);
        }
        return shot;
    }

    /// The same, and which way what stopped it faces: `normal` (Q12, towards
    /// where the line came from) and `at`, the point on it. False if nothing
    /// stopped it, or nothing that says which way it faces.
    /// (A brush is met as its clip hull, a hand's breadth out from what is
    /// drawn: `at` is brought back onto the face, for one that is square to
    /// the axes -- as doors are.) `metMover`: the brush entity that stopped it,
    /// or -1 for the world (or for one it only passed within a hull's
    /// breadth of, as traceShot does not count it).
    // (One copy, not one in every monster that looks for a wall.)
    [[gnu::noinline]] inline bool traceShotWall(const MoveWorld &world, const fx *start, const fx *end, fx *at, fx *normal,
                                                int *metMover = nullptr)
    {
        std::int32_t fraction = kFractionOne;
        int wall = -1;
        bool found = false;
        int on = -1;
        if (world.nodes != nullptr &&
            !detail::lineThrough(world, world.headNode, 0, kFractionOne, start, end, fraction, 0, -1, &wall) &&
            wall >= 0)
        {
            const Plane &plane = world.standing.planes[wall >> 1];
            for (int i = 0; i < 3; ++i)
                normal[i] = (wall & 1) ? -(plane.normal[i] >> 18) : (plane.normal[i] >> 18);
            found = true;
        }
        fx back = 0;
        fx stop[3];
        detail::shotStop(start, end, fraction, stop); // (as traceShot)
        const detail::SegmentReach reach = detail::segmentReach(true, start, stop);
        detail::MoversNear near;
        detail::moversNear(world, reach, near);
        for (int k = 0, nearCount = near.size(world); k < nearCount; ++k)
        {
            const int m = near.at(k);
            if (!detail::moverMayTouch(world, m, reach))
                continue;
            const MoverHull &mover = world.movers[m];
            if (!mover.solid || !detail::nearMover(mover, true, start, stop))
                continue;
            fx a[3], b[3];
            detail::toMoverSpace(mover, start, a);
            detail::toMoverSpace(mover, end, b);
            const Trace t = traceHull(mover.crouched, a, b);
            if (t.startSolid || t.allSolid || !t.hit || t.fraction >= fraction)
                continue;
            fraction = t.fraction;
            // (Which it is: one whose own box the line goes into, as
            // traceShot has it. One met only by its hull's breadth is in
            // front of what was hit.)
            on = -1;
            if (metMover != nullptr && world.brushes != nullptr && world.models != nullptr)
            {
                const Model &model = world.models[world.brushes[m].model];
                fx mins[3] = {fxFromInt(model.mins[0]), fxFromInt(model.mins[1]), fxFromInt(model.mins[2])};
                fx maxs[3] = {fxFromInt(model.maxs[0]), fxFromInt(model.maxs[1]), fxFromInt(model.maxs[2])};
                detail::boxCentred(mover, mins, maxs);
                on = detail::lineIntoBox(a, b, mins, maxs) >= 0 ? m : -1;
            }
            else if (metMover != nullptr)
                on = m;
            back = 0;
            for (int i = 0; i < 3; ++i)
            {
                normal[i] = t.normal[i] >> 18;
                const fx n = normal[i] < 0 ? -normal[i] : normal[i];
                back += (i == 2 ? 18 : 16) * n;
            }
            found = true;
        }
        if (!found || fraction >= kFractionOne)
            return false;
        if (metMover != nullptr)
            *metMover = on;
        for (int i = 0; i < 3; ++i)
        {
            at[i] = start[i] + static_cast<fx>((static_cast<std::int64_t>(end[i] - start[i]) * fraction) >>
                                                kFractionShift);
            at[i] -= static_cast<fx>((static_cast<std::int64_t>(normal[i]) * back) >> 12);
        }
        return true;
    }

    /// A point moved from `start` to `end`: what the game moves anything 8
    /// units across or less as (its hull 0 -- a grenade, a satchel, a piece
    /// thrown out), as traceAll answers for a person. It stops where the
    /// world as it is drawn is met, a 32nd of a unit short of it as the
    /// engine stops; a brush entity is met as traceShotWall meets it. In
    /// solid at the start, it goes nowhere (`startSolid`).
    [[gnu::noinline]] inline Trace tracePoint(const MoveWorld &world, const fx *start, const fx *end)
    {
        Trace t;
        t.allSolid = false;
        for (int i = 0; i < 3; ++i)
            t.endPos[i] = end[i];
        if (world.nodes != nullptr && pointContents(world, start) == kContentsSolid)
        {
            t.startSolid = t.allSolid = true;
            t.fraction = 0;
            for (int i = 0; i < 3; ++i)
                t.endPos[i] = start[i];
            return t;
        }
        fx at[3], normal[3];
        int mover = -1;
        if (!traceShotWall(world, start, end, at, normal, &mover))
            return t;
        // (Off it by a 32nd, along the way it faces: 128 of Q12.)
        for (int i = 0; i < 3; ++i)
        {
            t.endPos[i] = at[i] + (normal[i] >> 5);
            t.normal[i] = normal[i] << (kNormalShift - kFxShift);
        }
        // How far along: by the axis it went furthest on (a division, but
        // only when something was met).
        int axis = 0;
        fx most = 0;
        for (int i = 0; i < 3; ++i)
        {
            const fx d = end[i] - start[i] < 0 ? start[i] - end[i] : end[i] - start[i];
            if (d > most)
            {
                most = d;
                axis = i;
            }
        }
        std::int64_t fraction =
            most == 0 ? 0 : (static_cast<std::int64_t>(t.endPos[axis] - start[axis]) << kFractionShift) / (end[axis] - start[axis]);
        fraction = fraction < 0 ? 0 : (fraction > kFractionOne ? kFractionOne : fraction);
        t.fraction = static_cast<std::int32_t>(fraction);
        if (t.fraction == 0)
        {
            for (int i = 0; i < 3; ++i)
                t.endPos[i] = start[i];
        }
        t.hit = true;
        t.mover = static_cast<std::int16_t>(mover);
        return t;
    }

    /// lineClear, with the brushes `seeThrough(brush)` says can be seen
    /// through (glass, a grate) not in the way: whether something behind
    /// them can be seen from `start`.
    template <class SeeThrough>
    inline bool sightClear(const MoveWorld &world, const fx *start, const fx *end, SeeThrough &&seeThrough);

    inline HL_TRACE_NOINLINE bool lineClear(const MoveWorld &world, const fx *start, const fx *end)
    {
        HL_TRACE_SPENT(kTraceLineClear);
        return sightClear(world, start, end, [](int) { return false; });
    }

    template <class SeeThrough>
    inline bool sightClear(const MoveWorld &world, const fx *start, const fx *end, SeeThrough &&seeThrough)
    {
        // (traceShot's answer, without its search for the nearest: the
        // first thing in the way will do -- the world first, which is the
        // cheapest and most often it.)
        if (world.nodes != nullptr)
        {
            std::int32_t hit = kFractionOne;
            if (!detail::lineThrough(world, world.headNode, 0, kFractionOne, start, end, hit, 0) && hit < kFractionOne)
                return false;
        }
        else
        {
            const Trace t = traceHull(world.crouched, start, end);
            if (t.startSolid || t.fraction < kFractionOne)
                return false;
        }
        const detail::SegmentReach reach = detail::segmentReach(true, start, end);
        detail::MoversNear near;
        detail::moversNear(world, reach, near);
        for (int k = 0, nearCount = near.size(world); k < nearCount; ++k)
        {
            const int m = near.at(k);
            if (!detail::moverMayTouch(world, m, reach))
                continue;
            const MoverHull &mover = world.movers[m];
            if (!mover.solid || seeThrough(m) || !detail::nearMover(mover, true, start, end))
                continue;
            fx a[3], b[3];
            detail::toMoverSpace(mover, start, a);
            detail::toMoverSpace(mover, end, b);
            const Trace t = traceHull(mover.crouched, a, b);
            if (!t.startSolid && !t.allSolid && t.fraction < kFractionOne)
                return false;
        }
        return true;
    }
} // namespace hldata
