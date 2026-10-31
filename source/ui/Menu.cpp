#include "global_header.hpp"

#include <cstdio>
#include <cstdlib>

#include "ui/Menu.hpp"
#include "HlNetNext.hpp"

#include "HlCommands.hpp"
#include "HlRemap.hpp"
#include "HlSkill.hpp"
#include "Achievements.hpp"
#include "Global.hpp"
#include "Settings.hpp"
#include "audio/DspAudio.hpp"
#include "game/Input.hpp"
#include "audio/Sound.hpp"
#include "data/LevelFile.hpp"
#include "game/SaveGame.hpp"
#include "net/DlSend.hpp"
#include "net/NetGame.hpp"
#include "render/BottomScreen.hpp"
#include "render/HudDraw.hpp"
#include "render/MapCard.hpp"
#include "render/ProfileView.hpp"
#include "ui/Keyboard.hpp"
#include "ui/TouchCanvas.hpp"

namespace hlrt
{
    Menu menu;

    namespace
    {
        using namespace hldata;

        enum class Kind
        {
            Button,
            Value,  ///< shows a setting; a press changes it
            Slider,
        };
        enum Id
        {
            kResume,
            kEndMatch,
            kHandLeft,     ///< a console sent the game: which hand it is played with
            kHandRight,
            kSave,
            kSaveGame,
            kLoadGame,
            kContinue,
            kYes,
            kNo,
            kSlot, ///< a line of the list (which, by its row)
            kNewGame,
            kOptions,
            kCommand,
            kDsp, ///< the room effects
            kOutput,
            kQuality,
            kMusic,
            kEffects,
            kVoices,
            kAmbience,
            kReadout,
            kHand,
            kControls,
            kScheme,
            kPadSide,
            kPadUp,
            kMultiplayer,
            kNetHost,
            kNetJoin,
            kNetMap,
            kNetFrags,
            kNetMinutes,
            kNetSkill,
            kNetStart,
            kNetFound, ///< a game found (which, by its row)
            kLeave,
            kBob,
            kBack,
            kGraphics,
            kFog,
            kFogStrength,
            kFogDistance,
            kFogColour,
            kModelDetail,
            kWorldDetail,
            kModelLimit,
            kDrawDistance,
            kDuck,
            kButtons,
            kTouchAim,
            kAimSpeed,
            kPitchSpeed,
            kInvert,
            kFirmness,
            kFirmPress,
            kKey,     ///< an action's button (which, by its row: buttonActions)
            kMore,    ///< the buttons' other screen
            kResetKeys,
            kAutoaim,
            kSkillEasy, ///< a new game's difficulty (kSkillEasy + its level - 1)
            kSkillMedium,
            kSkillHard,
            kAchievementsOpen,
            kPopups,       ///< the achievements' pop-ups (a setting)
            kGameHalfLife, ///< NEW GAME's: the game itself
            kGameTraining, ///< ... the Hazard Course
            kGameUplink,   ///< ... Half-Life: Uplink
            kNewSkill,     ///< ... and how hard (its value row)
            kQuit,         ///< the title's: back to whatever started the game
            kSendDemo,     ///< MULTIPLAYER's: a demo sent to a console without the game
            kProfile,      ///< ... the player's name, model, colours and spray
            kProfName,     ///< PROFILE's: the name the others see
            kProfModel,
            kProfTop,      ///< ... the model's top colour (a slider of hues)
            kProfBottom,
            kProfSpray,    ///< ... the logo sprayed (into ui/logos.hic)
            kProfSprayColour,
            kNetMode,      ///< HOST's MATCH: deathmatch or teamplay
            kNetMost,      ///< ... how many may play
            kNetStay,      ///< ... weapons stay
            kNetFall,      ///< ... fall damage
            kNetFriendly,  ///< ... friendly fire
            kNetLamp,      ///< ... the flashlight allowed
            kNetAim,       ///< ... autoaim
            kNetGuests,    ///< ... consoles without the game let in
            kNetChat,      ///< NEXT MAP's: how long a match's scores are shown
            kNetTotals,    ///< ... a lobby's matches added up
            kNetNext,      ///< ... how the next map is chosen
            kRotPick,      ///< ... Rotation's: the map to add
            kRotAdd,       ///< ... that map put at the end
            kRotUp,        ///< ... the one chosen moved up,
            kRotDown,      ///< ... down,
            kRotOut,       ///< ... or taken off
            kRotItem,      ///< ... the list itself: this and the next kRotationMost - 1
            kRotItemLast = kRotItem + kRotationMost - 1,
            kNetTeam,      ///< PLAYERS': the team this console's player would be on
            kSeat,         ///< ... a player's place (which, by its row)
            kDlSeat,       ///< SEND THE GAME's: a console that has come for it (which, by its row)
            kStartSeat,    ///< STARTING's: a player, and how far it has got (which, by its row)
        };
        struct Row
        {
            Kind kind;
            Id id;
            const char *label;
        };
        const Row kMain[] = {{Kind::Button, kResume, "RESUME"},       {Kind::Button, kSave, "QUICK SAVE"},
                             {Kind::Button, kSaveGame, "SAVE GAME"}, {Kind::Button, kLoadGame, "LOAD GAME"},
                             {Kind::Button, kNewGame, "NEW GAME"},   {Kind::Button, kOptions, "OPTIONS"},
                             {Kind::Button, kAchievementsOpen, "ACHIEVEMENTS"},
                             {Kind::Button, kCommand, "COMMAND"},
                             {Kind::Button, kLeave, "QUIT TO MAIN MENU"}};
        // The title: CONTINUE where there is something to go on with, then
        // the game's own main menu; the Hazard Course where it is on the
        // card; QUIT where there is somewhere to go back to.
        const Row kTitleRows[] = {{Kind::Button, kContinue, "CONTINUE"},
                                  {Kind::Button, kNewGame, "NEW GAME"},
                                  {Kind::Button, kLoadGame, "LOAD GAME"},
                                  {Kind::Button, kGameTraining, "HAZARD COURSE"},
                                  {Kind::Button, kMultiplayer, "MULTIPLAYER"},
                                  {Kind::Button, kOptions, "OPTIONS"},
                                  {Kind::Button, kAchievementsOpen, "ACHIEVEMENTS"},
                                  {Kind::Button, kQuit, "QUIT"}};
        // A game between consoles (source/net/NetGame.hpp).
        const Row kMultiRows[] = {{Kind::Button, kNetHost, "HOST"},
                                  {Kind::Button, kNetJoin, "JOIN"},
                                  {Kind::Button, kSendDemo, "SEND DEMO"},
                                  {Kind::Button, kProfile, "PROFILE"},
                                  {Kind::Button, kOptions, "OPTIONS"},
                                  {Kind::Button, kBack, "BACK"}};
        const Row kNetPauseRows[] = {{Kind::Button, kResume, "RESUME"}, {Kind::Button, kOptions, "OPTIONS"},
                                     {Kind::Button, kEndMatch, "END MATCH"}, {Kind::Button, kLeave, "LEAVE GAME"}};
        // The games found: made when they are shown, then BACK.
        constexpr int kFoundListed = 4;
        Row foundRows[kFoundListed + 1];
        NetGame::Found foundList[kFoundListed];
        int foundCount = 0;
        char joinWhy[48] = "";
#ifdef HLDS_CHILD
        constexpr bool childConsole = true;
#else
        constexpr bool childConsole = false;
#endif
        // The lobby's model of the player chosen: whether it could not be
        // read (not tried again), the colours it has, and whether it is up.
        int lobbyModelTried = 0, lobbyModelColours = -1;
        bool lobbyWas = false;
        int lobbyCharacter = 0;
        const Row kHandRows[] = {{Kind::Button, kHandLeft, "LEFT HAND"}, {Kind::Button, kHandRight, "RIGHT HAND"}};
        const Row kConfirmRows[] = {{Kind::Button, kYes, "YES"}, {Kind::Button, kNo, "NO"}};
        // A new game's difficulty, as the game's NEW GAME asks it.
        const Row kSkillRows[] = {{Kind::Button, kSkillEasy, "EASY"},
                                  {Kind::Button, kSkillMedium, "MEDIUM"},
                                  {Kind::Button, kSkillHard, "DIFFICULT"},
                                  {Kind::Button, kBack, "BACK"}};
        // NEW GAME, as the game's own dialog has it: how hard, then which
        // game -- the game itself, Uplink (only if it is on the card) -- or
        // not. (The Hazard Course is the title's own row.)
        const Row kNewGameRows[] = {{Kind::Value, kNewSkill, "Difficulty"},
                                    {Kind::Button, kGameHalfLife, "PLAY HALF-LIFE"},
                                    {Kind::Button, kGameUplink, "PLAY UPLINK"},
                                    {Kind::Button, kBack, "CANCEL"}};
        char question[40];
        const char *const kFirmName[kFirmActions] = {"off", "fire", "use", "jump", "alt fire"};

        // Where the rows are: the first page's are big and few, the
        // others' as many as the screen holds.
        struct Layout
        {
            int x, top, width, height, pitch;
        };
        constexpr Layout kMainLayout = {28, 32, 200, 18, 20};
        // (The pause menu's nine: a little closer.)
        constexpr Layout kPauseLayout = {28, 26, 200, 16, 18};
        constexpr Layout kOptionLayout = {8, 2, 240, 17, 19};

        /// What a slider slides, and between what (0..10, but the aim's
        /// speeds: 1..20).
        std::uint8_t &slid(Id id)
        {
            switch (id)
            {
            case kAimSpeed:    return settings.aimSpeed;
            case kPitchSpeed:  return settings.pitchSpeed;
            case kFirmness:    return settings.firmness;
            case kFogStrength: return settings.graphics.fogStrength;
            case kFogDistance: return settings.graphics.fogDistance;
            case kProfTop:     return settings.topColour;
            case kProfBottom:  return settings.bottomColour;
            case kMusic:       return settings.volume[kVolumeMusic];
            case kEffects:     return settings.volume[kVolumeEffects];
            case kVoices:      return settings.volume[kVolumeVoices];
            default:           return settings.volume[kVolumeAmbience];
            }
        }
        int slidLeast(Id id) { return id == kAimSpeed || id == kPitchSpeed ? 1 : 0; }
        int slidMost(Id id)
        {
            return id == kAimSpeed || id == kPitchSpeed ? kAimSpeedMost
                                                        : (id == kProfTop || id == kProfBottom ? 255 : kVolumeMost);
        }
        bool isColour(Id id) { return id == kProfTop || id == kProfBottom; }
        bool isVolume(Id id) { return id == kMusic || id == kEffects || id == kVoices || id == kAmbience; }
        // What is on the card, for the rows that are there only if it is
        // (Menu::probeCard): the game, its Hazard Course, Uplink.
        bool onCard[3] = {true, false, false};
        // ... whether there is a save to go on with, and somewhere to quit to.
        bool saveThere = false;
        bool quitThere = false;

        /// A row that is not shown at all: one that depends on what is not
        /// on the card (rule: nothing is offered that is not there).
        bool hiddenRow(Id id)
        {
            return (id == kGameTraining && !onCard[1]) || (id == kGameUplink && !onCard[2]) ||
                   (id == kContinue && !saveThere) || (id == kQuit && !quitThere) || (id == kEndMatch && (!netGame.isHost() || netGame.match().over));
        }

        const Row *tableOf(int page, int &count);

        /// A page's rows as they are shown: its table less what is hidden.
        const Row *rowsOf(int page, int &count)
        {
            static Row shown[16];
            int all = 0;
            const Row *table = tableOf(page, all);
            count = 0;
            for (int i = 0; i < all && count < 16; ++i)
            {
                if (!hiddenRow(table[i].id))
                    shown[count++] = table[i];
            }
            return shown;
        }

        const Row *tableOf(int page, int &count)
        {
            // (A sheet -- the settings, the saves, the achievements -- has
            // no rows of these: Menu::drawSheet.)
            count = 0;
            switch (page)
            {
            case 0: count = static_cast<int>(sizeof(kMain) / sizeof(kMain[0])); return kMain;
            case 3: count = static_cast<int>(sizeof(kTitleRows) / sizeof(kTitleRows[0])); return kTitleRows;
            case 5: count = 2; return kConfirmRows;
            case 20: count = 2; return kHandRows;
            case 7: count = static_cast<int>(sizeof(kMultiRows) / sizeof(kMultiRows[0])); return kMultiRows;
            case 9: count = foundCount + 1; return foundRows;
            case 11: count = static_cast<int>(sizeof(kNetPauseRows) / sizeof(kNetPauseRows[0])); return kNetPauseRows;
            case 14: count = static_cast<int>(sizeof(kSkillRows) / sizeof(kSkillRows[0])); return kSkillRows;
            case 16: count = static_cast<int>(sizeof(kNewGameRows) / sizeof(kNewGameRows[0])); return kNewGameRows;
            default: return kMain;
            }
        }
    } // namespace

    int Menu::rowCount() const
    {
        int count = 0;
        rowsOf(static_cast<int>(page_), count);
        return count;
    }

    void Menu::openTitle()
    {
        open();
        probeCard();
        title_ = true;
        page_ = Page::Title;
        draw();
    }

    void Menu::openHand()
    {
        open(false);
        title_ = true;
        page_ = Page::Hand;
        draw();
        // (As it stands until one is touched.)
        drawRow(settings.leftHanded ? 0 : 1, true);
    }

    void Menu::openLobby()
    {
        open(false);
        probeCard();
        title_ = true;
        sheetFrom_ = Page::Main; // (a page left behind by a stream, or a lost link)
        openSheet(netGame.isHost() ? Page::NetHost : Page::NetLobby);
    }

    /// Which game: the game, its Hazard Course, Uplink -- each one there
    /// only if its first map is on the card.
    void Menu::probeCard()
    {
        static const char *const kFirst[3] = {nullptr, "t0a0", "hldemo1"};
        for (int i = 1; i < 3; ++i)
        {
            hldata::LevelHeader header{};
            gameThere_[i] = Level::peekHeader(kFirst[i], header);
            onCard[i] = gameThere_[i];
        }
        saveThere = saves_ != nullptr && saveMostRecent(*saves_) >= 0;
    }

    void Menu::setQuittable(bool can) { quitThere = can; }

    void Menu::openNewGame()
    {
        probeCard();
        if (skill_ < hldata::kSkillEasy || skill_ > hldata::kSkillHard)
            skill_ = hldata::kSkillMedium;
        page_ = Page::NewGame;
        draw();
    }

    void Menu::ask(Action action, const char *text)
    {
        asked_ = action;
        std::snprintf(question, sizeof(question), "%s", text);
        page_ = Page::Confirm;
        draw();
    }

    int netlinkFailStage();

    bool Menu::big() const
    {
        return page_ == Page::Main || page_ == Page::Title || page_ == Page::Confirm || page_ == Page::Multiplayer ||
               page_ == Page::NetPause || page_ == Page::Difficulty || page_ == Page::NewGame || page_ == Page::Hand;
    }

    /// What of the lobby is on the screen: who is there, what they are
    /// called, how they stand -- to draw it again when it is not so.
    unsigned Menu::netSignature() const
    {
        const NetMatch &match = netGame.match();
        unsigned sum = static_cast<unsigned>(netGame.phase()) * 7919u + match.profileStamp * 104729u +
                       static_cast<unsigned>(match.rules.mode) * 31u + match.rules.most + netGame.guestRoom() * 977u;
        // (What is being sent to the consoles given the game, and how far.)
        const char *what = "";
        int consoles = 0, percent = 0;
        if (netGame.sending(what, consoles, percent))
            sum = sum * 131u + static_cast<unsigned>(consoles * 101 + percent) + static_cast<unsigned char>(what[0]);
        for (int p = 0; p < kNetPlayers; ++p)
        {
            sum = sum * 7u + static_cast<unsigned>(netGame.bars(p) + 1);
            sum = sum * 31u + (netHere(match, p) ? 1u : 0u);
            for (const char *c = match.names[p]; *c != 0; ++c)
                sum = sum * 31u + static_cast<unsigned char>(*c);
            sum = sum * 31u + static_cast<unsigned>(match.frags[p] + 1000) + static_cast<unsigned>(match.deaths[p]) * 131u;
        }
        return sum;
    }

    /// Out of a game: where a console goes. (Sent the game: there is only
    /// looking for another. And one told its host is coming back too.)
    void Menu::outOfGame()
    {
#ifndef HLDS_CHILD
        if (netGame.backSoon()[0] == 0)
        {
            page_ = Page::Multiplayer;
            draw();
            const char *why = netGame.ended();
            if (why[0] != 0)
                showReason(why);
            return;
        }
#endif
        openJoin();
    }

    void Menu::openJoin()
    {
        open(false);
        title_ = true;
        mapCard.hide();
        netTick_ = 0;
        // (Said under the list for as long as it is looked at.)
        std::snprintf(joinWhy, sizeof(joinWhy), "%s", netGame.ended());
        page_ = Page::NetJoin;
        netGame.scan();
        showFound();
    }

    /// The games found, as rows; then BACK.
    void Menu::showFound()
    {
        foundCount = netGame.found(foundList, kFoundListed);
        for (int i = 0; i < foundCount; ++i)
            foundRows[i] = Row{Kind::Button, kNetFound, foundList[i].line};
#ifdef HLDS_CHILD
        foundRows[foundCount] = Row{Kind::Button, kBack, "LOOK AGAIN"};
#else
        foundRows[foundCount] = Row{Kind::Button, kBack, "BACK"};
#endif
        page_ = Page::NetJoin;
        draw();
    }

    void Menu::open(bool show)
    {
        bottomScreen.release();
        title_ = false;
        page_ = netGame.playing() ? Page::NetPause : Page::Main;
        active_ = -1;
        // (The touch that opened it, if there was one, is not a press.)
        touching_ = (keysHeld() & KEY_TOUCH) != 0;
        if (show)
            draw();
    }

    void Menu::draw()
    {
        touchCanvas.pageOff();
        drawWhole();
        touchCanvas.pageOn();
    }

    void Menu::drawWhole()
    {
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        // (Whatever the last page coloured: a score table's, a lobby's.)
        touchCanvas.tint(1, 0, 2, 24, nullptr);
        if (sheet())
        {
            drawSheet();
            return;
        }
        if (big())
        {
            const FontView &title = touchCanvas.font(page_ == Page::Confirm ? kFontMedium : kFontTitle);
            drawTextCentred(canvas, title, 128, page_ == Page::Confirm ? 14 : 8,
                            page_ == Page::Main || page_ == Page::NetPause
                                ? "PAUSED"
                                : (page_ == Page::Title       ? "HALF-LIFE"
                                   : page_ == Page::Multiplayer ? "MULTIPLAYER"
                                   : page_ == Page::Difficulty  ? "DIFFICULTY"
                                   : page_ == Page::NewGame     ? "NEW GAME"
                                   : page_ == Page::Hand        ? "WHICH HAND?"
                                                                : question),
                            inkFor(title, kUiOrange));
        }
        for (int row = 0; row < rowCount(); ++row)
            drawRow(row, false);
        if (page_ == Page::NetJoin)
        {
            const FontView &small = touchCanvas.font(kFontSmall);
            const int top = kOptionLayout.top + rowCount() * kOptionLayout.pitch + 4;
            drawTextCentred(canvas, small, 128, top,
                            netGame.phase() == NetGame::Phase::Joining ? "Joining..." : "Looking for games...",
                            inkFor(small, kUiOrange));
            if (joinWhy[0] != 0)
                drawTextCentred(canvas, small, 128, 170, joinWhy, inkFor(small, kUiGrey));
#ifdef HLDS_CHILD
            drawTextCentred(canvas, small, 128, 150, global.twlChild ? "DSi mode, 16 MB" : "DS mode", inkFor(small, kUiGrey));
#endif
        }
        if (page_ == Page::NetPause)
        {
            bottomScreen.drawScores(kMainLayout.top + rowCount() * kMainLayout.pitch + 4);
            netShown_ = netSignature();
        }
    }

