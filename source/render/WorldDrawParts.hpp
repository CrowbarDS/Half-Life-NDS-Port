#pragma once

// The small things both halves of the world renderer use: the half that runs
// from fast memory (WorldDraw.itcm.arm.cpp: choosing what is seen and sending
// it) and the half that does not need to (WorldDrawRest.arm.cpp: setting up,
// brushes, the sky). Inline, so each half has its own copy where it runs.

#include <cstdint>

#include "HlPlaneMath.hpp"
#include "data/LevelFile.hpp"
#include "render/Camera.hpp"

namespace hlrt::worldparts
{

        using namespace hldata;

        // Deeper than any tree the map compilers produce; a walk that would
        // overflow it drops the far branch instead of the stack.
        constexpr int kWalkStack = 512;
        inline std::int16_t walkStack[kWalkStack];

        /// True when the box is entirely behind one of the planes
        /// (shared/HlCull.hpp: 16-bit multiplies).
        inline bool boxOutside(const CullPlane *planes, const std::int16_t *mins,
                               const std::int16_t *maxs)
        {
            return cullBoxOutside(planes, Camera::kPlaneCount, mins, maxs);
        }

        inline bool sphereOutside(const CullPlane *planes, const std::int16_t *center,
                                  std::int32_t radius, int planeCount = Camera::kPlaneCount)
        {
            return cullSphereOutside(planes, planeCount, center, radius);
        }

        /// Descend the render tree to the leaf holding `pos`.
        inline int findLeaf(const LevelView &view, const fx *pos)
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
} // namespace hlrt::worldparts
