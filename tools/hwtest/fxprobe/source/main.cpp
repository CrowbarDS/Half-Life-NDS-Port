// Effects probe: what the hardware does with the tricks the effects plan
// leans on, measured by capturing the picture into bank D and reading it
// back, so the answers are numbers on the bottom screen and not a judgement
// by eye.
//
//   1. Lines: does a triangle with two equal vertices draw as a line? How
//      wide, translucent, fogged, cut at the screen's edge and at the near
//      plane?
//   2. Blended sprites over the 3D layer with BLDALPHA 16/16 (the head-up
//      display's setting): is the sprite ADDED to the 3D picture, clamped at
//      31? Do two sprites over each other blend, or does the front one win?
//   3. A DMA started by each horizontal blank, writing BG0HOFS from a table:
//      does each line of the 3D picture move by its own amount, and what is
//      seen where the picture has moved away from the edge? And again on
//      DMA 1 while the geometry FIFO is kept fed as the game feeds it (DMA
//      0 on a DS, NDMA 0 on a DSi): is any line's move late?
//
// After the tests, A shows each scene live, B the results again.

#include <nds.h>

#include <stdio.h>

namespace
{
    // --- Capture -------------------------------------------------------------

    u16 *const kCapture = VRAM_D;

    /// The next whole frame, into bank D: the 3D layer alone, or the picture
    /// as the 2D engine made it.
    bool capture(bool composited)
    {
        vramSetBankD(VRAM_D_LCD);
        swiWaitForVBlank();
        REG_DISPCAPCNT = DCAP_BANK(DCAP_BANK_VRAM_D) | DCAP_SIZE(DCAP_SIZE_256x192) | DCAP_MODE(DCAP_MODE_A) |
                         DCAP_SRC_A(composited ? DCAP_SRC_A_COMPOSITED : DCAP_SRC_A_3DONLY) | DCAP_ENABLE;
        for (int n = 0; n < 4 && (REG_DISPCAPCNT & DCAP_ENABLE); ++n)
            swiWaitForVBlank();
        return (REG_DISPCAPCNT & DCAP_ENABLE) == 0;
    }

    int red(int x, int y) { return kCapture[y * 256 + x] & 31; }
    int green(int x, int y) { return (kCapture[y * 256 + x] >> 5) & 31; }

    /// The brightest pixel (by red + green) within three rows of `y` in
    /// column `x`: where a line lands is a row either way of where it was
    /// put.
    u16 nearest(int x, int y)
    {
        u16 best = 0;
        int most = -1;
        for (int v = y - 3; v <= y + 3; ++v)
        {
            if (v < 0 || v >= 192)
                continue;
            const u16 c = kCapture[v * 256 + x];
            const int sum = (c & 31) + ((c >> 5) & 31);
            if (sum > most)
            {
                most = sum;
                best = c;
            }
        }
        return best;
    }
    int nearRed(int x, int y) { return nearest(x, y) & 31; }
    int nearGreen(int x, int y) { return (nearest(x, y) >> 5) & 31; }

    // --- 3D set-up -------------------------------------------------------------

    /// One unit is 32 pixels: a pixel is 128 in a v16.
    v16 px(int p) { return static_cast<v16>(p * 128); }