    // -------------------------------------------------------------- sheets

    namespace
    {
        /// How much of `text` the next line of a column `width` wide takes:
        /// as many whole words as fit.
        int wrapped(const FontView &font, const char *text, int width)
        {
            int fits = textFit(font, text, width);
            if (text[fits] != '\0')
            {
                int space = fits;
                while (space > 0 && text[space] != ' ')
                    --space;
                fits = space > 0 ? space : fits;
            }
            return fits;
        }
        const char *skipSpaces(const char *text)
        {
            while (*text == ' ')
                ++text;
            return text;
        }
        /// `count` characters of `text` (all of it if negative) at (x, y), in
        /// no more than `width`: what does not fit is left off, and "..."
        /// says so. Where it ends.
        int drawFitted(Canvas &canvas, const FontView &font, int x, int y, const char *text, const Ink &ink, int width,
                       int count = -1)
        {
            if (textWidth(font, text, count) <= width)
                return drawText(canvas, font, x, y, text, ink, count);
            int fits = textFit(font, text, width - textWidth(font, "..."));
            while (fits > 0 && text[fits - 1] == ' ')
                --fits;
            const int end = drawText(canvas, font, x, y, text, ink, fits);
            return drawText(canvas, font, end, y, "...", ink);
        }
        /// `text` in the lines of a box from `line` to `last` (of `pitch`),
        /// a word at a time; on the last, what does not fit is left off and
        /// said to be.
        void drawWrapped(Canvas &canvas, const FontView &font, int x, int y, int pitch, int line, int last,
                         const char *text, const Ink &ink, int width)
        {
            for (; line <= last && *text != '\0'; ++line)
            {
                if (line == last)
                {
                    drawFitted(canvas, font, x, y + line * pitch, text, ink, width);
                    return;
                }
                const int fits = wrapped(font, text, width);
                drawText(canvas, font, x, y + line * pitch, text, ink, fits);
                text = skipSpaces(text + fits);
            }
        }

        // Where things are: the chapter between its arrows along the top;
        // the list, a band of whole rows of tiles (so that what is drawn
        // in it, scrolled or slid, is cut at its edges for nothing); the
        // box that reads out the row touched; the buttons at the bottom.
        // Every one of them at least 20 high, for a finger.
        constexpr int kSheetHeadHeight = 22;
        constexpr int kSheetListTop = 24;
        constexpr int kSheetBarTop = 171;
        constexpr int kSheetBarHeight = 20;
        constexpr int kSheetListWidth = 246;
        constexpr int kSheetSlop = 6;   ///< how far a touch may wander and still be a touch
        constexpr int kSheetSwipe = 48; ///< how far the list has to be slid to turn to another chapter
        // A slider's track in its row, from the row's left.
        constexpr int kSheetTrackLeft = 112, kSheetTrackWidth = 124;
        // What a touch can be on, besides a row (its number).
        constexpr int kSheetNothing = -1, kSheetLeft = -2, kSheetRight = -3, kSheetList = -6, kSheetYes = -20,
                      kSheetNo = -21;
        constexpr int kSheetBar = -10; ///< ... and the buttons along the bottom: kSheetBar - n
        struct SheetBox
        {
            int x, y, w, h;
            bool has(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
        };
        constexpr SheetBox kSheetLeftBox = {2, 1, 30, 20}, kSheetRightBox = {224, 1, 30, 20};
        constexpr SheetBox kSheetYesBox = {148, kSheetBarTop, 50, kSheetBarHeight};
        constexpr SheetBox kSheetNoBox = {202, kSheetBarTop, 50, kSheetBarHeight};
        /// The `n`th of `count` buttons along the bottom.
        SheetBox sheetBarBox(int n, int count)
        {
            if (count == 1)
                return SheetBox{4, kSheetBarTop, 248, kSheetBarHeight};
            if (count == 2)
                return n == 0 ? SheetBox{4, kSheetBarTop, 148, kSheetBarHeight} : SheetBox{156, kSheetBarTop, 96, kSheetBarHeight};
            return SheetBox{4 + n * 84, kSheetBarTop, 80, kSheetBarHeight};
        }
        // What YES does, once asked.
        constexpr int kAskReset = 1, kAskOverwrite = 2, kAskLoad = 3, kAskDelete = 4, kAskLeave = 5, kAskKick = 6,
                      kAskSend = 7;
        int kickWho = -1;

        /// What depends on the letters' height.
        struct SheetGeometry
        {
            int line, row, listHeight, detailTop, detailHeight;
        };
        SheetGeometry sheetGeometry()
        {
            SheetGeometry g;
            g.line = touchCanvas.font(kFontSmall).height();
            g.row = g.line + 5 > 20 ? g.line + 5 : 20;
            g.detailHeight = 4 * g.line + 4;
            g.detailTop = kSheetBarTop - 2 - g.detailHeight;
            g.listHeight = (g.detailTop - 2 - kSheetListTop) / 8 * 8;
            return g;
        }
        /// The list's band of the touch screen, as a canvas of its own.
        Canvas sheetBand(const SheetGeometry &g)
        {
            Canvas &screen = touchCanvas.canvas();
            Canvas band = screen;
            band.tiles = screen.tiles + (kSheetListTop / 8) * (screen.width / 8) * 8;
            band.height = g.listHeight;
            return band;
        }

        /// A button of a sheet, with its word in the middle of it (grey if
        /// it does nothing now).
        void drawSheetButton(const SheetBox &box, const char *label, bool down, bool dead = false)
        {
            Canvas &canvas = touchCanvas.canvas();
            const FontView &medium = touchCanvas.font(kFontMedium), &small = touchCanvas.font(kFontSmall);
            const FontView &font = textWidth(medium, label) <= box.w - 8 ? medium : small;
            canvas.button(box.x, box.y, box.w, box.h, down && !dead);
            drawTextCentred(canvas, font, box.x + box.w / 2, box.y + (box.h - font.height()) / 2, label,
                            inkFor(font, dead ? kUiGrey : (down ? kUiWhite : kUiOrange)));
        }

        // --- The achievements.

        int achCount(int chapter)
        {
            return hldata::kAchChapterStart[chapter + 1] - hldata::kAchChapterStart[chapter];
        }
        int achOf(int chapter, int n)
        {
            const int id = hldata::kAchByChapter[hldata::kAchChapterStart[chapter] + n];
            return id < hldata::kAchievementCount ? id : 0;
        }
        /// A hidden one says nothing of itself until it is earned.
        const char *achTitle(const hldata::AchievementTracker &tracker, int id)
        {
            return hldata::kAchievements[id].hidden && !tracker.has(id) ? "Hidden achievement"
                                                                         : hldata::kAchievements[id].title;
        }
        /// What a row says at its end: earned, how far along, or nothing.
        void achStatus(const hldata::AchievementTracker &tracker, int id, char *out, std::size_t size)
        {
            if (tracker.has(id))
                std::snprintf(out, size, "earned");
            else if (hldata::kAchievements[id].goal != 0)
                std::snprintf(out, size, "%d/%d", tracker.progress(id), hldata::kAchievements[id].goal);
            else
                out[0] = '\0';
        }
        /// How wide a row's title may be, beside that.
        int achTitleRoom(const FontView &font, const char *status)
        {
            return kSheetListWidth - 28 - textWidth(font, status);
        }
        const char *achText(const hldata::AchievementTracker &tracker, int id)
        {
            return hldata::kAchievements[id].hidden && !tracker.has(id) ? "Earned somewhere along the way."
                                                                         : hldata::kAchievements[id].text;
        }

        /// The trophy of the top screen's pop-up, small: lit, or not yet.
        void drawTrophy(Canvas &canvas, int x, int y, bool lit)
        {
            static constexpr const char *kCup[12] = {
                "############", "#.########.#", "#.##o#####.#", ".#.#o####.#.", "...#o####...", "....####....",
                ".....##.....", ".....##.....", "....####....", "...######...", "...######...", "............",
            };
            for (int row = 0; row < 12; ++row)
            {
                for (int col = 0; col < 12; ++col)
                {
                    const char c = kCup[row][col];
                    if (c != '.')
                        canvas.pixel(x + col, y + row,
                                     lit ? (c == 'o' ? kUiWhite + 2 : kUiOrange + 2) : (c == 'o' ? kUiGrey : kUiGrey + 1));
                }
            }
        }

        // --- MULTIPLAYER's tiles: four, two by two, each with a picture of
        // our own (none of the console maker's), then BACK under them.

        /// The pictures, twelve by twelve ('#' lit, 'o' bright, '.' nothing),
        /// drawn twice the size: sending (an antenna, waves out), receiving
        /// (an antenna, an arrow in), a demo (a console, an arrow out), a
        /// profile (a head and shoulders), the options (a cog).
        constexpr int kTiles = 5;
        constexpr const char *kTileIcons[kTiles][12] = {
            {"............", ".#........#.", "#..#....#..#", "#.#..##..#.#", "#.#.#oo#.#.#", "#..#.oo.#..#",
             ".#...##...#.", ".....##.....", ".....##.....", "....####....", "...######...", "............"},
            {".....##.....", ".....##.....", "...#.##.#...", "....####....", ".....##.....", "............",
             "....#..#....", "...#.oo.#...", ".....oo.....", ".....##.....", "....####....", "...######..."},
            {"..########..", "..#oooooo#..", "..#oooooo#..", "..########..", "..#......#..", "..#.#..#.#..",
             "..########..", "............", ".....##.....", "...######...", "....####....", ".....##....."},
            {"....####....", "...######...", "...##oo##...", "...######...", "....####....", "............",
             "..########..", ".##########.", ".####oo####.", ".##########.", ".##########.", "............"},
            {".....##.....", "..#.####.#..", ".##########.", "..###..###..", ".###....###.", "####.oo.####",
             "####.oo.####", ".###....###.", "..###..###..", ".##########.", "..#.####.#..", ".....##....."}};
        void drawTileIcon(Canvas &canvas, int x, int y, int which, bool lit)
        {
            for (int row = 0; row < 12; ++row)
            {
                for (int col = 0; col < 12; ++col)
                {
                    const char c = kTileIcons[which][row][col];
                    if (c == '.')
                        continue;
                    const int colour = lit ? (c == 'o' ? kUiWhite + 2 : kUiOrange + 2) : (c == 'o' ? kUiGrey + 1 : kUiGrey);
                    canvas.fill(x + col * 2, y + row * 2, 2, 2, colour);
                }
            }
        }
        struct TileBox
        {
            int x, y, w, h;
        };
        /// Where MULTIPLAYER's row `row` is: a tile (the first five: three,
        /// then two under them in the middle), or BACK.
        /// WHICH HAND?: a big button each, on its own side.
        TileBox handTile(int row) { return TileBox{8 + row * 124, 44, 116, 132}; }
        TileBox multiTile(int row)
        {
            if (row >= kTiles)
                return TileBox{28, 142, 200, 20};
            if (row < 3)
                return TileBox{8 + row * 82, 28, 76, 52};
            return TileBox{49 + (row - 3) * 82, 84, 76, 52};
        }

        // --- The settings: a category a page, each a list of rows.

        constexpr int kSettingsChapters = 6;
        const char *const kSettingsChapterNames[kSettingsChapters] = {"SOUND",     "GRAPHICS", "CONTROLS",
                                                                      "TOUCH AIM", "BUTTONS",  "OTHER"};
        const Row kSoundRows[] = {{Kind::Value, kDsp, "Room effects"},  {Kind::Value, kOutput, "Output"},
                                  {Kind::Value, kQuality, "Quality"},    {Kind::Slider, kMusic, "Music"},
                                  {Kind::Slider, kEffects, "Effects"},   {Kind::Slider, kVoices, "Voices"},
                                  {Kind::Slider, kAmbience, "Ambience"}};
        const char *const kQualityName[hldata::kQualityChoices] = {"standard", "high"};
        const char *const kRoomName[hldata::kRoomChoices] = {"off", "classic", "enhanced"};
        const char *const kOutputName[hldata::kOutputChoices] = {"speakers", "headphones", "mono"};
        const Row kGraphicsRows[] = {{Kind::Value, kFog, "Fog"},
                                     {Kind::Slider, kFogStrength, "Fog strength"},
                                     {Kind::Slider, kFogDistance, "Fog distance"},
                                     {Kind::Value, kFogColour, "Fog colour"},
                                     {Kind::Value, kModelDetail, "Model detail"},
                                     {Kind::Value, kWorldDetail, "World detail"},
                                     {Kind::Value, kModelLimit, "Models at once"},
                                     {Kind::Value, kDrawDistance, "Draw distance"}};
        const Row kControlRows[] = {{Kind::Value, kScheme, "Layout"},          {Kind::Value, kHand, "Stylus hand"},
                                    {Kind::Value, kDuck, "Crouch"},            {Kind::Value, kPadSide, "Pad left/right"},
                                    {Kind::Value, kPadUp, "Pad up/down"},      {Kind::Value, kAutoaim, "Autoaim"}};
        const Row kTouchRows[] = {{Kind::Slider, kAimSpeed, "Aim speed"},
                                  {Kind::Slider, kPitchSpeed, "Up/down speed"},
                                  {Kind::Value, kInvert, "Invert up/down"},
                                  {Kind::Slider, kFirmness, "Firmness"},
                                  {Kind::Value, kFirmPress, "Firm press"}};
        const Row kOtherRows[] = {{Kind::Value, kBob, "View bob"},
                                  {Kind::Value, kReadout, "Debug readout"},
                                  {Kind::Value, kPopups, "Achievement pop-ups"}};
        /// Category `chapter`'s rows (BUTTONS: one an action, made here).
        const Row *settingsRows(int chapter, int &count)
        {
            static Row keyRows[kActionCount];
            switch (chapter)
            {
            case 0: count = static_cast<int>(sizeof(kSoundRows) / sizeof(kSoundRows[0])); return kSoundRows;
            case 1: count = static_cast<int>(sizeof(kGraphicsRows) / sizeof(kGraphicsRows[0])); return kGraphicsRows;
            case 2: count = static_cast<int>(sizeof(kControlRows) / sizeof(kControlRows[0])); return kControlRows;
            case 3: count = static_cast<int>(sizeof(kTouchRows) / sizeof(kTouchRows[0])); return kTouchRows;
            case 4:
                for (int a = 0; a < kActionCount; ++a)
                    keyRows[a] = Row{Kind::Value, kKey, actionName(a)};
                count = kActionCount;
                return keyRows;
            default: count = static_cast<int>(sizeof(kOtherRows) / sizeof(kOtherRows[0])); return kOtherRows;
            }
        }
        // --- PROFILE: the player as the others see them in a match, a sheet
        // of one page whose rows are settings' rows.
        const Row kProfileRows[] = {{Kind::Value, kProfName, "Name"},
                                    {Kind::Value, kProfModel, "Model"},
                                    {Kind::Slider, kProfTop, "Top colour"},
                                    {Kind::Slider, kProfBottom, "Bottom colour"},
                                    {Kind::Value, kProfSpray, "Spray"},
                                    {Kind::Value, kProfSprayColour, "Spray colour"}};
        const char *const kSprayColourName[kSprayColours] = {"orange", "yellow",     "blue",  "light blue", "green",
                                                             "red",    "brown",      "light grey", "dark grey"};
        /// The spray logos (ui/logos.hic), read while PROFILE is open.
        std::uint32_t *logoWords = nullptr;
        LogosView logos;
        void readLogos()
        {
            std::free(logoWords);
            logoWords = nullptr;
            logos = LogosView();
            std::FILE *file = std::fopen(global.commonPath(kLogosFile).c_str(), "rb");
            if (file == nullptr)
                return;
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            // (The size is the card's word: bounded before it is believed.)
            const long most = static_cast<long>(sizeof(IconsHeader) + kLogosMost * (kLogoSize * iconRowWords(kLogoSize) * 4 +
                                                                                    kLogoNameBytes));
            logoWords = size > 0 && size <= most ? static_cast<std::uint32_t *>(std::malloc(static_cast<std::size_t>(size)))
                                                 : nullptr;
            const bool got = logoWords != nullptr &&
                             std::fread(logoWords, 1, static_cast<std::size_t>(size), file) == static_cast<std::size_t>(size);
            std::fclose(file);
            if (!got || !logos.open(reinterpret_cast<const std::uint8_t *>(logoWords), static_cast<std::size_t>(size)))
            {
                std::free(logoWords);
                logoWords = nullptr;
                logos = LogosView();
            }
        }
        void dropLogos()
        {
            std::free(logoWords);
            logoWords = nullptr;
            logos = LogosView();
        }
        /// Where the spray is shown, in the box under PROFILE's list: a block
        /// of cells of the touch screen in colours of their own (its colour).
        constexpr int kSprayCellX = 27, kSprayCellY = 17;
        // (The pages that are sheets of settings' rows, by their numbers:
        // Menu::Page's.)
        constexpr int kPageSettings = 1, kPageHost = 8, kPageLobby = 10, kPageProfile = 17, kPageSend = 18,
                      kPageStarting = 19;
        /// HOST's MATCH: the settings of the match it is to start.
        const Row kHostRows[] = {{Kind::Value, kNetMode, "Mode"},
                                 {Kind::Value, kNetMap, "Map"},
                                 {Kind::Value, kNetFrags, "Frag limit"},
                                 {Kind::Value, kNetMinutes, "Time limit"},
                                 {Kind::Value, kNetMost, "Players"},
                                 {Kind::Value, kNetStay, "Weapons stay"},
                                 {Kind::Value, kNetFall, "Fall damage"},
                                 {Kind::Value, kNetFriendly, "Friendly fire"},
                                 {Kind::Value, kNetLamp, "Flashlight"},
                                 {Kind::Value, kNetAim, "Autoaim"},
                                 {Kind::Value, kNetSkill, "Difficulty"},
                                 {Kind::Value, kNetGuests, "Without the game"}};
#ifndef HLDS_CHILD
        /// The chapters of HOST.
        constexpr int kHostMatch = 0, kHostNext = 1, kHostPlayers = 2;
        /// NEXT MAP's: what follows a match; with Rotation, its list.
        int rotChosen = 0, rotPick = 0;
        const Row *nextRows(int &count)
        {
            static Row rows[8 + kRotationMost];
            static char names[kRotationMost][8];
            count = 0;
            rows[count++] = Row{Kind::Value, kNetNext, "Next map"};
            rows[count++] = Row{Kind::Value, kNetChat, "Scores shown"};
            rows[count++] = Row{Kind::Value, kNetTotals, "Session totals"};
            if (settings.nextMode != kNextRotation)
                return rows;
            rows[count++] = Row{Kind::Value, kRotPick, "Map to add"};
            rows[count++] = Row{Kind::Value, kRotAdd, "Add it"};
            rows[count++] = Row{Kind::Value, kRotUp, "Move up"};
            rows[count++] = Row{Kind::Value, kRotDown, "Move down"};
            rows[count++] = Row{Kind::Value, kRotOut, "Take off"};
            for (int n = 0; n < settings.rotationCount; ++n)
            {
                std::snprintf(names[n], sizeof(names[n]), "%s%d", n == rotChosen ? "> " : "", n + 1);
                rows[count++] = Row{Kind::Value, static_cast<Id>(kRotItem + n), names[n]};
            }
            return rows;
        }
        /// The name of the map with this hash, as the card has it.
        const char *rotName(std::uint32_t hash)
        {
            for (const NetMapInfo &map : netGame.mapInfo())
            {
                if (nameHash(map.name.c_str()) == hash)
                    return map.name.c_str();
            }
            return "(not on this card)";
        }
#else
        constexpr int kHostMatch = 0, kHostPlayers = 1;
#endif
        /// PLAYERS (the host's second chapter, a client's lobby): the team
        /// asked for, in teamplay; then a row for each place the match has.
        const Row *seatRows(int &count)
        {
            static Row rows[kNetPlayers + 1];
            count = 0;
            if (netGame.match().rules.teams())
                rows[count++] = Row{Kind::Value, kNetTeam, "Your team"};
            const int most = netGame.match().rules.most;
            for (int n = 0; n < most && n < kNetPlayers; ++n)
                rows[count++] = Row{Kind::Value, kSeat, ""};
            return rows;
        }
        /// SEND THE GAME: a row for each console that has come for it.
        const Row *sendRows(int &count)
        {
            static Row rows[15];
            count = 0;
#ifndef HLDS_CHILD
            const unsigned here = dlsend::consoles();
            for (int n = 1; n < 16; ++n)
            {
                if ((here >> n) & 1)
                    rows[count++] = Row{Kind::Value, kDlSeat, ""};
            }
#endif
            return rows;
        }
        /// Console `row` of those come: its number with the radio (-1: none).
        int sendConsole(int row)
        {
#ifndef HLDS_CHILD
            const unsigned here = dlsend::consoles();
            for (int n = 1; n < 16; ++n)
            {
                if (((here >> n) & 1) && row-- == 0)
                    return n;
            }
#else
            (void)row;
#endif
            return -1;
        }
        /// STARTING: a row for each player there.
        int startPlayer(int row)
        {
            for (int p = 0; p < kNetPlayers; ++p)
            {
                if (netHere(netGame.match(), p) && row-- == 0)
                    return p;
            }
            return -1;
        }
        const Row *startRows(int &count)
        {
            static Row rows[kNetPlayers];
            count = 0;
            while (count < kNetPlayers && startPlayer(count) >= 0)
                rows[count++] = Row{Kind::Value, kStartSeat, ""};
            return rows;
        }
        /// A sheet's rows: OPTIONS' category `chapter`'s, PROFILE's, HOST's
        /// (MATCH, PLAYERS), the lobby's, SEND THE GAME's, STARTING's.
        const Row *sheetRows(int page, int chapter, int &count)
        {
            if (page == kPageSend)
                return sendRows(count);
            if (page == kPageStarting)
                return startRows(count);
            if (page == kPageProfile)
            {
                count = static_cast<int>(sizeof(kProfileRows) / sizeof(kProfileRows[0]));
                return kProfileRows;
            }
            if (page == kPageHost && chapter == kHostMatch)
            {
                count = static_cast<int>(sizeof(kHostRows) / sizeof(kHostRows[0]));
                return kHostRows;
            }
#ifndef HLDS_CHILD
            if (page == kPageHost && chapter == kHostNext)
                return nextRows(count);
#endif
            if (page == kPageHost || page == kPageLobby)
                return seatRows(count);
            return settingsRows(chapter, count);
        }

