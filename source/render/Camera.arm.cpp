#include "render/Camera.hpp"

#include "Global.hpp"
#include "HlFog.hpp"
#include "Settings.hpp"


#include <NEAMain.h>

namespace hlrt
{
    Camera camera;

    namespace
    {
        // The cull planes are a little WIDER than the picture (47 and 39
        // degrees of half-angle against the 45.1 and 37 actually drawn): the
        // hardware clips exactly, so all a loose plane costs is a few polygons,
        // while a tight one drops geometry at the screen's edge.
        // (At the field of view of 90 across; another works its own out:
        // setFov.)
        std::int32_t kSinH = 2996; // sin 47, Q12
        std::int32_t kCosH = 2793;
        std::int32_t kSinV = 2578; // sin 39
        std::int32_t kCosV = 3183;
        // ... and through the crossbow's sight (10 and 7.5 drawn): 12, 9.5
        // (14 across on a wide screen).
        std::int32_t kZoomSinH = 852;
        std::int32_t kZoomCosH = 4006;
        constexpr std::int32_t kZoomSinV = 676;
        constexpr std::int32_t kZoomCosV = 4040;

        // (Angles below in turns, 65536 to the circle, and worked out in
        // fixed point: the double-precision library would cost a guest sent
        // the game 3 KB of its memory.)
        constexpr int kTwoDegrees = 364;
        constexpr int turnsOfHalf(int degrees)
        {
            return degrees * 65536 / 720;
        }
        constexpr int degreesOfHalf(int turns)
        {
            return (turns * 720 + 32768) / 65536;
        }
        /// The half-angle whose tangent is tan(half) * num / den.
        int scaledHalf(int half, int num, int den)
        {
            hldata::fx s = 0, c = 0;
            hldata::fxSinCos(static_cast<std::uint16_t>(half), s, c);
            return hldata::fxAtan2Turns(s * num, c * den);
        }
        /// The half-angle across of a picture `half` high, at the screen's
        /// shape: 4:3, or 16:10.
        int acrossOf(int half, bool wide)
        {
            return wide ? scaledHalf(half, 8, 5) : scaledHalf(half, 4, 3);
        }
        void sinCosOf(int turns, std::int32_t &sine, std::int32_t &cosine)
        {
            hldata::fx s = 0, c = 0;
            hldata::fxSinCos(static_cast<std::uint16_t>(turns), s, c);
            sine = s;
            cosine = c;
        }
        // Pitch stops at 89 degrees.
        constexpr std::int16_t kMaxPitch = 16202;

        std::int32_t mulQ12(std::int32_t a, std::int32_t b)
        {
            return (a * b) >> 12;
        }

        /// dot(v, pos) with v in Q12 and pos in Q12 units: the result is Q12
        /// (v) times whole units, the scale every CullPlane::dist is in.
        std::int32_t dotPos(const std::int32_t *v, const hldata::fx *pos)
        {
            const std::int64_t d = static_cast<std::int64_t>(v[0]) * pos[0] +
                                   static_cast<std::int64_t>(v[1]) * pos[1] +
                                   static_cast<std::int64_t>(v[2]) * pos[2];
            return static_cast<std::int32_t>(d >> 12);
        }
    } // namespace

    void Camera::clampPitch()
    {
        if (pitch > kMaxPitch)
            pitch = kMaxPitch;
        if (pitch < -kMaxPitch)
            pitch = -kMaxPitch;
    }

