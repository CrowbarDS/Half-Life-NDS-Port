#pragma once

// Walking: how anything with legs gets about.
//
// A step is the game's own: allowed if, lifted a stair's height, nothing is
// in the way, and there is floor under where it ends. A walk to somewhere is
// straight there when every step of that can be made, and otherwise from
// path node to path node by the table the converter worked out (PathNext) --
// the console looks its way up; it never searches.
//
// Monsters chase with this (HlMonsters.hpp), and people sent somewhere by a
// script walk there with it (HlLogic.hpp).
//
// The lines that seeing and shooting use are in HlMovers.hpp, where the
// player's own movement can reach them too.
//
// std-only and inline: the host tests run it.

#include "HlActors.hpp"
#include "HlPlayerMove.hpp"
#include "HlUi.hpp"

namespace hldata
{
    /// How long a step of a walk test is.
    inline constexpr fx kWalkStep = fxFromInt(16);

    // ------------------------------------------------------------ marks

    inline constexpr std::uint16_t kMarkShot = 3 | 3 << 5 | 3 << 10;    ///< a hole: nearly black
    inline constexpr std::uint16_t kMarkBlood = 10 | 1 << 5 | 1 << 10; ///< dried dark, as the game's is on a wall
    inline constexpr std::uint16_t kMarkAlien = 13 | 14 << 5 | 2 << 10; ///< a dull yellow, not a bright one
    inline constexpr std::uint16_t kMarkSoot = 2 | 2 << 5 | 2 << 10;
    /// A wall's own cracks and dents (the map's): thin and faint in the
    /// game, which a picture this small cannot be -- so not black either.
    inline constexpr std::uint16_t kMarkWall = 11 | 11 << 5 | 10 << 10;
    inline constexpr std::uint16_t kMarkCrack = 26 | 28 << 5 | 29 << 10; ///< glass, cracked: pale

    /// Specks off the wall at `at`, which faces `normal`: `count` of them,
    /// thrown off it and falling, in `colour`.
    inline void specksAdd(ActorWorld &a, const fx *at, const fx *normal, int count, std::uint16_t colour, int speed = 90)
    {
        for (int n = 0; n < count; ++n)
        {
            fx from[3], velocity[3];
            for (int k = 0; k < 3; ++k)
            {
                a.seed = a.seed * 1664525u + 1013904223u;
                const int stray = static_cast<int>((a.seed >> 10) % static_cast<std::uint32_t>(speed + 1)) - speed / 2;
                from[k] = at[k] + normal[k]; // (a unit clear of the wall)
                velocity[k] = normal[k] * (speed / 2) + fxFromInt(stray);
            }
            velocity[2] += fxFromInt(speed / 3);
            Piece &speck = pieceAdd(a, kPieceSpeck, from, velocity, 180 + static_cast<int>((a.seed >> 9) % 120));
            speck.half = 1;
            speck.colour = colour;
        }
    }

    /// A shot (or, with `blow`, a swing) has landed on the world at the end
    /// of the line from `start` towards `end`: its hole and its dust. False
    /// if the line lands on nothing that takes a mark.
    inline bool markShot(ActorWorld &a, const MoveWorld &world, const fx *start, const fx *end, bool blow)
    {
        fx at[3], normal[3];
        if (!traceShotWall(world, start, end, at, normal))
            return false;
        if (blow)
        {
            decalAdd(a, kDecalDent, at, normal, 8, kMarkShot);
            specksAdd(a, at, normal, 2, static_cast<std::uint16_t>(13 | 13 << 5 | 13 << 10), 60);
        }
        else
        {
            a.seed = a.seed * 1664525u + 1013904223u;
            decalAdd(a, static_cast<std::uint8_t>(kDecalShot + ((a.seed >> 9) % kDecalShots)), at, normal, 8, kMarkShot);
            specksAdd(a, at, normal, 3, static_cast<std::uint16_t>(15 | 15 << 5 | 14 << 10));
        }
        return true;
    }

    /// Blood, from a wound at `from` made by something travelling `along`
    /// (Q12, unit): if there is a wall or a floor within a body's length
    /// behind it, a stain there. `much`: a killing blow, or a shotgun's.
    inline bool markBlood(ActorWorld &a, const MoveWorld &world, const fx *from, const fx *along, bool alien, bool much)
    {
        a.seed = a.seed * 1664525u + 1013904223u;
        const int reach = 72 + static_cast<int>((a.seed >> 9) % 56);
        // (On along the shot, and a little down: blood falls.)
        const fx to[3] = {from[0] + along[0] * reach, from[1] + along[1] * reach,
                          from[2] + along[2] * reach - fxFromInt(reach / 3)};
        fx at[3], normal[3];
        if (!traceShotWall(world, from, to, at, normal))
            return false;
        const std::uint8_t picture = alien ? ((a.seed >> 14) & 1 ? kDecalAlien : kDecalAlien2)
                                           : static_cast<std::uint8_t>(kDecalBlood + ((a.seed >> 14) % kDecalBloods));
        decalAdd(a, picture, at, normal, (much ? 24 : 18) + static_cast<int>((a.seed >> 18) % 4), alien ? kMarkAlien : kMarkBlood);
        return true;
    }