        // --- HOST and the lobby: the match's settings (settings v14's), and
        // who is there.

        /// Which of the card's multiplayer maps is hosted: the settings', if
        /// the card has it; else the first (-1: none).
        int hostIndex()
        {
            const std::vector<NetMapInfo> &maps = netGame.mapInfo();
            for (std::size_t i = 0; i < maps.size(); ++i)
            {
                if (maps[i].name == settings.hostMap)
                    return static_cast<int>(i);
            }
            return maps.empty() ? -1 : 0;
        }
        /// HOST's map fits every console sent the game that is here (as they
        /// have said what a map may take of them), or none is.
        bool hostMapFits()
        {
            const int at = hostIndex();
            return !settings.allowGuests || at < 0 ||
                   netMapOffered(netGame.mapInfo()[static_cast<std::size_t>(at)], true, netGame.guestRoom());
        }
        void setHostMap(int index)
        {
            const std::vector<NetMapInfo> &maps = netGame.mapInfo();
            if (index < 0 || index >= static_cast<int>(maps.size()))
                return;
            const std::string &name = maps[static_cast<std::size_t>(index)].name;
            std::memset(settings.hostMap, 0, sizeof(settings.hostMap));
            std::memcpy(settings.hostMap, name.data(),
                        name.size() < sizeof(settings.hostMap) ? name.size() : sizeof(settings.hostMap) - 1);
        }
        /// How many are in the game (the host among them).
        int playersHere()
        {
            int here = 0;
            for (int p = 0; p < kNetPlayers; ++p)
                here += netHere(netGame.match(), p) ? 1 : 0;
            return here;
        }
        /// The player in place `n` of PLAYERS: those there, in turn; then
        /// -1, an open place.
        int seatPlayer(int n)
        {
            for (int p = 0; p < kNetPlayers; ++p)
            {
                if (netHere(netGame.match(), p) && n-- == 0)
                    return p;
            }
            return -1;
        }
        /// The players' swatches back to the touch screen's own colours
        /// (Menu::drawSeat).
        void untintSeats();
        /// Why a row of HOST or of the lobby can do nothing now (null: it
        /// can).
        const char *hostDead(Id id)
        {
            switch (id)
            {
            case kNetFriendly:
                return settings.hostMode == kModeTeamplay ? nullptr : "Only in teamplay: there are no teams.";
            case kNetGuests:
                return netAnyGuestMap(netGame.mapInfo(), netGame.guestRoom())
                           ? nullptr
                           : "No map on this card fits a console without the game.";
            case kNetMap:
                return netGame.mapInfo().empty() ? "No multiplayer map on the card." : nullptr;
            default:
                return nullptr;
            }
        }
        /// Signal bars: four of them, rising, lit up to `bars` (-1: not known,
        /// none drawn).
        void drawBars(Canvas &canvas, int x, int y, int bars)
        {
            if (bars < 0)
                return;
            for (int b = 0; b < 4; ++b)
            {
                const int high = 3 + b * 2;
                canvas.fill(x + b * 3, y + 9 - high, 2, high, b < bars ? kUiOrange + 2 : kUiGrey + 1);
            }
        }

        /// What a setting is now, as its row says it.
        void settingValue(Id id, int action, char *out, std::size_t size)
        {
#ifndef HLDS_CHILD
            if (id >= kRotItem && id <= kRotItemLast)
            {
                std::snprintf(out, size, "%s", rotName(settings.rotation[id - kRotItem]));
                return;
            }
#endif
            switch (id)
            {
            case kNetMode: std::snprintf(out, size, "%s", settings.hostMode == kModeTeamplay ? "teamplay" : "deathmatch"); break;
            case kNetMap:
            {
                const int at = hostIndex();
                std::snprintf(out, size, "%s", at < 0 ? "none" : netGame.mapInfo()[static_cast<std::size_t>(at)].name.c_str());
                break;
            }
            case kNetFrags:
                if (settings.fragLimit == 0)
                    std::snprintf(out, size, "none");
                else
                    std::snprintf(out, size, "%d", settings.fragLimit);
                break;
            case kNetMinutes:
                if (settings.minuteLimit == 0)
                    std::snprintf(out, size, "none");
                else
                    std::snprintf(out, size, "%d minutes", settings.minuteLimit);
                break;
            case kNetMost: std::snprintf(out, size, "up to %d", settings.maxPlayers); break;
#ifndef HLDS_CHILD
            case kNetChat: std::snprintf(out, size, "%d seconds", settings.chatSeconds); break;
            case kNetTotals: std::snprintf(out, size, "%s", settings.sessionTotals ? "on" : "off"); break;
            case kNetNext:
            {
                static const char *const kNames[kNextChoices] = {"host chooses", "rotation", "vote", "winner picks", "take turns"};
                std::snprintf(out, size, "%s", kNames[settings.nextMode]);
                break;
            }
            case kRotPick:
                std::snprintf(out, size, "%s", netGame.mapInfo().empty() ? "none"
                              : netGame.mapInfo()[static_cast<std::size_t>(rotPick) % netGame.mapInfo().size()].name.c_str());
                break;
            case kRotAdd:
            case kRotUp:
            case kRotDown:
            case kRotOut: out[0] = 0; break;
#endif
            case kNetStay: std::snprintf(out, size, "%s", settings.weaponsStay ? "on" : "off"); break;
            case kNetFall: std::snprintf(out, size, "%s", settings.fallDamage != 0 ? "realistic" : "normal"); break;
            case kNetFriendly: std::snprintf(out, size, "%s", settings.friendlyFire ? "on" : "off"); break;
            case kNetLamp: std::snprintf(out, size, "%s", settings.flashlight ? "allowed" : "not allowed"); break;
            case kNetAim: std::snprintf(out, size, "%s", settings.hostAutoaim ? "on" : "off"); break;
            case kNetGuests: std::snprintf(out, size, "%s", settings.allowGuests ? "let in" : "not let in"); break;
            case kNetTeam:
            {
                const NetMatch &match = netGame.match();
                const int team = match.profiles[match.me].team, asked = match.myProfile.asked;
                if (team == 0)
                    std::snprintf(out, size, "...");
                else if (asked != 0 && asked != team)
                    std::snprintf(out, size, "%d (%d is full)", team, asked);
                else
                    std::snprintf(out, size, "%d", team);
                break;
            }
            case kProfName: std::snprintf(out, size, "%s", settings.playerName()); break;
            case kProfModel: std::snprintf(out, size, "%s", kCharacterNames[settings.playerModel % kCharacterCount]); break;
            case kProfSpray:
                std::snprintf(out, size, "%s", logos.count() == 0 ? "none here" : logos.name(settings.spray % logos.count()));
                break;
            case kProfSprayColour: std::snprintf(out, size, "%s", kSprayColourName[settings.sprayColour % kSprayColours]); break;
            case kDsp: std::snprintf(out, size, "%s", kRoomName[settings.roomEffects]); break;
            case kOutput: std::snprintf(out, size, "%s", kOutputName[settings.outputMode]); break;
            case kQuality:
                std::snprintf(out, size, "%s", isDSiMode() ? kQualityName[settings.soundQuality] : "standard (DS)");
                break;
            case kReadout: std::snprintf(out, size, "%s", settings.debugReadout ? "on" : "off"); break;
            case kBob: std::snprintf(out, size, "%s", settings.viewBob ? "on" : "off"); break;
            case kAutoaim: std::snprintf(out, size, "%s", settings.autoaim ? "on" : "off"); break;
            case kPopups: std::snprintf(out, size, "%s", settings.achievementPopups ? "on" : "off"); break;
            case kFog: std::snprintf(out, size, "%s", kFogTypeName[settings.graphics.fog]); break;
            case kFogColour: std::snprintf(out, size, "%s", kFogColourName[settings.graphics.fogColour]); break;
            case kModelDetail: std::snprintf(out, size, "%s", kDetailName[settings.graphics.modelDetail]); break;
            case kWorldDetail: std::snprintf(out, size, "%s", kDetailName[settings.graphics.worldDetail]); break;
            case kModelLimit:
                if (settings.graphics.modelLimit == 0)
                    std::snprintf(out, size, "auto");
                else
                    std::snprintf(out, size, "%d", settings.graphics.modelLimit);
                break;
            case kDrawDistance: std::snprintf(out, size, "%s", kDrawDistanceName[settings.graphics.drawDistance]); break;
            // (Which side of the touch screen its column of buttons is, and
            // which hand has the pad and the trigger.)
            case kHand: std::snprintf(out, size, "%s", settings.leftHanded ? "left" : "right"); break;
            case kScheme:
                std::snprintf(out, size, "%s",
                              settings.controlScheme == kControlsModern
                                  ? "modern"
                                  : (settings.controlScheme == kControlsCustom ? "custom" : "classic"));
                break;
            case kDuck: std::snprintf(out, size, "%s", settings.duckToggle ? "toggle" : "hold"); break;
            case kInvert: std::snprintf(out, size, "%s", settings.invertPitch ? "on" : "off"); break;
            case kFirmPress:
                std::snprintf(out, size, "%s", Input::pressureSeen() ? kFirmName[settings.firmPress % kFirmActions] : "n/a here");
                break;
            case kPadSide: std::snprintf(out, size, "%s", settings.padTurns ? "turn" : "sidestep"); break;
            case kPadUp: std::snprintf(out, size, "%s", settings.padLooks ? "look" : "move"); break;
            case kKey:
            {
                // Its buttons (or the question, while asked).
                std::uint16_t keys[kActionCount];
                keysOf(settings.controls(), keys);
                int n = 0;
                out[0] = '\0';
                for (int bit = 0; bit < 12 && n < static_cast<int>(size) - 1; ++bit)
                {
                    if (keys[action] & (1u << bit))
                        n += std::snprintf(out + n, size - static_cast<std::size_t>(n), "%s%s", n > 0 ? "/" : "",
                                           buttonName(1u << bit));
                }
                if (n == 0)
                    std::snprintf(out, size, "-");
                break;
            }
            default: out[0] = '\0'; break;
            }
        }
        /// What a setting does, for the box under the list.
        const char *settingHelp(Id id)
        {
            switch (id)
            {
            case kDsp:
                return DspAudio::available()
                           ? "The rooms heard: off, as the game has them, or richer from the DSi's sound processor."
                           : "The rooms heard -- a tunnel, a chamber, under water -- as the game has them, or off.";
            case kOutput: return "Speakers: a little narrower, for the console's own. Headphones: all of it. Mono: one.";
            case kQuality:
                return isDSiMode() ? "High: the sound made at the rate the DSi's speakers play it, 47.6 kHz. A little more work."
                                   : "On a DSi: the sound made at the rate its speakers play it.";
            case kMusic: return "How loud the music is.";
            case kEffects: return "How loud the guns, the blows and the world are.";
            case kVoices: return "How loud the people and the monsters speak.";
            case kAmbience: return "How loud the hum and drip of a place is.";
            case kFog: return "Fog, where the view ends: off, only where the map is too big to draw whole, or always.";
            case kFogStrength: return "How thick the fog is.";
            case kFogDistance: return "How near the fog begins.";
            case kFogColour: return "The colour the far distance fades into.";
            case kModelDetail: return "How much of the people and monsters is drawn. Lower is faster.";
            case kWorldDetail: return "How much of the walls is drawn far off. Lower is faster.";
            case kModelLimit: return "How many people and monsters are drawn at once: auto, or a number.";
            case kDrawDistance: return "How far the world is drawn. Nearer is faster.";
            case kScheme: return "Which button does what: classic, modern, or your own (BUTTONS).";
            case kHand: return "Which hand holds the stylus: the touch screen's buttons go to the other side.";
            case kDuck: return "Crouch held down, or pressed once to crouch and again to stand.";
            case kPadSide: return "What the pad's left and right do: step aside, or turn.";
            case kPadUp: return "What the pad's up and down do: walk, or look up and down.";
            case kAutoaim: return "The aim bends towards a monster near it, on EASY and MEDIUM, as the game's does.";
            case kAimSpeed: return "How far the view turns across for a stroke of the stylus.";
            case kPitchSpeed: return "How far the view turns up and down for a stroke of the stylus.";
            case kInvert: return "The stylus moved down looks up.";
            case kFirmness: return "How light a touch of the stylus is ignored.";
            case kFirmPress: return "What pressing the stylus hard does, where the console can tell.";
            case kBob: return "The view bobs with a walk and leans into a step aside, as the game's does.";
            case kReadout: return "Numbers along the top of the touch screen: how fast, where, how much.";
            case kPopups: return "An achievement earned is shown on the top screen as it is. Off, it is still earned.";
            case kKey: return "Touch an action, then press its button. START: leave it as it was.";
#ifdef HLDS_CHILD
            case kProfName: return "The name the others see in a match: this console's own.";
#else
            case kProfName: return "The name the others see in a match. Touch it again to type another.";
#endif
            case kNetMode:
                return "Deathmatch: every one for themselves. Teamplay: two teams, the frags of each added up. Co-op: coming later.";
            case kNetMap: return "Where the match is played. The top screen shows it.";
            case kNetFrags: return "The match ends when a player (in teamplay, a team) has this many frags.";
            case kNetMinutes: return "The match ends after this long.";
            case kNetMost: return "How many may play, this console among them: the rest are told the game is full.";
#ifndef HLDS_CHILD
            case kNetChat: return "How long the final scores stay up when a match ends.";
            case kNetTotals: return "Adds up each player's frags over the matches of this lobby.";
            case kNetNext:
                switch (settings.nextMode)
                {
                case kNextRotation: return "The maps of a list you make, in order, round and round.";
                case kNextVote: return "Everyone votes between maps drawn at random.";
                case kNextWinner: return "The winner chooses the next map.";
                case kNextTurns: return "Each player in turn chooses the next map.";
                default: return "Everyone comes back to this lobby, and you choose.";
                }
            case kRotPick: return "Touch to go through the maps; ADD IT puts this one at the end of the list.";
            case kRotAdd: return "Puts the map above at the end of the list.";
            case kRotUp:
            case kRotDown: return "Moves the map marked > in the list.";
            case kRotOut: return "Takes the map marked > off the list.";
#endif
            case kNetStay: return "A weapon taken stays where it lay for the others; whoever has it cannot take it again.";
            case kNetFall: return "Normal: any hard fall costs 10. Realistic: the harder the fall, the more it costs.";
            case kNetFriendly: return "In teamplay: whether teammates' shots hurt each other.";
            case kNetLamp: return "Whether the suit's flashlight may be lit in the match.";
            case kNetAim: return "Whether the guns' aim bends towards a player near it.";
            case kNetSkill: return "How hard the map's chargers and anything of its own are, on every console.";
            case kNetGuests:
                return "Consoles without Half-Life can join, sent the game: only the maps that fit them are offered.";
            case kNetTeam: return "The team you would be on. The host puts you there if it keeps the teams even.";
            case kProfModel: return "Who the others see you as. A console with no room for it sees the suit, in your colours.";
            case kProfTop: return "The colour of the top of the suit, as the others see it.";
            case kProfBottom: return "The colour of the rest of the suit.";
            case kProfSpray: return "The picture sprayed on a wall in a match: the game's logos, and your own logos/.";
            case kProfSprayColour: return "The colour it is sprayed in.";
            default: return "";
            }
        }
        /// A setting changed by a step (a value to its next or last, a
        /// slider a notch). True if it changed.
        bool settingStep(Id id, int step)
        {
#ifndef HLDS_CHILD
            // (A map of the list: the one MOVE and TAKE OFF are about.)
            if (id >= kRotItem && id <= kRotItemLast)
            {
                rotChosen = id - kRotItem;
                return true;
            }
#endif
            auto cycle = [&](std::uint8_t &value, int count)
            {
                value = static_cast<std::uint8_t>((value + count + step) % count);
            };
            switch (id)
            {
            case kDsp:
            {
                // (Enhanced only where there is a DSP to give it.) At once:
                // the music goes on from where it was.
                const int choices = DspAudio::available() ? hldata::kRoomChoices : hldata::kRoomEnhanced;
                const int at = settings.roomEffects < choices ? settings.roomEffects : hldata::kRoomClassic;
                settings.roomEffects = static_cast<std::uint8_t>((at + choices + step) % choices);
                sound.applySoundMode();
                return true;
            }
            case kOutput:
                cycle(settings.outputMode, hldata::kOutputChoices);
                sound.applySoundMode();
                return true;
            case kQuality:
                if (!isDSiMode())
                    return false;
                cycle(settings.soundQuality, hldata::kQualityChoices);
                sound.applySoundMode();
                return true;
            case kReadout: settings.debugReadout = !settings.debugReadout; return true;
            case kBob: settings.viewBob = !settings.viewBob; return true;
            case kAutoaim: settings.autoaim = !settings.autoaim; return true;
            case kPopups: settings.achievementPopups = !settings.achievementPopups; return true;
            case kFog: cycle(settings.graphics.fog, kFogTypes); return true;
            case kFogColour: cycle(settings.graphics.fogColour, kFogColours); return true;
            case kModelDetail: cycle(settings.graphics.modelDetail, kDetailLevels); return true;
            case kWorldDetail: cycle(settings.graphics.worldDetail, kDetailLevels); return true;
            case kDrawDistance: cycle(settings.graphics.drawDistance, kDrawDistances); return true;
            case kModelLimit:
            {
                // auto, 4, 6, 8, 10, 12, and round again.
                static const std::uint8_t kLimits[] = {0, 4, 6, 8, 10, 12};
                int at = 0;
                for (int i = 0; i < 6; ++i)
                    at = kLimits[i] == settings.graphics.modelLimit ? i : at;
                settings.graphics.modelLimit = kLimits[(at + 6 + step) % 6];
                return true;
            }
            case kHand: settings.leftHanded = !settings.leftHanded; return true;
            case kScheme:
            {
                // (Into CUSTOM with nothing made yet: it starts as the layout
                // it came from.)
                const std::uint8_t next = static_cast<std::uint8_t>((settings.controlScheme + kControlsSchemes + step) % kControlsSchemes);
                bool none = true;
                for (std::uint16_t key : settings.keys)
                    none = none && key == 0;
                if (next == kControlsCustom && none)
                    settings.makeCustom();
                settings.controlScheme = next;
                return true;
            }
            case kPadSide: settings.padTurns = !settings.padTurns; return true;
            case kPadUp: settings.padLooks = !settings.padLooks; return true;
            case kDuck: settings.duckToggle = !settings.duckToggle; return true;
            case kInvert: settings.invertPitch = !settings.invertPitch; return true;
            case kFirmPress:
                if (!Input::pressureSeen())
                    return false;
                cycle(settings.firmPress, kFirmActions);
                return true;
            case kProfSpray:
                if (logos.count() == 0)
                    return false;
                settings.spray = static_cast<std::uint8_t>((settings.spray % logos.count() + logos.count() + step) %
                                                           logos.count());
                return true;
            case kProfSprayColour: cycle(settings.sprayColour, kSprayColours); return true;
            case kProfModel:
                cycle(settings.playerModel, kCharacterCount);
#ifndef HLDS_CHILD
                profileView.open(settings.topColour, settings.bottomColour, settings.playerModel);
#endif
                netGame.sayProfile();
                return true;
            case kNetMode:
                // (Deathmatch and teamplay: co-op is not played yet.)
                settings.hostMode = settings.hostMode == kModeTeamplay ? kModeDeathmatch : kModeTeamplay;
                netGame.hostChanged();
                return true;
            case kNetMap:
            {
                const int next = netMapStep(netGame.mapInfo(), hostIndex(), step, settings.allowGuests, netGame.guestRoom());
                if (next < 0)
                    return false;
                setHostMap(next);
                netGame.hostChanged();
                return true;
            }
            case kNetFrags:
            case kNetMinutes:
            {
                static const std::uint8_t kFrags[] = {0, 5, 10, 20, 30, 50};
                static const std::uint8_t kMinutes[] = {0, 5, 10, 15, 20, 30};
                const std::uint8_t *limits = id == kNetFrags ? kFrags : kMinutes;
                std::uint8_t &value = id == kNetFrags ? settings.fragLimit : settings.minuteLimit;
                int at = 0;
                for (int i = 0; i < 6; ++i)
                    at = limits[i] == value ? i : at;
                value = limits[(at + 6 + step) % 6];
                netGame.hostChanged();
                return true;
            }
            case kNetMost:
            {
                // (Not fewer than are in already.)
                const int here = playersHere();
                int most = settings.maxPlayers + step;
                most = most > kPlayersMost ? kPlayersLeast : (most < kPlayersLeast ? kPlayersMost : most);
                if (most < here)
                    most = step > 0 ? here : kPlayersMost;
                settings.maxPlayers = static_cast<std::uint8_t>(most);
                netGame.hostChanged();
                return true;
            }
#ifndef HLDS_CHILD
            case kNetChat:
            {
                const int next = settings.chatSeconds + step * 5;
                settings.chatSeconds = static_cast<std::uint8_t>(next > kChatMost ? kChatLeast : (next < kChatLeast ? kChatMost : next));
                netGame.hostChanged();
                return true;
            }
            case kNetTotals: settings.sessionTotals = !settings.sessionTotals; netGame.hostChanged(); return true;
            case kNetNext:
                // (Vote, the winner's choice and turns are not built yet.)
                settings.nextMode = settings.nextMode == kNextRotation ? kNextHost : kNextRotation;
                netGame.hostChanged();
                return true;
            case kRotPick:
            {
                const int count = static_cast<int>(netGame.mapInfo().size());
                if (count == 0)
                    return false;
                rotPick = (rotPick + step + count) % count;
                return true;
            }
            case kRotAdd:
                if (netGame.mapInfo().empty())
                    return false;
                return netRotationAdd(settings.rotation, settings.rotationCount, kRotationMost,
                                      nameHash(netGame.mapInfo()[static_cast<std::size_t>(rotPick) % netGame.mapInfo().size()].name.c_str()));
            case kRotUp:
            case kRotDown:
            {
                const int by = id == kRotUp ? -1 : 1;
                if (!netRotationMove(settings.rotation, settings.rotationCount, rotChosen, by))
                    return false;
                rotChosen += by;
                return true;
            }
            case kRotOut:
                if (!netRotationRemove(settings.rotation, settings.rotationCount, rotChosen))
                    return false;
                if (rotChosen >= settings.rotationCount && rotChosen > 0)
                    --rotChosen;
                return true;
#endif
            case kNetStay: settings.weaponsStay = !settings.weaponsStay; netGame.hostChanged(); return true;
            case kNetFall: settings.fallDamage = settings.fallDamage != 0 ? 0 : 1; netGame.hostChanged(); return true;
            case kNetFriendly:
                if (hostDead(id) != nullptr)
                    return false;
                settings.friendlyFire = !settings.friendlyFire;
                netGame.hostChanged();
                return true;
            case kNetLamp: settings.flashlight = !settings.flashlight; netGame.hostChanged(); return true;
            case kNetAim: settings.hostAutoaim = !settings.hostAutoaim; netGame.hostChanged(); return true;
            case kNetGuests:
                if (hostDead(id) != nullptr)
                    return false;
                settings.allowGuests = !settings.allowGuests;
                // (Let in: a map that does not fit them gives way to one that does.)
                if (settings.allowGuests)
                    setHostMap(netMapKept(netGame.mapInfo(), hostIndex(), true, netGame.guestRoom()));
                netGame.hostChanged();
                return true;
            case kNetTeam:
            {
                const int asked = netGame.match().myProfile.asked;
                netGame.askTeam((asked + 3 + step) % 3);
                return true;
            }
            case kProfTop:
            case kProfBottom:
            {
                // (A notch of the pad: a sixteenth of the way round.)
                std::uint8_t &value = slid(id);
                value = static_cast<std::uint8_t>(value + step * 16);
                profileView.setColours(settings.topColour, settings.bottomColour);
                return true;
            }
            case kMusic:
            case kEffects:
            case kVoices:
            case kAmbience:
            case kFogStrength:
            case kFogDistance:
            case kAimSpeed:
            case kPitchSpeed:
            case kFirmness:
            {
                std::uint8_t &value = slid(id);
                const int level = value + step;
                if (level < slidLeast(id) || level > slidMost(id))
                    return false;
                value = static_cast<std::uint8_t>(level);
                if (isVolume(id))
                    sound.applyVolumes();
                return true;
            }
            default:
                return false;
            }
        }

