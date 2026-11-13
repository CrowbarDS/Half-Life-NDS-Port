#pragma once

// The view's cull planes, and the tests the world's walk makes against them:
// here rather than in the renderer so that the host's tests run them.
//
// A plane's normal is Q12 and a box's corners and a sphere's centre are
// 16-bit, so each product is one of the ARM9's 16-bit multiplies (SMULBB,
// SMLABB: HlDsp.hpp) -- which needs the normal to fit 16 bits. It does, by
// what it is made of (cullNormals, below), and kCullNormalMost says how far:
//
//   - every sine and cosine the camera uses is at most 4096 in size: libnds's
//     sinLerp and cosLerp (a Q15 table that tops at 32768, interpolated
//     between neighbours, >> 3) and fxSinCos (HlFixed.hpp: a table that tops
//     at 16384, >> 2);
//   - so the view's forward is at most 4096 (a product of two of them, >> 12,
//     or one alone); right and up are too, and leaning (roll) turns them
//     through two such products each: at most 8192;
//   - a side plane's normal is forward times a sine plus right (or up) times
//     a cosine, each >> 12: at most 4096 + 8192 = 12288; the far plane's is
//     -forward, at most 4096.
//
// Camera::buildPlanes holds the planes it makes to that as well, at run time.

#include <cstdint>

#include "HlDsp.hpp"

namespace hldata
{
    /// Inside when dot(normal, point) - dist >= 0. `normal` is Q12 and points
    /// are whole Half-Life units, so both sides of the comparison are Q12.
    struct CullPlane
    {
        std::int32_t normal[3];
        std::int32_t dist;
    };

    inline constexpr std::int32_t kCullSineMost = 4096;    ///< any sine or cosine, Q12
    inline constexpr std::int32_t kCullForwardMost = 4096; ///< the view's forward, any part
    inline constexpr std::int32_t kCullSideMost = 8192;    ///< its right and up, leaning
    inline constexpr std::int32_t kCullNormalMost =
        kCullForwardMost * kCullSineMost / 4096 + kCullSideMost * kCullSineMost / 4096;
    static_assert(kCullNormalMost == 12288);
    static_assert(kCullNormalMost <= 32767, "a cull plane's normal must fit SMULBB's 16 bits");

    /// The view's five planes' normals (left, right, bottom, top, far), from
    /// its axes and the sines and cosines of its half-angles across and high
    /// (two degrees looser than the picture: Camera.arm.cpp).
    inline void cullNormals(const std::int32_t *forward, const std::int32_t *right, const std::int32_t *up,
                            std::int32_t sinH, std::int32_t cosH, std::int32_t sinV, std::int32_t cosV,
                            CullPlane *planes)
    {
        const auto mulQ12 = [](std::int32_t a, std::int32_t b) { return (a * b) >> 12; };
        for (int i = 0; i < 3; ++i)
        {
            const std::int32_t fh = mulQ12(forward[i], sinH);
            const std::int32_t rh = mulQ12(right[i], cosH);
            const std::int32_t fv = mulQ12(forward[i], sinV);
            const std::int32_t uv = mulQ12(up[i], cosV);
            planes[0].normal[i] = fh + rh; // left edge: inside is to its right
            planes[1].normal[i] = fh - rh;
            planes[2].normal[i] = fv + uv; // bottom
            planes[3].normal[i] = fv - uv;
            planes[4].normal[i] = -forward[i]; // far
        }
    }

    /// Whether every normal is within kCullNormalMost (and so 16-bit).
    inline bool cullNormalsFit(const CullPlane *planes, int count)
    {
        for (int p = 0; p < count; ++p)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (planes[p].normal[i] > kCullNormalMost || planes[p].normal[i] < -kCullNormalMost)
                    return false;
            }
        }
        return true;
    }

    /// True when the box is entirely behind one of the planes.
    inline bool cullBoxOutside(const CullPlane *planes, int count, const std::int16_t *mins, const std::int16_t *maxs)
    {
        for (int p = 0; p < count; ++p)
        {
            const CullPlane &plane = planes[p];
            // The corner furthest along the normal: if even that is behind
            // the plane, the whole box is. (Three products of at most 12288
            // by 32768: the sum is within 31 bits.)
            const std::int32_t d =
                smlabb(plane.normal[2], plane.normal[2] >= 0 ? maxs[2] : mins[2],
                       smlabb(plane.normal[1], plane.normal[1] >= 0 ? maxs[1] : mins[1],
                              smulbb(plane.normal[0], plane.normal[0] >= 0 ? maxs[0] : mins[0])));
            if (d < plane.dist)
                return true;
        }
        return false;
    }

    /// True when the sphere is entirely behind one of the planes.
    inline bool cullSphereOutside(const CullPlane *planes, int count, const std::int16_t *center, std::int32_t radius)
    {
        const std::int32_t reach = radius << 12;
        for (int p = 0; p < count; ++p)
        {
            const CullPlane &plane = planes[p];
            const std::int32_t d = smlabb(plane.normal[2], center[2],
                                          smlabb(plane.normal[1], center[1], smulbb(plane.normal[0], center[0])));
            if (d - plane.dist < -reach)
                return true;
        }
        return false;
    }
} // namespace hldata
