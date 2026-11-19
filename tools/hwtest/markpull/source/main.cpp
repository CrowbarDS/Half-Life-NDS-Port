// The mark-pull probe: wall marks drawn nearer the eye along their own lines
// of sight, with the ordinary depth test, against the "same depth" test the
// game uses (EffectDraw::drawDecals, polygon bit 14).
//
// A mark pulled toward the eye along each corner's line of sight covers the
// same pixels on the screen; only its depth changes. With the ordinary test
// it is then drawn wherever its wall is farther than it -- all of it, where
// the "same depth" test eats a mark seen at a low angle in patches (the mark
// probe, tools/hwtest/README.md: measured on a DSi). What it may cost: a mark
// shows over something that stands in front of its wall by less than the
// pull.
//
// Each scene is drawn the game's way (its projection, its view scale of 8,
// quarter-unit vertices, W-buffering, manual sort), captured into VRAM D and
// counted. A mark's coverage is how many of its pixels show, out of those it
// covers drawn alone: 100 is whole, "." too few pixels to say.
//
//   P1 coverage: five marks along a wall 1024 long, the eye 256 from it,
//      seen at g degrees. The marks by the "same depth" test (eq), pulled
//      1/256, 1/128 or 1/64 of their distance, or by as much as the depth
//      changes across two pixels there and 1/128 more (2px). The worst of
//      the five, then their average.
//   P2 bleed: a surface d units in front of the wall over half of a mark:
//      of the mark's pixels on it, how many the mark is drawn on (it should
//      be none: the surface is in front).
//
// The results are shown page by page (A: next) and added to markpull.txt at
// the root of the card.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

#include <fat.h>
#include <nds.h>

namespace
{
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    constexpr i32 kUnit = 4096; ///< one Half-Life unit, Q12
    constexpr int kWidth = 256, kHeight = 192;
    constexpr int kMarkAlpha = 22; ///< EffectDraw's kDecalAlpha
    constexpr int kMarkId = 57;    ///< HlPolygonIds' kIdMarksFirst
    constexpr i32 kGameNear = 128; ///< Camera::kNearClip: 4 units

    struct V3
    {
        i32 x, y, z;
    };
    V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    V3 along(V3 dir, i32 length)
    {
        return {static_cast<i32>((static_cast<i64>(dir.x) * length) >> 12),
                static_cast<i32>((static_cast<i64>(dir.y) * length) >> 12),
                static_cast<i32>((static_cast<i64>(dir.z) * length) >> 12)};
    }

    std::FILE *out = nullptr;
    std::string outPath;
    void say(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        std::vprintf(format, args);
        va_end(args);
        if (out != nullptr)
        {
            va_start(args, format);
            std::vfprintf(out, format, args);
            va_end(args);
        }
    }

    void waitForA()
    {
        if (out != nullptr)
        {
            std::fclose(out);
            out = std::fopen(outPath.c_str(), "a");
        }
        std::printf("A: next\n");
        for (;;)
        {
            swiWaitForVBlank();
            scanKeys();
            if (keysDown() & KEY_A)
                break;
        }
        consoleClear();
    }

    struct Eye
    {
        V3 pos;
        int yaw;
    };

    /// The game's projection and view (as the mark probe has them).
    void setEye(const Eye &eye)
    {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspectivef32(degreesToAngle(74), inttof32(256) / 192, kGameNear, 128 << 12);
        glMatrixMode(GL_MODELVIEW);
        const i32 c = cosLerp(eye.yaw), s = sinLerp(eye.yaw);
        const i32 turn[3][3] = {{c, 0, -s}, {0, kUnit, 0}, {s, 0, c}};
        m4x3 m;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m.m[i * 3 + j] = turn[j][i] * 8;
        const i32 e[3] = {eye.pos.x, eye.pos.y, eye.pos.z};
        for (int j = 0; j < 3; ++j)
        {
            i64 sum = 0;
            for (int i = 0; i < 3; ++i)
                sum += static_cast<i64>(turn[j][i]) * e[i];
            m.m[9 + j] = static_cast<i32>(-((sum + (1 << 18)) >> 19));
        }
        glLoadMatrix4x3(&m);
    }

