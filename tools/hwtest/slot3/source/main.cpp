// The bank D probe: what the 3D engine draws from a texture in slot 3 (Bank
// D) when Bank D is not a texture bank at that moment.
//
// The game is to keep every texture that is not 4x4 compressed in Bank D
// during play. A map change captures the last picture into Bank D (display
// capture only writes Banks A to D), so the frame being captured is drawn
// while D is the capture's, not a texture slot. What do its polygons
// textured from D show then: nothing, palette colour 0, the texels still,
// or garbage? melonDS is not the reference for that; the console is.
//
// Four quads, one per column: A3I5 (palette colour 2, red, alpha 7), PAL4
// (colour 1, green), PAL256 (colour 5, yellow), all 8x8 in Bank D, and an
// untextured white one to check the picture. Colour 0 of the palette is
// blue; the clear colour magenta.
//
//   1. D a texture slot (slot 3): what is right. Captured into Bank B.
//   2. D in LCD mode while the frame is drawn. Captured into B.
//   3. D in LCD mode and the capture written into D itself: the game's
//      freeze, exactly.
//   4. D mapped as a main-screen background (the frozen picture shown).
//   5. D switched off.
//
// Each case is drawn 30 frames running; each quad's inner pixels are
// counted as its own colour, clear, colour 0, or other (the most common
// other is named), and how many of the 30 frames counted otherwise than the
// first. The results are shown (A: next) and added to slot3.txt at the root
// of the card.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

#include <fat.h>
#include <nds.h>

namespace
{
    constexpr int kWidth = 256;
    constexpr u16 kClear = RGB15(31, 0, 31);
    constexpr u16 kColour0 = RGB15(0, 0, 31);
    constexpr u16 kWhite = RGB15(31, 31, 31);

    struct Kind
    {
        const char *name;
        u32 format;    ///< GFX_TEX_FORMAT's format field
        u32 offset;    ///< its texels' offset in Bank D
        int index;     ///< the palette colour all its texels are
        u16 colour;    ///< that colour
    };
    const Kind kKinds[3] = {
        {"A3I5", 1, 0x000, 2, RGB15(31, 0, 0)},
        {"PAL4", 2, 0x100, 1, RGB15(0, 31, 0)},
        {"PAL256", 4, 0x200, 5, RGB15(31, 31, 0)},
    };

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

    /// The textures' texels into Bank D (which this leaves in LCD mode).
    void writeTexels()
    {
        vramSetBankD(VRAM_D_LCD);
        auto *d = reinterpret_cast<volatile u16 *>(VRAM_D);
        for (int i = 0; i < 0x400 / 2; ++i)
            d[i] = 0;
        // (VRAM takes no byte writes: two texels at a time.)
        const u16 a3i5 = static_cast<u16>(((7 << 5) | 2) * 0x0101);
        for (int i = 0; i < 64 / 2; ++i)
            d[(kKinds[0].offset >> 1) + i] = a3i5;
        for (int i = 0; i < 16 / 2; ++i)
            d[(kKinds[1].offset >> 1) + i] = 0x5555;
        for (int i = 0; i < 64 / 2; ++i)
            d[(kKinds[2].offset >> 1) + i] = 0x0505;
    }

    void writePalette()
    {
        vramSetBankE(VRAM_E_LCD);
        auto *e = reinterpret_cast<volatile u16 *>(VRAM_E);
        for (int i = 0; i < 256; ++i)
            e[i] = RGB15(8, 8, 8);
        e[0] = kColour0;
        for (const Kind &k : kKinds)
            e[k.index] = k.colour;
        vramSetBankE(VRAM_E_TEX_PALETTE);
    }

    /// Column c's quad, in clip space (the matrices are the identity).
    void quad(int c)
    {
        const v16 x0 = static_cast<v16>(-4096 + c * 2048 + 256), x1 = static_cast<v16>(x0 + 1536);
        glBegin(GL_QUADS);
        glTexCoord2t16(0, 0);
        glVertex3v16(x0, 2048, 0);
        glTexCoord2t16(8 << 4, 0);
        glVertex3v16(x1, 2048, 0);
        glTexCoord2t16(8 << 4, 8 << 4);
        glVertex3v16(x1, -2048, 0);
        glTexCoord2t16(0, 8 << 4);
        glVertex3v16(x0, -2048, 0);
        glEnd();
    }

    void drawScene()
    {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glPolyFmt(POLY_ALPHA(31) | POLY_ID(0) | POLY_CULL_NONE);
        glColor(kWhite);
        for (int c = 0; c < 3; ++c)
        {
            const Kind &k = kKinds[c];
            GFX_TEX_FORMAT = ((0x60000u + k.offset) >> 3) | (k.format << 26);
            GFX_PAL_FORMAT = 0;
            quad(c);
        }
        GFX_TEX_FORMAT = 0;
        quad(3);
    }

    struct Tally
    {
        int own = 0, clear = 0, colour0 = 0, other = 0;
        u16 commonOther = 0;
        int commonCount = 0;
        bool operator!=(const Tally &t) const
        {
            return own != t.own || clear != t.clear || colour0 != t.colour0 || other != t.other;
        }
    };

