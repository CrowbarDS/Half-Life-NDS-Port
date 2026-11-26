#pragma once

// What is underfoot.
//
// A step sounds of what it lands on: the game keeps a list of its textures
// by what they are made of (sound/materials.txt), and plays a different
// footstep for metal, dirt, a vent, a grate, tiles and shallow water. The
// converter puts each texture's material in its flags (HlTexture.hpp). What
// is left for the console is to know which texture the player is standing
// on -- and a collision trace does not say: it knows planes, not pictures.
//
// So it is looked for, once a step (not once a frame): among the surfaces of
// the leaf the feet are in, the one that faces up and passes nearest under
// them; or, standing on a brush (a lift, a door laid flat, the tram), among
// that brush's own surfaces. A leaf has under three surfaces on average.
//
// A wall is looked for the same way, where a blow or a round lands: what it
// sounds of (materialHit).
//
// std-only and inline: the host tests run it.

#include "HlHullTrace.hpp"
#include "HlLevelReader.hpp"
#include "HlSound.hpp"

namespace hldata
{
    /// The leaf of the world `pos` is in.
    inline int levelLeafAt(const LevelView &view, const fx *pos)
    {
        std::int32_t index = view.models[0].headNode;
        while (index >= 0)
        {
            const Node &node = view.nodes[index];
            const fx d = planeDistance(view.planes[node.plane], view.planeTypes[node.plane], pos);
            index = node.children[d >= 0 ? 0 : 1];
        }
        return -1 - index;
    }

    namespace detail
    {
        /// Of surfaces `first`..`first + count` (through `marks`, or
        /// straight), the best floor under `feet` -- which are `shift`ed
        /// first, for a brush that has moved from where it was built:
        /// facing up, the feet on its plane within a stride's height, and
        /// its middle the nearest. -1 for none.
        inline int floorAmong(const LevelView &view, const std::uint16_t *marks, std::uint32_t first,
                              std::uint32_t count, const fx *feet, fx &bestDistance)
        {
            int best = -1;
            for (std::uint32_t n = 0; n < count; ++n)
            {
                const std::uint32_t index = marks != nullptr ? marks[first + n] : first + n;
                if (index >= view.surfaceCount)
                    continue;
                const Surface &surface = view.surfaces[index];
                if (surface.texture == kNoTexture || surface.texture >= view.textureCount)
                    continue;
                const std::uint16_t planeIndex = surface.planeRef & kSurfacePlaneMask;
                const Plane &plane = view.planes[planeIndex];
                // (Q30; the face looks along the normal, or against it.)
                const std::int32_t up = (surface.planeRef & kSurfacePlaneBack) ? -plane.normal[2] : plane.normal[2];
                if (up < (7 << 27)) // 0.875 * 0.8: steeper than that is a wall
                    continue;
                fx off = planeDistance(plane, view.planeTypes[planeIndex], feet);
                off = off < 0 ? -off : off;
                if (off > fxFromInt(24))
                    continue;
                const fx d[3] = {fxFromInt(surface.center[0]) - feet[0], fxFromInt(surface.center[1]) - feet[1], 0};
                const fx across = fxLength(d); // (one root, not two)
                const fx distance = across + off * 8;
                // (Not one whose whole extent is somewhere else.)
                if (across > fxFromInt(surface.radius) + fxFromInt(16))
                    continue;
                if (best < 0 || distance < bestDistance)
                {
                    best = static_cast<int>(index);
                    bestDistance = distance;
                }
            }
            return best;
        }

