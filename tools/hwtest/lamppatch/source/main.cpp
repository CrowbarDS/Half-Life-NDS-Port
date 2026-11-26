// The lamp-patch probe: the lamp's light drawn the way the game adds it, as a
// solid patch of the wall's own texture laid over the wall
// (shared/HlLampPatch.hpp, the game's own code), against the wall it lies on.
//
// The wall is two triangles, 240 x 240, its corners four colours apart (as
// the level's are); the light 8 units off its middle (the game's octagon of
// 72 units). The patch is the wall's two triangles each cut to the octagon's
// pieces, drawn by the "same depth" test solid (eq) or all but solid, alpha
// 30 (eq30: see-through polygons are drawn after all the solid ones, so its
// wall is always there first -- the solid ones are drawn in the hardware's
// own order, by where they are on the screen), or solid and pulled toward
// the eye by as much as the depth changes across two pixels there and 1/128
// more (2px: EffectDraw's marks). Each scene is drawn the game's way (its
// projection, view scale of 8, quarter-unit vertices, W-buffering) and
// captured into VRAM D.
//
//   P1 coverage: of the pixels the patch covers drawn alone, how many show
//      it over the wall (100: whole; the rest are holes, the wall through).
//   P2 unseen: the patch with no light added -- every corner the wall's own
//      colour there: of the patch's pixels, how many differ from the wall
//      alone, and by most how much (a patch that shows where it adds
//      nothing would show its edge as a seam).
//   P3 by eye: a textured wall with the lamp on it, at four angles, by both
//      tests (A: next).
//   P4 how near "same depth" is: a square on a wall, square on, moved off
//      it a quarter unit at a time: the offsets where it all passes, and
//      where any of it does, by W and by Z, at 64 to 3000 units.
//   P5 a panel: the patch over its wall and over a panel 4 units in front
//      of it (as a button's on its wall), drawn as the game does today
//      (alpha 30: "same depth" from 30 degrees, pulled below), solid and
//      pulled one or two pixels' depth by the ordinary test, or at alpha 30
//      by "same depth" pushed 5 units away from the eye.
//   P6 by eye: the patch in highlight shading (a grey for each index of
//      the toon table, added) beside the ordinary one.
//   P7 by eye, on a warm-lit wall of bricks in eight shades: the solid
//      patch as the game draws it; in highlight shading with a table of 0
//      up to a threshold, then rising (both set with the D-pad); and with
//      the texture's palette doubled and the patch's colours halved, over
//      31 kept -- to set beside the 25th anniversary's over-bright.
//
// Results are shown page by page (A: next) and added to lamppatch.txt at the
// root of the card.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

#include <fat.h>
#include <nds.h>

#include "HlLampPatch.hpp"

namespace
{
    using hldata::fx;
    using hldata::LampCorner;
    using hldata::LampLight;
    using i32 = std::int32_t;
    using i64 = std::int64_t;

    constexpr i32 kUnit = 4096;
    constexpr int kWidth = 256, kHeight = 192;
    constexpr i32 kGameNear = 128; ///< Camera::kNearClip: 4 units

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

    /// The eye: `grazing` degrees to the wall (z = 0, facing +z), `away`
    /// units from its middle, level with it.
    i32 eye[3];
    void setEye(int grazing, int away = 256)
    {
        const int yaw = degreesToAngle(90 - grazing);
        eye[0] = static_cast<i32>((static_cast<i64>(sinLerp(yaw)) * away * kUnit) >> 12);
        eye[1] = 0;
        eye[2] = static_cast<i32>((static_cast<i64>(cosLerp(yaw)) * away * kUnit) >> 12);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspectivef32(degreesToAngle(74), inttof32(256) / 192, kGameNear, 128 << 12);
        glMatrixMode(GL_MODELVIEW);
        const i32 c = cosLerp(yaw), s = sinLerp(yaw);
        const i32 turn[3][3] = {{c, 0, -s}, {0, kUnit, 0}, {s, 0, c}};
        m4x3 m;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m.m[i * 3 + j] = turn[j][i] * 8;
        for (int j = 0; j < 3; ++j)
        {
            i64 sum = 0;
            for (int i = 0; i < 3; ++i)
                sum += static_cast<i64>(turn[j][i]) * eye[i];
            m.m[9 + j] = static_cast<i32>(-((sum + (1 << 18)) >> 19));
        }
        glLoadMatrix4x3(&m);
    }

