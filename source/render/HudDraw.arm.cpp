#include "global_header.hpp"

#include "render/HudDraw.hpp"
#include "audio/Sound.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "render/FrameFreeze.hpp"

#include "Global.hpp"
#include "HlFont.hpp"
#include "HlHudLayout.hpp"
#include "HlLamp.hpp"
#include "HlPlayerMove.hpp"
#include "HlUi.hpp"
#include "HlPolygonIds.hpp"
#include "data/TextureCache.hpp"
#include "render/ScreenFade.hpp"
#include "game/Game.hpp"
#include "game/Player.hpp"
#include "render/Camera.hpp"
#include "render/MapCard.hpp"
#include "render/ProfileView.hpp"

namespace hlrt
{
    HudDraw hudDraw;

    namespace
    {
        using namespace hldata;

        /// Where the crosshair is put, in pixels from the middle, for the
        /// guns' aim bent by autoaim (the game's SET_CROSSHAIRANGLE): the
        /// angle as the picture shows it, at its field of view.
        void crosshairShift(int &dx, int &dy)
        {
            const AutoAim &aim = player.state.autoAim.value;
            dx = dy = 0;
            if (aim.yaw == 0 && aim.pitch == 0)
                return;
            fx s, c, hs, hc;
            fxSinCos(static_cast<std::uint16_t>(aim.yaw), s, c);
            fxSinCos(static_cast<std::uint16_t>(camera.fovAcross * 65536 / 720), hs, hc);
            if (c > 0 && hs > 0)
                dx = static_cast<int>(-(static_cast<std::int64_t>(s) * hc * 128) / (static_cast<std::int64_t>(c) * hs));
            const int high = camera.zoomed ? Camera::kZoomFovDegrees : camera.fovDegrees;
            fxSinCos(static_cast<std::uint16_t>(aim.pitch), s, c);
            fxSinCos(static_cast<std::uint16_t>(high * 65536 / 720), hs, hc);
            if (c > 0 && hs > 0)
                dy = static_cast<int>(-(static_cast<std::int64_t>(s) * hc * 96) / (static_cast<std::int64_t>(c) * hs));
        }
        /// ... and its colour: red while it is on a monster, on EASY (the
        /// game's AllowAutoTargetCrosshair: its "autoaim" crosshair).
        int crosshairBank(int plain, int red)
        {
            return player.state.autoAim.value.onTarget && skillLevelOf(game.logic) == kSkillEasy ? red : plain;
        }

        // A point of the screen as a place in front of the eye. The view is
        // 74 degrees high (Camera::kFovDegrees): at a depth of kDepth units
        // (of the sixteenths the matrix below scales by) a pixel is kPixel
        // of them, 96 * kPixel being kDepth * tan(37 degrees). The depth is
        // six of the game's units: past the near plane (four), nearer than
        // a wall can be.
        // (kPixel is the camera's hudPixel: it follows the picture.)
        constexpr int kDepth = 3058;

        /// How far off the flat pictures are drawn: a sixteenth, at the
        /// display's depth; further, for the same pixels, behind PROFILE's
        /// model (ProfileView: which stands nearer than they would be).
        int flatScale = 256;
        void beginFlat()
        {
            MATRIX_CONTROL = GL_MODELVIEW;
            MATRIX_IDENTITY = 0;
            MATRIX_SCALE = flatScale; // a sixteenth: whole numbers below are exact
            MATRIX_SCALE = flatScale;
            MATRIX_SCALE = flatScale;
        }

        void corner(int sx, int sy, int s, int t)
        {
            GFX_TEX_COORD = TEXTURE_PACK(s << 4, t << 4);
            GFX_VERTEX16 = (static_cast<std::uint32_t>((96 - sy) * camera.hudPixel) << 16) |
                           (static_cast<std::uint32_t>((sx - 128) * camera.hudPixel) & 0xFFFF);
            GFX_VERTEX16 = static_cast<std::uint32_t>(-kDepth) & 0xFFFF;
        }

        /// The texture's `from` at (x, y) of the screen, `w` by `h` there.
        void quad(int x, int y, int w, int h, UiRect from)
        {
            GFX_BEGIN = GL_QUADS;
            corner(x, y, from.x, from.y);
            corner(x, y + h, from.x, from.y + from.h);
            corner(x + w, y + h, from.x + from.w, from.y + from.h);
            corner(x + w, y, from.x + from.w, from.y);
        }

        constexpr std::uint16_t kOrange = RGB15(31, 20, 2);
        constexpr std::uint16_t kRed = RGB15(31, 3, 2);
        constexpr std::uint16_t kDim = RGB15(14, 9, 1);

        // ------------------------------------------------- the display in 2D

        /// The display's pictures (ui/hud.128x128.htx), as the card has them:
        /// A3I5 texels, whose top three bits are how bright (the game's
        /// display is added to the picture, its brightness its alpha here).
        std::uint8_t atlas[kHudAtlasWidth * kHudAtlasHeight];

        /// Its sprites' sheet: video bank F, 256 by 128 in sixteen colours,
        /// which follows TopText's (bank G) in the sprites' tiles -- tile 512
        /// on, the same 32 to a row. Laid out as:
        ///   rows  0..31, all across:   the bottom row of the screen (y 160..191)
        ///   rows 32..63, x 192..255:   the row above it on the right (y 144..175):
        ///                              a second kind of ammunition
        ///   rows 32..63, x 0..31:      the crosshair
        ///   rows 32..47, x 32..63:     the lamp
        ///   rows 64..127, x 0..63:     a solid block (colour 1): the red of
        ///                              being hurt, any size, from one place
        hldata::Canvas sheet;
        bool sheetMade = false;
        /// The sheet is drawn here, not in bank F, whose tiles the screen is
        /// being drawn from: present() puts the rows that have changed into
        /// the bank, right after the vertical blank begins -- in step with
        /// the sprites' table (onVblank) and the 3D picture. (Drawn straight
        /// into the bank, the numbers were rubbed out and drawn again while
        /// the screen showed them: a flicker each time health went up.)
        std::uint32_t sheetRam[256 * 128 / 8];
        std::uint16_t sheetDirty = 0; ///< a bit per row of tiles (eight pixels)

        void touchRows(int y, int h)
        {
            if (h <= 0)
                return;
            const int first = y < 0 ? 0 : y >> 3, last = (y + h - 1) >> 3;
            for (int row = first; row <= last && row < 16; ++row)
                sheetDirty |= static_cast<std::uint16_t>(1u << row);
        }
        void fillSheet(int x, int y, int w, int h, int colour)
        {
            sheet.fill(x, y, w, h, colour);
            touchRows(y, h);
        }
        constexpr int kSheetTile = 512;
        constexpr int tileOf(int x, int y)
        {
            return kSheetTile + (y >> 3) * 32 + (x >> 3);
        }

        /// Which of the sprites' palettes says what colours are: a ramp of
        /// seven steps of brightness in each (colours 1..7); the display's
        /// own also has its red in 8..14 (low health, in the same row).
        /// The marks' (one for each side) and the kinds of hurt's change as
        /// they fade: set in the vertical blank.
        constexpr int kBankHud = 1, kBankWhite = 2, kBankDim = 3, kBankRed = 4, kBankFlash = 5, kBankPain = 6,
                      kBankHurts = 10;