    /// Vertices drawn this many 4096ths as far from the eye as they are:
    /// nearer, the same on the screen.
    V3 scaleEye = {0, 0, 0};
    i32 scaleBy = kUnit;
    void vertex(V3 p)
    {
        if (scaleBy != kUnit)
        {
            const V3 e = scaleEye;
            p = {e.x + static_cast<i32>((static_cast<i64>(p.x - e.x) * scaleBy) >> 12),
                 e.y + static_cast<i32>((static_cast<i64>(p.y - e.y) * scaleBy) >> 12),
                 e.z + static_cast<i32>((static_cast<i64>(p.z - e.z) * scaleBy) >> 12)};
        }
        // (EffectDraw's corner(): quarter units, floored.)
        glVertex3v16(static_cast<v16>(p.x >> 10), static_cast<v16>(p.y >> 10), static_cast<v16>(p.z >> 10));
    }

    struct Plane
    {
        V3 centre, n, u, v;
    };
    const Plane kWall = {{0, 0, 0}, {0, 0, kUnit}, {kUnit, 0, 0}, {0, kUnit, 0}};
    V3 onPlane(const Plane &p, i32 u, i32 v, i32 off) { return p.centre + along(p.u, u) + along(p.v, v) + along(p.n, off); }
    void quad(const Plane &p, i32 u0, i32 v0, i32 u1, i32 v1, i32 off)
    {
        vertex(onPlane(p, u0, v0, off));
        vertex(onPlane(p, u1, v0, off));
        vertex(onPlane(p, u1, v1, off));
        vertex(onPlane(p, u0, v1, off));
    }

    struct Scene
    {
        int grazing = 60;
        int distance = 256;
        int marks = 1;
        i32 at[5] = {0};
        i32 half = 32 * kUnit;
        /// A surface this far in front of the wall over the right half of
        /// the first mark (0: none).
        i32 before = 0;
    };

    Eye eyeFor(const Scene &s)
    {
        const int yaw = degreesToAngle(90 - s.grazing);
        const V3 back = {sinLerp(yaw), 0, cosLerp(yaw)};
        return {kWall.centre + along(back, s.distance * kUnit), yaw};
    }

    u32 solid() { return POLY_ALPHA(31) | POLY_ID(0) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS; }

    void drawWall(const Scene &s, bool surface)
    {
        glPolyFmt(solid());
        glColor(RGB15(0, 0, 31));
        glBegin(GL_QUADS);
        quad(kWall, -512 * kUnit, -128 * kUnit, 512 * kUnit, 128 * kUnit, 0);
        glEnd();
        if (surface && s.before != 0)
        {
            glColor(RGB15(0, 31, 0));
            glBegin(GL_QUADS);
            quad(kWall, s.at[0], -s.half - 8 * kUnit, s.at[0] + s.half + 8 * kUnit, s.half + 8 * kUnit, s.before);
            glEnd();
        }
    }

    /// Each mark alone, solid, red with green 4 x its number.
    void drawMarksAlone(const Scene &s)
    {
        glPolyFmt(solid());
        glBegin(GL_QUADS);
        for (int k = 0; k < s.marks; ++k)
        {
            glColor(RGB15(31, k * 4, 0));
            quad(kWall, s.at[k] - s.half, -s.half, s.at[k] + s.half, s.half, 0);
        }
        glEnd();
    }

    V3 currentEye = {0, 0, 0};