    /// A vertex (Q12 units), pulled `pull` 4096ths of the way to the eye.
    void vertex(const fx *p, i32 pull)
    {
        i32 q[3];
        for (int k = 0; k < 3; ++k)
            q[k] = pull == 0 ? p[k] : eye[k] + static_cast<i32>((static_cast<i64>(p[k] - eye[k]) * (kUnit - pull)) >> 12);
        glVertex3v16(static_cast<v16>(q[0] >> 10), static_cast<v16>(q[1] >> 10), static_cast<v16>(q[2] >> 10));
    }

    /// The wall's two triangles: corners four colours apart, the texture
    /// one texel a unit.
    LampCorner corner(int x, int y, int level)
    {
        LampCorner c{};
        c.pos[0] = x * kUnit;
        c.pos[1] = y * kUnit;
        c.pos[2] = 0;
        c.colour = static_cast<std::uint16_t>(level | level << 5 | level << 10);
        c.texCoord = (static_cast<std::uint32_t>(x * 16) & 0xFFFF) | (static_cast<std::uint32_t>(y * 16) << 16);
        return c;
    }
    // (240 square: the largest face the map compiler leaves, as the game's
    // lightmaps want them no larger.)
    const LampCorner kWallA[3] = {corner(-120, -120, 8), corner(120, -120, 14), corner(120, 120, 26)};
    const LampCorner kWallB[3] = {corner(-120, -120, 8), corner(120, 120, 26), corner(-120, 120, 20)};
    /// The same wall in a warm light (red, green 7/8 of it, blue 5/8), as
    /// a lamp-lit room's: P7.
    LampCorner warm(LampCorner c)
    {
        const int level = c.colour & 31;
        c.colour = static_cast<std::uint16_t>(level | (level * 7 / 8) << 5 | (level * 5 / 8) << 10);
        return c;
    }
    const LampCorner kWarmA[3] = {warm(kWallA[0]), warm(kWallA[1]), warm(kWallA[2])};
    const LampCorner kWarmB[3] = {warm(kWallB[0]), warm(kWallB[1]), warm(kWallB[2])};
    /// The wall drawn and lit: the grey one, or (P7) the warm one.
    const LampCorner *wallA = kWallA, *wallB = kWallB;
    const fx kNormal[3] = {0, 0, kUnit};

    enum class Paint
    {
        Own,   ///< the corners' own colours
        Red,   ///< solid red
        Green, ///< solid green
        Blue,  ///< solid blue
        Half,  ///< half the colour the lamp makes there, over-bright kept (P7)
    };
    std::uint16_t paintOf(Paint paint, std::uint16_t own)
    {
        switch (paint)
        {
        case Paint::Red: return RGB15(31, 0, 0);
        case Paint::Green: return RGB15(0, 31, 0);
        case Paint::Blue: return RGB15(0, 0, 31);
        default: return own;
        }
    }

    std::uint16_t halfOver(const fx *pos);

    void triangle(const LampCorner *c, Paint paint, i32 pull)
    {
        for (int k = 0; k < 3; ++k)
        {
            GFX_COLOR = paint == Paint::Half ? halfOver(c[k].pos) : paintOf(paint, c[k].colour);
            GFX_TEX_COORD = c[k].texCoord;
            vertex(c[k].pos, pull);
        }
    }

    void drawWall(Paint paint)
    {
        glPolyFmt(POLY_ALPHA(31) | POLY_ID(0) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS);
        glBegin(GL_TRIANGLES);
        triangle(wallA, paint, 0);
        triangle(wallB, paint, 0);
        glEnd();
    }

    /// The pull for the patch, as the marks have it: the depth across two
    /// pixels where it is, and 1/128 more. (Floats: this is a probe.)
    i32 twoPixels()
    {
        const float dx = eye[0] / 4096.0f, dz = eye[2] / 4096.0f;
        const float cosine = dz / __builtin_sqrtf(dx * dx + dz * dz);
        const float tangent = __builtin_sqrtf(1.0f - cosine * cosine) / (cosine > 0.02f ? cosine : 0.02f);
        i32 f = static_cast<i32>((2.0f * tangent / 128.0f + 1.0f / 128.0f) * 4096.0f);
        return f < 16 ? 16 : (f > 2048 ? 2048 : f);
    }