    void flat2D()
    {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrthof32(0, inttof32(8), inttof32(6), 0, -inttof32(1), inttof32(1));
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void line(int x0, int y0, int x1, int y1)
    {
        glBegin(GL_TRIANGLES);
        glVertex3v16(px(x0), px(y0), 0);
        glVertex3v16(px(x1), px(y1), 0);
        glVertex3v16(px(x1), px(y1), 0);
        glEnd();
    }

    void rect(int x0, int y0, int x1, int y1)
    {
        glBegin(GL_QUADS);
        glVertex3v16(px(x0), px(y0), 0);
        glVertex3v16(px(x0), px(y1), 0);
        glVertex3v16(px(x1), px(y1), 0);
        glVertex3v16(px(x1), px(y0), 0);
        glEnd();
    }

    /// Hand the scene over and let it be shown for two frames.
    void show()
    {
        glFlush(0);
        swiWaitForVBlank();
        swiWaitForVBlank();
    }

    void clearTo(int r, int g, int b, int alpha)
    {
        glClearColor(r, g, b, alpha);
        glClearPolyID(63);
        glClearDepth(0x7FFF);
    }

    // --- 1. Lines ----------------------------------------------------------------

    void sceneLines()
    {
        clearTo(0, 0, 0, 31);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glEnable(GL_FOG);
        glFogShift(0);
        glFogOffset(0);
        glFogColor(31, 0, 0, 31);
        for (int i = 0; i < 32; ++i)
            glFogDensity(i, 127);
        flat2D();

        glColor(RGB15(31, 31, 31));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
        line(20, 20, 235, 20);  // across
        line(20, 40, 100, 90);  // slanted
        line(128, 100, 128, 100 + 1); // two pixels long
        glPolyFmt(POLY_ALPHA(15) | POLY_CULL_NONE | POLY_ID(5));
        line(20, 110, 235, 110); // translucent
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0) | POLY_FOG);
        line(20, 130, 235, 130); // fogged
        rect(20, 170, 60, 185);  // fogged, a control: is it the fog or the line?
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
        glPushMatrix();
        glTranslatef32(inttof32(2), 0, 0);
        line(136, 150, 250, 150); // 200..314: over the right edge
        glPopMatrix(1);
        show();
    }

    void sceneNearLine()
    {
        clearTo(0, 0, 0, 31);
        glDisable(GL_FOG);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspectivef32(degreesToAngle(70), floattof32(256.0f / 192.0f), floattof32(0.1f), inttof32(40));
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glColor(RGB15(31, 31, 31));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
        // Down the middle of the floor, from far ahead to behind the eye.
        glBegin(GL_TRIANGLES);
        glVertex3v16(0, floattov16(-0.5f), floattov16(-6.0f));
        glVertex3v16(0, floattov16(-0.5f), floattov16(2.0f));
        glVertex3v16(0, floattov16(-0.5f), floattov16(2.0f));
        glEnd();
        show();
    }

    /// How many rows near `y` are lit in column `x` (a line's width).
    int widthAt(int x, int y)
    {
        int n = 0;
        for (int v = y - 4; v <= y + 4; ++v)
            n += (v >= 0 && v < 192 && (red(x, v) > 8 || green(x, v) > 8)) ? 1 : 0;
        return n;
    }

    void testLines()
    {
        sceneLines();
        if (!capture(false))
        {
            printf("1 lines: capture failed\n");
            return;
        }
        int slanted = 0;
        for (int y = 36; y < 95; ++y)
        {
            for (int x = 16; x < 105; ++x)
                slanted += red(x, y) > 8 ? 1 : 0;
        }
        int edge = 0;
        for (int x = 200; x < 256; ++x)
            edge += nearRed(x, 150) > 8 ? 1 : 0;
        printf("1 LINES (fog red: r31 g0)\n");
        printf(" across: width %d red %d\n", widthAt(128, 20), nearRed(128, 20));
        printf(" slanted: %d px (81: 1 wide)\n", slanted);
        printf(" 2 px long: width %d\n", widthAt(128, 100));
        printf(" alpha 15: red %d (31 solid)\n", nearRed(128, 110));
        printf(" fog: r%d g%d, quad r%d g%d\n", nearRed(128, 130), nearGreen(128, 130), red(40, 178),
               green(40, 178));
        printf(" to the edge: %d of 56 px\n", edge);

        sceneNearLine();
        if (!capture(false))
            return;
        int lit = 0, lowest = -1;
        for (int y = 96; y < 192; ++y)
        {
            bool row = false;
            for (int x = 120; x < 136; ++x)
                row = row || red(x, y) > 8;
            if (row)
            {
                ++lit;
                lowest = y;
            }
        }
        printf(" near plane: %d rows, to y %d\n", lit, lowest);
    }

    // --- 2. Blended sprites over 3D ----------------------------------------------

    u16 *spriteTiles = nullptr;

    void sceneSprites()
    {
        clearTo(0, 0, 0, 0); // see-through: the backdrop shows below
        glDisable(GL_FOG);
        flat2D();
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
        glColor(RGB15(10, 10, 10));
        rect(0, 0, 128, 150);
        glColor(RGB15(25, 25, 25));
        rect(128, 0, 255, 150);
        BG_PALETTE[0] = RGB15(6, 6, 6);

        SPRITE_PALETTE[1] = RGB15(12, 12, 12);
        SPRITE_PALETTE[16 + 1] = RGB15(5, 5, 5);
        // (Sprite 0 is in front of sprite 1.)
        oamSet(&oamMain, 0, 112, 40, 0, 0, SpriteSize_32x32, SpriteColorFormat_16Color, spriteTiles, -1, false,
               false, false, false, false);
        oamSet(&oamMain, 1, 92, 60, 0, 1, SpriteSize_32x32, SpriteColorFormat_16Color, spriteTiles, -1, false,
               false, false, false, false);
        oamSet(&oamMain, 2, 112, 158, 0, 0, SpriteSize_32x32, SpriteColorFormat_16Color, spriteTiles, -1, false,
               false, false, false, false);
        for (int n = 0; n < 3; ++n)
            oamMain.oamMemory[n].blendMode = OBJMODE_BLENDED;
        REG_BLDCNT = 0x3F00; // as the head-up display: every layer below a blended sprite
        REG_BLDALPHA = 16 | 16 << 8;
        swiWaitForVBlank();
        oamUpdate(&oamMain);
        show();
    }

    void hideSprites()
    {
        oamClear(&oamMain, 0, 0);
        swiWaitForVBlank();
        oamUpdate(&oamMain);
    }

    void testSprites()
    {
        sceneSprites();
        const bool ok = capture(true);
        hideSprites();
        if (!ok)
        {
            printf("2 sprites: capture failed\n");
            return;
        }
        printf("2 SPRITE OVER 3D, 16/16\n");
        printf(" 10+12: %d (22 added)\n", red(120, 50));
        printf(" 25+12: %d (31 clamped)\n", red(136, 50));
        printf(" 10 alone: %d\n", red(60, 50));
        printf(" 10+5 back sprite: %d (15)\n", red(96, 80));
        printf(" both: %d (22 front wins)\n", red(116, 66));
        printf(" backdrop 6+12: %d (18)\n", red(120, 170));
    }

    // --- 3. Per-line scroll by DMA ------------------------------------------------

    u16 scroll[193];
    int scrollChannel = 0;

    void armScroll()
    {
        const int c = scrollChannel;
        DMA_CR(c) = 0;
        REG_BG0HOFS = scroll[0];
        DMA_SRC(c) = reinterpret_cast<u32>(&scroll[1]);
        DMA_DEST(c) = reinterpret_cast<u32>(&REG_BG0HOFS);
        DMA_CR(c) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_16_BIT | DMA_SRC_INC | DMA_DST_FIX | 1;
    }

    // The geometry FIFO kept fed, as the game feeds it: MTX_IDENTITY four to
    // a word (no numbers after it, harmless), by DMA 0 in its FIFO mode on a
    // DS (NEA's display lists), NDMA 0 on a DSi (the world's), started again
    // each time it ends.
    u32 nops[2048];
    volatile bool feeding = false;

    void feed()
    {
        if (!feeding)
            return;
        if (isDSiMode())
        {
            REG_NDMA_SRC(0) = reinterpret_cast<u32>(nops);
            REG_NDMA_DEST(0) = reinterpret_cast<u32>(&GFX_FIFO);
            REG_NDMA_LENGTH(0) = 2048;
            REG_NDMA_BLENGTH(0) = 112;
            REG_NDMA_CR(0) = NDMA_ENABLE | NDMA_START_FIFO | NDMA_SRC_INC | NDMA_DST_FIX | NDMA_IRQ_REQ;
        }
        else
        {
            DMA_SRC(0) = reinterpret_cast<u32>(nops);
            DMA_DEST(0) = reinterpret_cast<u32>(&GFX_FIFO);
            DMA_CR(0) = DMA_FIFO | DMA_IRQ_REQ | 2048;
        }
    }

    void startFeeding()
    {
        for (u32 &word : nops)
            word = 0x15151515;
        DC_FlushRange(nops, sizeof(nops));
        const u32 irq = isDSiMode() ? IRQ_NDMA0 : IRQ_DMA0;
        irqSet(irq, feed);
        irqEnable(irq);
        feeding = true;
        feed();
    }

    void stopFeeding()
    {
        feeding = false;
        if (isDSiMode())
            while (REG_NDMA_CR(0) & NDMA_ENABLE)
                ;
        else
            while (DMA_CR(0) & DMA_ENABLE)
                ;
        irqDisable(isDSiMode() ? IRQ_NDMA0 : IRQ_DMA0);
    }

    bool scrolling = false;

    void onVBlank()
    {
        if (scrolling)
            armScroll();
    }

    void sceneScroll(bool right)
    {
        clearTo(0, 0, 0, 31);
        flat2D();
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(0));
        glColor(RGB15(31, 31, 31));
        rect(60, 0, 64, 191);
        glColor(RGB15(0, 31, 0));
        rect(246, 0, 250, 191);
        BG_PALETTE[0] = RGB15(0, 0, 20);
        // Left by y % 16 pixels, or right by it.
        for (int y = 0; y < 193; ++y)
            scroll[y] = static_cast<u16>(right ? (512 - y % 16) & 511 : y % 16);
        DC_FlushRange(scroll, sizeof(scroll));
        scrolling = true;
        show();
    }

    void stopScroll()
    {
        scrolling = false;
        swiWaitForVBlank();
        DMA_CR(scrollChannel) = 0;
        REG_BG0HOFS = 0;
    }

    /// Of the lines of the picture captured: moved by their own amount, by
    /// the line before's (late), or neither (and the first of those).
    void countScroll(int &right, int &late, int &other, int &first)
    {
        right = late = other = 0;
        first = -1;
        for (int y = 0; y < 192; ++y)
        {
            int at = -1;
            for (int x = 0; x < 256 && at < 0; ++x)
                at = (red(x, y) > 20 && green(x, y) > 20) ? x : -1;
            const int moved = 60 - at;
            if (moved == y % 16)
                ++right;
            else if (y > 0 && moved == (y - 1) % 16)
                ++late;
            else
            {
                ++other;
                first = first < 0 ? y : first;
            }
        }
    }

    void testScroll()
    {
        printf("3 LINE SCROLL BY HBLANK DMA\n");
        sceneScroll(false);
        bool ok = capture(true);
        int right = 0, late = 0, other = 0, first = -1;
        if (ok)
        {
            countScroll(right, late, other, first);
            printf(" %d ok %d late %d off (y %d)\n", right, late, other, first);
        }
        // Again on DMA 1, the geometry FIFO fed all the while (from once
        // the scene is sent: a NOP must not come between a command and its
        // numbers).
        stopScroll();
        scrollChannel = 1;
        sceneScroll(false);
        ok = capture(true);
        if (ok)
        {
            countScroll(right, late, other, first);
            printf(" dma1: %d ok %d late %d off\n", right, late, other);
        }
        startFeeding();
        ok = capture(true);
        stopFeeding();
        if (ok)
        {
            countScroll(right, late, other, first);
            printf(" fed: %d ok %d late %d off\n", right, late, other);
        }
        stopScroll();
        scrollChannel = 0;
        sceneScroll(true);
        ok = capture(true);
        if (ok)
        {
            // Moved right by up to 15: what fills the left edge, and does the
            // green bar at 246 come round to the left?
            int wrapped = 0, blue = 0;
            for (int y = 0; y < 192; ++y)
            {
                if (y % 16 < 8)
                    continue;
                for (int x = 0; x < 4; ++x)
                {
                    const u16 c = kCapture[y * 256 + x];
                    wrapped += ((c >> 5) & 31) > 20 ? 1 : 0;
                    blue += ((c >> 10) & 31) > 12 && (c & 31) < 4 ? 1 : 0;
                }
            }
            printf(" right: wrap %d bkdrop %d px\n", wrapped, blue);
        }
        stopScroll();
    }

    void results()
    {
        consoleClear();
        printf("HL effects probe\n");
        testLines();
        testSprites();
        testScroll();
        printf("A: scenes live  B: again\n");
    }
} // namespace