    /// ... and under something at `at` that has died, or come apart, or
    /// landed in pieces: a stain on the floor, if there is floor within
    /// `drop` units.
    inline bool markFloor(ActorWorld &a, const MoveWorld &world, const fx *at, int drop, std::uint8_t picture, int half,
                          std::uint16_t colour)
    {
        const fx from[3] = {at[0], at[1], at[2] + fxFromInt(4)};
        const fx to[3] = {at[0], at[1], at[2] - fxFromInt(drop)};
        fx on[3], normal[3];
        if (!traceShotWall(world, from, to, on, normal) || normal[2] < kFxOne / 2)
            return false;
        decalAdd(a, picture, on, normal, half, colour);
        return true;
    }

    /// A blast at `at`: its scorch on the floor under it (or the nearest
    /// wall straight down), and what it throws up.
    inline void markBlast(ActorWorld &a, const MoveWorld &world, const fx *at, int size)
    {
        const fx from[3] = {at[0], at[1], at[2] + fxFromInt(8)};
        const fx to[3] = {at[0], at[1], at[2] - fxFromInt(72)};
        fx on[3], normal[3];
        if (!traceShotWall(world, from, to, on, normal))
            return;
        decalAdd(a, kDecalScorch, on, normal, size < 32 ? 32 : (size > 56 ? 56 : size), kMarkSoot);
        specksAdd(a, on, normal, 5, static_cast<std::uint16_t>(6 | 5 << 5 | 4 << 10), 220);
    }

    /// The small things in flight, for `ms` more milliseconds: they fall,
    /// turn, land (bouncing once, then lying) and are gone when their time
    /// is up. Returns which casings rang on the floor this time: bit 0 a
    /// brass one (or a can), bit 1 a shotgun's.
    inline int piecesTick(ActorWorld &a, const MoveWorld &world, int ms)
    {
        int rang = 0;
        for (Piece &piece : a.pieces)
        {
            if (piece.kind == 0)
                continue;
            piece.leftMs -= ms;
            if (piece.leftMs <= 0)
            {
                piece.kind = 0;
                continue;
            }
            if (piece.bounces >= 2)
                continue; // it lies where it fell
            piece.spin = static_cast<std::uint16_t>(piece.spin + piece.spinRate * ms);
            piece.velocity[2] -= static_cast<fx>((static_cast<std::int64_t>(fxFromInt(800)) * piece.weight * ms) / 256000);
            // (What drifts does not fall faster than a leaf.)
            if (piece.weight < 256 && piece.velocity[2] < -fxFromInt(120))
                piece.velocity[2] = -fxFromInt(120);
            fx to[3];
            for (int i = 0; i < 3; ++i)
                to[i] = piece.pos[i] + static_cast<fx>((static_cast<std::int64_t>(piece.velocity[i]) * ms) / 1000);
            // (Specks are too many and too brief to be worth a line each.)
            if (piece.kind == kPieceSpeck)
            {
                for (int i = 0; i < 3; ++i)
                    piece.pos[i] = to[i];
                continue;
            }
            const Shot shot = traceShot(world, piece.pos, to);
            if (shot.fraction >= kFractionOne)
            {
                for (int i = 0; i < 3; ++i)
                    piece.pos[i] = to[i];
                continue;
            }
            // Landed on something: up off it again once, at a third of the
            // speed, then it lies. (Which way the thing faces is not asked:
            // what a shard lands on is nearly always a floor.)
            ++piece.bounces;
            if ((piece.kind == kPieceShell || piece.kind == kPieceCan) && piece.bounces == 1)
                rang |= 1;
            else if (piece.kind == kPieceShotShell && piece.bounces == 1)
                rang |= 2;
            const fx speed = piece.velocity[2] < 0 ? -piece.velocity[2] : piece.velocity[2];
            if (piece.bounces >= 2 || speed < fxFromInt(60))
            {
                // It lies: and what lies is gone in a third of the time it
                // had left (a floor of things lying is a floor of polygons).
                piece.bounces = 2;
                piece.velocity[0] = piece.velocity[1] = piece.velocity[2] = 0;
                piece.leftMs = piece.leftMs / 3 < 300 ? (piece.leftMs < 300 ? piece.leftMs : 300) : piece.leftMs / 3;
                continue;
            }
            piece.velocity[0] /= 3;
            piece.velocity[1] /= 3;
            piece.velocity[2] = speed / 3;
            piece.spinRate = static_cast<std::int16_t>(piece.spinRate / 2);
        }
        return rang;
    }

    inline bool pointToFloor(const MoveWorld &world, fx *pos, fx down)
    {
        const fx from[3] = {pos[0], pos[1], pos[2] + fxFromInt(1)};
        const fx to[3] = {pos[0], pos[1], pos[2] - down};
        const Shot shot = traceShot(world, from, to);
        if (shot.fraction <= 0 || shot.fraction >= kFractionOne)
            return false;
        fx z = from[2] - static_cast<fx>((static_cast<std::int64_t>(from[2] - to[2]) * shot.fraction) >>
                                         kFractionShift);
        // (A brush is met as the box a crouching person cannot enter: half
        // that person's height above where it is.)
        if (shot.mover >= 0)
            z -= kCrouchedFeet;
        if (z < pos[2])
            pos[2] = z;
        return true;
    }

    // ------------------------------------------------------------- walking

