#pragma once

#include "EngineObject.hpp"

// The eye: where it is, where it looks, and the two things the renderer needs
// from that every frame -- the matrix handed to the geometry engine and the
// planes the BSP walk culls against.
//
// AXES. Positions stay in Half-Life's axes (X forward, Y left, Z up) all the
// way to the hardware; the level's vertices are in them too. The geometry
// engine wants X right, Y up and the view down -Z, and that change of axes is
// folded into the view matrix here rather than baked into any data.
//
// SCALE. A level vertex is a 16-bit quarter-unit (HlFixed.hpp) that the
// hardware reads as 4.12, i.e. as (units / 1024). View space is set so that one
// view unit is 128 Half-Life units, which keeps every product inside the
// hardware's 20.12 and leaves 1/32 unit of precision; the matrix therefore
// carries a scale of 8. The clip planes below are in those view units.

#include <nds.h>

#include "HlFixed.hpp"

namespace hlrt
{
    /// Inside when dot(normal, point) - dist >= 0. `normal` is Q12 and points
    /// are whole Half-Life units, so both sides of the comparison are Q12.
    struct CullPlane
    {
        std::int32_t normal[3];
        std::int32_t dist;
    };

    class Camera
    {
    public:
        static constexpr int kPlaneCount = 5; ///< left, right, bottom, top, far

        hldata::fx pos[3] = {0, 0, 0};
        std::uint16_t yaw = 0;   ///< turns; 0 looks down +X, positive turns left
        std::int16_t pitch = 0;  ///< turns; positive looks up
        /// What is added to those for the picture only, and goes again:
        /// the kick of a weapon or a blow, the lean into a sidestep, the
        /// roll of a fall (shared/HlView.hpp). Turns.
        std::int16_t kickPitch = 0;
        std::int16_t kickYaw = 0;
        std::int16_t roll = 0;   ///< positive leans to the right

        // Derived by update(). Unit vectors in Q12.
        std::int32_t forward[3];
        std::int32_t right[3];
        std::int32_t up[3];
        /// How far the map can be seen at all (LevelHeader::farUnits; 0: not
        /// known): the view is drawn no farther (HlFog.hpp's drawDistanceFor).
        int mapFarUnits = 0;
        m4x3 view;
        CullPlane planes[kPlaneCount];

        /// Vertical field of view handed to NEA, in degrees. 74 vertical is 90
        /// horizontal on a 4:3 screen, which is what Half-Life uses.
        static constexpr int kFovDegrees = 74;
        /// ... and through the crossbow's sight: the game's 20 across, 15 high.
        static constexpr int kZoomFovDegrees = 15;
        /// The crossbow's sight is up: the picture and the cull planes are
        /// the narrow ones (refreshProjection hands NEA kZoomFovDegrees).
        bool zoomed = false;
        /// The field of view (the COMMAND line's fov): across, as the game
        /// gives it (90 by default), and the height that is on a 4:3 screen.
        int fovAcross = 90;
        int fovDegrees = kFovDegrees;
        /// How many of the head-up display's units a pixel is, at the depth
        /// it draws at (HudDraw.arm.cpp), for the picture as it is now: down,
        /// and across (more on a wide screen, where a pixel is shown wider).
        int hudPixel = 24;
        int hudPixelX = 24;
        /// The top screen is shown at 16:10 (a 3DS's wide TwlBg stretches
        /// the 256 by 192 pixels to 384 by 240): the picture keeps its
        /// height and sees wider, as the game does on a wide screen.
        bool wide = false;
        /// The field of view across as the picture shows it: fovAcross on
        /// 4:3, more on 16:10.
        int fovDrawnAcross = 90;
        /// A field of view `across` degrees wide (60 to 110) on 4:3: the
        /// picture and the cull planes.
        void setFov(int across);
        /// The screen shown 16:10 or not: the picture's shape, its width
        /// and the planes.
        void setWide(bool on);
        /// setWide as the screen setting says (its auto: Global::wideTop).
        void applyScreenShape();
        /// The picture handed to NEA again: narrow or not, at the FOV.
        void refreshProjection();
        /// Clip planes in view units (128 Half-Life units each), 20.12.
        ///
        /// What is drawn ends at kFarUnits: that is the cull plane the walk
        /// tests against, and it is as far as any map of the game lets one
        /// see in practice (the longest sight line there could be is about
        /// 12,000 units). The HARDWARE's far plane is twice as far again, and
        /// on purpose: its place in the projection matrix is a number a few
        /// units of the last place wide, good to within tens of units at
        /// best, and it must never be the one that decides. Depth does not
        /// suffer for it: the build uses W-buffering, where depth is distance
        /// and as fine far away as near.
        static constexpr int kNearClip = 128;         ///< 4 units
        static constexpr int kFarClip = 128 << 12;    ///< 16384 units
        static constexpr int kFarUnits = 8192;

        /// Recompute the vectors, the matrix and the cull planes (for the whole
        /// screen) from pos, yaw and pitch.
        void update();
        /// Rebuild only the cull planes (the matrix is as it was).
        void buildPlanes();
        /// Load the view matrix into the geometry engine's modelview.
        void apply() const;

        /// Limit pitch to just short of straight up and down, where yaw stops
        /// meaning anything.
        void clampPitch();
    };

    HL_ENGINE_OBJECT(Camera, camera);
} // namespace hlrt