        // --- The saves.

        /// What a save is called in the list.
        void saveName(int slot, char *out, std::size_t size)
        {
            if (slot == kSlotAuto)
                std::snprintf(out, size, "AUTO");
            else if (slot == kSlotQuick)
                std::snprintf(out, size, "QUICK");
            else
                std::snprintf(out, size, "SAVE %u", static_cast<unsigned>(slot - kSlotManual + 1) % 100u);
        }
        /// When it was made: the day and the time if the console's clock
        /// said, else how long had been played.
        void saveWhen(const SaveHeading &h, char *out, std::size_t size)
        {
            if (h.year != 0)
                std::snprintf(out, size, "%02u/%02u %02u:%02u", h.day, h.month, h.hour, h.minute);
            else
                std::snprintf(out, size, "%u:%02u", static_cast<unsigned>(h.playSeconds / 3600),
                              static_cast<unsigned>(h.playSeconds / 60 % 60));
        }
    } // namespace

    void Menu::openSheet(Page page)
    {
        page_ = page;
        sheetScroll_ = 0;
        sheetSelected_ = -1;
        sheetSlide_ = sheetSlideTo_ = 0;
        sheetPressed_ = kSheetNothing;
        sheetDrag_ = 0;
        sheetAsking_ = 0;
        capturing_ = -1;
        if (page == Page::Saves)
            listSaves();
        if (page == Page::Profile)
        {
            readLogos();
            profileView.open(settings.topColour, settings.bottomColour, settings.playerModel);
        }
        // (The touch that opened it is not one here, nor a button still held.)
        sheetHeld_ = keysHeld();
        sheetTouching_ = (sheetHeld_ & KEY_TOUCH) != 0;
        draw();
    }

    /// The saves the sheet lists: to load from, all but the arrival; to
    /// save into, the player's own (after NEW SAVE). The most recent first.
    void Menu::listSaves()
    {
        savesListed_.clear();
        if (saves_ != nullptr)
            saveListed(*saves_, !saving_, savesListed_);
    }

    int &Menu::sheetChapter()
    {
        static int none = 0;
        none = 0;
        return page_ == Page::Achievements ? achChapter_
               : page_ == Page::Settings   ? settingsChapter_
               : page_ == Page::NetHost    ? hostChapter_
                                           : none;
    }

    int Menu::sheetChapters() const
    {
        return page_ == Page::Achievements ? hldata::kAchChapterCount
               : page_ == Page::Settings   ? kSettingsChapters
               : page_ == Page::NetHost    ? kHostPlayers + 1
                                           : 1;
    }

    int Menu::sheetCount(int chapter) const
    {
        if (page_ == Page::Achievements)
            return achCount(chapter);
        if (optionSheet())
        {
            int count = 0;
            sheetRows(static_cast<int>(page_), chapter, count);
            return count;
        }
        return static_cast<int>(savesListed_.size()) + (saving_ ? 1 : 0);
    }

    void Menu::drawSheet()
    {
        drawSheetHeader();
        drawSheetList();
        drawSheetDetail();
        drawSheetBar();
    }

    /// The chapter between its arrows (a sheet of one: its name alone).
    void Menu::drawSheetHeader()
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        canvas.fill(0, 0, 256, kSheetListTop, kUiBack);
        const bool arrows = sheetChapters() > 1;
        if (arrows)
        {
            drawSheetButton(kSheetLeftBox, "<", sheetPressed_ == kSheetLeft);
            drawSheetButton(kSheetRightBox, ">", sheetPressed_ == kSheetRight);
        }
        const int chapter = sheetChapter();
        const char *name = page_ == Page::Achievements ? hldata::kAchChapterNames[chapter]
                           : page_ == Page::Settings   ? kSettingsChapterNames[chapter]
                           : page_ == Page::Profile    ? "PROFILE"
                           : page_ == Page::NetHost    ? (chapter == kHostMatch ? "MATCH" : chapter == kHostPlayers ? "PLAYERS" : "NEXT MAP")
                           : page_ == Page::NetLobby   ? "LOBBY"
                           : page_ == Page::NetSend    ? "SEND THE GAME"
                           : page_ == Page::NetStarting ? "STARTING"
                           : saving_                   ? "SAVE GAME"
                                                       : "LOAD GAME";
        const int left = arrows ? kSheetLeftBox.x + kSheetLeftBox.w + 4 : 4;
        const int room = (arrows ? kSheetRightBox.x - 4 : 252) - left;
        const int wide = textWidth(small, name);
        const int x = wide <= room ? left + (room - wide) / 2 : left;
        drawFitted(canvas, small, x, (kSheetHeadHeight - small.height()) / 2 + 1, name, inkFor(small, kUiOrange), room);
    }

    /// The list: the chapter's rows where the stylus has put them, and,
    /// while it is slid aside, the next or the last chapter's coming in.
    void Menu::drawSheetList()
    {
        // (The players' colours are drawn again where they are now.)
        if (page_ == Page::NetHost || page_ == Page::NetLobby)
            untintSeats();
        const SheetGeometry g = sheetGeometry();
        Canvas band = sheetBand(g);
        band.fill(0, 0, 256, g.listHeight, kUiBack);
        const int chapter = sheetChapter(), chapters = sheetChapters();
        drawSheetRows(chapter, sheetSlide_, sheetScroll_);
        if (sheetSlide_ < 0)
            drawSheetRows((chapter + 1) % chapters, sheetSlide_ + 256, 0);
        else if (sheetSlide_ > 0)
            drawSheetRows((chapter + chapters - 1) % chapters, sheetSlide_ - 256, 0);
        // How far down, when there is more than shows.
        const int whole = sheetCount(chapter) * g.row;
        if (whole > g.listHeight && sheetSlide_ == 0)
        {
            const int most = whole - g.listHeight;
            const int tall = g.listHeight * g.listHeight / whole;
            band.fill(251, sheetScroll_ * (g.listHeight - tall) / most, 3, tall, kUiGrey + 1);
        }
    }

    /// `chapter`'s rows, `x` across and scrolled `scroll` down.
    void Menu::drawSheetRows(int chapter, int x, int scroll)
    {
        const SheetGeometry g = sheetGeometry();
        Canvas band = sheetBand(g);
        const FontView &small = touchCanvas.font(kFontSmall);
        const bool current = chapter == sheetChapter() && x == sheetSlide_;
        int settingCount = 0;
        const Row *rows = optionSheet() ? sheetRows(static_cast<int>(page_), chapter, settingCount) : nullptr;
        for (int n = 0; n < sheetCount(chapter); ++n)
        {
            const int y = n * g.row - scroll;
            if (y + g.row <= 0 || y >= g.listHeight)
                continue;
            const bool chosen = current && n == sheetSelected_;
            const bool pressed = current && n == sheetPressed_ && sheetDrag_ == 0;
            band.button(x + 2, y + 1, kSheetListWidth, g.row - 2, chosen || pressed);
            if (chosen)
                band.frame(x + 2, y + 1, kSheetListWidth, g.row - 2, kUiOrange + 2);
            const int textY = y + (g.row - g.line) / 2;
            if (page_ == Page::Achievements)
            {
                const hldata::AchievementTracker &tracker = achievements.tracker;
                const int id = achOf(chapter, n);
                const bool got = tracker.has(id);
                drawTrophy(band, x + 6, y + (g.row - 12) / 2, got);
                char status[16];
                achStatus(tracker, id, status, sizeof(status));
                drawFitted(band, small, x + 22, textY, achTitle(tracker, id), inkFor(small, got ? kUiWhite : kUiGrey),
                           achTitleRoom(small, status));
                drawText(band, small, x + kSheetListWidth - 2 - textWidth(small, status), textY, status,
                         inkFor(small, got ? kUiOrange : kUiWhite));
            }
            else if (optionSheet())
            {
                const Row &item = rows[n];
                if (item.id == kSeat)
                {
                    drawSeat(band, x, y, textY, n - (rows[0].id == kNetTeam ? 1 : 0), x == 0 && current);
                    continue;
                }
                if (item.id == kStartSeat)
                {
                    // Its name, and how far it has got.
                    const int p = startPlayer(n);
                    char status[24] = "";
                    const int how = p >= 0 ? netGame.startStatus(p, status, sizeof(status)) : 0;
                    drawFitted(band, small, x + 8, textY, p >= 0 ? netGame.match().names[p] : "",
                               inkFor(small, kUiWhite), kSheetListWidth - 90);
                    drawText(band, small, x + kSheetListWidth - 4 - textWidth(small, status), textY, status,
                             inkFor(small, how == 2 ? kUiOrange : kUiGrey));
                    continue;
                }
#ifndef HLDS_CHILD
                if (item.id == kDlSeat)
                {
                    // Its owner's name, and how it stands.
                    const int console = sendConsole(n);
                    char status[16] = "";
                    const char *name = "";
                    if (console > 0)
                        dlsend::describe(console, status, sizeof(status), name);
                    drawFitted(band, small, x + 8, textY, name[0] != 0 ? name : "A console", inkFor(small, kUiWhite),
                               kSheetListWidth - 70);
                    drawText(band, small, x + kSheetListWidth - 4 - textWidth(small, status), textY, status,
                             inkFor(small, std::strcmp(status, "failed") == 0 ? kUiGrey : kUiOrange));
                    continue;
                }
#endif
#ifdef HLDS_CHILD
                const bool nameLocked = item.id == kProfName;
#else
                constexpr bool nameLocked = false;
#endif
                const bool dead = nameLocked || (item.id == kFirmPress && !Input::pressureSeen()) ||
                                  (item.id == kQuality && !isDSiMode()) || 
                                  (item.id == kProfSpray && logos.count() == 0) || hostDead(item.id) != nullptr;
                drawText(band, small, x + 8, textY, item.label, inkFor(small, dead ? kUiGrey : kUiWhite));
                if (item.kind == Kind::Slider)
                {
                    // The track, how far along, and the knob.
                    const int least = slidLeast(item.id), most = slidMost(item.id);
                    const int left = x + kSheetTrackLeft, middle = y + g.row / 2;
                    const int knob = left + kSheetTrackWidth * (slid(item.id) - least) / (most - least);
                    band.fill(left, middle - 3, kSheetTrackWidth, 6, kUiTrack);
                    band.frame(left - 1, middle - 4, kSheetTrackWidth + 2, 8, kUiEdge);
                    if (!isColour(item.id))
                        band.fill(left, middle - 3, knob - left, 6, kUiOrange + (chosen ? 2 : 1));
                    band.fill(knob - 3, y + 3, 7, g.row - 6, chosen ? kUiWhite + 2 : kUiWhite + 1);
                    band.frame(knob - 3, y + 3, 7, g.row - 6, kUiShadow);
                }
                else
                {
                    char value[40];
                    static const char *const kSkillNames[] = {"easy", "medium", "difficult"};
                    if (item.id == kKey && capturing_ == n)
                        std::snprintf(value, sizeof(value), "press a button");
                    else if (item.id == kNetSkill)
                        std::snprintf(value, sizeof(value), "%s", kSkillNames[(skill_ - 1) % 3]);
                    else
                        settingValue(item.id, n, value, sizeof(value));
                    const int room = kSheetListWidth - 16 - textWidth(small, item.label);
                    const int wide = textWidth(small, value) < room ? textWidth(small, value) : room;
                    drawFitted(band, small, x + kSheetListWidth - 4 - wide, textY, value,
                               inkFor(small, dead ? kUiGrey : kUiOrange), room);
                }
            }
            else
            {
                // NEW SAVE; or a save, its name, its chapter and when.
                const int k = n - (saving_ ? 1 : 0);
                if (k < 0)
                {
                    const bool full = saves_ == nullptr || saveFreeManual(*saves_) < 0;
                    drawText(band, small, x + 8, textY, "NEW SAVE", inkFor(small, full ? kUiGrey : kUiOrange));
                    continue;
                }
                const int slot = savesListed_[k];
                const SaveSlot &save = saves_->slots[slot];
                const bool good = save.status == kSaveGood;
                char name[16], when[24];
                saveName(slot, name, sizeof(name));
                if (good)
                    saveWhen(save.heading, when, sizeof(when));
                else
                    std::snprintf(when, sizeof(when), "%s", save.status == kSaveBad ? "damaged" : "other version");
                const int end = drawText(band, small, x + 8, textY, name, inkFor(small, good ? kUiOrange : kUiGrey));
                const int whenWide = textWidth(small, when);
                const char *chapter = save.heading.chapter[0] != 0 ? save.heading.chapter : save.heading.map;
                if (good)
                    drawFitted(band, small, end + 8, textY, chapter, inkFor(small, kUiWhite),
                               x + kSheetListWidth - 10 - whenWide - (end + 8));
                drawText(band, small, x + kSheetListWidth - 4 - whenWide, textY, when, inkFor(small, kUiGrey));
            }
        }
    }