    enum Way
    {
        kEqual,   ///< solid, "same depth"
        kEqual30, ///< alpha 30, "same depth"
        kPull,    ///< solid, pulled two pixels' depth
        kWays,
    };
    const char *const kWayNames[kWays] = {"eq", "eq30", "2px"};

    int patchTriangles = 0;
    /// The patch, by the game's code: `strength` of 256 (0: none added).
    void drawPatch(Paint paint, int strength, int way, bool alone)
    {
        const LampLight light{{0, 0, 8 * kUnit}, strength > 0 ? strength : 1};
        const i32 pull = way != kPull || alone ? 0 : twoPixels();
        glPolyFmt(POLY_ALPHA(way == kEqual30 && !alone ? 30 : 31) | POLY_ID(alone ? 0 : 62) | POLY_CULL_NONE |
                  POLY_RENDER_FAR_POLYS | (way != kPull && !alone ? POLY_DEPTH_TEST_EQUAL : 0));
        glBegin(GL_TRIANGLES);
        patchTriangles = 0;
        for (const LampCorner *tri : {wallA, wallB})
        {
            patchTriangles += hldata::lampPatchTriangle(tri, kNormal, light, [&](const LampCorner *c, int n) {
                for (int i = 1; i + 1 < n; ++i)
                {
                    const LampCorner fan[3] = {c[0], c[i], c[i + 1]};
                    LampCorner made[3] = {fan[0], fan[1], fan[2]};
                    // (Strength 0: each corner exactly as the wall is there.)
                    triangle(made, paint, pull);
                }
            });
        }
        glEnd();
    }

    const u16 *grab(bool wBuffer = true)
    {
        glFlush(GL_TRANS_MANUALSORT | (wBuffer ? GL_WBUFFERING : 0));
        swiWaitForVBlank();
        REG_DISPCAPCNT = DCAP_ENABLE | DCAP_MODE(DCAP_MODE_A) | DCAP_SRC_A(DCAP_SRC_A_3DONLY) |
                         DCAP_SIZE(DCAP_SIZE_256x192) | DCAP_BANK(DCAP_BANK_VRAM_D);
        while (REG_DISPCAPCNT & DCAP_ENABLE)
        {
        }
        return VRAM_D;
    }

    std::uint8_t mask[kWidth * kHeight];
    u16 wallAlone[kWidth * kHeight];

    void testCoverage()
    {
        say("P1 coverage: %% of the patch\nshown over its wall\n g     eq  eq30   2px  tris\n");
        for (const int grazing : {90, 60, 45, 30, 20, 10, 5})
        {
            setEye(grazing);
            glDisable(GL_TEXTURE_2D);
            drawPatch(Paint::Red, 256, kEqual, true);
            const u16 *pic = grab();
            int ref = 0;
            for (int i = 0; i < kWidth * kHeight; ++i)
            {
                mask[i] = (pic[i] & 31) >= 24 ? 1 : 0;
                ref += mask[i];
            }
            say("%2d  ", grazing);
            for (int way = 0; way < kWays; ++way)
            {
                setEye(grazing);
                drawWall(Paint::Blue);
                drawPatch(Paint::Green, 256, way, false);
                pic = grab();
                int shown = 0;
                for (int i = 0; i < kWidth * kHeight; ++i)
                    shown += mask[i] && ((pic[i] >> 5) & 31) >= 24 ? 1 : 0;
                if (ref < 12)
                    say("     .");
                else
                    say("  %4d", shown * 100 / ref);
            }
            say("  %4d\n", patchTriangles);
        }
        say("(eq: \"same depth\", solid; eq30:\nthe same at alpha 30; 2px:\nsolid, pulled as the marks are)\n");
        waitForA();
    }