        /// The sprites: after TopText's sixteen.
        constexpr int kFirstSprite = 16;
        constexpr int kSprites = 32;
        std::uint16_t shadow[kSprites][3];
        std::uint16_t flashColour = 0, painColour[4][7] = {}, hurtColour[7] = {};
        /// What the vertical blank puts in the sprites' table: shadow, once
        /// it is whole (a frame's sprites half made are never shown).
        std::uint16_t shown[kSprites][3];
        std::uint16_t shownFlash = 0, shownPain[4][7] = {}, shownHurt[7] = {};

        /// `colour` at `level` sevenths, and at `strength` (of 256).
        std::uint16_t shade(std::uint16_t colour, int level, int strength = 256)
        {
            const int r = (colour & 31) * level * strength / (7 * 256), g = ((colour >> 5) & 31) * level * strength / (7 * 256),
                      b = ((colour >> 10) & 31) * level * strength / (7 * 256);
            return static_cast<std::uint16_t>(r | g << 5 | b << 10);
        }
        void ramp(int bank, int first, std::uint16_t colour)
        {
            for (int level = 1; level <= 7; ++level)
                SPRITE_PALETTE[bank * 16 + first + level - 1] = shade(colour, level);
        }
        /// ... into one of the ramps the vertical blank writes.
        void ramp(std::uint16_t (&to)[7], std::uint16_t colour, int strength)
        {
            for (int level = 1; level <= 7; ++level)
                to[level - 1] = shade(colour, level, strength);
        }

        /// A picture of the atlas onto the sheet at (x, y), brightness as its
        /// colour (+ `base`: 7 for the red of the display's own palette).
        /// `skip` columns at its left are left out, and `skipTop` rows at its
        /// top. `add`: added to what is there, as the game adds one picture
        /// over another (a suit's fill over its outline).
        void blit(int x, int y, UiRect from, int base = 0, int skip = 0, int skipTop = 0, bool add = false)
        {
            touchRows(y + skipTop, from.h - skipTop);
            for (int dy = skipTop; dy < from.h; ++dy)
            {
                for (int dx = skip; dx < from.w; ++dx)
                {
                    int level = atlas[(from.y + dy) * kHudAtlasWidth + from.x + dx] >> 5;
                    if (add && level != 0)
                    {
                        const int was = sheet.at(x + dx, y + dy);
                        level += was > base ? was - base : 0;
                        level = level > 7 ? 7 : level;
                    }
                    if (level != 0)
                        sheet.pixel(x + dx, y + dy, level + base);
                }
            }
        }
        /// The pictures of a driven train's speed (kHudTrainFile), as the
        /// display's are: read, A3I5.
        std::uint8_t trainPictures[kHudTrainWidth * kHudTrainHeight];
        /// Picture `n` of them onto the sheet at (x, y), in its 32 square.
        void blitTrain(int x, int y, int n)
        {
            const UiRect from = hudTrain(n);
            touchRows(y, 32);
            for (int dy = 0; dy < 32; ++dy)
            {
                for (int dx = 0; dx < 32; ++dx)
                {
                    const int level = dx < from.w && dy < from.h
                                          ? trainPictures[(from.y + dy) * kHudTrainWidth + from.x + dx] >> 5
                                          : 0;
                    sheet.pixel(x + dx, y + dy, level);
                }
            }
        }
        /// Which of them is on its sprite's cell: -1 none yet.
        int shownTrain = -1;

        /// `value` in the display's digits, its last digit ending at `right`.
        /// Returns where it starts.
        int digits(int right, int y, int value, int base)
        {
            value = value < 0 ? 0 : (value > 999 ? 999 : value);
            int x = right;
            do
            {
                x -= kHudDigitWidth;
                blit(x, y, hudDigit(value % 10), base);
                value /= 10;
            } while (value != 0);
            return x;
        }

        /// Sprite `n` of ours: `shape` (0 square, 1 wide, 2 tall) and
        /// `size` (0..3) as the hardware has them, from the sheet's (sx, sy),
        /// in palette `bank`, added to the picture.
        void sprite(int n, int x, int y, int shape, int size, int sx, int sy, int bank)
        {
            shadow[n][0] = static_cast<std::uint16_t>((y & 0xFF) | 0x0400 | (shape << 14));
            shadow[n][1] = static_cast<std::uint16_t>((x & 0x1FF) | (size << 14));
            // (Priority 1: under a fade's colour, ScreenFade.)
            shadow[n][2] = static_cast<std::uint16_t>(tileOf(sx, sy) | (bank << 12) | ATTR2_PRIORITY(1));
        }
        void hide(int n)
        {
            shadow[n][0] = 0x0200;
        }

        /// The pictures of the kinds of hurt being shown, and how bright
        /// they are this frame (0..256).
        HudHurts hurts;
        int hurtPulse = 0;

        /// What is in hand: a gun with a clip, or a count of one kind.
        bool clipped(int weapon)
        {
            return weapon == kWeaponGlock || weapon == kWeaponShotgun || weapon == kWeaponMp5 || weapon == kWeaponPython ||
                   weapon == kWeaponCrossbow || weapon == kWeaponRpg;
        }
        bool counted(int weapon)
        {
            return weapon == kWeaponGrenade || weapon == kWeaponSatchel || weapon == kWeaponTripmine ||
                   weapon == kWeaponSnark || weapon == kWeaponGauss || weapon == kWeaponEgon || weapon == kWeaponHornetgun;
        }
        /// Its ammunition's picture in the 640 set (the game's weapon_*.txt).
        UiRect ammo640(int weapon)
        {
            switch (weapon)
            {
            case kWeaponPython: return hud640Ammo(kHudAmmo640_357);
            case kWeaponShotgun: return hud640Ammo(kHudAmmo640Buckshot);
            case kWeaponCrossbow: return hud640Ammo(kHudAmmo640Bolt);
            case kWeaponRpg: return hud640Ammo(kHudAmmo640Rocket);
            case kWeaponGauss:
            case kWeaponEgon: return hud640Ammo(kHudAmmo640Uranium);
            case kWeaponHornetgun: return hud640Ammo(kHudAmmo640Hornet);
            case kWeaponGrenade: return hud640Ammo(kHudAmmo640Grenade);
            case kWeaponSatchel: return hud640Ammo(kHudAmmo640Satchel);
            case kWeaponSnark: return hud640Ammo(kHudAmmo640Snark);
            case kWeaponTripmine: return hud640Ammo(kHudAmmo640Tripmine);
            default: return hud640Ammo(kHudAmmo640_9mm);
            }
        }
        /// `value` (0..999) in a field of three from `x640` (at 640 by
        /// 480), its digits each put by `put(x, digit)`.
        template <class Put>
        void field640(int x640, int value, Put &&put)
        {
            value = value < 0 ? 0 : (value > 999 ? 999 : value);
            const int digit[3] = {value / 100, value / 10 % 10, value % 10};
            for (int k = hudFieldFirst(value); k < 3; ++k)
                put(hudDs(x640 + k * kHud640DigitStep), hud640Digit(digit[k]));
        }
        /// The game's colours: its yellowish (255, 160, 0), the red of low
        /// health (250, 0, 0) and of an empty lamp (255, 16, 16).
        constexpr std::uint16_t kYellowish = RGB15(31, 20, 0);
        constexpr std::uint16_t kPainRed = RGB15(31, 0, 0);
        constexpr std::uint16_t kRedish = RGB15(31, 2, 2);
        /// A mark of where a hurt came from shows from 0.4 of its full
        /// strength (256) up, and never dimmer than half (health.cpp).
        constexpr int kPainShows = 103;