    namespace detail
    {
        /// A trace as the thing at `index` (or as nobody, -1): its own box
        /// is not in its way.
        inline Trace walkerTrace(const MoveWorld &world, const ActorWorld *a, int index, bool small,
                                 const fx *start, const fx *end)
        {
            SolidBox *own = (a != nullptr && a->boxes != nullptr && index >= 0) ? &a->boxes[index] : nullptr;
            const bool was = own != nullptr && own->active;
            if (own != nullptr)
                own->active = false;
            const Trace t = traceAll(world, small, start, end);
            if (own != nullptr)
                own->active = was;
            return t;
        }

        /// The floor under (x, y) between `top` and `bottom`, as a point
        /// falls (the engine's MOVE_NOMONSTERS: the world and the brushes,
        /// no actors): into `z`; false if there is none that near. A brush
        /// is taken as its box -- what monsters stand on is.
        inline bool floorUnder(const MoveWorld &world, fx x, fx y, fx top, fx bottom, fx &z)
        {
            const fx from[3] = {x, y, top}, to[3] = {x, y, bottom};
            std::int32_t hit = kFractionOne;
            bool found = !lineThrough(world, world.headNode, 0, kFractionOne, from, to, hit, 0);
            z = found ? top - static_cast<fx>((static_cast<std::int64_t>(top - bottom) * hit) >> kFractionShift)
                      : bottom;
            const int ux = x >> kFxShift, uy = y >> kFxShift;
            for (int m = 0; m < world.moverCount; ++m)
            {
                if (world.reach != nullptr)
                {
                    // (Whole units, rounded outwards: never "no" for a yes.)
                    const MoverReach &r = world.reach[m];
                    if (ux < r.lo[0] || ux > r.hi[0] || uy < r.lo[1] || uy > r.hi[1] || r.hi[2] < (z >> kFxShift) ||
                        r.lo[2] > (top >> kFxShift) + 1)
                        continue;
                }
                const MoverHull &mover = world.movers[m];
                if (!mover.solid || x < mover.mins[0] || x > mover.maxs[0] || y < mover.mins[1] ||
                    y > mover.maxs[1] || mover.mins[2] > top)
                    continue;
                // (Begun inside it: stopped where it began.)
                const fx at = mover.maxs[2] < top ? mover.maxs[2] : top;
                if (at >= z && at >= bottom)
                {
                    z = at;
                    found = true;
                }
            }
            return found;
        }

        /// SV_CheckBottom: whether a walker `half` wide with its feet at
        /// `feet` stands on something -- its middle on a floor within a
        /// stair of its feet, and no corner more than a stair below that.
        /// Corners all over solid world are let through at once, which is
        /// most of the time. (A world with no tree has nothing to ask: yes.)
        inline bool walkBottom(const MoveWorld &world, const fx *feet, fx half)
        {
            if (world.nodes == nullptr)
                return true;
            bool solid = true;
            for (int k = 0; k < 4 && solid; ++k)
            {
                const fx p[3] = {feet[0] + (k & 1 ? half : -half), feet[1] + (k & 2 ? half : -half),
                                 feet[2] - fxFromInt(1)};
                solid = world.leaves[worldLeaf(world, p)].contents == kContentsSolid;
            }
            if (solid)
                return true;
            const fx top = feet[2] + kStepSize, bottom = feet[2] - kStepSize;
            fx mid;
            if (!floorUnder(world, feet[0], feet[1], top, bottom, mid))
                return false;
            for (int k = 0; k < 4; ++k)
            {
                fx z;
                if (!floorUnder(world, feet[0] + (k & 1 ? half : -half), feet[1] + (k & 2 ? half : -half), top,
                                bottom, z) ||
                    mid - z > kStepSize)
                    return false;
            }
            return true;
        }
    } // namespace detail

    inline constexpr int kWalkStanding = 0;
    inline constexpr int kWalkCrouched = 1;
    inline constexpr int kWalkLarge = 2;