        /// Of surfaces `first`..`first + count` (through `marks`, or
        /// straight), the one `at` is on that faces `normal` (Q12): within
        /// `reach` units of its plane, facing that way, and passing
        /// nearest. -1 for none.
        inline int wallAmong(const LevelView &view, const std::uint16_t *marks, std::uint32_t first,
                             std::uint32_t count, const fx *at, const fx *normal, int reach = 6)
        {
            int best = -1;
            fx bestDistance = 0;
            for (std::uint32_t n = 0; n < count; ++n)
            {
                const std::uint32_t index = marks != nullptr ? marks[first + n] : first + n;
                if (index >= view.surfaceCount)
                    continue;
                const Surface &surface = view.surfaces[index];
                if (surface.texture == kNoTexture || surface.texture >= view.textureCount)
                    continue;
                const std::uint16_t planeIndex = surface.planeRef & kSurfacePlaneMask;
                const Plane &plane = view.planes[planeIndex];
                const std::int64_t side = (surface.planeRef & kSurfacePlaneBack) ? -1 : 1;
                // Facing the same way (Q30 by Q12), and the point on its plane.
                const std::int64_t facing = side * (static_cast<std::int64_t>(plane.normal[0]) * normal[0] +
                                                    static_cast<std::int64_t>(plane.normal[1]) * normal[1] +
                                                    static_cast<std::int64_t>(plane.normal[2]) * normal[2]);
                if (facing < (static_cast<std::int64_t>(7) << 39)) // 0.875
                    continue;
                fx off = planeDistance(plane, view.planeTypes[planeIndex], at);
                off = off < 0 ? -off : off;
                if (off > fxFromInt(reach))
                    continue;
                const fx d[3] = {fxFromInt(surface.center[0]) - at[0], fxFromInt(surface.center[1]) - at[1],
                                 fxFromInt(surface.center[2]) - at[2]};
                const fx distance = fxLength(d) + off;
                if (distance > fxFromInt(surface.radius) + fxFromInt(16) + off)
                    continue;
                if (best < 0 || distance < bestDistance)
                {
                    best = static_cast<int>(index);
                    bestDistance = distance;
                }
            }
            return best;
        }
    } // namespace detail

    /// What the wall at `at` is made of, which faces `normal` (Q12): the
    /// surface of the leaf in front of it that lies in that plane and
    /// passes nearest. Concrete if none is found. (What a blow of the
    /// crowbar, or a round, sounds like.)
    inline Material wallMaterial(const LevelView &view, const fx *at, const fx *normal)
    {
        // (A little out from the wall: the point itself is on the fence
        // between two leaves.)
        const fx out[3] = {at[0] + normal[0] * 2, at[1] + normal[1] * 2, at[2] + normal[2] * 2};
        const int leaf = levelLeafAt(view, out);
        if (leaf < 0 || static_cast<std::uint32_t>(leaf) >= view.leafCount)
            return Material::Concrete;
        const Leaf &l = view.leaves[leaf];
        const int best = detail::wallAmong(view, view.marks, l.firstMark, l.markCount, at, normal);
        return best < 0 ? Material::Concrete : textureMaterial(view.textures[view.surfaces[best].texture].flags);
    }

    /// ... and of a brush entity (into the level's Brushes): `at` and
    /// `normal` in its own space, where its surfaces were built (a door,
    /// a crate: what it is drawn with, as the game looks it up). What it
    /// is met by may stand a little out from what is drawn (a clip brush
    /// in front of a pane): its faces are looked for further from the
    /// point -- there are few of them. Material::Count if none is there:
    /// what was met is in front of something else.
    inline Material brushMaterial(const LevelView &view, int brush, const fx *at, const fx *normal)
    {
        if (brush < 0 || static_cast<std::uint32_t>(brush) >= view.brushCount)
            return Material::Count;
        const Model &model = view.models[view.brushes[brush].model];
        const int best = detail::wallAmong(view, nullptr, model.firstSurface, model.surfaceCount, at, normal, 20);
        return best < 0 ? Material::Count : textureMaterial(view.textures[view.surfaces[best].texture].flags);
    }

