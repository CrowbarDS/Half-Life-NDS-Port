#include "game/GameChoice.hpp"

#ifndef HLDS_CHILD
#include <nds.h>

#include "Achievements.hpp"
#include "Global.hpp"
#include "Settings.hpp"
#include "audio/Sound.hpp"
#include "game/Input.hpp"
#include "game/SaveGame.hpp"
#include "net/NetGame.hpp"
#include "render/BottomScreen.hpp"
#include "render/HudDraw.hpp"
#include "render/TopText.hpp"
#include "ui/Menu.hpp"
#include "ui/TouchCanvas.hpp"

namespace hlrt::choice
{
    HL_TWL_CODE void pickStart()
    {
        const std::uint8_t games = global.scanGames();
        menu.setGames(games);
        // (A DSi plays them all; the one played last if it is there.)
        int first = settings.lastGame;
        if (first >= hldata::kGameCount || ((games >> first) & 1) == 0)
        {
            first = hldata::kGameHalfLife;
            for (int g = 0; g < hldata::kGameCount; ++g)
            {
                if ((games >> g) & 1)
                {
                    first = g;
                    break;
                }
            }
        }
        hldata::activeGame = static_cast<hldata::GameId>(first);
    }

    HL_TWL_CODE void switchTo(hldata::GameId game, std::string &startMap, std::vector<hldata::NetMapInfo> &multiplayer,
                              Halt halt)
    {
        hudDraw.forgetGame();
        bottomScreen.forgetIcons();
        topText.forgetTitle();
        hldata::activeGame = game;
        global.remountCommon();
        startMap.clear();
        multiplayer.clear();
        std::string why;
        const Global::DataStatus now = global.ReadManifest(startMap, &multiplayer, &why, &global.dataVersion);
        if (now != Global::DataStatus::Ok)
        {
            const std::string advice = (why.empty() ? std::string() : why + " ") + Global::DataStatusHint(now);
            halt("Cannot start:", Global::DataStatusTitle(now), advice.c_str());
        }
        netGame.setMaps(multiplayer);
        achievements.reload();
        achievements.started();
        hudDraw.load();
        SaveGame::forget();
        menu.setSaves(&SaveGame::index());
        touchCanvas.recolour();
    }

    HL_TWL_CODE bool choose(std::string &startMap, std::vector<hldata::NetMapInfo> &multiplayer, Halt halt)
    {
        menu.openGames(settings.lastGame);
        while (true)
        {
            Global::waitVBlank();
            input.poll(false);
            sound.frame();
            if (menu.frame() == Menu::Action::ChooseGame)
                break;
        }
        const auto chosen = static_cast<hldata::GameId>(menu.chosenGame());
        if (settings.lastGame != chosen)
        {
            settings.lastGame = chosen;
            settings.save();
        }
        const bool other = chosen != hldata::activeGame;
        if (other)
            switchTo(chosen, startMap, multiplayer, halt);
        // (The page gone: nothing of it under the intro.)
        touchCanvas.pageOff();
        touchCanvas.canvas().clear();
        touchCanvas.pageOn();
        touchCanvas.recolour();
        return other;
    }

    HL_TWL_CODE void atBoot(bool readOk, std::string &startMap, std::vector<hldata::NetMapInfo> &multiplayer, Halt halt)
    {
        if (!choose(startMap, multiplayer, halt) && !readOk)
            switchTo(hldata::activeGame, startMap, multiplayer, halt);
    }

    HL_TWL_CODE void change(std::string &startMap, std::vector<hldata::NetMapInfo> &multiplayer, Halt halt)
    {
        if (choose(startMap, multiplayer, halt))
            hudDraw.playIntro();
    }
} // namespace hlrt::choice
#endif
