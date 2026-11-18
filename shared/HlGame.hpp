#pragma once

// The games a card can hold: Half-Life and its two expansions.
//
// Each has its own folder of the player's game (steamDir, beside valve/),
// its own folder on the card (cardDir, under _nds/hl_nds/data/) and its own
// look: the colour its HUD and menus are drawn in. Half-Life's card folder
// is data/ itself, as it always was, so a card made before the expansions
// reads as Half-Life's.
//
// The expansions are DSi only: a DS's heap holds Half-Life's maps and no
// more (HlDsRoom.hpp), and the expansions bring code of their own.

#include <cstdint>

namespace hldata
{
    enum GameId : std::uint8_t
    {
        kGameHalfLife = 0,
        kGameBlueShift = 1,
        kGameOpFor = 2,
        kGameCount = 3,
    };

    /// A DS colour: five bits of red, green and blue (r | g << 5 | b << 10).
    inline constexpr std::uint16_t gameRgb15(int r, int g, int b)
    {
        return static_cast<std::uint16_t>(r | g << 5 | b << 10);
    }

    struct GameInfo
    {
        const char *steamDir;  ///< the player's game folder: valve, bshift, gearbox
        const char *cardDir;   ///< under data/: "" (Half-Life), "bshift/", "gearbox/"
        const char *heading;   ///< the title page's heading
        const char *startMap;  ///< where NEW GAME starts (liblist.gam's startmap)
        const char *trainMap;  ///< the training course (liblist.gam's trainmap)
        const char *trainName; ///< ... as the title's row names it
        const char *uplinkMap; ///< the demo, or null
        std::uint8_t weaponSlots;
        bool dsiOnly;
        bool achievements; ///< Half-Life: Legacy's (HlAchievements.hpp) are Half-Life's
        bool multiplayer;  ///< matches between consoles, Download Play
        /// The title's animated logo is the game's own media/logo.avi or
        /// none: Half-Life's over another game's menu would name the wrong one.
        bool ownLogoOnly;
        /// The HUD's colours: the classic layout's and its dim half, and the
        /// 640 layout's (255, 160, 0 in Half-Life).
        std::uint16_t hud;
        std::uint16_t hudDim;
        std::uint16_t hud640;
        /// The menus' and touch screen's accent, faint, half and full
        /// (HlFont.hpp: kUiOrange), and a button being pressed (kUiPanelDown).
        std::uint16_t accent[3];
        std::uint16_t pressed;
    };

    inline constexpr GameInfo kGames[kGameCount] = {
        {"valve", "", "HALF-LIFE", "c0a0", "t0a0", "HAZARD COURSE", "hldemo1", 5, false, true, true, false,
         gameRgb15(31, 20, 2), gameRgb15(14, 9, 1), gameRgb15(31, 20, 0),
         {gameRgb15(10, 5, 0), gameRgb15(20, 11, 0), gameRgb15(31, 20, 2)}, gameRgb15(12, 8, 2)},
        // Blue Shift's HUD is (95, 95, 255).
        {"bshift", "bshift/", "BLUE SHIFT", "ba_tram1", "ba_hazard1", "HAZARD COURSE", nullptr, 5, true, false, false, true,
         gameRgb15(11, 11, 31), gameRgb15(5, 5, 14), gameRgb15(11, 11, 31),
         {gameRgb15(4, 4, 10), gameRgb15(7, 7, 20), gameRgb15(11, 11, 31)}, gameRgb15(4, 4, 12)},
        // Opposing Force's is (0, 160, 0), and it has seven slots of weapons.
        {"gearbox", "gearbox/", "OPPOSING FORCE", "of0a0", "ofboot0", "BOOT CAMP", nullptr, 7, true, false, false, true,
         gameRgb15(0, 20, 0), gameRgb15(0, 9, 0), gameRgb15(0, 20, 0),
         {gameRgb15(0, 7, 0), gameRgb15(0, 13, 0), gameRgb15(0, 20, 0)}, gameRgb15(2, 9, 2)},
    };

    inline const GameInfo &gameInfo(GameId game)
    {
        return kGames[game < kGameCount ? game : kGameHalfLife];
    }

    /// The game a folder of the player's is (its name, any case), or
    /// kGameCount for none of these.
    inline GameId gameFromFolder(const char *name)
    {
        for (int g = 0; g < kGameCount; ++g)
        {
            const char *a = kGames[g].steamDir;
            const char *b = name;
            while (*a != '\0' && *b != '\0' && (*a == *b || (*b >= 'A' && *b <= 'Z' && *a == *b + 32)))
            {
                ++a;
                ++b;
            }
            if (*a == '\0' && *b == '\0')
                return static_cast<GameId>(g);
        }
        return kGameCount;
    }

    /// The game being played: the ROM sets it once the player has chosen,
    /// the converter for the game it cooks. Half-Life until then.
    inline GameId activeGame = kGameHalfLife;

    inline const GameInfo &activeGameInfo()
    {
        return gameInfo(activeGame);
    }
} // namespace hldata