    /// One step of a walk: `feet` moved by (dx, dy) if something that size
    /// can make it -- lifted a stair's height, nothing in the way; then let
    /// down, floor within two stairs' height. `feet` is changed only if so.
    ///
    /// `hull` is the size: kWalkStanding, kWalkCrouched, or kWalkLarge --
    /// which goes as a standing person does, but only to where something 64
    /// units across has room in the world (the game's large hull). `half`
    /// is half its width for what it stands on (0: the hull's own).
    /// `blockedBy`, if given: which actor's box stopped the step (-1 if
    /// none did).
    HL_HOT bool walkStep(const MoveWorld &world, const ActorWorld *a, int index, int hull,
                         fx *feet, fx dx, fx dy, fx half = 0, int *blockedBy = nullptr)
    {
        if (blockedBy != nullptr)
            *blockedBy = -1;
        const bool small = hull == kWalkCrouched;
        const fx lift = small ? kCrouchedFeet : kStandingFeet;
        fx start[3] = {feet[0], feet[1], feet[2] + lift};
        fx up[3] = {start[0], start[1], start[2] + kStepSize};
        Trace raised = detail::walkerTrace(world, a, index, small, start, up);
        if (raised.startSolid)
        {
            // Stood exactly on a wall's face counts as in it (the trace's
            // rule, and the game's): someone put there -- a script's mark
            // against the soda machine or along a table's edge, c1a0d's --
            // would never take a step again. The game's step is tried where
            // it goes, not where it stands; here, from a unit along the way,
            // or a unit to either side of it: one of them is off the face
            // unless the step is into the wall.
            if (dx == 0 && dy == 0)
                return false;
            const fx sx = dx > 0 ? fxFromInt(1) : (dx < 0 ? -fxFromInt(1) : 0);
            const fx sy = dy > 0 ? fxFromInt(1) : (dy < 0 ? -fxFromInt(1) : 0);
            const fx tries[3][2] = {{sx, sy}, {-sy, sx}, {sy, -sx}};
            const fx feetAt[2] = {start[0], start[1]};
            bool free = false;
            for (int t = 0; t < 3 && !free; ++t)
            {
                start[0] = feetAt[0] + tries[t][0];
                start[1] = feetAt[1] + tries[t][1];
                up[0] = start[0];
                up[1] = start[1];
                raised = detail::walkerTrace(world, a, index, small, start, up);
                free = !raised.startSolid;
            }
            if (!free)
                return false;
        }
        const fx along[3] = {raised.endPos[0] + dx, raised.endPos[1] + dy, raised.endPos[2]};
        const Trace across = detail::walkerTrace(world, a, index, small, raised.endPos, along);
        if (across.startSolid || across.fraction < kFractionOne)
        {
            if (blockedBy != nullptr)
                *blockedBy = across.box;
            return false;
        }
        // Not deeper into someone it is already inside of (a move out of
        // them is let through, as the trace lets it: nobody is held fast
        // in another) -- two that go for one spot do not merge into one.
        // (Not on a console sent the game: a match has no monsters.)
#ifndef HLDS_CHILD
        if (index >= 0)
        {
            for (int n = 0; n < world.boxCount; ++n)
            {
                const SolidBox &box = world.boxes[n];
                if (n == index || !box.active || start[2] - lift > box.maxs[2] || start[2] - lift + fxFromInt(64) < box.mins[2])
                    continue;
                const fx cx = (box.mins[0] + box.maxs[0]) / 2, cy = (box.mins[1] + box.maxs[1]) / 2;
                // (Its body in theirs by more than a hand's breadth: a
                // touch is the look ahead's to see to -- it waits, or goes
                // round.)
                const fx rx = (box.maxs[0] - box.mins[0]) / 2 + fxFromInt(8), ry = (box.maxs[1] - box.mins[1]) / 2 + fxFromInt(8);
                const fx ox = start[0] - cx, oy = start[1] - cy;
                if (ox <= -rx || ox >= rx || oy <= -ry || oy >= ry)
                    continue;
                // (Inside it: towards its middle is deeper.)
                if (static_cast<std::int64_t>(ox) * dx + static_cast<std::int64_t>(oy) * dy < 0)
                {
                    if (blockedBy != nullptr)
                        *blockedBy = n;
                    return false;
                }
            }
        }
#endif
        const fx down[3] = {along[0], along[1], start[2] - kStepSize};
        const Trace drop = detail::walkerTrace(world, a, index, small, along, down);
        if (drop.startSolid || drop.fraction >= kFractionOne)
            return false; // nothing to stand on: it does not walk off edges
        // Nor onto someone: no one stands on another's head (the game's
        // SV_CheckBottom asks the world alone).
#ifndef HLDS_CHILD
        if (drop.box >= 0)
        {
            if (blockedBy != nullptr)
                *blockedBy = drop.box;
            return false;
        }
#endif
        // Nor over them (SV_CheckBottom) -- unless it is over one already:
        // then, as the engine's FL_PARTIALGROUND lets it, it may move, and
        // so get off it.
        if (half == 0)
            half = hull == kWalkLarge ? fxFromInt(32) : fxFromInt(16);
        const fx landed[3] = {drop.endPos[0], drop.endPos[1], drop.endPos[2] - lift};
        if (!detail::walkBottom(world, landed, half) && detail::walkBottom(world, feet, half))
            return false;
        if (hull == kWalkLarge && world.large.nodes != nullptr)
        {
            // Room for its width there? (If it has none where it is, it is
            // let move all the same: out of it.)
            const fx there[3] = {drop.endPos[0], drop.endPos[1], drop.endPos[2] - lift + fxFromInt(33)};
            const fx here[3] = {feet[0], feet[1], feet[2] + fxFromInt(33)};
            if (hullPointContents(world.large, world.large.root, there) == kContentsSolid &&
                hullPointContents(world.large, world.large.root, here) != kContentsSolid)
                return false;
        }
        feet[0] = landed[0];
        feet[1] = landed[1];
        feet[2] = landed[2];
        return true;
    }