        NEA_Material *loadingShown = nullptr;
        // The title screen's pictures (showMenu), and the intro's (playIntro).
        NEA_Material *menuShown = nullptr;
        NEA_Material *logoShown = nullptr;
        NEA_Material *introShown = nullptr;

        /// A picture over the whole screen, in the top 192 rows of its
        /// 256-wide texture.
        void screenPicture(NEA_Material *material)
        {
            NEA_MaterialUse(material);
            NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, static_cast<NEA_OtherFormatEnum>(0));
            GFX_COLOR = 0x7FFF;
            beginFlat();
            GFX_BEGIN = GL_QUADS;
            corner(0, 0, 0, 0);
            corner(0, 192, 0, kLoadingHeight);
            corner(256, 192, kLoadingSize, kLoadingHeight);
            corner(256, 0, kLoadingSize, 0);
        }

        void drawMenuLogo()
        {
            // The animated title over it, across the top as the game has it:
            // see-through by its own alpha, and on the picture's own depth
            // (drawn where that depth is: over it).
            NEA_MaterialUse(logoShown);
            NEA_PolyFormat(31, 1, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, NEA_DEPTH_TEST_EQUAL);
            GFX_COLOR = 0x7FFF;
            beginFlat();
            constexpr int kTop = 10;
            GFX_BEGIN = GL_QUADS;
            corner(0, kTop, 0, 0);
            corner(0, kTop + kLogoHeight, 0, kLogoHeight);
            corner(256, kTop + kLogoHeight, kLogoWidth, kLogoHeight);
            corner(256, kTop, kLogoWidth, 0);
        }

        void drawMenu()
        {
            if (menuShown == nullptr)
                return;
            // (PROFILE's model, 84 units off: the picture and the title 32
            // times as far as they are, about 190.)
            flatScale = profileView.shown() ? 256 * 32 : 256;
            screenPicture(menuShown);
            if (logoShown != nullptr)
                drawMenuLogo();
            // (A multiplayer map's card, in the lobby: on the picture, as
            // the title is.)
            if (mapCard.shown() && !mapCard.hidden())
            {
                beginFlat();
                mapCard.draw(corner);
            }
            flatScale = 256;
            profileView.draw();
        }

        // Where the intro's picture is on the screen, and how big.
        int introX = 0, introY = 0, introW = 0, introH = 0;