    void testUnseen()
    {
        say("P2 unseen: a patch adding no\nlight over its wall: pixels of\nit that differ, and by most\n g    eq30       2px\n");
        for (const int grazing : {90, 45, 20, 10})
        {
            setEye(grazing);
            glDisable(GL_TEXTURE_2D);
            drawPatch(Paint::Red, 256, kEqual, true);
            const u16 *pic = grab();
            for (int i = 0; i < kWidth * kHeight; ++i)
                mask[i] = (pic[i] & 31) >= 24 ? 1 : 0;
            setEye(grazing);
            drawWall(Paint::Own);
            pic = grab();
            for (int i = 0; i < kWidth * kHeight; ++i)
                wallAlone[i] = pic[i];
            say("%2d  ", grazing);
            for (const int way : {static_cast<int>(kEqual30), static_cast<int>(kPull)})
            {
                setEye(grazing);
                drawWall(Paint::Own);
                drawPatch(Paint::Own, 0, way, false);
                pic = grab();
                int in = 0, differ = 0, most = 0;
                for (int i = 0; i < kWidth * kHeight; ++i)
                {
                    if (!mask[i])
                        continue;
                    ++in;
                    const int d = (pic[i] & 31) - (wallAlone[i] & 31);
                    const int a = d < 0 ? -d : d;
                    differ += a != 0;
                    most = a > most ? a : most;
                }
                say("  %3d%% by %2d", in > 0 ? differ * 100 / in : 0, most);
            }
            say("\n");
        }
        say("(0%%: the patch's edge cannot\nbe seen; by 1: a level's step)\n");
        waitForA();
    }

    void testByEye()
    {
        // A texture of bricks, two greys, 32 x 32.
        static u16 texels[32 * 32];
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
            {
                const bool mortar = y % 8 == 0 || (x + (y / 8 % 2) * 8) % 16 == 0;
                texels[y * 32 + x] = static_cast<u16>((mortar ? RGB15(12, 12, 11) : RGB15(24, 20, 16)) | 0x8000);
            }
        int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(0, texture);
        glTexImage2D(0, 0, GL_RGBA, 32, 32, 0, TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T, texels);
        for (const int grazing : {90, 45, 20, 10})
            for (int way = -1; way < kWays; ++way)
            {
                if (way == kEqual)
                    continue; // (it draws nothing)
                consoleClear();
                std::printf("P3 by eye: %d deg, %s\n", grazing, way < 0 ? "the wall alone" : kWayNames[way]);
                for (int frame = 0; frame < 2; ++frame)
                {
                    setEye(grazing);
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(0, texture);
                    drawWall(Paint::Own);
                    if (way >= 0)
                        drawPatch(Paint::Own, 256, way, false);
                    glFlush(GL_TRANS_MANUALSORT | GL_WBUFFERING);
                    swiWaitForVBlank();
                }
                std::printf("A: next\n");
                for (;;)
                {
                    setEye(grazing);
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(0, texture);
                    drawWall(Paint::Own);
                    if (way >= 0)
                        drawPatch(Paint::Own, 256, way, false);
                    glFlush(GL_TRANS_MANUALSORT | GL_WBUFFERING);
                    swiWaitForVBlank();
                    scanKeys();
                    if (keysDown() & KEY_A)
                        break;
                }
            }
        glDisable(GL_TEXTURE_2D);
        consoleClear();
    }

    // ---- P4: how near "same depth" is, by W and by Z -------------------

    /// A square `half` units from its middle to its sides, facing the eye,
    /// `at` quarter units off the wall toward it (negative: behind it).
    void drawSquare(int at, int half, std::uint32_t format, u16 colour)
    {
        glPolyFmt(format);
        glBegin(GL_QUADS);
        GFX_COLOR = colour;
        for (const int corner : {0, 1, 2, 3})
        {
            const i32 x = (corner == 1 || corner == 2) ? half * kUnit : -half * kUnit;
            const i32 y = corner >= 2 ? half * kUnit : -half * kUnit;
            glVertex3v16(static_cast<v16>(x >> 10), static_cast<v16>(y >> 10), static_cast<v16>(at));
        }
        glEnd();
    }

