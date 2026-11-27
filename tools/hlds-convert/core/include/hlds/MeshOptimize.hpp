#pragma once

// Fewer vertices for the same picture.
//
// The DS runs out of VERTICES before it runs out of polygons (6144 a pass),
// and a map compiler's output is wasteful in exactly that currency: it cuts
// every large face into pieces no more than 240 units across so that each can
// carry a lightmap, and the DS then pays four vertices for every piece. There
// are no lightmaps here -- light is baked into the vertices -- so much of that
// cutting buys nothing. This file takes back what can be taken back WITHOUT
// CHANGING WHAT IS DRAWN, in three steps:
//
//  1. MERGE two coplanar faces of one texture across a shared edge, when the
//     result is still convex and the light on it is affine -- one plane of
//     colour over the whole polygon, to within one step of the DS's 5-bit
//     colour. An affine colour is drawn identically however the polygon is
//     cut into triangles, so the merged face shades as its parts did.
//     And only when it PAYS: two quads that would become a five-sided face
//     cost seven vertices merged and six as a strip, so they are left alone.
//     That is judged on the END result, not merge by merge (optimizeFaces).
//
//  2. DROP a vertex that lies in the middle of a straight edge, when EVERY
//     face that has a vertex there has it mid-edge too, and its colour is the
//     blend of its two neighbours' (same tolerance). The compiler puts such
//     vertices wherever another face's corner touches an edge; those must stay
//     -- remove one and the two faces no longer share an edge exactly, which
//     on this hardware is a line of background showing through. The rule keeps
//     them, because the touching corner is not mid-edge.
//
//  3. GROUP what is left by plane and texture, and send each group's quads as
//     strips that run from face to face, so that vertices on a shared edge are
//     sent once.
//
// Steps 1 and 2 are "guarded": they can be switched off (--geometry lossless),
// leaving only step 3, which never removes a vertex.

#include <cstdint>
#include <vector>

#include "hlds/FaceBuilder.hpp"
#include "hlds/TextureSize.hpp"

namespace hlds
{
    struct MeshFace
    {
        /// In the BSP's order: clockwise seen from in front.
        std::vector<FacePoint> points;
        /// Faces may be merged or stripped together only when this is equal:
        /// same plane, side, texture and texture mapping.
        int key = 0;
        double normal[3] = {0, 0, 0}; ///< towards the viewer
        /// The BSP faces this one stands for (more than one after a merge).
        std::vector<int> sources;
        /// Vertices this face used to have and no longer does: dropped from an
        /// edge, or swallowed by a merge. They are kept so that every later
        /// step is judged against ALL of the map's light on this face, not
        /// just against what the previous step left. Without them each drop
        /// is one colour step from its neighbours and ten drops are ten steps
        /// from the truth.
        std::vector<FacePoint> ghosts;
    };

    struct OptimizeReport
    {
        int merges = 0;
        int removedVertices = 0;
    };

    /// One step of the DS's 5-bit colour, in 8-bit units. This is the promise:
    /// at every vertex the map had, the colour drawn is within one step of the
    /// colour the map gave it. A dropped mid-edge vertex is tested against it
    /// directly; a merged face's fitted plane of colour is allowed only HALF
    /// of it, because the corners that are kept and the point that is not are
    /// each allowed to miss the plane, on opposite sides.
    /// The tolerance is
    /// applied to the colours as SENT -- already cut to 5 bits -- not to the
    /// light they were cut from: judged before the cut, three roundings can
    /// stack up and the error on screen reaches three steps.
    inline constexpr int kColourTolerance = 8;
    /// A merged face is not allowed to grow past this, in units: texture
    /// coordinates on the DS reach +-2048 texels and no further.
    inline constexpr double kMaxMergedExtent = 1024.0;
    /// Faces further apart than this are not put in one group: a group is
    /// culled as a whole, so a small one culls better.
    inline constexpr double kGroupCell = 256.0;

    /// Steps 1 and 2, in place. Faces that were merged away are removed.
/// Give every side the vertices that lie on it.
    ///
    /// Half-Life's maps are full of T-junctions: a face's side runs straight
    /// past the corner where two of its neighbours meet. The PC's rasteriser
    /// is fine enough that this seldom shows; the DS fills the one long side
    /// and the two short ones differently, and a row of background pixels
    /// shows along the join. So every side of every face is given each vertex
    /// of the model that is on it, with the light and texture coordinates the
    /// side has at that point -- the face looks the same and now ends where
    /// its neighbours do. Returns the number of vertices added.
    ///
    /// Call with all the drawn faces of ONE model, before anything else.
    int weldTJunctions(std::vector<MeshFace> &faces);

        void optimizeFaces(std::vector<MeshFace> &faces, OptimizeReport &report);

    /// A see-through sheet just over a floor (ba_canal1's puddle, two units
    /// over it): `face` cut along the outlines of the `floors` under it --
    /// facing the same way, at most `reach` units below it -- so that each
    /// piece lies over one floor polygon, its corners over that polygon's.
    /// The DS works out a polygon's depth across it only roughly, more
    /// roughly the larger it is: one large sheet over many floor polygons is
    /// out by more than the two units between them, and the floor shows
    /// through it in patches along the floor's edges. A piece over its own
    /// polygon is out by nearly what that polygon is, and stays over it.
    /// Each piece comes with the index into `floors` it lies over (-1: over
    /// none, what is left of `face` past them). Nothing over a floor: `face`
    /// alone, -1.
    std::vector<std::pair<MeshFace, int>> cutOverFloors(const MeshFace &face, const std::vector<MeshFace> &floors,
                                                        double reach);

    /// Step 3's grouping: lists of indices into `faces`, each one surface.
    std::vector<std::vector<int>> groupFaces(const std::vector<MeshFace> &faces);

    /// Step 3's strips: the command run for one group.
    BuiltSurface buildGroupSurface(const std::vector<const MeshFace *> &group, TextureSize cooked,
                                   std::uint32_t sourceWidth, std::uint32_t sourceHeight);
} // namespace hlds