    /// What a hit on a material sounds like: one of `count` of its
    /// sounds from `first`, `step` apart, at `loud` (of 127); and how
    /// much of the crowbar's own ring it lets through (`ring`). The game's
    /// (TEXTURETYPE_PlaySound): a breakable's are quieter, which is for
    /// whoever plays them.
    struct MaterialHit
    {
        CommonSound first;
        std::uint8_t count;
        std::uint16_t step; ///< (the grate's two are its first and its fourth)
        std::uint8_t loud;
        std::uint8_t ring;
    };
    inline const MaterialHit &materialHit(Material made)
    {
        static constexpr MaterialHit kHits[static_cast<int>(Material::Count)] = {
            {CommonSound::Step1, 2, 1, 114, 76},     // concrete: 0.9, 0.6
            {CommonSound::Metal1, 2, 1, 114, 38},    // metal: 0.9, 0.3
            {CommonSound::Dirt1, 3, 1, 114, 13},     // dirt: 0.9, 0.1
            {CommonSound::Duct1, 1, 1, 64, 38},      // vent: 0.5, 0.3
            {CommonSound::Grate1, 2, 3, 114, 64},    // grate: 0.9, 0.5
            {CommonSound::Tile1, 4, 1, 102, 25},     // tile: 0.8, 0.2
            {CommonSound::Slosh1, 4, 1, 114, 0},     // slosh: 0.9, 0
            {CommonSound::WoodHit1, 3, 1, 114, 25},  // wood: 0.9, 0.2
            {CommonSound::GlassHit1, 3, 1, 102, 25}, // computer: 0.8, 0.2
            {CommonSound::GlassHit1, 3, 1, 102, 25}, // glass
            {CommonSound::BulletFlesh, 2,
             static_cast<std::uint16_t>(static_cast<int>(CommonSound::BulletFlesh2) - static_cast<int>(CommonSound::BulletFlesh)),
             127, 25}, // flesh: 1.0, 0.2
        };
        return kHits[static_cast<int>(made) < static_cast<int>(Material::Count) ? static_cast<int>(made) : 0];
    }
    /// Which of its sounds, by `roll`.
    inline std::uint16_t materialHitSound(const MaterialHit &hit, std::uint32_t roll)
    {
        // (Of `count` by a multiply, not a divide.)
        const int which = static_cast<int>(((roll & 0xFFFFu) * hit.count) >> 16);
        return static_cast<std::uint16_t>(static_cast<int>(hit.first) + which * hit.step);
    }

    /// What the floor under `feet` is made of. `brush`: the brush the
    /// player stands on (into the level's Brushes), or -1 for the world;
    /// `brushAt`: where that brush is now, if it has moved (else null).
    inline Material floorMaterial(const LevelView &view, const fx *feet, int brush, const fx *brushAt = nullptr)
    {
        fx distance = 0;
        int found = -1;
        if (brush >= 0 && static_cast<std::uint32_t>(brush) < view.brushCount)
        {
            const Brush &b = view.brushes[brush];
            const Model &model = view.models[b.model];
            // (Its surfaces are where it was built: the feet go there.)
            fx local[3] = {feet[0], feet[1], feet[2]};
            for (int a = 0; a < 3 && brushAt != nullptr; ++a)
                local[a] -= brushAt[a];
            found = detail::floorAmong(view, nullptr, model.firstSurface, model.surfaceCount, local, distance);
        }
        if (found < 0)
        {
            const int leaf = levelLeafAt(view, feet);
            if (leaf >= 0 && static_cast<std::uint32_t>(leaf) < view.leafCount)
                found = detail::floorAmong(view, view.marks, view.leaves[leaf].firstMark, view.leaves[leaf].markCount,
                                           feet, distance);
            if (found < 0)
            {
                // (The feet are in the leaf above the floor's often enough:
                // a little lower, then.)
                const fx lower[3] = {feet[0], feet[1], feet[2] - fxFromInt(4)};
                const int under = levelLeafAt(view, lower);
                if (under >= 0 && under != leaf && static_cast<std::uint32_t>(under) < view.leafCount)
                    found = detail::floorAmong(view, view.marks, view.leaves[under].firstMark,
                                               view.leaves[under].markCount, feet, distance);
            }
        }
        return found < 0 ? Material::Concrete : textureMaterial(view.textures[view.surfaces[found].texture].flags);
    }
} // namespace hldata