    void testEqualWindow()
    {
        say("P4 \"same depth\": offsets\n(units) where a square on its\nwall passes, all / any of it\n");
        for (const bool wBuffer : {true, false})
            for (const int away : {64, 256, 1024, 3000})
            {
                // (A square a fifth of the view across, on a wall that
                // fills it.)
                const int half = away / 8 < 4 ? 4 : away / 8;
                setEye(90, away);
                drawSquare(0, half, POLY_ALPHA(31) | POLY_ID(0) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS, RGB15(31, 0, 0));
                const u16 *pic = grab(wBuffer);
                int ref = 0;
                for (int i = 0; i < kWidth * kHeight; ++i)
                {
                    mask[i] = (pic[i] & 31) >= 24 ? 1 : 0;
                    ref += mask[i];
                }
                // Quarter units, -16 units to +16.
                int allLo = 999, allHi = -999, anyLo = 999, anyHi = -999;
                for (int at = -64; at <= 64; ++at)
                {
                    setEye(90, away);
                    drawSquare(0, half * 6, POLY_ALPHA(31) | POLY_ID(0) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS,
                               RGB15(0, 0, 31));
                    drawSquare(at, half, POLY_ALPHA(30) | POLY_ID(62) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS |
                                       POLY_DEPTH_TEST_EQUAL,
                               RGB15(0, 31, 0));
                    pic = grab(wBuffer);
                    int shown = 0;
                    for (int i = 0; i < kWidth * kHeight; ++i)
                        shown += mask[i] && ((pic[i] >> 5) & 31) >= 24 ? 1 : 0;
                    if (shown > 0)
                    {
                        anyLo = at < anyLo ? at : anyLo;
                        anyHi = at > anyHi ? at : anyHi;
                    }
                    if (ref > 0 && shown == ref)
                    {
                        allLo = at < allLo ? at : allLo;
                        allHi = at > allHi ? at : allHi;
                    }
                }
                say("%c %4d: ", wBuffer ? 'W' : 'Z', away);
                if (allLo > allHi)
                    say("all none");
                else
                    say("all %+d..%+d", allLo * 25, allHi * 25);
                if (anyLo > anyHi)
                    say(", any none\n");
                else
                    say(", any %+d..%+d\n", anyLo * 25, anyHi * 25);
            }
        say("(hundredths of a unit; + is\ntoward the eye)\n");
        waitForA();
    }

    // ---- P5: the patch's ways, over its wall and over a panel ----------

    enum Mode
    {
        kToday,    ///< alpha 30: "same depth" at 30 degrees or more, else pulled as the marks
        kSolid1px, ///< solid, "less", pulled one pixel's depth (and a floor)
        kSolid2px, ///< the same, two pixels
        kBack,     ///< alpha 30, "same depth", pushed 5 units away from the eye
        kModes,
    };
    const char *const kModeNames[kModes] = {"today", "s1px", "s2px", "back"};

    /// How far toward the eye (4096ths of the way, negative: away) mode
    /// `mode` puts the patch, and how it is drawn.
    i32 modePull(int mode, int grazing)
    {
        const float dx = eye[0] / 4096.0f, dz = eye[2] / 4096.0f;
        const float away = __builtin_sqrtf(dx * dx + dz * dz);
        const float cosine = dz / away;
        const float tangent = __builtin_sqrtf(1.0f - cosine * cosine) / (cosine > 0.02f ? cosine : 0.02f);
        switch (mode)
        {
        case kToday: return grazing >= 30 ? 0 : twoPixels();
        case kSolid1px: return 8 + static_cast<i32>(tangent / 128.0f * 4096.0f);
        case kSolid2px: return 8 + static_cast<i32>(2.0f * tangent / 128.0f * 4096.0f);
        default: return -static_cast<i32>(5.0f / away * 4096.0f);
        }
    }
    std::uint32_t modeFormat(int mode, int grazing)
    {
        const bool solid = mode == kSolid1px || mode == kSolid2px;
        const bool equal = mode == kBack || (mode == kToday && grazing >= 30);
        return POLY_ALPHA(solid ? 31 : 30) | POLY_ID(solid ? 0 : 62) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS |
               (equal ? POLY_DEPTH_TEST_EQUAL : 0);
    }

    /// The patch in a way of its own (pull and format given).
    void drawPatchAs(Paint paint, int strength, i32 pull, std::uint32_t format)
    {
        const LampLight light{{0, 0, 8 * kUnit}, strength > 0 ? strength : 1};
        glPolyFmt(format);
        glBegin(GL_TRIANGLES);
        for (const LampCorner *tri : {wallA, wallB})
        {
            hldata::lampPatchTriangle(tri, kNormal, light, [&](const LampCorner *c, int n) {
                for (int i = 1; i + 1 < n; ++i)
                {
                    const LampCorner fan[3] = {c[0], c[i], c[i + 1]};
                    triangle(fan, paint, pull);
                }
            });
        }
        glEnd();
    }