int main()
{
    videoSetMode(MODE_0_3D);
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    consoleDemoInit();
    glInit();
    glViewport(0, 0, 255, 191);
    REG_DISPCNT |= DISPLAY_SPR_ACTIVE | DISPLAY_SPR_1D;
    oamInit(&oamMain, SpriteMapping_1D_32, false);
    spriteTiles = oamAllocateGfx(&oamMain, SpriteSize_32x32, SpriteColorFormat_16Color);
    for (int n = 0; n < 32 * 32 / 4; ++n)
        spriteTiles[n] = 0x1111; // every texel colour 1
    irqSet(IRQ_VBLANK, onVBlank);
    irqEnable(IRQ_VBLANK);

    results();
    int scene = -1;
    for (;;)
    {
        swiWaitForVBlank();
        scanKeys();
        const int down = keysDown();
        if (down & KEY_B)
        {
            hideSprites();
            stopScroll();
            results();
            scene = -1;
        }
        else if (down & KEY_A)
        {
            hideSprites();
            stopScroll();
            scene = (scene + 1) % 5;
            consoleClear();
            static const char *const kNames[] = {"lines", "line through the near plane", "sprites over 3D",
                                                 "scroll left", "scroll right"};
            printf("scene: %s\n\nA: next  B: results\n", kNames[scene]);
            switch (scene)
            {
            case 0:
                sceneLines();
                break;
            case 1:
                sceneNearLine();
                break;
            case 2:
                sceneSprites();
                break;
            case 3:
                sceneScroll(false);
                break;
            default:
                sceneScroll(true);
                break;
            }
        }
    }
}