    /// Column c's inner pixels (clear of the quad's edges).
    Tally count(const u16 *pixels, int c, u16 own)
    {
        Tally t;
        static u16 seen[64];
        static int times[64];
        int kinds = 0;
        for (int y = 60; y < 132; ++y)
        {
            for (int x = 64 * c + 12; x < 64 * c + 52; ++x)
            {
                const u16 p = pixels[y * kWidth + x] & 0x7FFF;
                if (p == own)
                {
                    ++t.own;
                    continue;
                }
                if (p == kClear)
                {
                    ++t.clear;
                    continue;
                }
                if (p == kColour0)
                {
                    ++t.colour0;
                    continue;
                }
                ++t.other;
                int k = 0;
                while (k < kinds && seen[k] != p)
                    ++k;
                if (k == kinds && kinds < 64)
                {
                    seen[kinds] = p;
                    times[kinds++] = 0;
                }
                if (k < kinds && ++times[k] > t.commonCount)
                {
                    t.commonCount = times[k];
                    t.commonOther = p;
                }
            }
        }
        return t;
    }

    enum class Mode
    {
        Slot3,
        LcdCaptureB,
        LcdCaptureD,
        MainBg,
        Off
    };

    /// Draws the scene with Bank D as `mode` has it and captures that frame.
    const u16 *drawAndCapture(Mode mode)
    {
        drawScene();
        glFlush(0);
        swiWaitForVBlank();
        // In the blank, before the new frame's first line is drawn.
        switch (mode)
        {
        case Mode::Slot3:
            vramSetBankD(VRAM_D_TEXTURE_SLOT3);
            break;
        case Mode::LcdCaptureB:
        case Mode::LcdCaptureD:
            vramSetBankD(VRAM_D_LCD);
            break;
        case Mode::MainBg:
            vramSetBankD(VRAM_D_MAIN_BG_0x06000000);
            break;
        case Mode::Off:
            VRAM_D_CR = 0;
            break;
        }
        const bool intoD = mode == Mode::LcdCaptureD;
        REG_DISPCAPCNT = DCAP_ENABLE | DCAP_MODE(DCAP_MODE_A) | DCAP_SRC_A(DCAP_SRC_A_3DONLY) |
                         DCAP_SIZE(DCAP_SIZE_256x192) | DCAP_BANK(intoD ? DCAP_BANK_VRAM_D : DCAP_BANK_VRAM_B);
        while (REG_DISPCAPCNT & DCAP_ENABLE)
        {
        }
        if (!intoD)
            return VRAM_B;
        // (Read back through LCD mode: it is in LCD mode already.)
        return VRAM_D;
    }

    void runCase(const char *name, Mode mode)
    {
        constexpr int kFrames = 30;
        Tally first[4], last[4];
        int differed[4] = {0, 0, 0, 0};
        for (int f = 0; f < kFrames; ++f)
        {
            // Each frame starts from D a texture slot with its texels whole
            // (case 3 writes over them).
            writeTexels();
            vramSetBankD(VRAM_D_TEXTURE_SLOT3);
            const u16 *pixels = drawAndCapture(mode);
            for (int c = 0; c < 4; ++c)
            {
                last[c] = count(pixels, c, c < 3 ? kKinds[c].colour : kWhite);
                if (f == 0)
                    first[c] = last[c];
                else if (last[c] != first[c])
                    ++differed[c];
            }
        }
        say("-- %s\n       own  clr   c0  oth  d\n", name);
        for (int c = 0; c < 4; ++c)
        {
            const Tally &t = last[c];
            say("%-6s%5d%5d%5d%5d%3d\n", c < 3 ? kKinds[c].name : "white", t.own, t.clear, t.colour0, t.other,
                differed[c]);
            if (t.other > 0)
                say("  other mostly %04X x%d\n", t.commonOther, t.commonCount);
        }
    }
} // namespace

int main()
{
    consoleDemoInit(); // (the sub screen's text: Bank C)
    if (isDSiMode())
        setCpuClock(true); // as the game runs

    videoSetMode(MODE_0_3D);
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_LCD);
    glInit();
    glEnable(GL_TEXTURE_2D | GL_BLEND);
    glDisable(GL_ANTIALIAS | GL_FOG | GL_OUTLINE | GL_ALPHA_TEST);
    glClearColor(31, 0, 31, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);
    writePalette();

    std::printf("HL bank D texture probe\n\n");
    if (fatInitDefault())
    {
        outPath = std::string(fatGetDefaultDrive()) + "slot3.txt";
        out = std::fopen(outPath.c_str(), "a");
    }
    say("== %s, results %s\n", isDSiMode() ? "DSi mode" : "DS mode",
        out != nullptr ? "also in slot3.txt" : "on screen only");
    say("Pixels of each quad (2880):\nown: its colour; clr: clear;\nc0: palette colour 0;\noth: other; d: frames of 30\nunlike the first.\n");
    waitForA();

    for (;;)
    {
        runCase("1 D slot 3 (right)", Mode::Slot3);
        runCase("2 D LCD, capture B", Mode::LcdCaptureB);
        waitForA();
        runCase("3 D LCD, capture D", Mode::LcdCaptureD);
        runCase("4 D main BG", Mode::MainBg);
        waitForA();
        runCase("5 D off", Mode::Off);
        say("== end\n");
        waitForA();
    }
}