    namespace
    {
        // Where the players' swatches are: cells of the touch screen in
        // colours of their own, a palette bank a place (2.., after the
        // spray's).
        constexpr int kSwatchCellX = 1, kSwatchFirstBank = 2;
        void untintSeats()
        {
            for (int bank = 0; bank < kNetPlayers; ++bank)
                touchCanvas.tint(kSwatchCellX, 0, 2, 24, nullptr, kSwatchFirstBank + bank);
        }
    } // namespace

    /// Place `seat` of PLAYERS, its row `y` down the list's band: a player's
    /// colours, their name, their team and how their link is; or an open
    /// place. `tinted`: the list stands still, and the swatch is drawn in
    /// the player's own colours (cells of their own).
    void Menu::drawSeat(Canvas &band, int x, int y, int textY, int seat, bool tinted)
    {
        const SheetGeometry g = sheetGeometry();
        const FontView &small = touchCanvas.font(kFontSmall);
        const NetMatch &match = netGame.match();
        const int p = seatPlayer(seat);
        if (p < 0)
        {
            drawText(band, small, x + 28, textY, "open", inkFor(small, kUiGrey));
            return;
        }
        // The swatch: the first whole row of cells inside the row.
        const int screenY = kSheetListTop + y;
        const int cellY = (screenY + 2 + 7) / 8;
        // (Not one scrolled up past the list's top: those cells are the header's.)
        if (tinted && cellY >= kSheetListTop / 8 && cellY * 8 + 8 <= screenY + g.row - 1 && cellY < 24)
        {
            const NetProfile &profile = match.profiles[p];
            const int bandY = cellY * 8 - kSheetListTop;
            band.fill(kSwatchCellX * 8, bandY, 8, 8, 1);
            band.fill(kSwatchCellX * 8 + 8, bandY, 8, 8, 2);
            const std::uint16_t three[3] = {hueColour(profile.known ? profile.top : 30),
                                            hueColour(profile.known ? profile.bottom : 6), uiColour(kUiEdge)};
            touchCanvas.tint(kSwatchCellX, cellY, 2, 1, three, kSwatchFirstBank + seat);
        }
        char name[40];
        std::snprintf(name, sizeof(name), "%s%s%s", match.names[p][0] != 0 ? match.names[p] : "...",
                      p == 0 ? " (host)" : "", p == match.me ? " (you)" : "");
        drawFitted(band, small, x + 28, textY, name, inkFor(small, p == match.me ? kUiWhite : kUiOrange), 150);
        if (match.rules.teams() && match.profiles[p].team != 0)
        {
            char team[4];
            std::snprintf(team, sizeof(team), "T%d", match.profiles[p].team);
            drawText(band, small, x + 192, textY, team, inkFor(small, kUiWhite));
        }
        drawBars(band, x + kSheetListWidth - 20, y + (g.row - 10) / 2, p == match.me ? -1 : netGame.bars(p));
    }

    /// The box under the list: the row touched, read out in full; or, with
    /// none, what the page is.
    void Menu::drawSheetDetail()
    {
        const SheetGeometry g = sheetGeometry();
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        canvas.fill(0, g.detailTop - 1, 256, g.detailHeight + 2, kUiBack);
        canvas.frame(2, g.detailTop, 252, g.detailHeight, kUiEdge);
        // (The spray's cells as every cell is, until it is shown in them.)
        touchCanvas.tint(kSprayCellX, kSprayCellY, 4, 4, nullptr);
        constexpr int kLast = 3, kWidth = 244;
        const int x = 7, y = g.detailTop + 2;
        const int chapter = sheetChapter();
        const bool chosen = sheetSelected_ >= 0 && sheetSelected_ < sheetCount(chapter);
        if (page_ == Page::Achievements)
        {
            const hldata::AchievementTracker &tracker = achievements.tracker;
            if (chosen)
            {
                // Its whole title -- if its row had to cut it short; else what
                // it asks has all four lines.
                const int id = achOf(chapter, sheetSelected_);
                const bool got = tracker.has(id);
                const char *title = achTitle(tracker, id);
                char status[16];
                achStatus(tracker, id, status, sizeof(status));
                int first = 0;
                if (textWidth(small, title) > achTitleRoom(small, status))
                {
                    drawFitted(canvas, small, x, y, title, inkFor(small, got ? kUiOrange : kUiWhite), kWidth);
                    first = 1;
                }
                drawWrapped(canvas, small, x, y, g.line, first, kLast, achText(tracker, id),
                            inkFor(small, got ? kUiWhite : kUiGrey), kWidth);
                return;
            }
            int here = 0;
            for (int n = 0; n < achCount(chapter); ++n)
                here += tracker.has(achOf(chapter, n)) ? 1 : 0;
            char earned[48];
            std::snprintf(earned, sizeof(earned), "This chapter: %d of %d - in all: %d of %d", here, achCount(chapter),
                          tracker.earned(), static_cast<int>(hldata::kAchievementCount));
            drawFitted(canvas, small, x, y, earned, inkFor(small, kUiOrange), kWidth);
            drawWrapped(canvas, small, x, y, g.line, 1, kLast,
                        "Touch one to read it. Slide the list, or touch < >, for another chapter.",
                        inkFor(small, kUiGrey), kWidth);
            return;
        }
#ifndef HLDS_CHILD
        if (page_ == Page::NetSend)
        {
            char said[160];
            int here = 0;
            for (unsigned bits = dlsend::consoles(); bits != 0; bits &= bits - 1)
                ++here;
            switch (dlsend::state())
            {
            case dlsend::State::Gathering:
                std::snprintf(said, sizeof(said), "%d console%s here%s. START sends the game to them all at once.",
                              here, here == 1 ? "" : "s", dlsend::full() ? " (the most there is room for)"
                                                                         : "; more can still come");
                break;
            case dlsend::State::Sending:
                // (And what the radio is doing: the rate, and the counts that
                // say why it is not more.)
                std::snprintf(said, sizeof(said), "Sending the game: %d%%. %s", dlsend::percent(), dlsend::rate());
                break;
            case dlsend::State::Starting:
            case dlsend::State::Done:
                std::snprintf(said, sizeof(said), "Starting the game on them. They join this one by themselves.");
                break;
            case dlsend::State::Failed:
                std::snprintf(said, sizeof(said), "It could not be sent. CANCEL, and try again.");
                break;
            default:
                std::snprintf(said, sizeof(said), "On the other consoles, open DS Download Play and choose HALF-LIFE "
                                                  "DS. (A console needs FlashMe, or a Download Play that does not check "
                                                  "signatures.)");
                break;
            }
            drawWrapped(canvas, small, x, y, g.line, 0, kLast, said, inkFor(small, kUiGrey), kWidth);
            return;
        }
#endif
        if (page_ == Page::NetStarting)
        {
            char said[160];
            const char *what = "";
            int consoles = 0, percent = 0;
            if (netGame.sending(what, consoles, percent))
            {
                // (And what the radio is doing: how fast, and why not faster.)
                char radio[64];
                netGame.radioLine(radio, sizeof(radio));
                std::snprintf(said, sizeof(said), "Sending %s to %d console%s: %d%%. %s", what, consoles,
                              consoles == 1 ? "" : "s", percent, radio);
            }
            else
                std::snprintf(said, sizeof(said), "Every console loads the map; the match begins on all of them "
                                                  "together. CANCEL: back to the lobby.");
            drawWrapped(canvas, small, x, y, g.line, 0, kLast, said, inkFor(small, kUiGrey), kWidth);
            return;
        }
        if (page_ == Page::NetHost || page_ == Page::NetLobby)
        {
            const NetMatch &match = netGame.match();
            char said[160];
            const char *what = "";
            int consoles = 0, percent = 0;
            if (!chosen && page_ == Page::NetHost && (sendWhy_[0] != 0 || netGame.sending(what, consoles, percent)))
            {
                // What is being sent to the consoles given the game (or why
                // it could not be given).
                if (sendWhy_[0] != 0)
                    std::snprintf(said, sizeof(said), "%s", sendWhy_);
                else
                    std::snprintf(said, sizeof(said), "Sending %s to %d console%s: %d%%.%s", what, consoles,
                                  consoles == 1 ? "" : "s", percent,
                                  netGame.phase() == NetGame::Phase::Serving ? " The match begins when they have it."
                                                                             : "");
                drawWrapped(canvas, small, x, y, g.line, 0, kLast, said, inkFor(small, kUiWhite), kWidth);
                return;
            }
            if (!chosen)
            {
                if (page_ == Page::NetHost)
                    std::snprintf(said, sizeof(said), "%s",
                                  chapter != kHostPlayers ? "Touch a setting to change it. START when everyone is in; slide the "
                                                 "list, or touch < >, for who is here."
                                  : canSend()  ? "Who is in the game. SEND GAME first gives it to consoles without "
                                                 "it, by DS Download Play; START when everyone is in."
                                               : "Who is in the game, their colours, their team, how good their "
                                                 "link is. START when everyone is in.");
                else
                {
                    // What the host has set up, as its rules say.
                    const NetRules &rules = match.rules;
                    std::snprintf(said, sizeof(said), "%s on %s, up to %d. Weapons %s, falls %s%s, flashlight %s. "
                                                      "Waiting for the host to start.",
                                  rules.teams() ? "Teamplay" : "Deathmatch",
                                  netGame.lobbyMap()[0] != 0 ? netGame.lobbyMap() : "the host's map", rules.most,
                                  rules.has(kRuleWeaponsStay) ? "stay" : "go when taken",
                                  rules.has(kRuleRealisticFall) ? "realistic" : "normal",
                                  rules.teams() ? (rules.has(kRuleFriendlyFire) ? ", friendly fire" : ", no friendly fire")
                                                : "",
                                  rules.has(kRuleFlashlight) ? "allowed" : "not allowed");
                }
                drawWrapped(canvas, small, x, y, g.line, 0, kLast, said, inkFor(small, kUiGrey), kWidth);
                return;
            }
            int count = 0;
            const Row &item = sheetRows(static_cast<int>(page_), chapter, count)[sheetSelected_];
            if (item.id == kSeat)
            {
                const int p = seatPlayer(sheetSelected_ - (sheetRows(static_cast<int>(page_), chapter, count)[0].id == kNetTeam ? 1 : 0));
                if (p < 0)
                {
                    drawWrapped(canvas, small, x, y, g.line, 0, kLast, "An open place: a console looking for a game "
                                "can join.", inkFor(small, kUiGrey), kWidth);
                    return;
                }
                static const char *const kLinks[] = {"lost", "weak", "fair", "good", "strong"};
                const int bars = netGame.bars(p);
                // (A console sent the game: its memory, and what of it is free.)
                char room[56] = "";
                if (const std::uint32_t kb = netGame.roomOf(p))
                {
                    const std::uint32_t free = netGame.freeOf(p);
                    std::snprintf(room, sizeof(room), " %s: %lu.%lu MB, %lu.%lu free.",
                                  netGame.dsiTier(p) ? "DSi mode" : "Sent the game",
                                  static_cast<unsigned long>(kb / 1024), static_cast<unsigned long>(kb % 1024 * 10 / 1024),
                                  static_cast<unsigned long>(free / 1024),
                                  static_cast<unsigned long>(free % 1024 * 10 / 1024));
                }
                if (page_ == Page::NetHost && p > 0)
                    std::strncat(room, " Touch again to kick.", sizeof(room) - std::strlen(room) - 1);
                std::snprintf(said, sizeof(said), "%s: %d frags, %d deaths.%s%s%s", match.names[p][0] ? match.names[p] : "...",
                              match.frags[p], match.deaths[p], bars >= 0 && p != match.me ? " Link: " : "",
                              bars >= 0 && p != match.me ? kLinks[bars > 4 ? 4 : bars] : "", room);
                drawWrapped(canvas, small, x, y, g.line, 0, kLast, said, inkFor(small, kUiWhite), kWidth);
                return;
            }
            const char *why = hostDead(item.id);
            drawWrapped(canvas, small, x, y, g.line, 0, kLast, why != nullptr ? why : settingHelp(item.id),
                        inkFor(small, why != nullptr ? kUiGrey : kUiWhite), kWidth);
            return;
        }
        if (page_ == Page::Profile)
        {
            if (!chosen)
            {
                drawWrapped(canvas, small, x, y, g.line, 0, kLast,
                            "How the others see you in a match: touch a row to change it, drag a colour. The top "
                            "screen shows you in your colours.",
                            inkFor(small, kUiGrey), kWidth);
                return;
            }
            int count = 0;
            const Row &item = sheetRows(kPageProfile, 0, count)[sheetSelected_];
            const bool spray = (item.id == kProfSpray || item.id == kProfSprayColour) && logos.count() > 0;
            drawWrapped(canvas, small, x, y, g.line, 0, kLast, settingHelp(item.id), inkFor(small, kUiWhite),
                        spray ? kSprayCellX * 8 - x - 6 : kWidth);
            if (spray)
            {
                // The logo, in its colour: its three levels of paint the
                // block's own three colours, over the screen's.
                const std::uint8_t *paint = kSprayColour[settings.sprayColour % kSprayColours];
                const std::uint16_t back = uiColour(kUiBack);
                std::uint16_t three[3];
                for (int level = 1; level <= 3; ++level)
                {
                    int rgb[3];
                    for (int c = 0; c < 3; ++c)
                    {
                        const int under = (back >> (c * 5)) & 31;
                        rgb[c] = under + ((paint[c] >> 3) - under) * level / 3;
                    }
                    three[level - 1] = static_cast<std::uint16_t>(rgb[0] | rgb[1] << 5 | rgb[2] << 10);
                }
                const int logo = settings.spray % logos.count();
                for (int py = 0; py < kLogoSize; ++py)
                {
                    for (int px = 0; px < kLogoSize; ++px)
                        canvas.pixel(kSprayCellX * 8 + px, kSprayCellY * 8 + py, logos.level(logo, px, py));
                }
                touchCanvas.tint(kSprayCellX, kSprayCellY, 4, 4, three);
            }
            return;
        }
        if (page_ == Page::Settings)
        {
            if (capturing_ >= 0)
            {
                drawWrapped(canvas, small, x, y, g.line, 0, kLast, "Press its button now. START: leave it as it was.",
                            inkFor(small, kUiOrange), kWidth);
                return;
            }
            if (!chosen)
            {
                drawWrapped(canvas, small, x, y, g.line, 0, kLast,
                            "Touch a setting to change it, or to read what it does; drag a slider. Slide the list, or "
                            "touch < >, for the other settings. RESET: this page's as they were at first.",
                            inkFor(small, kUiGrey), kWidth);
                return;
            }
            int count = 0;
            const Row &item = settingsRows(chapter, count)[sheetSelected_];
            const char *help = item.id == kFirmPress && !Input::pressureSeen()
                                   ? "This console does not say how hard the stylus presses: not used here."
                                   : settingHelp(item.id);
            drawWrapped(canvas, small, x, y, g.line, 0, item.id == kScheme ? 1 : kLast, help, inkFor(small, kUiWhite), kWidth);
            // (The layout: what the buttons then do, as much as there is room for.)
            if (item.id == kScheme)
            {
                for (int line = 0; line + 2 <= kLast && controlsLegend(settings.controls(), line) != nullptr; ++line)
                    drawFitted(canvas, small, x, y + (line + 2) * g.line, controlsLegend(settings.controls(), line),
                               inkFor(small, kUiGrey), kWidth);
            }
            return;
        }
        // The saves.
        if (!chosen)
        {
            drawWrapped(canvas, small, x, y, g.line, 0, kLast,
                        saving_ ? "Touch NEW SAVE, or a save to put this game in its place."
                                : (savesListed_.empty() ? "There are no saves yet." : "Touch a save, then LOAD."),
                        inkFor(small, kUiGrey), kWidth);
            return;
        }
        const int k = sheetSelected_ - (saving_ ? 1 : 0);
        if (k < 0)
        {
            const bool full = saves_ == nullptr || saveFreeManual(*saves_) < 0;
            drawWrapped(canvas, small, x, y, g.line, 0, kLast,
                        full ? "99 saves: there is no room for another. Put it in the place of one, or delete one."
                             : "A new save of the game as it is now, kept beside the others.",
                        inkFor(small, full ? kUiOrange : kUiWhite), kWidth);
            return;
        }
        const int slot = savesListed_[k];
        const SaveSlot &save = saves_->slots[slot];
        if (save.status != kSaveGood)
        {
            drawWrapped(canvas, small, x, y, g.line, 0, kLast, saveLine(save).c_str(), inkFor(small, kUiGrey), kWidth);
            return;
        }
        const SaveHeading &h = save.heading;
        char line[64], when[24];
        drawFitted(canvas, small, x, y, h.chapter[0] != 0 ? h.chapter : h.map, inkFor(small, kUiOrange), kWidth);
        std::snprintf(line, sizeof(line), "%s - health %d", h.map, static_cast<int>(h.health));
        drawFitted(canvas, small, x, y + g.line, line, inkFor(small, kUiWhite), kWidth);
        if (h.year != 0)
        {
            std::snprintf(when, sizeof(when), "%02u/%02u/%04u %02u:%02u", h.day, h.month, h.year, h.hour, h.minute);
            drawFitted(canvas, small, x, y + 2 * g.line, when, inkFor(small, kUiGrey), kWidth);
        }
        std::snprintf(line, sizeof(line), "played %u:%02u", static_cast<unsigned>(h.playSeconds / 3600),
                      static_cast<unsigned>(h.playSeconds / 60 % 60));
        drawFitted(canvas, small, x, y + (h.year != 0 ? 3 : 2) * g.line, line, inkFor(small, kUiGrey), kWidth);
    }