    /// The marks, red and see-through: by the "same depth" test (pull 0),
    /// or `pull` 4096ths of their distance nearer with the ordinary one
    /// (pull < 0: as much as the depth changes across -pull pixels where
    /// each is, and 1/128 more).
    void drawMarks(const Scene &s, i32 pull)
    {
        glColor(RGB15(31, 0, 0));
        scaleEye = currentEye;
        for (int k = 0; k < s.marks; ++k)
        {
            i32 f = pull;
            if (pull < 0)
            {
                // A pixel is about 1/128 of the distance across; the depth
                // changes across it by that times the tangent of the angle
                // the eye meets the wall at. (Floats: this is a probe.)
                const V3 c = onPlane(kWall, s.at[k], 0, 0);
                const float dx = (currentEye.x - c.x) / 4096.0f, dy = (currentEye.y - c.y) / 4096.0f,
                            dz = (currentEye.z - c.z) / 4096.0f;
                const float length = __builtin_sqrtf(dx * dx + dy * dy + dz * dz);
                const float cosine = dz / length;
                const float tangent = __builtin_sqrtf(1.0f - cosine * cosine) / (cosine > 0.02f ? cosine : 0.02f);
                const float fraction = -pull * tangent / 128.0f + 1.0f / 128.0f;
                f = static_cast<i32>(fraction * 4096.0f);
                f = f < 16 ? 16 : (f > 2048 ? 2048 : f);
            }
            glPolyFmt(POLY_ALPHA(kMarkAlpha) | POLY_ID(kMarkId) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS |
                      (pull == 0 ? POLY_DEPTH_TEST_EQUAL : 0));
            scaleBy = kUnit - f;
            glBegin(GL_QUADS);
            quad(kWall, s.at[k] - s.half, -s.half, s.at[k] + s.half, s.half, 0);
            glEnd();
        }
        scaleBy = kUnit;
    }

    const u16 *grab()
    {
        glFlush(GL_TRANS_MANUALSORT | GL_WBUFFERING);
        swiWaitForVBlank();
        REG_DISPCAPCNT = DCAP_ENABLE | DCAP_MODE(DCAP_MODE_A) | DCAP_SRC_A(DCAP_SRC_A_3DONLY) |
                         DCAP_SIZE(DCAP_SIZE_256x192) | DCAP_BANK(DCAP_BANK_VRAM_D);
        while (REG_DISPCAPCNT & DCAP_ENABLE)
        {
        }
        return VRAM_D;
    }

    int red(u16 c) { return c & 31; }
    int green(u16 c) { return (c >> 5) & 31; }

    std::uint8_t owner[kWidth * kHeight];   ///< which mark a pixel is (0xFF: none)
    std::uint8_t surface[kWidth * kHeight]; ///< the surface in front is there

    struct Count
    {
        int ref[5] = {0};
        int hit[5] = {0};
        int onSurface = 0, markedSurface = 0;
    };

    Count measure(const Scene &s, i32 pull)
    {
        const Eye eye = eyeFor(s);
        currentEye = eye.pos;
        Count count;
        // The marks alone: which pixels are whose.
        setEye(eye);
        drawMarksAlone(s);
        const u16 *pic = grab();
        for (int i = 0; i < kWidth * kHeight; ++i)
        {
            owner[i] = red(pic[i]) >= 24 ? static_cast<std::uint8_t>(green(pic[i]) >> 2) : 0xFF;
            if (owner[i] != 0xFF)
                ++count.ref[owner[i] % 5];
        }
        // The wall and the surface: where the surface is.
        if (s.before != 0)
        {
            setEye(eye);
            drawWall(s, true);
            pic = grab();
            for (int i = 0; i < kWidth * kHeight; ++i)
                surface[i] = green(pic[i]) >= 24 ? 1 : 0;
        }
        // All of it.
        setEye(eye);
        drawWall(s, true);
        drawMarks(s, pull);
        pic = grab();
        for (int i = 0; i < kWidth * kHeight; ++i)
        {
            if (owner[i] == 0xFF)
                continue;
            const bool marked = red(pic[i]) >= 4;
            if (s.before != 0 && surface[i])
            {
                ++count.onSurface;
                count.markedSurface += marked;
            }
            else if (marked)
            {
                ++count.hit[owner[i] % 5];
            }
        }
        return count;
    }

    void cell(int hit, int ref)
    {
        if (ref < 12)
            say("   .");
        else
            say("%4d", hit * 100 / ref);
    }

    constexpr int kWays = 5;
    const i32 kPulls[kWays] = {0, 16, 32, 64, -2};
    const char *const kWayNames[kWays] = {" eq", "256", "128", " 64", "2px"};