    /// A panel 40 x 40 units, 4 in front of the wall, beside the light's
    /// middle (as a button's panel on its wall).
    void drawPanel(u16 colour)
    {
        glPolyFmt(POLY_ALPHA(31) | POLY_ID(1) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS);
        glBegin(GL_QUADS);
        GFX_COLOR = colour;
        for (const int corner : {0, 1, 2, 3})
        {
            const i32 x = (corner == 1 || corner == 2) ? 50 * kUnit : 10 * kUnit;
            const i32 y = corner >= 2 ? 20 * kUnit : -20 * kUnit;
            glVertex3v16(static_cast<v16>(x >> 10), static_cast<v16>(y >> 10), static_cast<v16>(16));
        }
        glEnd();
    }

    std::uint8_t panelMask[kWidth * kHeight];

    void testPanel()
    {
        say("P5 patch over its wall / on a\npanel 4 units in front (%%)\n g ");
        for (int mode = 0; mode < kModes; ++mode)
            say(" %7s", kModeNames[mode]);
        say("\n");
        for (const int grazing : {90, 60, 45, 30, 20, 10})
        {
            setEye(grazing);
            drawPatchAs(Paint::Red, 256, 0, POLY_ALPHA(31) | POLY_ID(0) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS);
            const u16 *pic = grab();
            for (int i = 0; i < kWidth * kHeight; ++i)
                mask[i] = (pic[i] & 31) >= 24 ? 1 : 0;
            setEye(grazing);
            drawWall(Paint::Blue);
            drawPanel(RGB15(31, 0, 0));
            pic = grab();
            int onWall = 0, onPanel = 0;
            for (int i = 0; i < kWidth * kHeight; ++i)
            {
                panelMask[i] = (pic[i] & 31) >= 24 ? 1 : 0;
                onWall += mask[i] && !panelMask[i];
                onPanel += mask[i] && panelMask[i];
            }
            say("%2d ", grazing);
            for (int mode = 0; mode < kModes; ++mode)
            {
                setEye(grazing);
                drawWall(Paint::Blue);
                drawPanel(RGB15(31, 0, 0));
                drawPatchAs(Paint::Green, 256, modePull(mode, grazing), modeFormat(mode, grazing));
                pic = grab();
                int wall = 0, panel = 0;
                for (int i = 0; i < kWidth * kHeight; ++i)
                {
                    if (!mask[i] || ((pic[i] >> 5) & 31) < 24)
                        continue;
                    if (panelMask[i])
                        ++panel;
                    else
                        ++wall;
                }
                say(" %3d/%3d", onWall > 0 ? wall * 100 / onWall : 0, onPanel > 0 ? panel * 100 / onPanel : 0);
            }
            say("\n");
        }
        say("(wall: 100 is whole; panel: 0\nis right)\n");
        waitForA();
    }

    // ---- P6: by eye, highlight shading ---------------------------------

    void testHighlight()
    {
        static u16 texels[32 * 32];
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
            {
                const bool mortar = y % 8 == 0 || (x + (y / 8 % 2) * 8) % 16 == 0;
                texels[y * 32 + x] = static_cast<u16>((mortar ? RGB15(12, 12, 11) : RGB15(24, 20, 16)) | 0x8000);
            }
        int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(0, texture);
        glTexImage2D(0, 0, GL_RGBA, 32, 32, 0, TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T, texels);
        // A grey for each index: highlight adds it, and multiplies by it.
        u16 table[32];
        for (int i = 0; i < 32; ++i)
            table[i] = static_cast<u16>(RGB15(i / 2, i / 2, i / 2));
        glSetToonTable(table);
        const char *const kNames[3] = {"the wall alone", "modulation (today)", "highlight"};
        for (const int grazing : {90, 30})
            for (int way = 0; way < 3; ++way)
            {
                consoleClear();
                std::printf("P6 by eye: %d deg, %s\n", grazing, kNames[way]);
                std::printf("A: next\n");
                for (;;)
                {
                    setEye(grazing);
                    glEnable(GL_TEXTURE_2D);
                    if (way == 2)
                        glEnable(GL_TOON_HIGHLIGHT);
                    else
                        glDisable(GL_TOON_HIGHLIGHT);
                    glBindTexture(0, texture);
                    drawWall(Paint::Own);
                    if (way > 0)
                        drawPatchAs(Paint::Own, 256, 0,
                                    POLY_ALPHA(30) | POLY_ID(62) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS |
                                        POLY_DEPTH_TEST_EQUAL | (way == 2 ? POLY_TOON_HIGHLIGHT : 0));
                    glFlush(GL_TRANS_MANUALSORT | GL_WBUFFERING);
                    swiWaitForVBlank();
                    scanKeys();
                    if (keysDown() & KEY_A)
                        break;
                }
            }
        glDisable(GL_TOON_HIGHLIGHT);
        glDisable(GL_TEXTURE_2D);
        consoleClear();
    }