    /// Whether something that size can walk from `from` to `to` (feet, both)
    /// in a straight line, in at most `steps` steps of 16 units.
    inline bool walkClear(const MoveWorld &world, const ActorWorld *a, int index, int small,
                          const fx *from, const fx *to, int steps)
    {
        fx at[3] = {from[0], from[1], from[2]};
        int pause = 0;
        for (int n = 0; n <= steps; ++n)
        {
            thinkPoint(pause, 5); // (three hull traces a step, and its footing)
            const fx d[3] = {to[0] - at[0], to[1] - at[1], 0};
            const fx distance = fxLength(d);
            if (distance <= kWalkStep)
            {
                const fx dz = to[2] - at[2];
                return dz < fxFromInt(40) && dz > -fxFromInt(40);
            }
            const fx dx = static_cast<fx>((static_cast<std::int64_t>(d[0]) * kWalkStep) / distance);
            const fx dy = static_cast<fx>((static_cast<std::int64_t>(d[1]) * kWalkStep) / distance);
            int by = -1;
            if (!walkStep(world, a, index, small, at, dx, dy, 0, &by))
            {
                // Stopped by whoever stands where it is going -- the place
                // it walks to is in their very body: they are what it goes
                // for -- as good as there (CheckLocalMove: a step into the
                // target is legal).
#ifndef HLDS_CHILD
                if (by >= 0 && by < world.boxCount)
                {
                    const SolidBox &box = world.boxes[by];
                    return to[0] >= box.mins[0] && to[0] <= box.maxs[0] && to[1] >= box.mins[1] &&
                           to[1] <= box.maxs[1] && to[2] >= box.mins[2] - fxFromInt(40) &&
                           to[2] <= box.maxs[2] + fxFromInt(40);
                }
#endif
                return false;
            }
        }
        return false;
    }

    /// FTriangulate: a place beside what is in the way of a walker going
    /// from `from` to `far` (feet, both) that it can walk to, and from there
    /// on to `far`. Tried to the right of the way and then to the left,
    /// past the obstacle by the walker's size and out by three times it,
    /// then twice its size further out each time, eight times. `size` is
    /// its width (held to 24..48, as the game holds it); `checked` how far
    /// ahead the way was clear. Into `apex`; false if there is none.
    inline bool walkTriangulate(const MoveWorld &world, const ActorWorld *a, int index, int small, const fx *from,
                                const fx *far, fx size, fx checked, fx *apex)
    {
        if (size < fxFromInt(24))
            size = fxFromInt(24);
        else if (size > fxFromInt(48))
            size = fxFromInt(48);
        const fx d[3] = {far[0] - from[0], far[1] - from[1], 0};
        const fx length = fxLength(d);
        if (length <= 0)
            return false;
        const fx forward[2] = {static_cast<fx>((static_cast<std::int64_t>(d[0]) << kFxShift) / length),
                               static_cast<fx>((static_cast<std::int64_t>(d[1]) << kFxShift) / length)};
        // (The way, crossed with up: to its right.)
        const fx right[2] = {forward[1], -forward[0]};
        const fx ahead = checked + size;
        fx out = size * 3;
        auto legs = [&](const fx *p)
        {
            const fx e[3] = {p[0] - from[0], p[1] - from[1], 0};
            const fx g[3] = {far[0] - p[0], far[1] - p[1], 0};
            return walkClear(world, a, index, small, from, p, (fxLength(e) >> (kFxShift + 4)) + 2) &&
                   walkClear(world, a, index, small, p, far, (fxLength(g) >> (kFxShift + 4)) + 2);
        };
        for (int round = 0; round < 8; ++round, out += size * 2)
        {
            for (int side = 0; side < 2; ++side)
            {
                const fx across = side == 0 ? out : -out;
                const fx p[3] = {from[0] + fxMul(forward[0], ahead) + fxMul(right[0], across),
                                 from[1] + fxMul(forward[1], ahead) + fxMul(right[1], across), from[2]};
                if (legs(p))
                {
                    for (int k = 0; k < 3; ++k)
                        apex[k] = p[k];
                    return true;
                }
            }
        }
        return false;
    }

    /// The path node to make for from `feet`: the nearest that can be seen
    /// from there, of the three nearest; -1 if there is none.
    inline int pathNearest(const ActorWorld &a, const MoveWorld &world, const fx *feet, int hull = -1,
                           int index = -1, bool air = false)
    {
        int best[3] = {-1, -1, -1};
        fx bestD[3] = {0, 0, 0};
        for (int n = 0; n < a.nodeCount; ++n)
        {
            // (The nodes in the air are for what flies: `air`.)
            if ((a.nodes[n].flags & kPathNodeAir) && !air)
                continue;
            const fx d[3] = {a.nodes[n].pos[0] - feet[0], a.nodes[n].pos[1] - feet[1],
                             a.nodes[n].pos[2] - feet[2]};
            // (Height counts double: the node on the floor above is not near.)
            const fx distance = (d[0] < 0 ? -d[0] : d[0]) + (d[1] < 0 ? -d[1] : d[1]) +
                                2 * (d[2] < 0 ? -d[2] : d[2]);
            if (distance > fxFromInt(768))
                continue;
            for (int k = 0; k < 3; ++k)
            {
                if (best[k] >= 0 && bestD[k] <= distance)
                    continue;
                for (int j = 2; j > k; --j)
                {
                    best[j] = best[j - 1];
                    bestD[j] = bestD[j - 1];
                }
                best[k] = n;
                bestD[k] = distance;
                break;
            }
        }
        // One that can be WALKED to from there, for whoever is asking as
        // something that walks (a node across a rail can be seen and not
        // reached); failing that, one that can be seen.
        const fx eye[3] = {feet[0], feet[1], feet[2] + fxFromInt(20)};
        int seen = -1;
        for (int k = 0; k < 3; ++k)
        {
            if (best[k] < 0)
                break;
            const fx *p = a.nodes[best[k]].pos;
            const fx to[3] = {p[0], p[1], p[2] + fxFromInt(20)};
            if (!lineClear(world, eye, to))
                continue;
            if (hull < 0)
                return best[k];
            const fx d[3] = {p[0] - feet[0], p[1] - feet[1], 0};
            if (walkClear(world, &a, index, hull, feet, p, fxToInt(fxLength(d)) / 16 + 4))
                return best[k];
            seen = seen < 0 ? best[k] : seen;
        }
        return seen;
    }