    /// The buttons along the bottom; or, while something is asked, the
    /// question and YES, NO.
    void Menu::drawSheetBar()
    {
        Canvas &canvas = touchCanvas.canvas();
        canvas.fill(0, kSheetBarTop, 256, 192 - kSheetBarTop, kUiBack);
        if (sheetAsking_ != 0)
        {
            const FontView &small = touchCanvas.font(kFontSmall);
            drawFitted(canvas, small, 6, kSheetBarTop + (kSheetBarHeight - small.height()) / 2, sheetQuestion_,
                       inkFor(small, kUiOrange), kSheetYesBox.x - 10);
            drawSheetButton(kSheetYesBox, "YES", sheetPressed_ == kSheetYes);
            drawSheetButton(kSheetNoBox, "NO", sheetPressed_ == kSheetNo);
            return;
        }
        if (page_ == Page::Achievements)
        {
            drawSheetButton(sheetBarBox(0, 2), settings.achievementPopups ? "POP-UPS: ON" : "POP-UPS: OFF",
                            sheetPressed_ == kSheetBar);
            drawSheetButton(sheetBarBox(1, 2), "BACK", sheetPressed_ == kSheetBar - 1);
            return;
        }
        if (page_ == Page::NetHost)
        {
            // (START waits while a match's map is being sent; SEND GAME is
            // before anyone has come: the host's game is closed meanwhile.)
            const int buttons = sheetButtons();
            drawSheetButton(sheetBarBox(0, buttons), "START", sheetPressed_ == kSheetBar,
                            hostIndex() < 0 || netGame.phase() == NetGame::Phase::Serving || !hostMapFits());
            if (buttons == 3)
                drawSheetButton(sheetBarBox(1, 3), "SEND GAME", sheetPressed_ == kSheetBar - 1, playersHere() > 1);
            drawSheetButton(sheetBarBox(buttons - 1, buttons), "BACK", sheetPressed_ == kSheetBar - (buttons - 1));
            return;
        }
        if (page_ == Page::NetStarting)
        {
            drawSheetButton(sheetBarBox(0, 1), "CANCEL", sheetPressed_ == kSheetBar);
            return;
        }
#ifndef HLDS_CHILD
        if (page_ == Page::NetSend)
        {
            // (START once a console has come, and before it is being sent.)
            drawSheetButton(sheetBarBox(0, 2), "START", sheetPressed_ == kSheetBar,
                            dlsend::state() != dlsend::State::Gathering);
            drawSheetButton(sheetBarBox(1, 2), "CANCEL", sheetPressed_ == kSheetBar - 1);
            return;
        }
#endif
        if (page_ == Page::NetLobby)
        {
            // (PROFILE and OPTIONS without leaving: a console that was sent
            // the game has nowhere else to set them.)
            drawSheetButton(sheetBarBox(0, 3), "PROFILE", sheetPressed_ == kSheetBar);
            drawSheetButton(sheetBarBox(1, 3), "OPTIONS", sheetPressed_ == kSheetBar - 1);
            drawSheetButton(sheetBarBox(2, 3), "LEAVE", sheetPressed_ == kSheetBar - 2);
            return;
        }
        if (optionSheet())
        {
            drawSheetButton(sheetBarBox(0, 2), "RESET", sheetPressed_ == kSheetBar);
            drawSheetButton(sheetBarBox(1, 2), "BACK", sheetPressed_ == kSheetBar - 1);
            return;
        }
        // The saves: SAVE or LOAD the one touched; DELETE it, if it is the
        // player's; BACK.
        const int k = sheetSelected_ - (saving_ ? 1 : 0);
        const int slot = sheetSelected_ >= 0 && k >= 0 && k < static_cast<int>(savesListed_.size()) ? savesListed_[k] : -1;
        const bool newSave = saving_ && sheetSelected_ == 0;
        const bool can = newSave ? saves_ != nullptr && saveFreeManual(*saves_) >= 0
                                 : slot >= 0 && (saving_ || saves_->slots[slot].status == kSaveGood);
        drawSheetButton(sheetBarBox(0, 3), saving_ ? "SAVE" : "LOAD", sheetPressed_ == kSheetBar, !can);
        drawSheetButton(sheetBarBox(1, 3), "DELETE", sheetPressed_ == kSheetBar - 1, slot < kSlotManual);
        drawSheetButton(sheetBarBox(2, 3), "BACK", sheetPressed_ == kSheetBar - 2);
    }

    int Menu::sheetHit(int x, int y) const
    {
        if (sheetAsking_ != 0)
            return kSheetYesBox.has(x, y) ? kSheetYes : (kSheetNoBox.has(x, y) ? kSheetNo : kSheetNothing);
        if (sheetChapters() > 1)
        {
            if (kSheetLeftBox.has(x, y))
                return kSheetLeft;
            if (kSheetRightBox.has(x, y))
                return kSheetRight;
        }
        const int buttons = sheetButtons();
        for (int b = 0; b < buttons; ++b)
        {
            if (sheetBarBox(b, buttons).has(x, y))
                return kSheetBar - b;
        }
        const SheetGeometry g = sheetGeometry();
        if (y < kSheetListTop || y >= kSheetListTop + g.listHeight)
            return kSheetNothing;
        const int n = (y - kSheetListTop + sheetScroll_) / g.row;
        int chapter = const_cast<Menu *>(this)->sheetChapter();
        return n < sheetCount(chapter) && x >= 2 && x < 2 + kSheetListWidth ? n : kSheetList;
    }

    /// The next chapter (1) or the last (-1), slid in.
    void Menu::sheetTurn(int step)
    {
        if (sheetChapters() < 2)
            return;
        sound.ui(kUiSoundPress);
        sheetSlideTo_ = step > 0 ? -256 : 256;
    }

    /// Row `row` read out (-1: none), and kept in sight.
    void Menu::sheetSelect(int row)
    {
        const SheetGeometry g = sheetGeometry();
        sheetSelected_ = row;
        if (row >= 0)
        {
            if (row * g.row < sheetScroll_)
                sheetScroll_ = row * g.row;
            if ((row + 1) * g.row > sheetScroll_ + g.listHeight)
                sheetScroll_ = (row + 1) * g.row - g.listHeight;
        }
        const int over = sheetCount(sheetChapter()) * g.row - g.listHeight;
        const int most = over > 0 ? over : 0;
        sheetScroll_ = sheetScroll_ < 0 ? 0 : (sheetScroll_ > most ? most : sheetScroll_);
        drawSheetList();
        drawSheetDetail();
        drawSheetBar();
    }

    /// A setting's row stepped: its value to the next (or the last), its
    /// slider a notch. True if it changed.
    bool Menu::sheetAdjust(int row, int step)
    {
        if (!optionSheet() || row < 0)
            return false;
        int count = 0;
        const Row *rows = sheetRows(static_cast<int>(page_), sheetChapter(), count);
        if (row >= count)
            return false;
        // (The name: typed, by sheetAct, not stepped.)
        if (rows[row].id == kProfName)
            return false;
        if (rows[row].id == kKey)
        {
            // Asked for: the next button pressed is this action's.
            capturing_ = row;
            captureHeld_ = keysHeld() & 0xFFFu;
            drawSheetList();
            drawSheetDetail();
            return true;
        }
        // (HOST's difficulty: the menu's own, as NEW GAME's is.)
        if (rows[row].id == kNetSkill)
        {
            skill_ = (skill_ - 1 + 3 + step) % 3 + 1;
            sound.ui(kUiSoundPress);
            drawSheetList();
            drawSheetDetail();
            return true;
        }
        if (!settingStep(rows[row].id, step))
            return false;
        sound.ui(rows[row].kind == Kind::Slider ? kUiSoundTick : kUiSoundPress);
        drawSheetList();
        drawSheetDetail();
        return true;
    }

    /// A slider of a setting's row put where the stylus is (`x` on the
    /// screen). True if it moved.
    bool Menu::sheetSlider(int row, int x)
    {
        int count = 0;
        const Row *rows = sheetRows(static_cast<int>(page_), sheetChapter(), count);
        if (row < 0 || row >= count || rows[row].kind != Kind::Slider)
            return false;
        const Id id = rows[row].id;
        const int least = slidLeast(id), most = slidMost(id);
        const int left = 2 + kSheetTrackLeft;
        int level = ((x - left) * (most - least) + kSheetTrackWidth / 2) / kSheetTrackWidth + least;
        level = x < left ? least : (level > most ? most : level);
        std::uint8_t &value = slid(id);
        if (value == level)
            return false;
        value = static_cast<std::uint8_t>(level);
        if (isVolume(id))
            sound.applyVolumes();
        if (isColour(id))
            profileView.setColours(settings.topColour, settings.bottomColour);
        drawSheetList();
        return true;
    }

    int Menu::sheetSliderLevel(int row)
    {
        int count = 0;
        const Row *rows = sheetRows(static_cast<int>(page_), sheetChapter(), count);
        if (row < 0 || row >= count || rows[row].kind != Kind::Slider)
            return -1;
        return slid(rows[row].id);
    }

    void Menu::sheetAsk(int what, const char *question)
    {
        sheetAsking_ = what;
        std::snprintf(sheetQuestion_, sizeof(sheetQuestion_), "%s", question);
        drawSheetBar();
    }

    /// YES to what was asked.
    Menu::Action Menu::sheetYes()
    {
        const int what = sheetAsking_;
        sheetAsking_ = 0;
        switch (what)
        {
        case kAskReset:
        {
            // This page's settings as they were at first.
            settings.reset(page_ == Page::Profile ? Settings::kProfileCategory : settingsChapter_);
            profileView.setColours(settings.topColour, settings.bottomColour);
            if (settingsChapter_ == 0)
            {
                sound.applySoundMode();
                sound.applyVolumes();
            }
            capturing_ = -1;
            settings.save();
            drawSheet();
            return Action::None;
        }
        case kAskOverwrite:
            return Action::SaveTo;
        case kAskLoad:
            return Action::Load;
        case kAskLeave:
            return sheetLeave();
        case kAskKick:
            netGame.kick(kickWho);
            drawSheet();
            return Action::None;
        case kAskSend:
            netGame.sayBackSoon();
            sendBegin();
            return Action::None;
        case kAskDelete:
            SaveGame::remove(slot_);
            listSaves();
            sheetSelected_ = -1;
            drawSheet();
            return Action::None;
        default:
            drawSheetBar();
            return Action::None;
        }
    }

    /// A seat of HOST's PLAYERS, chosen again: its player let go, if asked.
    void Menu::askKick(int row)
    {
        int count = 0;
        const int chapter = sheetChapter();
        const Row *rows = sheetRows(static_cast<int>(page_), chapter, count);
        const int p = seatPlayer(row - (rows[0].id == kNetTeam ? 1 : 0));
        if (page_ != Page::NetHost || p <= 0)
            return;
        kickWho = p;
        static char question[32];
        std::snprintf(question, sizeof(question), "KICK %s?", netGame.match().names[p]);
        sheetAsk(kAskKick, question);
    }

    /// PROFILE's name typed on the keyboard; empty, the console's owner's
    /// again.
    void Menu::typeName()
    {
        touchCanvas.tint(kSprayCellX, kSprayCellY, 4, 4, nullptr);
        std::string name = settings.name;
        if (typeLine(name, kProfileNameBytes - 1, "YOUR NAME"))
        {
            std::memset(settings.name, 0, sizeof(settings.name));
            std::memcpy(settings.name, name.data(), name.size() < kProfileNameBytes ? name.size() : kProfileNameBytes - 1);
            settings.save();
        }
        // (The touch or the button that ended it is not one here.)
        sheetHeld_ = keysHeld();
        sheetTouching_ = (sheetHeld_ & KEY_TOUCH) != 0;
        sheetPressed_ = kSheetNothing;
    }

    /// A row touched a second time (or SAVE, LOAD): done.
    Menu::Action Menu::sheetAct(int row)
    {
        if (optionSheet())
        {
            int count = 0;
            const Row *rows = sheetRows(static_cast<int>(page_), sheetChapter(), count);
            if (row >= 0 && row < count && rows[row].id == kSeat)
                askKick(row);
            else if (row >= 0 && row < count && rows[row].id == kProfName)
            {
#ifndef HLDS_CHILD
                typeName();
#endif
            }
            else
                sheetAdjust(row, 1);
            return Action::None;
        }
        if (page_ != Page::Saves || row < 0)
            return Action::None;
        const int k = row - (saving_ ? 1 : 0);
        if (k < 0)
        {
            // NEW SAVE: in the first place there is room in.
            const int free = saves_ != nullptr ? saveFreeManual(*saves_) : -1;
            if (free < 0)
                return Action::None;
            slot_ = free;
            return Action::SaveTo;
        }
        if (k >= static_cast<int>(savesListed_.size()))
            return Action::None;
        slot_ = savesListed_[k];
        char name[16], question[40];
        saveName(slot_, name, sizeof(name));
        if (saving_)
        {
            std::snprintf(question, sizeof(question), "OVERWRITE %s?", name);
            sheetAsk(kAskOverwrite, question);
            return Action::None;
        }
        if (saves_->slots[slot_].status != kSaveGood)
            return Action::None;
        if (title_)
            return Action::Load;
        sheetAsk(kAskLoad, "LOAD? UNSAVED IS LOST");
        return Action::None;
    }

    Menu::Action Menu::sheetBarPress(int button)
    {
        if (page_ == Page::Achievements)
        {
            if (button == 0)
            {
                settings.achievementPopups = !settings.achievementPopups;
                drawSheetBar();
                return Action::None;
            }
            return sheetLeave();
        }
        if (page_ == Page::NetHost && button == 0)
        {
            // START: the match, on every console. (Whoever runs the screens
            // loads the map when the match says it has begun: main.cpp. A
            // host alone may look round a map, too -- though nobody can come
            // once it has begun.)
            const int at = hostIndex();
            if (at < 0 || netGame.phase() == NetGame::Phase::Serving || !hostMapFits())
                return Action::None;
            settings.save();
            untintSeats();
            netGame.start(netGame.maps()[static_cast<std::size_t>(at)], settings.fragLimit, settings.minuteLimit, skill_);
            netShown_ = 0;
            openSheet(Page::NetStarting);
            return Action::None;
        }
        if (page_ == Page::NetHost && button == 1 && sheetButtons() == 3)
        {
            if (playersHere() <= 1)
                sendBegin();
            else
                sheetAsk(kAskSend, "ALL OUT FOR A MOMENT: SEND?");
            return Action::None;
        }
#ifndef HLDS_CHILD
        if (page_ == Page::NetSend && button == 0)
        {
            sound.ui(kUiSoundPress);
            dlsend::go();
            drawSheetBar();
            drawSheetDetail();
            return Action::None;
        }
#endif
        if (page_ == Page::NetLobby && button < 2)
        {
            sound.ui(kUiSoundPress);
            untintSeats();
            mapCard.hide();
            sheetFrom_ = Page::NetLobby;
            openSheet(button == 0 ? Page::Profile : Page::Settings);
            return Action::None;
        }
        if (page_ == Page::NetHost || page_ == Page::NetLobby || page_ == Page::NetSend)
            return sheetBack();
        if (optionSheet())
        {
            if (button == 0)
            {
                char question[40];
                std::snprintf(question, sizeof(question), "RESET %s?",
                              page_ == Page::Profile ? "PROFILE" : kSettingsChapterNames[settingsChapter_]);
                sheetAsk(kAskReset, question);
                return Action::None;
            }
            return sheetLeave();
        }
        if (button == 0)
            return sheetAct(sheetSelected_);
        if (button == 1)
        {
            const int k = sheetSelected_ - (saving_ ? 1 : 0);
            if (sheetSelected_ < 0 || k < 0 || k >= static_cast<int>(savesListed_.size()) ||
                savesListed_[k] < kSlotManual)
                return Action::None;
            slot_ = savesListed_[k];
            char name[16], question[40];
            saveName(slot_, name, sizeof(name));
            std::snprintf(question, sizeof(question), "DELETE %s?", name);
            sheetAsk(kAskDelete, question);
            return Action::None;
        }
        return sheetLeave();
    }

    Menu::Action Menu::sheetLeave()
    {
        sound.ui(kUiSoundPress);
        // (What was changed on HOST is kept as it is left.)
        if (page_ == Page::NetHost)
            settings.save();
        capturing_ = -1;
        sheetAsking_ = 0;
        if (page_ == Page::Profile)
        {
            profileView.close();
            dropLogos();
            touchCanvas.tint(kSprayCellX, kSprayCellY, 4, 4, nullptr);
        }
        // OPTIONS and PROFILE opened from the lobby: back to it, the link
        // kept all the while -- and the others told of the profile as it is.
        const Page from = sheetFrom_;
        sheetFrom_ = Page::Main;
        if ((page_ == Page::Settings || page_ == Page::Profile) && from == Page::NetLobby)
        {
            if (page_ == Page::Profile)
                netGame.sayProfile();
            settings.save();
            openSheet(Page::NetLobby);
            return Action::None;
        }
        // (Out of the game: the others are told, the radio is silent.)
        const bool lobby = page_ == Page::NetHost || page_ == Page::NetLobby;
        if (lobby)
        {
            netGame.leave();
            untintSeats();
            mapCard.hide();
        }
        page_ = page_ == Page::Saves                  ? asker_
                : page_ == Page::Profile || lobby ? Page::Multiplayer
                : page_ == Page::Settings && from == Page::Multiplayer ? Page::Multiplayer
                : title_                 ? Page::Title
                : netGame.playing()      ? Page::NetPause
                                         : Page::Main;
        settings.save();
        active_ = -1;
        touching_ = (keysHeld() & KEY_TOUCH) != 0;
        draw();
        return Action::None;
    }

    void Menu::openHost()
    {
        if (netGame.host())
        {
            hostChapter_ = 0;
            openSheet(Page::NetHost);
            return;
        }
        char why[48];
        std::snprintf(why, sizeof(why), "The wireless would not start (%d)", netlinkFailStage());
        page_ = Page::Multiplayer;
        draw();
        showReason(why);
    }

    bool Menu::canSend() const
    {
#ifndef HLDS_CHILD
        return settings.allowGuests && dlsend::available();
#else
        return false;
#endif
    }

    void Menu::sendBegin()
    {
#ifndef HLDS_CHILD
        sound.ui(kUiSoundPress);
        sendWhy_[0] = 0;
        // (The host's game closed while the radio offers the program: there
        // is nobody in it yet. And the title's moving picture put away, as
        // for a map: a DS has not the memory for it and the program both --
        // its pictures are read out of a megabyte.)
        netGame.leave();
        hudDraw.hideMenu();
        const int at = hostIndex();
        char description[64];
        std::snprintf(description, sizeof(description), "%s on %s",
                      settings.hostMode == kModeTeamplay ? "Teamplay" : "Deathmatch",
                      at >= 0 ? netGame.mapInfo()[static_cast<std::size_t>(at)].name.c_str() : "the host's map");
        std::string why;
        if (!dlsend::begin(description, settings.maxPlayers, why))
        {
            std::snprintf(sendWhy_, sizeof(sendWhy_), "%s", why.c_str());
            hudDraw.showMenu();
            netGame.host();
            draw();
            return;
        }
        netShown_ = 0;
        openSheet(Page::NetSend);
#endif
    }

