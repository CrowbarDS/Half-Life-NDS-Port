#pragma once

// The light of a surface its corners cannot carry -- a pool of lamp light in
// the middle of a wall -- laid over it on a DSi, as the lamp's own light is
// (HlLampGrid.hpp).
//
// The console lights a face by its corners alone (LightBake.hpp, LightFit.hpp).
// Where what they blend to is far from the face's lightmap, a DSi draws the
// surface again over itself: cut into squares of the lightmap's own grid (a
// sample every 16 texels along the texture's axes; 32 or 64 where the light
// is smooth enough), each point inside lit as the lightmap has it there, each
// square a quad the hardware blends from corner to corner as the game smooths
// its lightmap between samples. The points on a face's outline are lit as the
// face's own run has them there, so the overlay meets the faces about it
// without a step. Drawn see-through (alpha 30) at the wall's own depth, after
// every solid polygon: what it misses of the wall -- the edge of a pixel, a
// wall seen side-on -- is the wall as a DS draws it, so it may put points
// anywhere, edges and all, and there is no crack to open.

#include <vector>

#include "hlds/BspFile.hpp"
#include "hlds/LightBake.hpp"
#include "hlds/MeshOptimize.hpp"

namespace hlds
{
    struct LightRefine
    {
        /// How far from its lightmap (of 255, the mean over its samples) a
        /// face may be lit by its corners before its surface is given light
        /// over it.
        double limit = 16;
        /// The grid's squares: 64 texels, else 32, else 16 -- the largest
        /// that brings the surface within `stopAt` (of 255, the mean) of its
        /// lightmap, going finer only while it stays within `mostPolygons`.
        double stopAt = 10;
        int mostPolygons = 64;
    };

    /// How far `face`'s corners are from its lightmap: the mean difference
    /// (of 255, in brightness) over the lightmap's samples inside it that are
    /// seen, its model put at `origin`. 0 for a face with none.
    double cornerLightError(const MeshFace &face, const BspFile &bsp, const LightBake &light, const double *origin);

    /// A surface's light laid over it (see above).
    struct LightOverlay
    {
        std::vector<MeshFace> pieces; ///< the squares (and what of them is inside a face), each a polygon of it
        double size = 0;              ///< the squares' side, texels
        double error = 0;             ///< how far it is from the lightmap: of 255, the mean over the samples
        int polygons = 0;             ///< what the pieces come to, cut as the console is sent them
    };

    /// The light laid over the faces `members` of a surface; no pieces if
    /// it brings them no nearer their lightmap.
    LightOverlay lightOverlay(const std::vector<const MeshFace *> &members, const BspFile &bsp,
                              const LightBake &light, const double *origin, const LightRefine &how);
} // namespace hlds