    // ---- P7: by eye, ways to light beyond the texture ------------------

    /// The colour (0..31 a channel, NOT stopped at 31) the lamp makes at
    /// `pos` on the wall, halved: the wall's light there run from its
    /// corners, the lamp's added (the game's octagon, full strength), made a
    /// colour by the converter's curve (255 * 338/256 * (light/255)^0.8).
    /// (Floats: a probe.)
    std::uint16_t halfOver(const fx *pos)
    {
        const float x = pos[0] / 4096.0f, y = pos[1] / 4096.0f;
        const LampCorner *tri = x >= y ? wallA : wallB;
        float w[3];
        if (x >= y)
        {
            w[2] = (y + 120) / 240, w[1] = (x - y) / 240;
        }
        else
        {
            w[1] = (x + 120) / 240, w[2] = (y - x) / 240;
        }
        w[0] = 1 - w[1] - w[2];
        const float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
        const float over = 72 - ((ax > ay ? ax : ay) + (ax > ay ? ay : ax) / 2);
        const float add = over > 0 ? over * 255.0f / 264.0f : 0; // (8-bit light)
        std::uint16_t out = 0;
        for (int ch = 0; ch < 3; ++ch)
        {
            float level = 0;
            for (int c = 0; c < 3; ++c)
                level += w[c] * ((tri[c].colour >> (5 * ch)) & 31);
            const int low = static_cast<int>(level);
            const int high = low < 31 ? low + 1 : 31;
            const float frac = level - low;
            const float light = (hldata::kLampLightOf[low] * (1 - frac) + hldata::kLampLightOf[high] * frac) / 16.0f + add;
            const float colour = 255.0f * 338.0f / 256.0f * __builtin_powf(light / 255.0f, 0.8f) / 8.0f;
            int half = static_cast<int>(colour / 2 + 0.5f);
            half = half > 31 ? 31 : half;
            out |= static_cast<std::uint16_t>(half << (5 * ch));
        }
        return out;
    }