        void drawIntro()
        {
            if (introShown == nullptr)
                return;
            NEA_MaterialUse(introShown);
            NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, static_cast<NEA_OtherFormatEnum>(0));
            GFX_COLOR = 0x7FFF;
            beginFlat();
            GFX_BEGIN = GL_QUADS;
            corner(introX, introY, 0, 0);
            corner(introX, introY + introH, 0, introH);
            corner(introX + introW, introY + introH, introW, introH);
            corner(introX + introW, introY, introW, 0);
        }

        void drawLoading()
        {
            if (loadingShown == nullptr)
                return;
            NEA_MaterialUse(loadingShown);
            NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), NEA_CULL_NONE, static_cast<NEA_OtherFormatEnum>(0));
            GFX_COLOR = 0x7FFF;
            beginFlat();
            // (The picture is in the top 192 rows of its texture.)
            GFX_BEGIN = GL_QUADS;
            corner(0, 0, 0, 0);
            corner(0, 192, 0, kLoadingHeight);
            corner(256, 192, kLoadingSize, kLoadingHeight);
            corner(256, 0, kLoadingSize, 0);
        }
    } // namespace

    void HudDraw::load()
    {
        if (lampMaterial_ == nullptr)
        {
            // The lamp's light: a round picture, solid at its middle and
            // thinning evenly to nothing at its rim, in thirty-two steps (and
            // nothing at all along its edges: past them the hardware goes
            // on with the edge). Made here: it is not the game's.
            static std::uint8_t texels[kLampTexels * kLampTexels];
            // (White: what colour the light is drawn in is the wall's own,
            // given with each surface -- WorldDraw's lampPass.)
            static const std::uint16_t colours[8] = {0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF};
            const int mid2 = kLampTexels - 1; // twice the middle
            for (int y = 0; y < kLampTexels; ++y)
            {
                for (int x = 0; x < kLampTexels; ++x)
                {
                    const int dx = 2 * x - mid2, dy = 2 * y - mid2;
                    // (Of 256: how far from the middle to the rim, one texel
                    // short of the edge.)
                    const int far = static_cast<int>(isqrt64(static_cast<std::uint64_t>(dx * dx + dy * dy) << 16)) /
                                    (mid2 - 2);
                    // (The game's light: brightest at its very middle, and
                    // less by the same amount for every step out.)
                    int solid = far >= 256 ? 0 : 31 * (256 - far) / 256;
                    if (x == 0 || y == 0 || x == kLampTexels - 1 || y == kLampTexels - 1)
                        solid = 0;
                    texels[y * kLampTexels + x] = static_cast<std::uint8_t>(solid << 3);
                }
            }
            NEA_Material *material = NEA_MaterialCreate();
            NEA_Palette *palette = NEA_PaletteCreate();
            if (material != nullptr && palette != nullptr &&
                NEA_MaterialTexLoad(material, NEA_A5PAL8, kLampTexels, kLampTexels, NEA_TEXGEN_POSITION, texels) == 1 &&
                NEA_PaletteLoad(palette, colours, 8, NEA_A5PAL8) == 1)
            {
                NEA_MaterialSetPalette(material, palette);
                lampMaterial_ = material;
            }
        }
        if (decalMaterial_ == nullptr && global.hasData)
        {
            NEA_Material *material = nullptr;
            NEA_Palette *palette = nullptr;
            if (TextureCache::loadFile(global.commonPath(kDecalAtlasFile).c_str(), false, kDecalAtlasWidth, kDecalAtlasSize,
                                       1, material, palette))
            {
                decalMaterial_ = material;
                decalPalette_ = palette;
            }
        }
        // The display's pictures: read, not uploaded (its sprites are made
        // from them: draw()). The 640 set's if the card has it, else the
        // 320 set's.
        if (!atlas_ && global.hasData)
        {
            // (Only for now: not kept beside a map.)
            constexpr std::size_t kBody = kHudAtlasWidth * kHudAtlasHeight + 64;
            auto *body = static_cast<std::uint8_t *>(std::malloc(kBody));
            hldata::TextureHeader header;
            for (const char *file : body != nullptr ? std::initializer_list<const char *>{kHud640File, kHudAtlasFile}
                                                    : std::initializer_list<const char *>{})
            {
                if (TextureCache::readFile(global.commonPath(file).c_str(), kHudAtlasWidth, kHudAtlasHeight, 1, header,
                                           body, kBody) &&
                    header.texelBytes == sizeof(atlas))
                {
                    std::memcpy(atlas, body, sizeof(atlas));
                    atlas_ = true;
                    atlas640_ = file == kHud640File;
                    sheetMade = false;
                    break;
                }
            }
            std::free(body);
        }
        // The train's pictures, the same way (a card cooked before them has
        // none, and shows none).
        if (!train_ && global.hasData)
        {
            constexpr std::size_t kBody = kHudTrainWidth * kHudTrainHeight + 64;
            auto *body = static_cast<std::uint8_t *>(std::malloc(kBody));
            hldata::TextureHeader header;
            if (TextureCache::readFile(global.commonPath(kHudTrainFile).c_str(), kHudTrainWidth, kHudTrainHeight, 1,
                                       header, body, kBody) &&
                header.texelBytes == sizeof(trainPictures))
            {
                std::memcpy(trainPictures, body, sizeof(trainPictures));
                train_ = true;
                shownTrain = -1;
            }
            std::free(body);
        }
    }

    void HudDraw::forget()
    {
        // (NEA has let go of them itself, with everything else.)
        lampMaterial_ = nullptr;
        decalMaterial_ = decalPalette_ = nullptr;
        loadingMaterial_ = loadingPalette_ = nullptr;
        loadingShown = nullptr;
        hurts = HudHurts();
    }

    void HudDraw::setStatus(bool suit, int health, int armor, int weapon, int clip, int spare, int extra)
    {
        suit_ = suit;
        health_ = health;
        armor_ = armor;
        weapon_ = weapon;
        clip_ = clip;
        spare_ = spare;
        extra_ = extra;
    }

    void HudDraw::draw()
    {
        // The kinds of hurt: those new this frame, those whose time is up.
        const auto now = static_cast<std::uint32_t>(static_cast<std::uint64_t>(Global::vblanks()) * 1000u / 60u);
        hurts.add(static_cast<std::uint8_t>(hurtKinds_), now);
        hurtKinds_ = 0;
        hurtPulse = HudHurts::pulse(now);
        hurts.tick(now, hurtPulse);
        if (game.logic.camera >= 0)
        {
            // (Nothing of it over a view from a trigger_camera.)
            for (int n = 0; n < kSprites; ++n)
                hide(n);
            flashColour = 0;
            std::memset(painColour, 0, sizeof(painColour));
            std::memset(hurtColour, 0, sizeof(hurtColour));
        }
        else if (atlas640_)
        {
            compose640();
        }
        else
        {
            compose();
        }
        const int state = enterCriticalSection();
        std::memcpy(shown, shadow, sizeof(shown));
        shownFlash = flashColour;
        std::memcpy(shownPain, painColour, sizeof(shownPain));
        std::memcpy(shownHurt, hurtColour, sizeof(shownHurt));
        leaveCriticalSection(state);
    }

    void HudDraw::clear()
    {
        for (int n = 0; n < kSprites; ++n)
            hide(n);
        flashColour = 0;
        std::memset(painColour, 0, sizeof(painColour));
        std::memset(hurtColour, 0, sizeof(hurtColour));
        const int state = enterCriticalSection();
        std::memcpy(shown, shadow, sizeof(shown));
        shownFlash = flashColour;
        std::memcpy(shownPain, painColour, sizeof(shownPain));
        std::memcpy(shownHurt, hurtColour, sizeof(shownHurt));
        leaveCriticalSection(state);
    }

    void HudDraw::compose()
    {
        static int shownHealth = -1, shownArmor = -1, shownWeapon = -1, shownClip = -1, shownSpare = -1,
                   shownExtra = -2, shownLamp = -1;
        for (int n = 0; n < kSprites; ++n)
            hide(n);
        flashColour = 0;
        std::memset(painColour, 0, sizeof(painColour));
        if (screenFade.curtained() || frameFreeze.frozen() || !atlas_)
            return;

        // The sprites on, in the tiles' 2D layout (TopText's), added to the
        // picture; bank F theirs. (Whoever sets the video mode up -- the 3D
        // engine brought up again -- takes all that away: done again.)
        if (VRAM_F_CR != (VRAM_ENABLE | VRAM_F_MAIN_SPRITE_0x06404000))
        {
            vramSetBankF(VRAM_F_MAIN_SPRITE_0x06404000);
            sheetMade = false;
        }
        REG_DISPCNT = (REG_DISPCNT & ~(DISPLAY_SPR_1D | DISPLAY_SPR_1D_BMP)) | DISPLAY_SPR_ACTIVE;
        REG_BLDCNT = 0x3F00;
        REG_BLDALPHA = 16 | (16 << 8);
        if (!sheetMade)
        {
            sheet.tiles = sheetRam;
            sheet.width = 256;
            sheet.height = 128;
            fillSheet(0, 0, 256, 128, 0);
            fillSheet(0, 64, 64, 64, 1);
            blit((32 - kHudCrosshair.w) / 2, 32 + (32 - kHudCrosshair.h) / 2, kHudCrosshair);
            ramp(kBankHud, 1, kOrange);
            ramp(kBankHud, 8, kRed);
            ramp(kBankWhite, 1, 0x7FFF);
            ramp(kBankDim, 1, kDim);
            ramp(kBankRed, 1, kRed);
            shownHealth = shownArmor = shownWeapon = shownClip = shownSpare = shownLamp = -1;
            shownExtra = -2;
            shownTrain = -1;
            sheetMade = true;
        }

        // Being hurt, which wants no suit: the screen reddened all over (a
        // sheet of twelve sprites, as bright as the hurt) and a red bar on
        // each side of its middle the hurt came from.
        if (flash_ > 0)
        {
            const int red = flash_ * 14 / 256;
            flashColour = static_cast<std::uint16_t>(red > 31 ? 31 : red);
            for (int k = 0; k < 12; ++k)
                sprite(8 + k, (k % 4) * 64, (k / 4) * 64, 0, 3, 0, 64, kBankFlash);
        }
        static constexpr std::int16_t kMarks[4][4] = {{104, 52, 48, 5}, {166, 72, 5, 48}, {104, 135, 48, 5}, {85, 72, 5, 48}};
        for (int n = 0; n < 4; ++n)
        {
            if (pain_[n] <= 0)
                continue;
            const int level = pain_[n] * 24 / 256;
            painColour[n][0] = static_cast<std::uint16_t>((31 * level / 24) | ((3 * level / 24) << 5) | ((2 * level / 24) << 10));
            const int x = kMarks[n][0], y = kMarks[n][1];
            if (kMarks[n][2] > kMarks[n][3])
            {
                sprite(20 + n * 2, x, y - 1, 1, 1, 0, 64, kBankPain + n);      // 32 by 8
                sprite(21 + n * 2, x + 32, y - 1, 1, 0, 0, 64, kBankPain + n); // 16 by 8
            }
            else
            {
                sprite(20 + n * 2, x - 1, y, 2, 1, 0, 64, kBankPain + n);      // 8 by 32
                sprite(21 + n * 2, x - 1, y + 32, 2, 0, 0, 64, kBankPain + n); // 8 by 16
            }
        }
        if (!suit_)
            return;

        const bool gun = weapon_ == kWeaponGlock || weapon_ == kWeaponShotgun || weapon_ == kWeaponMp5 ||
                         weapon_ == kWeaponPython || weapon_ == kWeaponCrossbow || weapon_ == kWeaponRpg;
        // (What is thrown or put down, and what is fed from one store: a
        // count of itself.)
        const bool counted = weapon_ == kWeaponGrenade || weapon_ == kWeaponSatchel || weapon_ == kWeaponTripmine ||
                             weapon_ == kWeaponSnark || weapon_ == kWeaponGauss || weapon_ == kWeaponEgon ||
                             weapon_ == kWeaponHornetgun;
        // The bottom row, drawn again when what it says has changed.
        const int healthShown = health_ < 25 ? -1000 - health_ : health_;
        if (healthShown != shownHealth || armor_ != shownArmor || weapon_ != shownWeapon || clip_ != shownClip ||
            spare_ != shownSpare || extra_ != shownExtra)
        {
            shownHealth = healthShown;
            shownArmor = armor_;
            shownWeapon = weapon_;
            shownClip = clip_;
            shownSpare = spare_;
            shownExtra = extra_;
            fillSheet(0, 0, 256, 32, 0);
            fillSheet(192, 32, 64, 32, 0);
            // (Screen y 160 is the sheet's row 0.)
            const int red = health_ < 25 ? 7 : 0;
            blit(6, 170 - 160, kHudCross, red);
            digits(62, 170 - 160, health_, red);
            blit(70, 168 - 160, armor_ > 0 ? kHudSuitFull : kHudSuitEmpty);
            digits(128, 170 - 160, armor_, 0);
            if (gun || counted)
            {
                const UiRect icon = weapon_ == kWeaponShotgun    ? kHudAmmoBuck
                                    : weapon_ == kWeaponPython   ? kHudAmmo357
                                    : weapon_ == kWeaponGrenade  ? kHudAmmoGrenade
                                    : weapon_ == kWeaponCrossbow ? kHudAmmoBolt
                                    : weapon_ == kWeaponSatchel  ? kHudAmmoSatchel
                                    : weapon_ == kWeaponTripmine ? kHudAmmoTripmine
                                    : weapon_ == kWeaponSnark    ? kHudAmmoSnark
                                    : weapon_ == kWeaponRpg      ? kHudAmmoRocket
                                    : weapon_ == kWeaponGauss || weapon_ == kWeaponEgon ? kHudAmmoUranium
                                    : weapon_ == kWeaponHornetgun ? kHudAmmoHornet
                                                                 : kHudAmmo9mm;
                blit(236, 169 - 160, icon);
                if (gun)
                {
                    const int at = digits(232, 170 - 160, spare_, 0);
                    digits(at - 10, 170 - 160, clip_, 0);
                }
                else
                {
                    digits(232, 170 - 160, clip_, 0);
                }
                if (extra_ >= 0)
                {
                    // (Screen y 144 is the sheet's row 32, there.)
                    blit(236, 148 - 144 + 32, kHudAmmoAr);
                    digits(232, 149 - 144 + 32, extra_, 0);
                }
            }
        }
        for (int k = 0; k < 4; ++k)
            sprite(k, k * 64, 160, 1, 3, k * 64, 0, kBankHud);
        if ((gun || counted) && extra_ >= 0)
            sprite(4, 192, 144, 1, 3, 192, 32, kBankHud);

        // The lamp, at the top right as the game has it: its outline, as
        // much of it filled as there is in it, and its beam while alight.
        // Dim while it is out, red when it is nearly empty.
        if (lampSuit_)
        {
            const int lampShown = lampPercent_ * 2 + (lampOn_ ? 1 : 0);
            if (lampShown != shownLamp)
            {
                shownLamp = lampShown;
                fillSheet(32, 32, 32, 16, 0);
                blit(32, 32, kHudLampEmpty);
                const int full = kHudLampFull.w * lampPercent_ / 100;
                if (full > 0)
                    blit(32, 32, kHudLampFull, 0, kHudLampFull.w - full);
                if (lampOn_)
                    blit(32 + kHudLampFull.w, 32, kHudLampBeam);
            }
            const int bank = lampPercent_ < 20 ? kBankRed : (lampOn_ ? kBankHud : kBankDim);
            sprite(5, 256 - 8 - kHudLampBeam.w - kHudLampFull.w, 6, 1, 2, 32, 32, bank); // 32 by 16
        }
        // The crosshair, for what is aimed (unless the COMMAND line's
        // crosshair 0 took it away).
        if ((gun || tankManned(game.logic) >= 0) && game.crosshair)
        {
            int dx, dy;
            crosshairShift(dx, dy);
            sprite(6, 128 - 16 + dx, 96 - 16 + dy, 0, 2, 0, 32, crosshairBank(kBankWhite, kBankRed));
        }
        // At a train's controls: how it is going, where the game shows it
        // (train.cpp: a third of the way across, over the numbers).
        const int train = train_ && suit_ ? trainSpeedStep(game.logic) : -1;
        if (train >= 0)
        {
            if (train != shownTrain)
            {
                shownTrain = train;
                blitTrain(64, 32, train);
            }
            sprite(30, 94, 140, 0, 2, 64, 32, kBankHud);
        }
    }

    /// compose() with the 640 set, where the game puts each piece. The sheet
    /// (bank F, as compose's) is laid out as:
    ///   rows  0..31, all across:   the bottom row of the screen (y 160..191)
    ///   rows 32..63, x 0..127:     the marks of a hurt from the front, the back
    ///   rows 32..63, x 128..255:   four kinds of hurt's pictures
    ///   rows 64..127, x 0..63:     the solid block (colour 1) of being hurt
    ///   rows 64..127, x 64..127:   the marks from the right, the left
    ///   rows 64..95, x 128..255:   the other four kinds of hurt's pictures
    ///   rows 96..111, x 128..143:  the crosshair; x 160..191: the lamp
    ///   rows 96..127, x 192..223:  how a driven train is going
    /// The sprites: 0..3 the bottom row, 4 the lamp, 5 the crosshair, 6..13
    /// the kinds of hurt, 14..17 the marks, 18..29 the red of being hurt, 30
    /// the train.
    void HudDraw::compose640()
    {
        static int shownHealth = -1, shownArmor = -1, shownWeapon = -1, shownClip = -1, shownSpare = -1,
                   shownExtra = -2, shownLamp = -1;
        constexpr int kMarkCell[4][2] = {{0, 32}, {64, 64}, {64, 32}, {96, 64}};
        constexpr int kLampCell[2] = {160, 96}, kCrosshairCell[2] = {128, 96}, kTrainCell[2] = {192, 96};
        auto hurtCell = [](int kind) { return 128 + (kind & 3) * 32; };
        auto hurtRow = [](int kind) { return kind < 4 ? 32 : 64; };
        for (int n = 0; n < kSprites; ++n)
            hide(n);
        flashColour = 0;
        std::memset(painColour, 0, sizeof(painColour));
        std::memset(hurtColour, 0, sizeof(hurtColour));
        if (screenFade.curtained() || frameFreeze.frozen() || !atlas_)
            return;

        // (As compose(): the sprites on, bank F theirs, added to the picture.)
        if (VRAM_F_CR != (VRAM_ENABLE | VRAM_F_MAIN_SPRITE_0x06404000))
        {
            vramSetBankF(VRAM_F_MAIN_SPRITE_0x06404000);
            sheetMade = false;
        }
        REG_DISPCNT = (REG_DISPCNT & ~(DISPLAY_SPR_1D | DISPLAY_SPR_1D_BMP)) | DISPLAY_SPR_ACTIVE;
        REG_BLDCNT = 0x3F00;
        REG_BLDALPHA = 16 | (16 << 8);
        if (!sheetMade)
        {
            sheet.tiles = sheetRam;
            sheet.width = 256;
            sheet.height = 128;
            fillSheet(0, 0, 256, 128, 0);
            fillSheet(0, 64, 64, 64, 1);
            for (int n = 0; n < 4; ++n)
                blit(kMarkCell[n][0], kMarkCell[n][1], kHud640Pain[n]);
            for (int k = 0; k < HudHurts::kKinds; ++k)
                blit(hurtCell(k), hurtRow(k), hud640Hurt(k));
            blit(kCrosshairCell[0] + 3, kCrosshairCell[1] + 3, kHud640Crosshair);
            ramp(kBankHud, 1, kYellowish);
            ramp(kBankHud, 8, kPainRed);
            ramp(kBankWhite, 1, 0x7FFF);
            ramp(kBankDim, 1, kDim);
            ramp(kBankRed, 1, kRedish);
            shownHealth = shownArmor = shownWeapon = shownClip = shownSpare = shownLamp = -1;
            shownExtra = -2;
            shownTrain = -1;
            sheetMade = true;
        }

        // Being hurt, which wants no suit: the screen reddened all over, as
        // compose() has it; the game's mark on each side the hurt came from,
        // in the display's colour (red when health is low), fading.
        if (flash_ > 0)
        {
            const int red = flash_ * 14 / 256;
            flashColour = static_cast<std::uint16_t>(red > 31 ? 31 : red);
            for (int k = 0; k < 12; ++k)
                sprite(18 + k, (k % 4) * 64, (k / 4) * 64, 0, 3, 0, 64, kBankFlash);
        }
        const std::uint16_t painIn = health_ > 25 ? kYellowish : kPainRed;
        for (int n = 0; n < 4; ++n)
        {
            if (pain_[n] < kPainShows)
                continue;
            ramp(painColour[n], painIn, pain_[n] > 256 ? 256 : (pain_[n] < 128 ? 128 : pain_[n]));
            const bool wide = (n & 1) == 0;
            sprite(14 + n, hudDs(kHud640PainAt[n].x), hudDs(kHud640PainAt[n].y), wide ? 1 : 2, 3, kMarkCell[n][0],
                   kMarkCell[n][1], kBankPain + n);
        }
        // The kinds of hurt, pulsing (which also wants no suit).
        if (hurts.any())
        {
            ramp(hurtColour, kYellowish, hurtPulse);
            for (int k = 0; k < HudHurts::kKinds; ++k)
            {
                if (hurts.stacked[k] >= 0)
                    sprite(6 + k, hudDs(hud640HurtAt(hurts.stacked[k]).x), hudDs(hud640HurtAt(hurts.stacked[k]).y), 0, 2,
                           hurtCell(k), hurtRow(k), kBankHurts);
            }
        }
        if (!suit_)
            return;

        const bool gun = clipped(weapon_), count = counted(weapon_);
        // The bottom row, drawn again when what it says has changed. (Screen
        // y 160 is the sheet's row 0.)
        const int healthShown = health_ <= 25 ? -1000 - health_ : health_;
        if (healthShown != shownHealth || armor_ != shownArmor || weapon_ != shownWeapon || clip_ != shownClip ||
            spare_ != shownSpare || extra_ != shownExtra)
        {
            shownHealth = healthShown;
            shownArmor = armor_;
            shownWeapon = weapon_;
            shownClip = clip_;
            shownSpare = spare_;
            shownExtra = extra_;
            fillSheet(0, 0, 256, 32, 0);
            constexpr int kTop = 160;
            const int row = hudDs(kHud640NumberY) - kTop;
            auto bar = [&](int x640) { fillSheet(hudDs(x640), row, 1, kHud640DigitHeight, 6); };
            // Health, red when it is low; and its bar.
            const int red = health_ <= 25 ? 7 : 0;
            blit(hudDs(kHud640CrossX), hudDs(kHud640RowY) - kTop, kHud640Cross, red);
            field640(kHud640HealthX, health_, [&](int x, UiRect digit) { blit(x, row, digit, red); });
            bar(kHud640HealthBarX);
            // Armour: the suit, filled from the bottom as far as there is.
            const int suitX = hudDs(kHud640SuitX), suitY = hudDs(kHud640SuitY) - kTop;
            blit(suitX, suitY, kHud640SuitEmpty);
            const int empty = hudSuitEmptyRows(armor_);
            if (empty < kHud640SuitFull.h)
                blit(suitX, suitY, kHud640SuitFull, 0, 0, empty, true);
            field640(kHud640ArmorX, armor_, [&](int x, UiRect digit) { blit(x, row, digit); });
            // What is in hand: clip | the rest, or a count; then its icon.
            if (gun || count)
            {
                int x640 = kHud640CountX;
                if (gun)
                {
                    field640(kHud640ClipX, clip_, [&](int x, UiRect digit) { blit(x, row, digit); });
                    bar(kHud640ClipBarX);
                    x640 = kHud640SpareX;
                }
                field640(x640, gun ? spare_ : clip_, [&](int x, UiRect digit) { blit(x, row, digit); });
                blit(hudDs(hud640AmmoIconX(x640)), hudDs(kHud640NumberY - kHud640AmmoIconUp) - kTop, ammo640(weapon_));
                // The MP5's grenades, a row up, while it has any.
                if (extra_ > 0)
                {
                    const int up = hudDs(kHud640SecondY) - kTop;
                    field640(kHud640CountX, extra_, [&](int x, UiRect digit) { blit(x, up, digit); });
                    blit(hudDs(hud640AmmoIconX(kHud640CountX)), hudDs(kHud640SecondY - kHud640AmmoIconUp) - kTop,
                         hud640Ammo(kHudAmmo640ArGrenade));
                }
            }
        }
        for (int k = 0; k < 4; ++k)
            sprite(k, k * 64, 160, 1, 3, k * 64, 0, kBankHud);

        // The lamp, at the top right: its outline, as much of it filled as
        // there is in it (emptying from the left), its beam while alight.
        // Dim while it is out, red when it is nearly empty.
        if (lampSuit_)
        {
            const int lampShown = lampPercent_ * 2 + (lampOn_ ? 1 : 0);
            if (lampShown != shownLamp)
            {
                shownLamp = lampShown;
                fillSheet(kLampCell[0], kLampCell[1], 32, 16, 0);
                blit(kLampCell[0], kLampCell[1], kHud640LampEmpty);
                const int empty = hudLampEmptyColumns(lampPercent_);
                if (empty < kHud640LampFull.w)
                    blit(kLampCell[0], kLampCell[1], kHud640LampFull, 0, empty, 0, true);
                if (lampOn_)
                    blit(kLampCell[0] + hudDs(kHud640BeamX) - hudDs(kHud640LampX), kLampCell[1], kHud640LampBeam);
            }
            const int bank = lampPercent_ < 20 ? kBankRed : (lampOn_ ? kBankHud : kBankDim);
            sprite(4, hudDs(kHud640LampX), hudDs(kHud640LampY), 1, 2, kLampCell[0], kLampCell[1], bank); // 32 by 16
        }
        // The crosshair, for what is aimed (unless the COMMAND line's
        // crosshair 0 took it away).
        if ((gun || tankManned(game.logic) >= 0) && game.crosshair)
        {
            int dx, dy;
            crosshairShift(dx, dy);
            sprite(5, hudDs(kHud640CrosshairX) - 3 + dx, hudDs(kHud640CrosshairY) - 3 + dy, 0, 1, kCrosshairCell[0],
                   kCrosshairCell[1], crosshairBank(kBankWhite, kBankRed));
        }
        // At a train's controls: how it is going, where the game shows it.
        const int train = train_ && suit_ ? trainSpeedStep(game.logic) : -1;
        if (train >= 0)
        {
            if (train != shownTrain)
            {
                shownTrain = train;
                blitTrain(kTrainCell[0], kTrainCell[1], train);
            }
            sprite(30, hudDs(kHud640TrainX), hudDs(kHud640TrainY), 0, 2, kTrainCell[0], kTrainCell[1], kBankHud);
        }
    }

    void HudDraw::present()
    {
        if (sheetDirty == 0 || VRAM_F_CR != (VRAM_ENABLE | VRAM_F_MAIN_SPRITE_0x06404000))
            return;
        // (Words: video memory takes no single bytes.)
        auto *bank = reinterpret_cast<volatile std::uint32_t *>(0x06404000);
        for (int row = 0; row < 16; ++row)
        {
            if (!(sheetDirty & (1u << row)))
                continue;
            for (int w = row * 256; w < (row + 1) * 256; ++w)
                bank[w] = sheetRam[w];
        }
        sheetDirty = 0;
    }

    void HudDraw::onVblank()
    {
        auto *oam = reinterpret_cast<volatile std::uint16_t *>(0x07000000) + kFirstSprite * 4;
        for (int n = 0; n < kSprites; ++n)
        {
            oam[n * 4] = shown[n][0];
            oam[n * 4 + 1] = shown[n][1];
            oam[n * 4 + 2] = shown[n][2];
        }
        SPRITE_PALETTE[kBankFlash * 16 + 1] = shownFlash;
        for (int n = 0; n < 4; ++n)
        {
            for (int level = 0; level < 7; ++level)
                SPRITE_PALETTE[(kBankPain + n) * 16 + 1 + level] = shownPain[n][level];
        }
        for (int level = 0; level < 7; ++level)
            SPRITE_PALETTE[kBankHurts * 16 + 1 + level] = shownHurt[level];
    }

    bool HudDraw::showLoading()
    {
#ifdef HLDS_CHILD
        // (Sent the game, a load goes dark: the picture could not be held on
        // the screen while the map's textures are written -- FrameFreeze's
        // bank is the ARM7's -- and is not kept.)
        return false;
#endif
        if (loadingMaterial_ == nullptr)
        {
            NEA_Material *material = nullptr;
            NEA_Palette *palette = nullptr;
            if (!global.hasData || !TextureCache::loadFile(global.commonPath(kLoadingFile).c_str(), false, kLoadingSize,
                                                          kLoadingSize, 0, material, palette))
                return false;
            loadingMaterial_ = material;
            loadingPalette_ = palette;
        }
        loadingShown = static_cast<NEA_Material *>(loadingMaterial_);
        // Enough pictures of it that it is what the screen shows.
        for (int n = 0; n < 2; ++n)
        {
            NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
            global.process(drawLoading);
        }
        NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
        // What is on the screen now is kept as a picture (FrameFreeze), as
        // the last frame of a map is when one map gives way to another, and
        // the texture goes: nothing drawn out of texture memory is on the
        // screen while a map's textures are written into it, and the map
        // has all of video memory to itself.
        frameFreeze.freeze();
        hideLoading();
        return true;
    }

    bool HudDraw::playIntro()
    {
        if (!global.hasFat)
            return false;
        std::FILE *file = std::fopen(global.dataPath(kIntroFile).c_str(), "rb");
        if (file == nullptr)
            return false;
        IntroHeader header;
        if (std::fread(&header, sizeof(header), 1, file) != 1 || std::memcmp(header.magic, kIntroMagic, 4) != 0 ||
            header.version != kIntroVersion || header.frames == 0 || header.frameBytes != kIntroFrameBytes ||
            header.textureSize != kIntroTextureSize || header.frameMicros == 0 ||
            header.pictureWidth > kIntroTextureSize || header.pictureHeight > kIntroTextureSize)
        {
            std::fclose(file);
            return false;
        }
        introX = header.pictureX;
        introY = header.pictureY;
        introW = header.pictureWidth;
        introH = header.pictureHeight;
        // One place in video memory for its picture, and one for its colours:
        // each picture written there in turn, in the blank before it is shown.
        NEA_TextureRegion region{};
        NEA_PaletteRegion colours{};
        NEA_Material *material = NEA_MaterialCreate();
        NEA_Palette *palette = NEA_PaletteCreate();
        auto *frame = static_cast<std::uint8_t *>(std::malloc(kIntroFrameBytes));
        bool ok = material != nullptr && palette != nullptr && frame != nullptr &&
                  NEA_TextureRegionReserve4x4(kIntroTexelBytes, &region) >= kIntroTexelBytes &&
                  NEA_PaletteRegionReserve(kIntroPaletteColours * 2, &colours) >= kIntroPaletteColours * 2;
        ok = ok &&
             NEA_MaterialTexBindFixed(material, NEA_TEX4X4, kIntroTextureSize, kIntroTextureSize,
                                      static_cast<NEA_TextureFlags>(0), region.texels) == 1 &&
             NEA_PaletteBindFixed(palette, colours.base, kIntroPaletteColours, NEA_TEX4X4) == 1;
        // The pictures are read in order, one ahead of the one shown (a few
        // kilobytes from the card each fifteenth of a second): never a wait
        // for the card in the blank, nor so long a frame that the sound runs
        // dry. One fallen behind is skipped to.
        int filePicture = 0; // where the file is
        int pending = -1;    // the picture read and waiting to be shown
        auto readPicture = [&](int n)
        {
            if (n != filePicture &&
                std::fseek(file, static_cast<long>(sizeof(header) + static_cast<std::size_t>(n) * kIntroFrameBytes),
                           SEEK_SET) != 0)
                return false;
            if (std::fread(frame, kIntroFrameBytes, 1, file) != 1)
                return false;
            filePicture = n + 1;
            pending = n;
            return true;
        };
        int shown = -1;
        if (ok && readPicture(0))
        {
            NEA_MaterialSetPalette(material, palette);
            sound.setMusic(kIntroTrack, 0);
            const std::uint32_t start = Global::vblanks();
            // (Not a press that was down when it began.)
            scanKeys();
            std::uint32_t was = keysHeld();
            while (true)
            {
                NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
                const std::uint64_t micros = static_cast<std::uint64_t>(Global::vblanks() - start) * 1000000u / 60u;
                const int due = static_cast<int>(micros / header.frameMicros);
                if (pending >= 0 && pending <= due)
                {
                    NEA_VramWrite(region.texels, frame, kIntroTexelBytes);
                    NEA_VramWrite(NEA_Texture4x4IndexAddress(region.texels), frame + kIntroTexelBytes, kIntroIndexBytes);
                    NEA_VramWrite(colours.base, frame + kIntroTexelBytes + kIntroIndexBytes, kIntroPaletteColours * 2);
                    shown = pending;
                    pending = -1;
                    introShown = material;
                }
                NEA_Process(drawIntro);
                // (The screen is dark from start-up: let up once there is
                // a picture to see.)
                if (shown >= 0 && screenFade.curtained())
                    screenFade.curtainUp();
                sound.frame();
                scanKeys();
                const std::uint32_t held = keysHeld();
                if (held & ~was)
                    break; // (a button, or the stylus: enough)
                was = held;
                if (due >= header.frames)
                    break;
                // The next picture, now, while this one is shown.
                if (pending < 0)
                {
                    const int next = shown + 1 > due ? shown + 1 : due;
                    if (next < header.frames && !readPicture(next))
                        break;
                }
            }
            sound.setMusic(-1, 0);
        }
        // Black again, before its memory goes.
        introShown = nullptr;
        NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
        NEA_Process(drawIntro);
        NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
        if (material != nullptr)
            NEA_MaterialDelete(material);
        if (palette != nullptr)
            NEA_PaletteDelete(palette);
        if (region.texels != nullptr)
            NEA_TextureRegionRelease(&region);
        if (colours.base != nullptr)
            NEA_PaletteRegionRelease(&colours);
        std::free(frame);
        std::fclose(file);
        return shown >= 0;
    }

    bool HudDraw::showMenu()
    {
        // (The game's, on a card or sent by a host: a console that was sent
        // it has the background, and no animated name.)
        if (!global.hasData)
            return false;
        if (menuMaterial_ == nullptr)
        {
            NEA_Material *material = nullptr;
            NEA_Palette *palette = nullptr;
            if (!TextureCache::loadFile(global.commonPath(kMenuFile).c_str(), false, kLoadingSize, kLoadingSize, 0,
                                        material, palette))
                return false;
            menuMaterial_ = material;
            menuPalette_ = palette;
        }
        // The animated title: its pictures read from the card as they come.
        if (logoMaterial_ == nullptr)
        {
            std::FILE *file = std::fopen(global.commonPath(kLogoFile).c_str(), "rb");
            LogoHeader header;
            NEA_Material *material = NEA_MaterialCreate();
            NEA_Palette *palette = NEA_PaletteCreate();
            // (A grey ramp: what the texels say of their brightness.)
            static std::uint16_t greys[32];
            for (int n = 0; n < 32; ++n)
                greys[n] = static_cast<std::uint16_t>(n | n << 5 | n << 10);
            if (file != nullptr && std::fread(&header, sizeof(header), 1, file) == 1 &&
                std::memcmp(header.magic, kLogoMagic, 4) == 0 && header.version == kLogoVersion &&
                header.width == kLogoWidth && header.height == kLogoHeight && header.frames > 0 &&
                header.frameMicros > 0 && material != nullptr && palette != nullptr &&
                NEA_MaterialTexBlank(material, NEA_A3PAL32, kLogoWidth, kLogoTextureHeight,
                                     static_cast<NEA_TextureFlags>(0)) == 1 &&
                NEA_PaletteLoad(palette, greys, 32, NEA_A3PAL32) == 1)
            {
                NEA_MaterialSetPalette(material, palette);
                logoMaterial_ = material;
                logoPalette_ = palette;
                logoFile_ = file;
                logoFrames_ = header.frames;
                logoMicros_ = header.frameMicros;
                logoFrame_ = -1;
            }
            else
            {
                if (file != nullptr)
                    std::fclose(file);
                if (material != nullptr)
                    NEA_MaterialDelete(material);
                if (palette != nullptr)
                    NEA_PaletteDelete(palette);
            }
        }
        menuSince_ = Global::vblanks();
        menuShown = static_cast<NEA_Material *>(menuMaterial_);
        logoShown = nullptr;
        // (The picture hideMenu left on the screen gives way to it: a map
        // load thaws it as it ends, but one that failed -- or none at all,
        // SEND GAME's -- does not.)
        frameFreeze.thaw();
        return true;
    }

    void HudDraw::menuFrame()
    {
        if (menuShown == nullptr)
            return;
        // The title's picture due now (round and round), into its texture in
        // this blank, if it is another.
        if (!menuLogoOn_)
        {
            logoShown = nullptr;
            logoFrame_ = -1;
        }
        else if (logoMaterial_ != nullptr)
        {
            auto *material = static_cast<NEA_Material *>(logoMaterial_);
            const std::uint64_t micros = static_cast<std::uint64_t>(Global::vblanks() - menuSince_) * 1000000u / 60u;
            const int due = static_cast<int>((micros / logoMicros_) % static_cast<std::uint64_t>(logoFrames_));
            if (due != logoFrame_)
            {
                auto *file = static_cast<std::FILE *>(logoFile_);
                auto *texels = static_cast<std::uint8_t *>(NEA_MaterialTexGetData(material));
                constexpr int kBytes = kLogoWidth * kLogoHeight;
                if (texels != nullptr &&
                    std::fseek(file, static_cast<long>(sizeof(LogoHeader) + static_cast<std::size_t>(due) * kBytes),
                               SEEK_SET) == 0 &&
                    std::fread(texels, kBytes, 1, file) == 1)
                {
                    NEA_MaterialTexSetDirty(material);
                    NEA_MaterialTexVramUpdate(material);
                    logoFrame_ = due;
                    logoShown = material;
                }
            }
        }
        NEA_Process(drawMenu);
    }

    void HudDraw::hideMenu()
    {
        profileView.close();
        // (The lobby's map card goes with the title: its textures are not
        // to be beside a map's.)
        mapCard.hide();
        if (menuShown == nullptr)
            return;
        // What is on the screen stays there as a picture (as the loading
        // picture does), and its textures go.
        for (int n = 0; n < 2; ++n)
        {
            NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
            NEA_Process(drawMenu);
        }
        NEA_WaitForVBL(static_cast<NEA_UpdateFlags>(0));
        frameFreeze.freeze();
        menuShown = logoShown = nullptr;
        if (menuMaterial_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(menuMaterial_));
        if (menuPalette_ != nullptr)
            NEA_PaletteDelete(static_cast<NEA_Palette *>(menuPalette_));
        if (logoMaterial_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(logoMaterial_));
        if (logoPalette_ != nullptr)
            NEA_PaletteDelete(static_cast<NEA_Palette *>(logoPalette_));
        if (logoFile_ != nullptr)
            std::fclose(static_cast<std::FILE *>(logoFile_));
        menuMaterial_ = menuPalette_ = logoMaterial_ = logoPalette_ = logoFile_ = nullptr;
    }

    void HudDraw::hideLoading()
    {
        loadingShown = nullptr;
        if (loadingMaterial_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(loadingMaterial_));
        if (loadingPalette_ != nullptr)
            NEA_PaletteDelete(static_cast<NEA_Palette *>(loadingPalette_));
        loadingMaterial_ = loadingPalette_ = nullptr;
    }
} // namespace hlrt
