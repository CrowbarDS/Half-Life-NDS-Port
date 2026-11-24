#pragma once

// A face whose light its corners cannot carry -- a pool of lamp light in the
// middle of a wall -- given points of light inside it, for a DSi.
//
// The console lights a face by its corners alone (LightBake.hpp, LightFit.hpp).
// Where what the corners blend to is far from the face's lightmap, points are
// put inside the face where it is furthest from its lightmap, one after
// another, each lit as the lightmap has it there; and the face is cut into
// triangles through them from the inside: its edges keep exactly the
// vertices they had, so it meets its neighbours -- which a DS draws as
// before, and so does a DSi where they need nothing -- without a crack.

#include <vector>

#include "hlds/BspFile.hpp"
#include "hlds/LightBake.hpp"
#include "hlds/MeshOptimize.hpp"

namespace hlds
{
    struct LightRefine
    {
        /// How far from its lightmap (of 255, the mean over its samples) a
        /// face may be lit by its corners before it is given points inside.
        double limit = 16;
        /// Where the points go. Greedy: one at a time, each on the lightmap
        /// sample where the face as lit so far is furthest from its
        /// lightmap (the middle of a pool of light, the edge of a shadow),
        /// until nowhere is `stopAt` off (of 255) or the face has `most`.
        /// Else every `step` samples, on the lightmap's grid.
        bool greedy = true;
        double stopAt = 10;
        int most = 12;
        int step = 4;
    };

    /// How far `face`'s corners are from its lightmap: the mean difference
    /// (of 255, in brightness) over the lightmap's samples inside it that are
    /// seen, its model put at `origin`. 0 for a face with none.
    double cornerLightError(const MeshFace &face, const BspFile &bsp, const LightBake &light, const double *origin);

    /// `face` as triangles through points of light inside it (see above), if
    /// it needs them; else empty.
    std::vector<MeshFace> refineLight(const MeshFace &face, const BspFile &bsp, const LightBake &light,
                                      const double *origin, const LightRefine &how);
} // namespace hlds