    void testOverBright()
    {
        // Bricks of eight shades (dark to light) and mortar: PAL256, as the
        // level's textures are paletted. A second texture is the same with
        // every colour doubled (stopped at 31): the patch's.
        static std::uint8_t indices[32 * 32];
        u16 palette[16] = {}, doubled[16] = {};
        palette[0] = RGB15(10, 10, 9);
        for (int shade = 0; shade < 8; ++shade)
        {
            const int k = 6 + shade * 3; // (of 32)
            palette[1 + shade] = static_cast<u16>(RGB15(24 * k / 32 + 4, 20 * k / 32 + 3, 16 * k / 32 + 2));
        }
        for (int i = 0; i < 16; ++i)
        {
            int c[3];
            for (int ch = 0; ch < 3; ++ch)
            {
                c[ch] = ((palette[i] >> (5 * ch)) & 31) * 2;
                c[ch] = c[ch] > 31 ? 31 : c[ch];
            }
            doubled[i] = static_cast<u16>(RGB15(c[0], c[1], c[2]));
        }
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
            {
                const int row = y / 8, column = (x + (row % 2) * 8) / 16;
                const bool mortar = y % 8 == 0 || (x + (row % 2) * 8) % 16 == 0;
                indices[y * 32 + x] = static_cast<std::uint8_t>(mortar ? 0 : 1 + (row * 3 + column * 5) % 8);
            }
        int textures[2] = {0, 0};
        glGenTextures(2, textures);
        for (int t = 0; t < 2; ++t)
        {
            glBindTexture(0, textures[t]);
            glTexImage2D(0, 0, GL_RGB256, 32, 32, 0, TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T, indices);
            glColorTableEXT(0, 0, 16, 0, 0, t == 0 ? palette : doubled);
        }
        wallA = kWarmA;
        wallB = kWarmB;
        // The highlight's table: 0 up to `threshold`, then rising by `slope`
        // quarters a step, grey or warm.
        int threshold = 16, slope = 4;
        bool warmTable = false;
        const char *const kNames[4] = {"the wall alone", "solid patch (the game's)", "highlight patch",
                                       "doubled palette patch"};
        for (const int grazing : {90, 30})
            for (int way = 0; way < 4; ++way)
            {
                bool redraw = true;
                for (;;)
                {
                    if (redraw)
                    {
                        consoleClear();
                        std::printf("P7 by eye: %d deg\n%s\n", grazing, kNames[way]);
                        if (way == 2)
                            std::printf("toon: 0 to %d, then +%d/4 a\nstep, %s\n(Left/Right, Up/Down,\nSelect: grey/warm)\n",
                                        threshold, slope, warmTable ? "warm" : "grey");
                        std::printf("A: next\n");
                        u16 table[32];
                        for (int i = 0; i < 32; ++i)
                        {
                            int v = i <= threshold ? 0 : (i - threshold) * slope / 4;
                            v = v > 31 ? 31 : v;
                            table[i] = warmTable ? static_cast<u16>(RGB15(v, v * 7 / 8, v * 5 / 8))
                                                 : static_cast<u16>(RGB15(v, v, v));
                        }
                        glSetToonTable(table);
                        redraw = false;
                    }
                    setEye(grazing);
                    glEnable(GL_TEXTURE_2D);
                    if (way == 2)
                        glEnable(GL_TOON_HIGHLIGHT);
                    else
                        glDisable(GL_TOON_HIGHLIGHT);
                    glBindTexture(0, textures[0]);
                    drawWall(Paint::Own);
                    if (way > 0)
                    {
                        glBindTexture(0, textures[way == 3 ? 1 : 0]);
                        drawPatchAs(way == 3 ? Paint::Half : Paint::Own, 256, modePull(kSolid1px, grazing),
                                    POLY_ALPHA(31) | POLY_ID(0) | POLY_CULL_NONE | POLY_RENDER_FAR_POLYS |
                                        (way == 2 ? POLY_TOON_HIGHLIGHT : 0));
                    }
                    glFlush(GL_TRANS_MANUALSORT | GL_WBUFFERING);
                    swiWaitForVBlank();
                    scanKeys();
                    const u32 down = keysDown();
                    if (down & KEY_A)
                        break;
                    if (way == 2 && (down & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN | KEY_SELECT)))
                    {
                        threshold += (down & KEY_RIGHT) ? 1 : (down & KEY_LEFT) ? -1 : 0;
                        threshold = threshold < 0 ? 0 : (threshold > 31 ? 31 : threshold);
                        slope += (down & KEY_UP) ? 1 : (down & KEY_DOWN) ? -1 : 0;
                        slope = slope < 1 ? 1 : (slope > 32 ? 32 : slope);
                        warmTable = (down & KEY_SELECT) ? !warmTable : warmTable;
                        redraw = true;
                    }
                }
            }
        wallA = kWallA;
        wallB = kWallB;
        glDeleteTextures(2, textures);
        glDisable(GL_TOON_HIGHLIGHT);
        glDisable(GL_TEXTURE_2D);
        consoleClear();
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
    vramSetBankE(VRAM_E_TEX_PALETTE); // (P7's paletted bricks)
    glInit();
    glEnable(GL_BLEND);
    glDisable(GL_ANTIALIAS | GL_FOG | GL_OUTLINE | GL_ALPHA_TEST | GL_TEXTURE_2D);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);

    std::printf("HL lamp patch probe\n\n");
    if (fatInitDefault())
    {
        outPath = std::string(fatGetDefaultDrive()) + "lamppatch.txt";
        out = std::fopen(outPath.c_str(), "a");
    }
    say("== %s, results %s\n", isDSiMode() ? "DSi mode" : "DS mode",
        out != nullptr ? "also in lamppatch.txt" : "on screen only");
    waitForA();

    for (;;)
    {
        testEqualWindow();
        testPanel();
        testCoverage();
        testUnseen();
        testByEye();
        testHighlight();
        testOverBright();
        say("== end\n");
        waitForA();
    }
}