    void Menu::sendEnd()
    {
#ifndef HLDS_CHILD
        dlsend::end();
        hudDraw.showMenu();
        if (!netGame.host())
            std::snprintf(sendWhy_, sizeof(sendWhy_), "The wireless would not start again.");
        // (Who comes next is what the host will want to see.)
        hostChapter_ = kHostPlayers;
        openSheet(Page::NetHost);
#endif
    }

    Menu::Action Menu::sheetBack()
    {
        // (STARTING: given up, everyone back to the lobby.)
        if (page_ == Page::NetStarting)
        {
            sound.ui(kUiSoundPress);
            netGame.cancelStart();
            hostChapter_ = kHostPlayers;
            openSheet(Page::NetHost);
            return Action::None;
        }
        // (SEND THE GAME: given up, and the host's game again.)
        if (page_ == Page::NetSend)
        {
            sound.ui(kUiSoundPress);
            sendEnd();
            return Action::None;
        }
        // (The host, with others in its game: they are let go too, so asked.)
        if (page_ == Page::NetHost && playersHere() > 1 && sheetAsking_ == 0)
        {
            sound.ui(kUiSoundPress);
            sheetAsk(kAskLeave, "LEAVE THE GAME?");
            return Action::None;
        }
        return sheetLeave();
    }

    Menu::Action Menu::sheetFrame()
    {
        const SheetGeometry g = sheetGeometry();
        const std::uint32_t held = keysHeld();
        const std::uint32_t pressed = held & ~sheetHeld_;
        sheetHeld_ = held;

        // A button being asked for (BUTTONS): the next one pressed, START
        // to leave it as it was.
        if (capturing_ >= 0)
        {
            const std::uint32_t fresh = (held & 0xFFFu) & ~captureHeld_;
            captureHeld_ = held & 0xFFFu;
            if (fresh & KEY_START)
            {
                capturing_ = -1;
                drawSheetList();
                drawSheetDetail();
            }
            else if (fresh & kAssignable)
            {
                std::uint32_t button = 1;
                while (!(fresh & kAssignable & button))
                    button <<= 1;
                settings.makeCustom();
                assignKey(settings.keys, capturing_, button);
                capturing_ = -1;
                sound.ui(kUiSoundTick);
                drawSheetList();
                drawSheetDetail();
            }
            sheetTouching_ = (held & KEY_TOUCH) != 0;
            return Action::None;
        }

        // The buttons: L and R another chapter (left and right too, where
        // they change nothing else); up and down the row read out; A to do
        // it; B back (or NO).
        if (sheetDrag_ == 0 && sheetSlide_ == 0 && sheetSlideTo_ == 0 && !sheetTouching_)
        {
            const bool setting = optionSheet() && sheetSelected_ >= 0;
            if (sheetAsking_ != 0)
            {
                if (pressed & KEY_A)
                    return sheetYes();
                if (pressed & KEY_B)
                {
                    sheetAsking_ = 0;
                    drawSheetBar();
                }
            }
            else if (pressed & KEY_R)
                sheetTurn(1);
            else if (pressed & KEY_L)
                sheetTurn(-1);
            else if (pressed & (KEY_RIGHT | KEY_LEFT))
            {
                const int step = (pressed & KEY_RIGHT) ? 1 : -1;
                if (!(setting && sheetAdjust(sheetSelected_, step)))
                    sheetTurn(step);
            }
            else if (pressed & (KEY_UP | KEY_DOWN))
            {
                const int last = sheetCount(sheetChapter()) - 1;
                if (last >= 0)
                {
                    int row = sheetSelected_ < 0 ? 0 : sheetSelected_ + ((pressed & KEY_DOWN) ? 1 : -1);
                    row = row < 0 ? 0 : (row > last ? last : row);
                    sound.ui(kUiSoundTick);
                    sheetSelect(row);
                }
            }
            else if (pressed & KEY_A)
            {
                if (sheetSelected_ >= 0)
                    return sheetAct(sheetSelected_);
            }
            else if (pressed & KEY_B)
            {
                return sheetBack();
            }
        }

        // A slide let go of (or asked for) finishes by itself: quickly,
        // then slowing; past the edge, the next chapter is the one shown.
        if (sheetDrag_ != 2 && sheetSlide_ != sheetSlideTo_)
        {
            const int left = sheetSlideTo_ - sheetSlide_;
            int step = left / 3;
            if (step > -8 && step < 8)
                step = left > 0 ? (left < 8 ? left : 8) : (left > -8 ? left : -8);
            sheetSlide_ += step;
            if (sheetSlide_ == sheetSlideTo_ && sheetSlideTo_ != 0)
            {
                const int chapters = sheetChapters();
                int &chapter = sheetChapter();
                chapter = (chapter + (sheetSlideTo_ < 0 ? 1 : chapters - 1)) % chapters;
                sheetSlide_ = sheetSlideTo_ = 0;
                sheetScroll_ = 0;
                sheetSelected_ = -1;
                drawSheetHeader();
                drawSheetDetail();
            }
            drawSheetList();
        }

        // The stylus.
        const bool down = (held & KEY_TOUCH) != 0;
        if (down)
        {
            touchPosition touch;
            touchRead(&touch);
            sheetX_ = touch.px;
            sheetY_ = touch.py;
            Input::notePressure(touch.z1, touch.z2);
        }
        if (down && !sheetTouching_)
        {
            // (While a slide is going on, a touch only waits for it.)
            sheetPressed_ = sheetSlide_ == sheetSlideTo_ ? sheetHit(sheetX_, sheetY_) : kSheetNothing;
            sheetDownX_ = sheetX_;
            sheetDownY_ = sheetY_;
            sheetScrollAt_ = sheetScroll_;
            sheetDrag_ = 0;
            if (sheetPressed_ == kSheetLeft || sheetPressed_ == kSheetRight)
                drawSheetHeader();
            else if (sheetPressed_ <= kSheetBar || sheetPressed_ == kSheetYes || sheetPressed_ == kSheetNo)
                drawSheetBar();
            else if (sheetPressed_ >= 0)
            {
                // A slider touched on its track: it follows the stylus.
                int count = 0;
                const Row *rows = optionSheet() ? sheetRows(static_cast<int>(page_), sheetChapter(), count) : nullptr;
                if (rows != nullptr && sheetPressed_ < count && rows[sheetPressed_].kind == Kind::Slider &&
                    sheetX_ >= 2 + kSheetTrackLeft - 6)
                {
                    sheetDrag_ = 3;
                    if (sheetSelected_ != sheetPressed_)
                    {
                        sheetSelected_ = sheetPressed_;
                        drawSheetDetail();
                    }
                    // (The stylus down on it: heard at once, as a press,
                    // if it moved it. Slid from there, only where it rests:
                    // sliderSettle_.)
                    if (sheetSlider(sheetPressed_, sheetX_))
                        sound.ui(kUiSoundTick);
                    else
                        drawSheetList();
                    sliderSettle_.start(sheetSliderLevel(sheetPressed_), Global::vblanks());
                }
                else
                {
                    drawSheetList();
                }
            }
        }
        else if (down && sheetTouching_)
        {
            const int dx = sheetX_ - sheetDownX_, dy = sheetY_ - sheetDownY_;
            const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            if (sheetDrag_ == 3)
            {
                sheetSlider(sheetPressed_, sheetX_);
                if (sliderSettle_.step(sheetSliderLevel(sheetPressed_), Global::vblanks()))
                    sound.ui(kUiSoundTick);
            }
            else
            {
                if (sheetDrag_ == 0 && (sheetPressed_ >= 0 || sheetPressed_ == kSheetList) &&
                    (ax > kSheetSlop || ay > kSheetSlop))
                {
                    // Moved: no longer a touch of a row, but the list moving
                    // (across only where there is another chapter).
                    sheetDrag_ = ax > ay && sheetChapters() > 1 ? 2 : 1;
                    drawSheetList();
                }
                if (sheetDrag_ == 1)
                {
                    const int over = sheetCount(sheetChapter()) * g.row - g.listHeight;
                    const int most = over > 0 ? over : 0;
                    int scroll = sheetScrollAt_ - dy;
                    scroll = scroll < 0 ? 0 : (scroll > most ? most : scroll);
                    if (scroll != sheetScroll_)
                    {
                        sheetScroll_ = scroll;
                        drawSheetList();
                    }
                }
                else if (sheetDrag_ == 2)
                {
                    const int slide = dx < -256 ? -256 : (dx > 256 ? 256 : dx);
                    if (slide != sheetSlide_)
                    {
                        sheetSlide_ = slide;
                        drawSheetList();
                    }
                }
            }
        }
        else if (!down && sheetTouching_)
        {
            const int was = sheetPressed_;
            const int drag = sheetDrag_;
            // (A slider let go where it had not come to rest: heard there.)
            if (drag == 3 && sliderSettle_.lift())
                sound.ui(kUiSoundTick);
            sheetPressed_ = kSheetNothing;
            sheetDrag_ = 0;
            const bool same = drag == 0 && was != kSheetNothing && was != kSheetList && sheetHit(sheetX_, sheetY_) == was;
            Action action = Action::None;
            if (drag == 2)
            {
                // Slid far enough (a fifth of the way, a flick): the rest of
                // the way; else back.
                sheetSlideTo_ = sheetSlide_ < -kSheetSwipe ? -256 : (sheetSlide_ > kSheetSwipe ? 256 : 0);
                if (sheetSlideTo_ != 0)
                    sound.ui(kUiSoundTick);
            }
            else if (drag == 3)
            {
                drawSheetList();
            }
            else if (same && was == kSheetLeft)
            {
                sheetTurn(-1);
                drawSheetHeader();
            }
            else if (same && was == kSheetRight)
            {
                sheetTurn(1);
                drawSheetHeader();
            }
            else if (same && was == kSheetYes)
            {
                sound.ui(kUiSoundPress);
                action = sheetYes();
            }
            else if (same && was == kSheetNo)
            {
                sound.ui(kUiSoundPress);
                sheetAsking_ = 0;
                drawSheetBar();
            }
            else if (same && was <= kSheetBar)
            {
                sound.ui(kUiSoundPress);
                if (kSheetBar - was == sheetButtons() - 1)
                {
                    sheetTouching_ = false;
                    return sheetBack();
                }
                action = sheetBarPress(kSheetBar - was);
                drawSheetBar();
            }
            else if (same && was >= 0)
            {
                // A row: read out; touched again (a setting: at once), done.
                sound.ui(kUiSoundPress);
                const bool again = was == sheetSelected_;
                if (sheetAsking_ != 0)
                {
                    sheetAsking_ = 0;
                    drawSheetBar();
                }
                if (optionSheet())
                {
                    if (!again)
                        sheetSelect(was);
                    // (A name is typed only when touched again: once, it is
                    // read out.)
                    int shown = 0;
                    const Row *rows = sheetRows(static_cast<int>(page_), sheetChapter(), shown);
                    if (rows[was].id == kSeat)
                    {
                        if (again)
                            askKick(was);
                    }
                    else if (rows[was].id != kProfName)
                        sheetAdjust(was, 1);
                    else if (again)
                    {
#ifndef HLDS_CHILD
                        typeName();
#endif
                        return Action::None;
                    }
                }
                else if (again && page_ == Page::Saves)
                    action = sheetAct(was);
                else
                    sheetSelect(was);
            }
            else
            {
                // (Let go somewhere else: nothing, and nothing pressed.)
                if (was == kSheetLeft || was == kSheetRight)
                    drawSheetHeader();
                else if (was <= kSheetBar || was == kSheetYes || was == kSheetNo)
                    drawSheetBar();
                else
                    drawSheetList();
            }
            sheetTouching_ = down;
            return action;
        }
        sheetTouching_ = down;
        return Action::None;
    }

    void Menu::drawRow(int row, bool down)
    {
        Canvas &canvas = touchCanvas.canvas();
        const bool main = big();
        int count = 0;
        const Row &item = rowsOf(static_cast<int>(page_), count)[row];
        if (page_ == Page::Multiplayer)
        {
            // A tile: its picture, its word under it.
            const TileBox box = multiTile(row);
            // (The smaller letters for a word too wide for its tile.)
            const FontView &medium = touchCanvas.font(kFontMedium);
            const FontView &font = textWidth(medium, item.label) <= box.w - 4 ? medium : touchCanvas.font(kFontSmall);
            const bool dead = whyDead(item.id) != nullptr;
            canvas.button(box.x, box.y, box.w, box.h, down && !dead);
            if (row < kTiles)
                drawTileIcon(canvas, box.x + (box.w - 24) / 2, box.y + 4, row, !dead);
            drawTextCentred(canvas, font, box.x + box.w / 2,
                            row < kTiles ? box.y + box.h - font.height() - 4 : box.y + (box.h - font.height()) / 2,
                            item.label, inkFor(font, dead ? kUiGrey : (down ? kUiWhite : kUiOrange)));
            return;
        }
        if (page_ == Page::Hand)
        {
            const TileBox box = handTile(row);
            const FontView &font = touchCanvas.font(kFontMedium);
            canvas.button(box.x, box.y, box.w, box.h, down);
            drawTextCentred(canvas, font, box.x + box.w / 2, box.y + (box.h - font.height()) / 2, item.label,
                            inkFor(font, down ? kUiWhite : kUiOrange));
            return;
        }
        if (page_ == Page::NetJoin && item.id == kNetFound && row < foundCount)
        {
            // A room's card: the host, its match, how many, its signal.
            const NetGame::Found &found = foundList[row];
            const FontView &small = touchCanvas.font(kFontSmall);
            const int y = kOptionLayout.top + row * kOptionLayout.pitch;
            const bool dead = foundDead(row) != nullptr;
            canvas.button(kOptionLayout.x, y, kOptionLayout.width, kOptionLayout.height, down && !dead);
            char text[64];
            std::snprintf(text, sizeof(text), "%s  %s  %s  %d/%d", found.name,
                          found.room.version == 0 ? "" : (found.room.mode == kNetModeTeamplay ? "teamplay" : "deathmatch"),
                          found.map != nullptr ? found.map
#ifndef HLDS_CHILD
                                              : (found.room.mapHash != 0 ? "(not on this card)" : ""),
#else
                                              : "",
#endif

                          found.players, found.room.version != 0 ? found.room.most : found.most);
            const int textY = y + (kOptionLayout.height - small.height()) / 2;
            drawFitted(canvas, small, kOptionLayout.x + 6, textY, text,
                       inkFor(small, dead ? kUiGrey : (down ? kUiWhite : kUiOrange)), kOptionLayout.width - 30);
            drawBars(canvas, kOptionLayout.x + kOptionLayout.width - 18, y + (kOptionLayout.height - 10) / 2, found.bars);
            return;
        }
        const Layout &at = main ? ((page_ == Page::Main || page_ == Page::Title) ? kPauseLayout : kMainLayout) : kOptionLayout;
        const int y = at.top + row * at.pitch;
        const FontView &font = touchCanvas.font(main ? kFontMedium : kFontSmall);
        const int textY = y + (at.height - font.height()) / 2;
        // (A DS has no DSP to switch: the row is there, and says so. A save
        // that cannot be loaded, or is not the player's to write, likewise;
        // and CONTINUE with nothing to continue.)
        const bool dead = whyDead(item.id) != nullptr;
        char text[48];
        switch (item.id)
        {
        case kNewSkill:
        {
            static const char *const kNames[] = {"easy", "medium", "difficult"};
            std::snprintf(text, sizeof(text), "%s: %s", item.label, kNames[(skill_ - 1) % 3]);
            break;
        }
        default:
            std::snprintf(text, sizeof(text), "%s", item.label);
            break;
        }
        canvas.button(at.x, y, at.width, at.height, down && !dead);
        drawTextCentred(canvas, font, at.x + at.width / 2, textY, text,
                        inkFor(font, dead ? kUiGrey : (down ? kUiWhite : kUiOrange)));
    }

