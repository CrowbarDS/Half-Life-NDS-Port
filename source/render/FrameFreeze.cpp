#include "global_header.hpp"

#include "render/FrameFreeze.hpp"

#include "Global.hpp"

#include "render/ScreenFade.hpp"

#include "HlPreview.hpp"


namespace hlrt
{
    FrameFreeze frameFreeze;

    // The next whole frame of the screen into Bank D, which
    // must be the hardware's to write just now; every pixel of it marked
    // solid, since a bitmap layer leaves out those that are not.
    static void capture()
    {
        Global::waitVBlank();
        REG_DISPCAPCNT = DCAP_BANK(DCAP_BANK_VRAM_D) | DCAP_SIZE(DCAP_SIZE_256x192) | DCAP_MODE(DCAP_MODE_A) |
                         DCAP_SRC_A(DCAP_SRC_A_COMPOSITED) | DCAP_ENABLE;
        for (int n = 0; n < 4 && (n < 2 || (REG_DISPCAPCNT & DCAP_ENABLE)); ++n)
            Global::waitVBlank();
        std::uint16_t *pixels = VRAM_D;
        for (int i = 0; i < 256 * 192; ++i)
            pixels[i] |= 0x8000;
    }

    bool FrameFreeze::freeze()
    {
#ifdef HLDS_CHILD
        // (Sent the game, the memory of a DS is short of what the picture
        // takes, and a load goes dark instead. Not in DSi mode.)
        if (!global.twlChild)
            return false;
#endif
        if (frozen_)
            return true;
        frozen_ = true;

        // The screen as it is, into bank D, which is kept free for this
        // (a fade's colour has it otherwise: that layer goes).
        screenFade.layerLost();
        vramSetBankD(VRAM_D_LCD);
        capture();

        // Shown as a layer in place of the 3D one, which is switched off
        // behind it: what it would draw is about to lose its textures.
        savedControl_ = REG_DISPCNT;
        savedBg0_ = REG_BG0CNT;
        vramSetBankD(VRAM_D_MAIN_BG_0x06000000);
        REG_BG2CNT = BG_BMP16_256x256 | BG_BMP_BASE(0) | BG_PRIORITY(0);
        REG_BG2PA = 256;
        REG_BG2PB = 0;
        REG_BG2PC = 0;
        REG_BG2PD = 256;
        REG_BG2X = 0;
        REG_BG2Y = 0;
        REG_BG0CNT = (savedBg0_ & ~3) | BG_PRIORITY(1);
        REG_DISPCNT = ((savedControl_ & ~7ul) | 5ul | DISPLAY_BG2_ACTIVE) &
                      ~static_cast<unsigned long>(DISPLAY_BG0_ACTIVE | DISPLAY_SPR_ACTIVE);
        return true;
    }

    bool FrameFreeze::capturePreview(std::uint16_t *out)
    {
#ifdef HLDS_CHILD
        (void)out;
        return false; // (bank D is the ARM7's)
#endif
        if (frozen_)
            return false;
        screenFade.layerLost();
        vramSetBankD(VRAM_D_LCD);
        Global::waitVBlank();
        REG_DISPCAPCNT = DCAP_BANK(DCAP_BANK_VRAM_D) | DCAP_SIZE(DCAP_SIZE_256x192) | DCAP_MODE(DCAP_MODE_A) |
                         DCAP_SRC_A(DCAP_SRC_A_3DONLY) | DCAP_ENABLE;
        for (int n = 0; n < 4 && (n < 2 || (REG_DISPCAPCNT & DCAP_ENABLE)); ++n)
            Global::waitVBlank();
        if (REG_DISPCAPCNT & DCAP_ENABLE)
            return false;
        hldata::previewShrink(VRAM_D, out);
        return true;
    }

    void FrameFreeze::thaw()
    {
        if (!frozen_)
            return;
        frozen_ = false;
        REG_DISPCNT = savedControl_;
        REG_BG0CNT = savedBg0_;
        vramSetBankD(VRAM_D_LCD);
    }
} // namespace hlrt