    /// Who opens a door that is in their way: people, soldiers, and the
    /// monsters that have hands for it (the game's own list).
    inline bool opensDoors(std::uint8_t kind)
    {
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Scientist:
        case ActorKind::Guard:
        case ActorKind::Gman:
        case ActorKind::Generic:
        case ActorKind::Grunt:
        case ActorKind::Zombie:
        case ActorKind::Slave:
        case ActorKind::Bullsquid:
            return true;
        default:
            return false;
        }
    }

    /// The link from node `from` to node `to`, or -1.
    inline int pathLinkBetween(const ActorWorld &a, int from, int to)
    {
        const PathNode &node = a.nodes[from];
        for (int l = node.firstLink; l < node.firstLink + node.linkCount && l < a.linkCount; ++l)
        {
            if (a.links[l].to == to)
                return l;
        }
        return -1;
    }

    /// The node to go to next on the shortest way from node `from` to node
    /// `to`, for something that takes links marked `size` (a kPathLink*) and
    /// does or does not open doors: found now, over the links as they are
    /// now. kNoPathNode if there is no way; `to` itself if they are joined.
    ///
    /// (Every node's distance from `from`, nearest first, until `to` is
    /// reached: at most 254 nodes, a kilobyte on the stack. A shut door
    /// counts as a little further than it is.)
    inline std::uint8_t pathStep(const ActorWorld &a, int from, int to, std::uint8_t size, bool opens)
    {
        if (from < 0 || to < 0 || from >= a.nodeCount || to >= a.nodeCount || a.links == nullptr)
            return kNoPathNode;
        if (from == to)
            return static_cast<std::uint8_t>(to);
        std::uint16_t far[kMaxPathNodes];
        std::uint8_t before[kMaxPathNodes];
        std::uint8_t done[kMaxPathNodes];
        for (int n = 0; n < a.nodeCount; ++n)
        {
            far[n] = 0xFFFF;
            before[n] = kNoPathNode;
            done[n] = 0;
        }
        far[from] = 0;
        int pause = 0;
        for (;;)
        {
            thinkPoint(pause, 24); // (a scan of every node a time)
            int at = -1;
            for (int n = 0; n < a.nodeCount; ++n)
            {
                if (!done[n] && far[n] != 0xFFFF && (at < 0 || far[n] < far[at]))
                    at = n;
            }
            if (at < 0)
                return kNoPathNode;
            if (at == to)
                break;
            done[at] = 1;
            const PathNode &node = a.nodes[at];
            for (int l = node.firstLink; l < node.firstLink + node.linkCount && l < a.linkCount; ++l)
            {
                const PathLink &link = a.links[l];
                if (!(link.flags & size) || done[link.to])
                    continue;
                const std::uint8_t shut = a.linkShut != nullptr ? a.linkShut[l] : kLinkOpen;
                if (shut == kLinkNoWay || (shut == kLinkDoor && !opens))
                    continue;
                const std::uint32_t through = far[at] + link.length + (shut == kLinkDoor ? 128u : 0u) + 1u;
                if (through < far[link.to])
                {
                    far[link.to] = static_cast<std::uint16_t>(through > 0xFFFE ? 0xFFFE : through);
                    before[link.to] = static_cast<std::uint8_t>(at);
                }
            }
        }
        // Back along the way found, to its first step.
        int step = to;
        for (int guard = 0; guard < kMaxPathNodes && before[step] != from; ++guard)
        {
            if (before[step] == kNoPathNode)
                return kNoPathNode;
            step = before[step];
        }
        return static_cast<std::uint8_t>(step);
    }

    /// Which nodes can be got to from node `from` at all, over the links as
    /// they are now (for something of `size` that does or does not open
    /// doors): `reached[n]` is 1 for each. The same search as pathStep's, made
    /// once for every node -- what asking pathStep of each in turn would
    /// tell, for a node's worth of the cost.
    inline void pathReach(const ActorWorld &a, int from, std::uint8_t size, bool opens, std::uint8_t *reached)
    {
        for (int n = 0; n < a.nodeCount; ++n)
            reached[n] = 0;
        if (from < 0 || from >= a.nodeCount || a.links == nullptr)
            return;
        // (Only whether, not how far: a flood over the links will do.)
        std::uint8_t queue[kMaxPathNodes];
        int head = 0, tail = 0;
        reached[from] = 1;
        queue[tail++] = static_cast<std::uint8_t>(from);
        while (head < tail)
        {
            const PathNode &node = a.nodes[queue[head++]];
            for (int l = node.firstLink; l < node.firstLink + node.linkCount && l < a.linkCount; ++l)
            {
                const PathLink &link = a.links[l];
                if (!(link.flags & size) || reached[link.to])
                    continue;
                const std::uint8_t shut = a.linkShut != nullptr ? a.linkShut[l] : kLinkOpen;
                if (shut == kLinkNoWay || (shut == kLinkDoor && !opens))
                    continue;
                reached[link.to] = 1;
                if (tail < kMaxPathNodes)
                    queue[tail++] = link.to;
            }
        }
    }

    /// Which links something of that size takes.
    inline std::uint8_t pathSizeOf(int hull)
    {
        return hull == kWalkCrouched ? kPathLinkSmall : (hull == kWalkLarge ? kPathLinkLarge : kPathLinkHuman);
    }

    // ------------------------------------------------------------- going somewhere

    // What a tick of walking came to.
    inline constexpr int kWalkMoved = 0;   ///< nearer, or turning a corner
    inline constexpr int kWalkBlocked = 1; ///< something is in the way, for now
    inline constexpr int kWalkNoWay = 2;   ///< no way there is known

    namespace detail
    {
        inline void faceTo(ActorState &actor, fx dx, fx dy, int ms, int turnsPerSecond)
        {
            if (dx == 0 && dy == 0)
                return;
            const std::uint16_t want = fxAtan2Turns(dy, dx);
            const auto diff = static_cast<std::int16_t>(want - actor.yaw);
            const int most = (turnsPerSecond * ms) / 1000;
            const int turn = diff > most ? most : (diff < -most ? -most : diff);
            actor.yaw = static_cast<std::uint16_t>(actor.yaw + turn);
        }

        /// Whether a box with its feet at `feet` is in the player.
        inline bool boxInPlayer(const PlayerState &player, const fx *feet, fx half, fx height)
        {
            const fx tall = kPlayerHalfHeight[player.ducked ? 1 : 0];
            return feet[0] - half <= player.origin[0] + kPlayerHalfWidth &&
                   feet[0] + half >= player.origin[0] - kPlayerHalfWidth &&
                   feet[1] - half <= player.origin[1] + kPlayerHalfWidth &&
                   feet[1] + half >= player.origin[1] - kPlayerHalfWidth &&
                   feet[2] <= player.origin[2] + tall && feet[2] + height >= player.origin[2] - tall;
        }

        /// Work out where the walker at `index` should be making for on its
        /// way to `dest` (feet): `dest` itself, or a path node -- into
        /// `actor` (the walker, or a copy of it), with a shut door on the way
        /// to be asked to open left in `door` (a brush, or -1).
        inline void walkPlanInto(const ActorWorld &a, const MoveWorld &world, int index, ActorState &actor,
                                 int small, const fx *dest, int reach, int &door)
        {
            door = -1;
            const fx d[3] = {dest[0] - actor.pos[0], dest[1] - actor.pos[1], 0};
            // Straight there, if that is a walk that can be made.
            if (fxLength(d) < fxFromInt(reach) &&
                walkClear(world, &a, index, small, actor.pos, dest, reach / 16))
            {
                for (int i = 0; i < 3; ++i)
                    actor.goal[i] = dest[i];
                actor.hasGoal = true;
                actor.goalNode = actor.fromNode = -1;
                return;
            }
            // Otherwise by the nodes: to the nearest, and from each to the
            // next on the way to the one nearest where it is going.
            actor.hasGoal = false;
            if (a.nodeCount == 0)
                return;
            const int theirs = pathNearest(a, world, dest);
            if (theirs < 0)
                return;
            if (actor.goalNode < 0)
            {
                if (actor.fromNode >= 0)
                {
                    const bool opens = opensDoors(actor.kind);
                    const std::uint8_t next = pathStep(a, actor.fromNode, theirs, pathSizeOf(small), opens);
                    // (At the last node and still not there: no nearer by
                    // nodes.)
                    actor.goalNode = (next == kNoPathNode || next == actor.fromNode) ? -1 : next;
                    // A shut door on the way: it is asked for, and walked
                    // up to while it opens.
                    if (actor.goalNode >= 0 && a.linkShut != nullptr)
                    {
                        const int link = pathLinkBetween(a, actor.fromNode, actor.goalNode);
                        if (link >= 0 && a.linkShut[link] == kLinkDoor && a.links[link].brush != kNoBrush)
                            door = a.links[link].brush;
                    }
                }
                else
                {
                    actor.goalNode = static_cast<std::int16_t>(pathNearest(a, world, actor.pos, small, index));
                }
            }
            if (actor.goalNode < 0)
                return;
            for (int i = 0; i < 3; ++i)
                actor.goal[i] = a.nodes[actor.goalNode].pos[i];
            actor.hasGoal = true;
        }

        /// A door a walker wants opened (the logic opens it: a few a tick).
        inline void walkWantDoor(ActorWorld &a, int door)
        {
            if (door >= 0 && a.doorsWanted < 4)
                a.doorWanted[a.doorsWanted++] = static_cast<std::uint16_t>(door);
        }

        /// The same, for the walker itself, at once.
        inline void walkPlan(ActorWorld &a, const MoveWorld &world, int index, int small, const fx *dest, int reach)
        {
            int door = -1;
            walkPlanInto(a, world, index, a.states[index], small, dest, reach, door);
            walkWantDoor(a, door);
        }
    } // namespace detail

    /// One tick of the walker at `index` going to `dest` (feet) at `speed`
    /// units a second: it plans twice a second, turns to face its way, and
    /// takes the step if it can. It does not walk into `player` (if given),
    /// being `half` wide and `height` tall. Uses the actor's thinkMs,
    /// stuckMs, goal and node fields.
    ///
    /// `idealYaw`, if given, is where the way it is going is put, for the
    /// caller to turn it at its own pace (a monster: aiTurnTick, at its
    /// yaw speed); with none it turns itself, a turn a second.
    ///
    /// `still` says `dest` does not move (a mark, not a player): then a
    /// straight walk is looked for from further off, since it need be found
    /// only once -- the plan is made again only on reaching a node or on
    /// being held up.