    int Menu::rowAt(int x, int y) const
    {
        if (page_ == Page::Hand)
        {
            for (int row = 0; row < 2; ++row)
            {
                const TileBox box = handTile(row);
                if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h)
                    return row;
            }
            return -1;
        }
        if (page_ == Page::Multiplayer)
        {
            for (int row = 0; row < rowCount(); ++row)
            {
                const TileBox box = multiTile(row);
                if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h)
                    return row;
            }
            return -1;
        }
        const Layout &at = big() ? ((page_ == Page::Main || page_ == Page::Title) ? kPauseLayout : kMainLayout) : kOptionLayout;
        if (x < at.x || x >= at.x + at.width || y < at.top)
            return -1;
        const int row = (y - at.top) / at.pitch;
        return row < rowCount() && (y - at.top) % at.pitch < at.height ? row : -1;
    }

    const char *Menu::whyDead(int id) const
    {
        switch (id)
        {
        case kContinue:
            return saves_ == nullptr || saveMostRecent(*saves_) < 0 ? "No saved game to go on with" : nullptr;
        case kCommand:
            return title_ ? "The console is for a game being played" : nullptr;
        case kMultiplayer:
#ifndef HLDS_EXPERIMENTAL
            return "Multiplayer: experimental, not in this build";
#else
            return netGame.maps().empty() ? "No multiplayer map on the card" : nullptr;
#endif
        case kGameHalfLife:
            return gameThere_[0] ? nullptr : "Half-Life is not on the card";
        case kGameTraining:
            return gameThere_[1] ? nullptr : "The Hazard Course is not on the card";
        case kGameUplink:
            return gameThere_[2] ? nullptr : "Uplink is not on the card";
        case kSendDemo:
            return "Send a demo: coming later";
        default:
            return nullptr;
        }
    }

    const char *Menu::foundDead(int row) const
    {
        if (row < 0 || row >= foundCount)
            return nullptr;
        const NetGame::Found &found = foundList[row];
        if (found.room.version != 0 && found.room.version != kNetVersion)
            return "Another version of the game: it cannot be joined from this one";
        const int most = found.room.version != 0 ? found.room.most : found.most;
        if (found.players >= most)
            return "Full";
#ifdef HLDS_CHILD
        if (found.room.version == 0 || !found.room.guests)
            return "It lets in only consoles with the game";
#endif
        return nullptr;
    }

    void Menu::showReason(const char *why)
    {
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        canvas.fill(0, 168, 256, small.height() + 4, kUiBack);
        drawTextCentred(canvas, small, 128, 170, why, inkFor(small, kUiGrey));
    }

    Menu::Action Menu::fire(int row)
    {
        int count = 0;
        const Row &item = rowsOf(static_cast<int>(page_), count)[row];
        // (A row greyed out does nothing but say why.)
        if (const char *why = whyDead(item.id))
        {
            sound.ui(kUiSoundTick);
            showReason(why);
            return Action::None;
        }
        sound.ui(kUiSoundPress);
        switch (item.id)
        {
        case kResume:  return Action::Resume;
        case kHandLeft:
        case kHandRight:
            settings.leftHanded = item.id == kHandLeft;
            if (netGame.on())
                openLobby();
            else
                openJoin();
            return Action::None;
        case kEndMatch:
            asker_ = page_;
            ask(Action::EndMatch, "END THE MATCH?");
            break;
        case kSave:    return Action::Save;
        case kCommand: return Action::Command;
        case kQuit:    return Action::Quit;
        case kNewGame:
            // (From the title there is nothing to lose: straight to which
            // game. No save is touched either way: a new game only begins.)
            asker_ = page_;
            if (title_)
            {
                openNewGame();
                break;
            }
            ask(Action::NewGame, "NEW GAME?");
            break;
        case kGameHalfLife:
        case kGameTraining:
        case kGameUplink:
        {
            const int which = item.id - kGameHalfLife;
            if (!gameThere_[which])
            {
                drawRow(row, false);
                break;
            }
            static const char *const kMaps[3] = {"", "t0a0", "hldemo1"};
            map_ = kMaps[which];
            // (The Training Room is the same at any difficulty.)
            if (item.id == kGameTraining)
                skill_ = hldata::kSkillMedium;
            return Action::NewGame;
        }
        case kNewSkill:
            skill_ = skill_ % hldata::kSkillHard + 1;
            drawRow(row, false);
            break;
        case kSkillEasy:
        case kSkillMedium:
        case kSkillHard:
            skill_ = hldata::kSkillEasy + (item.id - kSkillEasy);
            return Action::NewGame;
        case kContinue:
            if (saves_ != nullptr && saveMostRecent(*saves_) >= 0)
            {
                slot_ = saveMostRecent(*saves_);
                return Action::Continue;
            }
            break;
        case kSaveGame:
        case kLoadGame:
            saving_ = item.id == kSaveGame;
            asker_ = page_;
            openSheet(Page::Saves);
            break;
        case kYes:
        {
            const Action action = asked_;
            asked_ = Action::None;
            // (A new game: yes, and then which.)
            if (action == Action::NewGame)
            {
                openNewGame();
                break;
            }
            return action;
        }
        case kNo:
            page_ = asker_;
            asked_ = Action::None;
            draw();
            break;
        case kOptions:
            sheetFrom_ = page_ == Page::Multiplayer ? page_ : Page::Main;
            openSheet(Page::Settings);
            break;
        case kAchievementsOpen:
            openSheet(Page::Achievements);
            break;
        case kBack:
            // Out of a lobby: out of the game, and the radio silent.
            if (page_ == Page::NetHost || page_ == Page::NetJoin || page_ == Page::NetLobby)
            {
                netGame.backSoonDone();
                netGame.leave();
                outOfGame();
                break;
            }
            // (From how hard, back to which game; from which game, to where
            // it was asked for.)
            page_ = page_ == Page::Difficulty ? Page::NewGame
                                              : (title_ ? Page::Title : (netGame.playing() ? Page::NetPause : Page::Main));
            draw();
            break;
        case kMultiplayer:
            if (!netGame.maps().empty())
            {
                page_ = Page::Multiplayer;
                draw();
            }
            break;
        case kNetHost:
        {
            // (Maps never photographed: their pictures first, once.)
            bool missing = false;
            for (const std::string &map : netGame.maps())
                missing = missing || (global.hasFat && !mapPictured(map));
            if (missing && !previewsTaken_)
            {
                previewsTaken_ = true;
                return Action::TakePreviews;
            }
            openHost();
            break;
        }
        case kNetJoin:
            joinWhy[0] = 0;
            if (netGame.scan())
                showFound();
            break;
        case kProfile:
            sheetFrom_ = Page::Multiplayer;
            openSheet(Page::Profile);
            break;
        case kNetFound:
            if (const char *why = foundDead(row))
            {
                sound.ui(kUiSoundDeny);
                showReason(why);
                break;
            }
            if (row < foundCount && netGame.join(foundList[row].index))
                draw();
            break;
        case kLeave:
            // (Out of a game alone, to the title: asked first, as NEW GAME
            // is -- what is not saved is lost.)
            if (!netGame.playing() && !title_ && page_ == Page::Main)
            {
                asker_ = page_;
                ask(Action::Leave, "QUIT TO MENU?");
                break;
            }
            return Action::Leave;
        default:
            break;
        }
        return Action::None;
    }

    Menu::Action Menu::frame()
    {
#ifndef HLDS_CHILD
        // Back in the lobby with the next map chosen: on to it, for everyone.
        if (page_ == Page::NetHost)
        {
            std::string next;
            if (netGame.nextNone())
                showReason("No map could come next: choose one.");
            if (netGame.nextDue(next))
            {
                bool fits = !settings.allowGuests;
                for (const NetMapInfo &map : netGame.mapInfo())
                {
                    if (map.name == next)
                        fits = fits || netMapOffered(map, true, netGame.guestRoom());
                }
                if (!fits)
                {
                    netGame.dropNext();
                    showReason("The next map does not fit a console here.");
                }
                else
                {
                    untintSeats();
                    netGame.startNext(next, settings.fragLimit, settings.minuteLimit);
                    netShown_ = 0;
                    openSheet(Page::NetStarting);
                }
            }
        }
#endif
        // The map's card on the top screen: HOST's map, the host's in the
        // lobby, the touched room's while looking.
        {
            const std::vector<NetMapInfo> &maps = netGame.mapInfo();
            const NetMapInfo *card = nullptr;
            const char *elsewhere = nullptr;
            if ((page_ == Page::NetHost || page_ == Page::NetStarting) && hostIndex() >= 0)
                card = &maps[static_cast<std::size_t>(hostIndex())];
            else if (page_ == Page::NetLobby || page_ == Page::NetJoin)
            {
                const char *name = page_ == Page::NetLobby ? netGame.lobbyMap()
                                   : foundCount > 0       ? (foundList[active_ >= 0 && active_ < foundCount ? active_ : 0].map
                                                                 ? foundList[active_ >= 0 && active_ < foundCount ? active_ : 0].map
                                                                 : "")
                                                          : "";
                for (const NetMapInfo &map : maps)
                {
                    if (name[0] != 0 && map.name == name)
                        card = &map;
                }
#ifndef HLDS_CHILD
                if (card == nullptr && page_ == Page::NetJoin && foundCount > 0)
                    elsewhere = "?";
#endif
                // (The host's map, not on this card -- or no card: its name,
                // and the host's picture of it, if it sent one.)
                if (card == nullptr && page_ == Page::NetLobby && name[0] != 0)
                    elsewhere = name;
            }
            // (What fits is what fits the consoles sent the game that are
            // here, as the host has it.)
            mapCard.setRoom(page_ == Page::NetHost || page_ == Page::NetStarting ? netGame.guestRoom() : 0);
            // The lobbies' top screen: no moving title; the map and how the
            // match is set up on the left, the player chosen on the right.
            const bool lobby = page_ == Page::NetHost || page_ == Page::NetLobby || page_ == Page::NetStarting;
            hudDraw.setMenuLogo(!lobby && page_ != Page::NetJoin);
            const NetRules &rules = netGame.match().rules;
            mapCard.setRules(lobby, rules.mode * 31u + rules.flags * 131u + rules.most * 7u + rules.frags * 1009u +
                                        rules.minutes * 65537u);
            mapCard.show(card, elsewhere);
            int seat = netGame.match().me;
            if (lobby && (page_ == Page::NetHost ? hostChapter_ == kHostPlayers : true) && sheetSelected_ >= 0)
            {
                int count = 0;
                const Row *rows = sheetRows(static_cast<int>(page_), sheetChapter(), count);
                if (sheetSelected_ < count && rows[sheetSelected_].id == kSeat)
                {
                    const int chosen = seatPlayer(sheetSelected_ - (rows[0].id == kNetTeam ? 1 : 0));
                    seat = chosen >= 0 ? chosen : seat;
                }
            }
            if (lobby)
            {
                const NetProfile &profile = netGame.match().profiles[seat];
                const int top = profile.known ? profile.top : settings.topColour;
                const int bottom = profile.known ? profile.bottom : settings.bottomColour;
                profileView.setPlace(42);
                int who = profile.known ? profile.model : settings.playerModel;
#ifdef HLDS_CHILD
                // (Asked for: the suit, until it comes.)
                if (who > 0 && !netGame.personHere(who))
                {
                    netGame.wantPerson(who);
                    who = 0;
                }
#endif
                if (who != lobbyCharacter && profileView.shown())
                    profileView.close();
                lobbyCharacter = who;
                if (!profileView.shown())
                {
                    if (lobbyModelTried != 1 && profileView.open(top, bottom, who))
                        lobbyModelColours = top | bottom << 8;
                    lobbyModelTried = profileView.shown() ? 0 : 1;
                }
                else if ((top | bottom << 8) != lobbyModelColours)
                {
                    profileView.setColours(top, bottom);
                    lobbyModelColours = top | bottom << 8;
                }
            }
            else if (lobbyWas)
            {
                profileView.setPlace(0);
                if (page_ != Page::Profile)
                    profileView.close();
            }
            lobbyWas = lobby;
#ifdef HLDS_CHILD
            // PROFILE, with the host there to ask: who they choose, to see it
            // (the suit until it comes).
            if (page_ == Page::Profile && netGame.on())
            {
                int who = settings.playerModel;
                if (who > 0 && !netGame.personHere(who))
                {
                    netGame.wantPerson(who);
                    who = 0;
                }
                if (who != lobbyCharacter || !profileView.shown())
                {
                    lobbyCharacter = who;
                    profileView.open(settings.topColour, settings.bottomColour, who);
                }
            }
#endif
        }
#ifndef HLDS_CHILD
        // SEND THE GAME: the sending goes on, and is shown as it does; once
        // the consoles have been started, the host's game again.
        if (page_ == Page::NetSend)
        {
            dlsend::frame();
            if (dlsend::state() == dlsend::State::Done)
            {
                sendEnd();
                return Action::None;
            }
            unsigned sum = static_cast<unsigned>(dlsend::state()) * 7919u + dlsend::consoles() * 31u;
            for (const char *c = dlsend::rate(); *c != 0; ++c)
                sum = sum * 31u + static_cast<unsigned char>(*c);
            for (int n = 1; n < 16; ++n)
            {
                if ((dlsend::consoles() >> n) & 1)
                {
                    char status[16];
                    const char *name;
                    dlsend::describe(n, status, sizeof(status), name);
                    for (const char *c = status; *c != 0; ++c)
                        sum = sum * 31u + static_cast<unsigned char>(*c);
                    sum = sum * 31u + static_cast<unsigned char>(name[0]);
                }
            }
            if (sum != netShown_ && !sheetTouching_ && sheetSlide_ == 0 && sheetDrag_ == 0)
            {
                netShown_ = sum;
                if (sheetSelected_ >= sheetCount(0))
                    sheetSelected_ = -1;
                touchCanvas.pageOff();
                drawSheetList();
                drawSheetDetail();
                drawSheetBar();
                touchCanvas.pageOn();
            }
        }
#endif
        // STARTING: who has got how far, as it goes; out of the game, back.
        if (page_ == Page::NetStarting)
        {
            if (netGame.phase() == NetGame::Phase::Off)
            {
                outOfGame();
                return Action::None;
            }
            unsigned sum = netSignature();
            char radio[64];
            netGame.radioLine(radio, sizeof(radio));
            for (const char *c = radio; *c != 0; ++c)
                sum = sum * 31u + static_cast<unsigned char>(*c);
            for (int p = 0; p < kNetPlayers; ++p)
            {
                char status[24] = "";
                netGame.startStatus(p, status, sizeof(status));
                for (const char *c = status; *c != 0; ++c)
                    sum = sum * 31u + static_cast<unsigned char>(*c);
            }
            if (sum != netShown_ && !sheetTouching_ && sheetSlide_ == 0 && sheetDrag_ == 0)
            {
                netShown_ = sum;
                if (sheetSelected_ >= sheetCount(0))
                    sheetSelected_ = -1;
                touchCanvas.pageOff();
                drawSheetList();
                drawSheetDetail();
                touchCanvas.pageOn();
            }
        }
        // HOST and the lobby change without being touched: who comes, how
        // they stand. Out of the game (the host gone, the link lost): back.
        if (page_ == Page::NetHost || page_ == Page::NetLobby)
        {
            if (netGame.phase() == NetGame::Phase::Off)
            {
                untintSeats();
                mapCard.hide();
                outOfGame();
                return Action::None;
            }
            if (netSignature() != netShown_ && !sheetTouching_ && sheetSlide_ == 0 && sheetDrag_ == 0)
            {
                netShown_ = netSignature();
                // (A console sent the game come, gone, or said what it has
                // room for: a map it cannot hold gives way to one it can.)
                if (page_ == Page::NetHost && settings.allowGuests && !hostMapFits())
                {
                    const int kept = netMapKept(netGame.mapInfo(), hostIndex(), true, netGame.guestRoom());
                    if (kept >= 0)
                    {
                        setHostMap(kept);
                        netGame.hostChanged();
                        settings.save();
                        netShown_ = netSignature();
                    }
                }
                // (Fewer places than before: nothing past them read out.)
                if (sheetSelected_ >= sheetCount(sheetChapter()))
                    sheetSelected_ = -1;
                touchCanvas.pageOff();
                drawSheetList();
                drawSheetDetail();
                drawSheetBar();
                touchCanvas.pageOn();
            }
        }
        if (sheet())
            return sheetFrame();
        // A game between consoles: the lobby changes without being touched.
        if (page_ == Page::NetJoin)
        {
            // Let in: the host's lobby. Not let in, or the host gone: out.
            if (netGame.phase() == NetGame::Phase::Lobby)
            {
                openSheet(Page::NetLobby);
                return Action::None;
            }
            else if (netGame.phase() == NetGame::Phase::Off)
            {
#ifdef HLDS_CHILD
                openJoin(); // (a game that would not have it: the reason, and the list)
#else
                outOfGame();
#endif
            }
            else if (netGame.phase() == NetGame::Phase::Scanning && ++netTick_ >= 45 && active_ < 0)
            {
                netTick_ = 0;
                showFound();
                // (The host that said it would be back, come back.)
                const char *back = netGame.backSoon();
                for (int i = 0; back[0] != 0 && i < foundCount; ++i)
                {
                    if (std::strcmp(foundList[i].name, back) == 0 && foundDead(i) == nullptr)
                    {
                        netGame.backSoonDone();
                        if (netGame.join(foundList[i].index))
                            draw();
                        break;
                    }
                }
            }
        }
        else if (page_ == Page::NetPause)
        {
            if (netSignature() != netShown_ && active_ < 0)
                draw();
        }
        const bool down = (keysHeld() & KEY_TOUCH) != 0;
        if (down)
        {
            touchPosition touch;
            touchRead(&touch);
            lastX_ = touch.px;
            lastY_ = touch.py;
            Input::notePressure(touch.z1, touch.z2);
        }
        Action action = Action::None;
        if (down && !touching_)
        {
            active_ = rowAt(lastX_, lastY_);
            if (active_ >= 0)
                drawRow(active_, true);
        }
        else if (!down && touching_ && active_ >= 0)
        {
            // Lifted: a button acts if it was lifted from where it was
            // pressed.
            const int row = active_;
            active_ = -1;
            if (rowAt(lastX_, lastY_) == row)
                action = fire(row);
            else
                drawRow(row, false);
        }
        touching_ = down;
        return action;
    }

    void Menu::console(ConsoleRun run)
    {
        // What the console has said, kept while the game runs (as the
        // game's own is): closed and opened again, it is all still there.
        static ConsoleLog log;
        if (log.count() == 0)
            log.print("help: the commands\n<command> help: how to use one");
        // The lines typed before, most recent last: up and down on the pad
        // bring them back.
        constexpr int kRemembered = 8;
        constexpr std::size_t kLongest = 32;
        static std::string earlier[kRemembered];
        static int earlierCount = 0;
        std::string typed;
        int recalled = earlierCount;
        auto remember = [&]()
        {
            if (typed.empty() || (earlierCount > 0 && earlier[earlierCount - 1] == typed))
                return;
            if (earlierCount == kRemembered)
            {
                for (int i = 1; i < kRemembered; ++i)
                    earlier[i - 1] = earlier[i];
                --earlierCount;
            }
            earlier[earlierCount++] = typed;
        };
        // The screen, from the top: what was said (kShown lines of the plain
        // letters), then the keyboard -- the line being typed, the keys,
        // and SPACE / ENTER / CANCEL along the bottom.
        constexpr int kShown = 7, kLineStep = 9, kLogTop = 0;
        bottomScreen.release();
        Canvas &canvas = touchCanvas.canvas();
        const FontView &plain = touchCanvas.plain();
        Keyboard keyboard(typed, kLongest);
        int scrolled = 0; // lines back from the newest
        auto said = [&]()
        {
            canvas.fill(0, kLogTop, 256, kShown * kLineStep, kUiBack);
            const int last = log.count() - scrolled;
            for (int n = 0; n < kShown; ++n)
            {
                const int at = last - kShown + n;
                if (at >= 0)
                    drawText(canvas, plain, 0, kLogTop + n * kLineStep, log.line(at), inkFor(plain, kUiGrey));
            }
            if (scrolled > 0)
                drawText(canvas, plain, 248, kLogTop, "^", inkFor(plain, kUiOrange));
        };
        canvas.clear();
        said();
        keyboard.draw();
        // The line given: said above, run, and what it said said too.
        // Whether the console is to close now.
        auto enter = [&]() -> bool
        {
            remember();
            recalled = earlierCount;
            const std::string line = typed;
            typed.clear();
            scrolled = 0;
            if (line.empty())
            {
                keyboard.drawPrompt();
                return false;
            }
            log.print(("] " + line).c_str());
            const ConsoleReply reply = run(line);
            if (!reply.text.empty())
                log.print(reply.text.c_str());
            if (reply.close)
                return true;
            // (What the line did may have had the touch screen -- a map
            // that did not load, a save: taken back, and drawn again whole.)
            bottomScreen.release();
            canvas.clear();
            said();
            keyboard.draw();
            return false;
        };

        while (true)
        {
            swiWaitForVBlank();
            sound.frame();
            scanKeys();
            switch (keyboard.poll())
            {
            case Keyboard::Event::Cancel:
                return;
            case Keyboard::Event::Enter:
                if (enter())
                    return;
                break;
            case Keyboard::Event::Up:
            case Keyboard::Event::Down:
                if (earlierCount > 0)
                {
                    recalled += (keysDown() & KEY_UP) ? -1 : 1;
                    recalled = recalled < 0 ? 0 : (recalled > earlierCount ? earlierCount : recalled);
                    typed = recalled < earlierCount ? earlier[recalled] : std::string();
                    sound.ui(kUiSoundTick);
                    keyboard.drawPrompt();
                }
                break;
            case Keyboard::Event::PageBack:
            case Keyboard::Event::PageOn:
            {
                // A page back (L) or on (R) through what was said.
                const int most = log.count() > kShown ? log.count() - kShown : 0;
                scrolled += (keysDown() & KEY_L) ? kShown - 1 : -(kShown - 1);
                scrolled = scrolled < 0 ? 0 : (scrolled > most ? most : scrolled);
                said();
                break;
            }
            default:
                break;
            }
        }
    }

    bool Menu::typeLine(std::string &text, std::size_t most, const char *heading)
    {
        bottomScreen.release();
        Canvas &canvas = touchCanvas.canvas();
        const FontView &font = touchCanvas.font(kFontMedium);
        canvas.clear();
        drawTextCentred(canvas, font, 128, 30, heading, inkFor(font, kUiOrange));
        std::string typing = text;
        Keyboard keyboard(typing, most);
        keyboard.draw();
        while (true)
        {
            swiWaitForVBlank();
            sound.frame();
            scanKeys();
            switch (keyboard.poll())
            {
            case Keyboard::Event::Enter:
                text = typing;
                draw();
                return true;
            case Keyboard::Event::Cancel:
                draw();
                return false;
            default:
                break;
            }
        }
    }
} // namespace hlrt