    void Camera::update()
    {
        // libnds angles are 32768 to the circle; ours are 65536.
        const std::uint16_t turned = static_cast<std::uint16_t>(yaw + kickYaw);
        int tilted = pitch + kickPitch;
        tilted = tilted > kMaxPitch ? kMaxPitch : (tilted < -kMaxPitch ? -kMaxPitch : tilted);
        const std::int32_t sy = sinLerp(static_cast<s16>(turned >> 1));
        const std::int32_t cy = cosLerp(static_cast<s16>(turned >> 1));
        const std::int32_t sp = sinLerp(static_cast<s16>(tilted >> 1));
        const std::int32_t cp = cosLerp(static_cast<s16>(tilted >> 1));

        forward[0] = mulQ12(cp, cy);
        forward[1] = mulQ12(cp, sy);
        forward[2] = sp;
        right[0] = sy;
        right[1] = -cy;
        right[2] = 0;
        up[0] = -mulQ12(sp, cy);
        up[1] = -mulQ12(sp, sy);
        up[2] = cp;
        if (roll != 0)
        {
            // Leaning: right and up turned about the way the view looks.
            const std::int32_t sr = sinLerp(static_cast<s16>(roll >> 1));
            const std::int32_t cr = cosLerp(static_cast<s16>(roll >> 1));
            for (int i = 0; i < 3; ++i)
            {
                const std::int32_t r = right[i], u = up[i];
                right[i] = mulQ12(r, cr) - mulQ12(u, sr);
                up[i] = mulQ12(u, cr) + mulQ12(r, sr);
            }
        }

        // The hardware multiplies a ROW vector by the matrix, so row i is where
        // Half-Life axis i ends up: (right, up, -forward) components, times the
        // scale of 8 (see the header). The last row is the translation.
        for (int i = 0; i < 3; ++i)
        {
            view.m[i * 3 + 0] = right[i] << 3;
            view.m[i * 3 + 1] = up[i] << 3;
            view.m[i * 3 + 2] = -(forward[i] << 3);
        }
        // dotPos gives Q12 * units; view units are 128 units, so >> 7 more.
        view.m[9] = -(dotPos(right, pos) >> 7);
        view.m[10] = -(dotPos(up, pos) >> 7);
        view.m[11] = dotPos(forward, pos) >> 7;

        buildPlanes();
    }

    void Camera::setFov(int across)
    {
        across = across < 60 ? 60 : (across > 110 ? 110 : across);
        fovAcross = across;
        // The height of it on a 4:3 screen (which a wide one keeps), what is
        // seen across at the screen's shape; and the planes, two degrees
        // looser than the picture each way, as above.
        const int halfAcross = turnsOfHalf(across);
        fovDegrees = degreesOfHalf(scaledHalf(halfAcross, 3, 4));
        const int halfHigh = turnsOfHalf(fovDegrees);
        const int drawn = wide ? acrossOf(halfHigh, true) : halfAcross;
        fovDrawnAcross = degreesOfHalf(drawn);
        sinCosOf(drawn + kTwoDegrees, kSinH, kCosH);
        sinCosOf(halfHigh + kTwoDegrees, kSinV, kCosV);
        sinCosOf(acrossOf(turnsOfHalf(kZoomFovDegrees), wide) + kTwoDegrees, kZoomSinH, kZoomCosH);
        refreshProjection();
        buildPlanes();
    }

    void Camera::setWide(bool on)
    {
        wide = on;
        // 1.6 in 20.12: the picture's 256 by 192 pixels shown at 16:10.
        NEA_SetAspect(on ? 6554 : 0);
        setFov(fovAcross);
    }

    void Camera::applyScreenShape()
    {
        const std::uint8_t shape = settings.screenShape;
        setWide(shape == hldata::kScreenWide || (shape == hldata::kScreenAuto && global.wideTop));
    }

    void Camera::refreshProjection()
    {
        const int high = zoomed ? kZoomFovDegrees : fovDegrees;
        NEA_SetFov(high);
        // A pixel at the display's depth (3058 of its units): 96 of them
        // are the depth times the tangent of half the height. Across, a
        // pixel of a picture shown wider is that much wider (6/5 on 16:10).
        // (Only when the picture changes: the crossbow's sight, the setting.)
        std::int32_t s = 0, c = 0;
        sinCosOf(turnsOfHalf(high), s, c);
        hudPixel = (3058 * s + 48 * c) / (96 * c);
        hudPixelX = wide ? (3058 * 6 * s + 240 * c) / (480 * c) : hudPixel;
    }

    void Camera::buildPlanes()
    {
        // Side planes: forward tilted towards the middle by the half-angle.
        const std::int32_t sinH = zoomed ? kZoomSinH : kSinH, cosH = zoomed ? kZoomCosH : kCosH;
        const std::int32_t sinV = zoomed ? kZoomSinV : kSinV, cosV = zoomed ? kZoomCosV : kCosV;
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
        for (int p = 0; p < 4; ++p)
            planes[p].dist = dotPos(planes[p].normal, pos);
        // (How far the world is drawn is the player's to say: no farther
        // than kFarUnits.)
        planes[4].dist = dotPos(planes[4].normal, pos) -
                         (hldata::drawDistanceFor(settings.graphics.drawDistance, mapFarUnits) << 12);
    }

    void Camera::apply() const
    {
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrix4x3(&view);
    }
} // namespace hlrt