    void testCoverage(bool worst)
    {
        say("P1 coverage, %s of 5 marks\n", worst ? "worst" : "average");
        say("wall 1024, eye 256 from it\n");
        say(" g  ");
        for (const char *name : kWayNames)
            say(" %s", name);
        say("\n");
        for (const int grazing : {90, 45, 20, 10, 5})
        {
            say("%2d  ", grazing);
            for (const i32 pull : kPulls)
            {
                Scene s;
                s.grazing = grazing;
                s.marks = 5;
                for (int k = 0; k < 5; ++k)
                    s.at[k] = (128 - 128 * k) * kUnit;
                const Count c = measure(s, pull);
                int hit = 0, ref = 0, low = 100, counted = 0;
                for (int k = 0; k < 5; ++k)
                {
                    if (c.ref[k] < 12)
                        continue;
                    hit += c.hit[k];
                    ref += c.ref[k];
                    low = c.hit[k] * 100 / c.ref[k] < low ? c.hit[k] * 100 / c.ref[k] : low;
                    ++counted;
                }
                if (counted == 0)
                    say("   .");
                else if (worst)
                    say("%4d", low);
                else
                    cell(hit, ref);
            }
            say("\n");
        }
        say("(eq: the game's \"same depth\";\n256..64: pulled that part of\nthe distance; 2px: by the depth\nof two pixels there, +1/128)\n");
        waitForA();
    }

    /// Each of the five marks at the low angles: the nearest first.
    void testPerMark()
    {
        say("P1 each mark (m1 nearest),\nand its pixels drawn alone\n");
        say(" g way   m1  m2  m3  m4  m5\n");
        for (const int grazing : {20, 10})
            for (int way = 0; way < kWays; ++way)
            {
                Scene s;
                s.grazing = grazing;
                s.marks = 5;
                for (int k = 0; k < 5; ++k)
                    s.at[k] = (128 - 128 * k) * kUnit;
                const Count c = measure(s, kPulls[way]);
                say("%2d %s  ", grazing, kWayNames[way]);
                for (int k = 0; k < 5; ++k)
                    cell(c.hit[k], c.ref[k]);
                say("\n");
                if (way == 0)
                {
                    say("   px    ");
                    for (int k = 0; k < 5; ++k)
                        say("%4d", c.ref[k] > 9999 ? 9999 : c.ref[k]);
                    say("\n");
                }
            }
        waitForA();
    }

    void testBleed()
    {
        say("P2 bleed: a surface d units\nin front over half a mark,\neye 256 away: %% of it marked\n");
        say(" g  d  ");
        for (const char *name : kWayNames)
            say(" %s", name);
        say("\n");
        for (const int grazing : {45, 20})
            for (const int d : {1, 2, 4, 8, 16})
            {
                say("%2d %2d  ", grazing, d);
                for (const i32 pull : kPulls)
                {
                    Scene s;
                    s.grazing = grazing;
                    s.before = d * kUnit;
                    const Count c = measure(s, pull);
                    cell(c.markedSurface, c.onSurface);
                }
                say("\n");
            }
        say("(0 is right: the surface is in\nfront of the wall)\n");
        waitForA();
    }
} // namespace

int main()
{
    consoleDemoInit();
    if (isDSiMode())
        setCpuClock(true); // as the game runs

    videoSetMode(MODE_0_3D);
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankD(VRAM_D_LCD);
    glInit();
    glEnable(GL_BLEND);
    glDisable(GL_ANTIALIAS | GL_FOG | GL_OUTLINE | GL_ALPHA_TEST | GL_TEXTURE_2D);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);

    std::printf("HL mark pull probe\n\n");
    if (fatInitDefault())
    {
        outPath = std::string(fatGetDefaultDrive()) + "markpull.txt";
        out = std::fopen(outPath.c_str(), "a");
    }
    say("== %s, results %s\n", isDSiMode() ? "DSi mode" : "DS mode",
        out != nullptr ? "also in markpull.txt" : "on screen only");
    waitForA();

    for (;;)
    {
        testCoverage(true);
        testCoverage(false);
        testPerMark();
        testBleed();
        say("== end\n");
        waitForA();
    }
}