#ifdef HL_DSL
    // (In the game library, the engine's own, in ITCM: game/GameApi.hpp.)
    int walkTick(ActorWorld &a, const MoveWorld &world, int index, int small, const fx *dest,
                        fx speed, int ms, const PlayerState *player, fx half, fx height,
                        bool still = false, std::uint16_t *idealYaw = nullptr);
#else
    HL_HOT int walkTick(ActorWorld &a, const MoveWorld &world, int index, int small, const fx *dest,
                        fx speed, int ms, const PlayerState *player, fx half, fx height,
                        bool still = false, std::uint16_t *idealYaw = nullptr)
    {
        ActorState &actor = a.states[index];
        actor.thinkMs -= ms;
        if (actor.thinkMs <= 0)
        {
            ThinkSlot *slot = a.think != nullptr && a.think->deferred && index < a.think->count
                                  ? &a.think->slots[index]
                                  : nullptr;
            bool planned = slot == nullptr;
            if (slot == nullptr)
            {
                detail::walkPlan(a, world, index, small, dest, still ? 1024 : 480);
            }
            else if (slot->kind == kThinkPlan && slot->state == kThinkDone)
            {
                // Worked out outside the frame: taken up, if it was worked
                // out from where the walker still is on its way.
                slot->state = kThinkFree;
                if (slot->basis == actor.fromNode)
                {
                    actor.hasGoal = slot->found;
                    actor.goalNode = slot->goalNode;
                    actor.fromNode = slot->fromNode;
                    for (int i = 0; i < 3; ++i)
                        actor.goal[i] = slot->out[i];
                    detail::walkWantDoor(a, slot->door);
                    planned = true;
                }
            }
            else if (slot->state == kThinkFree || (slot->kind != kThinkPlan && slot->state == kThinkDone))
            {
                // Asked; meanwhile on as it was going.
                slot->kind = kThinkPlan;
                slot->state = kThinkAsked;
                for (int i = 0; i < 3; ++i)
                    slot->at[i] = dest[i];
                slot->small = static_cast<std::int16_t>(small);
                slot->reach = static_cast<std::int16_t>(still ? 1024 : 480);
                slot->basis = actor.fromNode;
            }
            if (planned)
            {
                actor.thinkMs = still ? 60000 : 500;
                // (Nothing found: asked again -- soon by something chasing,
                // whose quarry moves; not so soon by something going to a
                // mark, for which looking is dear and little will have
                // changed.)
                if (!actor.hasGoal)
                    actor.thinkMs = still ? 1400 : 500;
            }
        }
        if (!actor.hasGoal)
            return kWalkNoWay;
        const fx d[3] = {actor.goal[0] - actor.pos[0], actor.goal[1] - actor.pos[1], 0};
        const fx distance = fxLength(d);
        // Arrived at a node: on to the next when it next plans, which is now.
        if (actor.goalNode >= 0 && distance < fxFromInt(20))
        {
            actor.fromNode = actor.goalNode;
            actor.goalNode = -1;
            actor.thinkMs = 0;
            return kWalkMoved;
        }
        if (distance < fxFromInt(4))
            return kWalkMoved;
        if (idealYaw != nullptr)
            *idealYaw = fxAtan2Turns(d[1], d[0]);
        else
            detail::faceTo(actor, d[0], d[1], ms, 65536); // a turn a second
        fx step = static_cast<fx>((static_cast<std::int64_t>(speed) * ms) / 1000);
        if (step > distance)
            step = distance;
        const fx dx = static_cast<fx>((static_cast<std::int64_t>(d[0]) * step) / distance);
        const fx dy = static_cast<fx>((static_cast<std::int64_t>(d[1]) * step) / distance);
        fx feet[3] = {actor.pos[0], actor.pos[1], actor.pos[2]};
        // The whole step, or none: a walker does not slide along what is in
        // its way (the game's monsters look ahead, stop, and go round it).
        bool moved = walkStep(world, &a, index, small, feet, dx, dy, half);
        // Not into the player: it stops in front of them.
        if (moved && player != nullptr && detail::boxInPlayer(*player, feet, half, height))
            moved = false;
        if (moved)
        {
            // (Hardly getting on -- a step that mostly climbs: held up all the
            // same, and after a second it plans again from where it has got to.)
            const fx went[3] = {feet[0] - actor.pos[0], feet[1] - actor.pos[1], 0};
            for (int i = 0; i < 3; ++i)
                actor.pos[i] = feet[i];
            if (fxLength(went) * 4 >= step)
            {
                actor.stuckMs = 0;
            }
            else if ((actor.stuckMs += ms) > 1000)
            {
                actor.stuckMs = 0;
                actor.goalNode = actor.fromNode = -1;
                actor.thinkMs = 0;
            }
            return kWalkMoved;
        }
        // Getting nowhere: after a second it starts again from where it is.
        actor.stuckMs += ms;
        if (actor.stuckMs > 1000)
        {
            actor.stuckMs = 0;
            actor.goalNode = actor.fromNode = -1;
            actor.thinkMs = 0;
        }
        return kWalkBlocked;
    }
#endif
} // namespace hldata
