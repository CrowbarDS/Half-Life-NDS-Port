#include "global_header.hpp"

#include "render/BottomScreen.hpp"

#include "HlPlayerMove.hpp"
#include "HlPreview.hpp"
#include "Settings.hpp"
#include "Global.hpp"
#include "HlTouchLayout.hpp"
#include "HlWeaponList.hpp"
#include "HlUi.hpp"
#include "net/NetGame.hpp"
#include "ui/TouchCanvas.hpp"

#include <cstring>

namespace hlrt
{
    BottomScreen bottomScreen;

    namespace
    {
        using namespace hldata;
        // showProgress's STOP, and whether it has been touched since.
        constexpr int kStopX = 88, kStopY = 152, kStopW = 80, kStopH = 22;
        volatile bool stopShown = false, stopHit = false;

        // The weapons' pictures (ui/weapons.hic), if the card has them.
        std::uint32_t iconWords[(sizeof(IconsHeader) + kWeaponIcons * kIconMaxHeight * iconRowWords(kIconMaxWidth) * 4) / 4];
        IconsView icons;

        /// The pad's tiles (it is whole tiles: 208 by 132 from 0 or 48, 24)
        /// copied from `from` to `to`, both laid out as the screen's.
        void copyPad(std::uint32_t *to, const std::uint32_t *from, bool mirrored)
        {
            const TouchRect r = touchLayout(mirrored, netGame.playing()).pad;
            const int left = r.x / 8, right = (r.x + r.w + 7) / 8, top = r.y / 8, bottom = (r.y + r.h + 7) / 8;
            for (int ty = top; ty < bottom; ++ty)
            {
                const int at = (ty * 32 + left) * 8;
                std::memcpy(to + at, from + at, static_cast<std::size_t>(right - left) * 32);
            }
        }

        // What a control is drawn in: at rest a dim amber line on the
        // screen's own dark, held (or in hand) the full amber on a lit
        // plate.
        void plate(const TouchRect &r, bool lit, bool faint = false)
        {
            touchCanvas.canvas().plate(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4, lit ? kUiPanelDown : kUiBack,
                                       lit ? kUiOrange + 2 : (faint ? kUiPanel : kUiOrange));
        }

        void label(const TouchRect &r, const char *text, bool lit)
        {
            Canvas &canvas = touchCanvas.canvas();
            const FontView &font = touchCanvas.font(kFontSmall);
            plate(r, lit);
            drawTextCentred(canvas, font, r.x + r.w / 2, r.y + (r.h - font.height()) / 2, text,
                            inkFor(font, lit ? kUiWhite : kUiOrange));
        }

        /// A chevron, its point up or down, five lines thick.
        void chevron(int cx, int cy, bool up, int colour)
        {
            Canvas &canvas = touchCanvas.canvas();
            for (int t = 0; t < 5; ++t)
            {
                const int tip = up ? cy - 7 + t : cy + 7 - t, base = up ? cy + 3 + t : cy - 3 - t;
                canvas.line(cx - 11, base, cx, tip, colour);
                canvas.line(cx + 11, base, cx, tip, colour);
            }
        }

        /// A ring, `thick` pixels of it inwards from `radius`.
        void ring(int cx, int cy, int radius, int thick, int colour)
        {
            Canvas &canvas = touchCanvas.canvas();
            const int outer = radius * radius, inner = (radius - thick) * (radius - thick);
            for (int y = -radius; y <= radius; ++y)
            {
                for (int x = -radius; x <= radius; ++x)
                {
                    const int d = x * x + y * y;
                    if (d <= outer && d > inner)
                        canvas.pixel(cx + x, cy + y, colour);
                }
            }
        }
    } // namespace

    void BottomScreen::create()
    {
        touchCanvas.create();
    }

    void BottomScreen::reset()
    {
        touchCanvas.reset();
        shown_ = Shown::Nothing;
    }

    void BottomScreen::release()
    {
        shown_ = Shown::Nothing;
    }

    void BottomScreen::loadIcons()
    {
        if (iconsTried_)
            return;
        iconsTried_ = true;
        if (!global.hasData)
            return;
        std::FILE *file = std::fopen(global.commonPath(kWeaponIconsFile).c_str(), "rb");
        if (file == nullptr)
            return;
        const std::size_t size = std::fread(iconWords, 1, sizeof(iconWords), file);
        std::fclose(file);
        icons.open(reinterpret_cast<const std::uint8_t *>(iconWords), size);
    }

    void BottomScreen::drawBar()
    {
        Canvas &canvas = touchCanvas.canvas();
        // (The bar has other buttons in a match: what was there before is
        // cleared, not drawn over.)
        matchBar_ = netGame.playing();
        const TouchLayout at = touchLayout(mirrored_, matchBar_);
        canvas.fill(0, 0, kTouchWidth, kTouchBarHeight, kUiBack);
        label(at.menu, "MENU", held_ == kTouchMenu);
        label(at.use, "USE", held_ == kTouchUse);
        if (at.spray.w > 0)
            label(at.spray, "SPRAY", held_ == kTouchSpray);
        // RELOAD is faint while there is nothing to reload: no gun in
        // hand, a full one, or nothing carried for it.
        if (canReload_ || held_ == kTouchReload)
        {
            label(at.reload, "RELOAD", held_ == kTouchReload);
        }
        else
        {
            const FontView &font = touchCanvas.font(kFontSmall);
            plate(at.reload, false, true);
            drawTextCentred(canvas, font, at.reload.x + at.reload.w / 2, at.reload.y + (at.reload.h - font.height()) / 2,
                            "RELOAD", inkFor(font, kUiGrey));
        }
    }

    void BottomScreen::drawPad()
    {
        Canvas &canvas = touchCanvas.canvas();
        const TouchRect r = touchLayout(mirrored_, netGame.playing()).pad;
        canvas.plate(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 6, kUiBack, kUiOrange);
        // The mark of the place in its middle, as faint as a line can be:
        // it is where the stylus rests, not something to read.
        const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        ring(cx, cy, 30, 4, kUiPanel);
        for (int t = 0; t < 5; ++t)
        {
            canvas.line(cx - 7 + t, cy - 17, cx + 9 + t, cy + 17, kUiPanel);
            canvas.line(cx - 1 + t, cy - 3, cx - 13 + t, cy + 17, kUiPanel);
        }
        canvas.fill(cx - 12, cy - 17, 10, 3, kUiPanel);
        // ... and a tick at the middle of each side.
        canvas.fill(cx - 8, r.y + 1, 16, 2, kUiOrange);
        canvas.fill(cx - 8, r.y + r.h - 3, 16, 2, kUiOrange);
        canvas.fill(r.x + 1, cy - 8, 2, 16, kUiOrange);
        canvas.fill(r.x + r.w - 3, cy - 8, 2, 16, kUiOrange);
    }

    HL_ARM void BottomScreen::drawColumn()
    {
        Canvas &canvas = touchCanvas.canvas();
        const TouchLayout at = touchLayout(mirrored_, netGame.playing());
        const FontView &font = touchCanvas.font(kFontSmall);
        const struct
        {
            const TouchRect &r;
            const char *name;
            bool up, lit;
        } buttons[] = {{at.jump, "JUMP", true, held_ == kTouchJump}, {at.duck, "DUCK", false, held_ == kTouchDuck}};
        for (const auto &b : buttons)
        {
            plate(b.r, b.lit);
            const int cx = b.r.x + b.r.w / 2;
            chevron(cx, b.r.y + (b.up ? 15 : b.r.h - 15), b.up, b.lit ? kUiWhite + 2 : kUiOrange + 1);
            drawTextCentred(canvas, font, cx, b.up ? b.r.y + b.r.h - font.height() - 3 : b.r.y + 3, b.name,
                            inkFor(font, b.lit ? kUiWhite : kUiOrange));
        }
        // Between them the lamp: a bulb with its rays, lit while it is
        // alight, and under it what is left in it. Faint with no suit to
        // have one.
        {
            const TouchRect &r = at.lamp;
            const bool lit = lampOn_ || held_ == kTouchLamp;
            plate(r, lit, !lampSuit_);
            const int colour = !lampSuit_ ? kUiGrey : (lit ? kUiWhite + 2 : kUiOrange + 1);
            const int cx = r.x + 13, cy = r.y + 12;
            ring(cx, cy, 5, lampOn_ ? 5 : 2, colour);
            for (int k = 0; k < 3; ++k)
            {
                // (Three rays, out to the side the gauge's numbers are.)
                const int dy = (k - 1) * 5;
                canvas.line(cx + 8, cy + dy, cx + 13, cy + dy + (k - 1) * 2, lampOn_ ? colour : (lampSuit_ ? kUiOrange : kUiGrey));
            }
            if (lampSuit_)
            {
                char text[8];
                std::snprintf(text, sizeof(text), "%d", lampBar_ < 0 ? 0 : lampBar_ * 100 / (r.w - 10));
                drawText(canvas, font, r.x + r.w - 5 - textWidth(font, text), r.y + 12 - font.height() / 2, text,
                         inkFor(font, lit ? kUiWhite : kUiOrange));
                // The gauge.
                const int wide = r.w - 10, left = r.x + 5, top = r.y + r.h - 9;
                canvas.fill(left, top, wide, 4, lit ? kUiBack : kUiTrack);
                canvas.fill(left, top, lampBar_ < 0 ? 0 : lampBar_, 4, lampBar_ * 5 < wide ? kUiRed : kUiOrange + 2);
            }
        }
    }

    void BottomScreen::setLamp(bool suit, bool on, int percent)
    {
        const int bar = suit ? percent * (touchLayout(false).lamp.w - 10) / 100 : -1;
        if (suit == lampSuit_ && on == lampOn_ && bar == lampBar_)
            return;
        lampSuit_ = suit;
        lampOn_ = on;
        lampBar_ = bar;
        if (shown_ == Shown::Pad)
            drawColumn();
    }

    HL_ARM void BottomScreen::drawSlots()
    {
        // (What a slot says with no picture to show: by weapon.)
        static const char *const kNames[kWeaponCount] = {"",     "BAR",  "9MM",   "SHOT", "GREN", "MP5",
                                                         ".357", "XBOW", "SATCH", "MINE", "SNARK",
                                                         "RPG",  "TAU",  "EGON",  "HIVE"};
        Canvas &canvas = touchCanvas.canvas();
        const TouchLayout at = touchLayout(mirrored_, netGame.playing());
        const FontView &font = touchCanvas.font(kFontSmall);
        loadIcons();
        for (int n = 0; n < kTouchSlots; ++n)
        {
            // The game's slot n + 1, a kind of weapon: shown as the one of
            // them in hand, or else the first of them carried.
            const TouchRect &r = at.slot[n];
            int count = 0;
            const std::uint8_t *list = weaponsOfCategory(n + 1, count);
            int weapon = kWeaponNone;
            for (int k = 0; k < count && weapon == kWeaponNone; ++k)
            {
                if (list[k] == weapon_ && (have_ & (1u << list[k])))
                    weapon = list[k];
            }
            for (int k = 0; k < count && weapon == kWeaponNone; ++k)
            {
                if (have_ & (1u << list[k]))
                    weapon = list[k];
            }
            const bool carried = weapon != kWeaponNone, inHand = carried && weapon == weapon_;
            plate(r, inHand, !carried);
            if (!carried)
                continue;
            const int cx = r.x + r.w / 2;
            const int top = r.y + (inHand && weapon != kWeaponCrowbar ? 3 : (r.h - icons.height()) / 2);
            const int icon = weapon - 1;
            if (icon < icons.count())
            {
                // The game's picture: dim while it waits, bright in hand.
                const int inks[4] = {0, inHand ? kUiOrange + 1 : kUiOrange, inHand ? kUiOrange + 2 : kUiOrange + 1,
                                     inHand ? kUiWhite + 2 : kUiOrange + 1};
                const int left = cx - icons.width() / 2;
                for (int y = 0; y < icons.height(); ++y)
                {
                    for (int x = 0; x < icons.width(); ++x)
                    {
                        if (const int level = icons.level(icon, x, y))
                            canvas.pixel(left + x, top + y, inks[level]);
                    }
                }
            }
            else
            {
                drawTextCentred(canvas, font, cx, r.y + (inHand && weapon != kWeaponCrowbar ? 3 : (r.h - font.height()) / 2),
                                kNames[weapon], inkFor(font, inHand ? kUiWhite : kUiOrange));
            }
            if (!inHand || weapon == kWeaponCrowbar)
                continue;
            // Under it, what is in it and what is carried for it -- or as
            // much of that as the slot is wide. (What is thrown or put down
            // is a count of itself.)
            char text[16];
            std::snprintf(text, sizeof(text), "%d/%d", clip_, spare_);
            const bool counted = weapon == kWeaponGrenade || weapon == kWeaponSatchel || weapon == kWeaponTripmine ||
                                 weapon == kWeaponSnark || weapon == kWeaponGauss || weapon == kWeaponEgon ||
                                 weapon == kWeaponHornetgun;
            if (counted || textWidth(font, text) > r.w - 6)
                std::snprintf(text, sizeof(text), "%d", clip_);
            drawTextCentred(canvas, font, cx, r.y + r.h - font.height() - 1, text, inkFor(font, kUiWhite));
        }
    }

    void BottomScreen::setWeaponList(const WeaponList &list)
    {
        const int hover = list.owned ? list.hover : -1;
        if (list.category == list_.category && list.slide == list_.slide && hover == listHover_ &&
            list.count == list_.count)
            return;
        list_ = list;
        listHover_ = hover;
        if (shown_ != Shown::Pad)
            return;
        // What was under it (it is all inside the pad's frame), then it where
        // it is now -- off the screen, from the pad as it is on it: put on
        // it whole, in the next vertical blank (present).
        copyPad(touchCanvas.copyTiles(), touchCanvas.screenTiles(), mirrored_);
        touchCanvas.offScreen(touchCanvas.copyTiles());
        drawPad();
        if (list_.open())
            drawWeaponList();
        touchCanvas.onScreen();
        padPending_ = true;
    }

    void BottomScreen::present()
    {
        if (!padPending_)
            return;
        padPending_ = false;
        if (shown_ == Shown::Pad)
            copyPad(touchCanvas.screenTiles(), touchCanvas.copyTiles(), mirrored_);
    }

    /// The list of a slot's weapons: each its picture and its name, the one
    /// in hand lit, the one under the stylus pressed, one with nothing to
    /// fire grey; risen out of the pad's bottom edge as far as it has slid.
    HL_ARM void BottomScreen::drawWeaponList()
    {
        static const char *const kFull[kWeaponCount] = {"",         "CROWBAR",  "9MM PISTOL", "SHOTGUN",  "GRENADES",
                                                        "MP5",      ".357",     "CROSSBOW",   "SATCHELS", "TRIPMINES",
                                                        "SNARKS",   "RPG",      "TAU CANNON", "GLUON GUN", "HIVEHAND"};
        const TouchLayout at = touchLayout(mirrored_, netGame.playing());
        const TouchRect r = weaponListRect(list_, at);
        const int drop = (256 - list_.slide) * (r.h + 4) / 256;
        // (Drawn into the screen's rows down to the pad's bottom edge, and no
        // further -- what is still below it is not seen, and nothing outside
        // the pad is ever touched: the pad drawn again puts all of it right.)
        Canvas band = touchCanvas.canvas();
        band.height = weaponListFloor(at) / 8 * 8;
        const FontView &font = touchCanvas.font(kFontSmall);
        loadIcons();
        const int top = r.y + drop;
        band.plate(r.x, top, r.w, r.h, 4, kUiBack, kUiOrange + 2);
        for (int n = 0; n < list_.count; ++n)
        {
            const int weapon = list_.weapons[n];
            const int y = top + 2 + n * kWeaponListRow;
            const bool inHand = weapon == weapon_, under = n == listHover_;
            const bool dry = (list_.empty & (1u << weapon)) != 0;
            band.button(r.x + 3, y + 1, r.w - 6, kWeaponListRow - 2, under);
            if (inHand)
                band.frame(r.x + 3, y + 1, r.w - 6, kWeaponListRow - 2, kUiOrange + 2);
            const int icon = weapon - 1;
            if (icon < icons.count())
            {
                const int base = dry ? kUiGrey : kUiOrange;
                const int inks[4] = {0, base, base + 1, dry ? kUiGrey + 2 : kUiWhite + 2};
                const int left = r.x + 6, iconTop = y + (kWeaponListRow - icons.height()) / 2;
                for (int py = 0; py < icons.height(); ++py)
                {
                    for (int px = 0; px < icons.width(); ++px)
                    {
                        if (const int level = icons.level(icon, px, py))
                            band.pixel(left + px, iconTop + py, inks[level]);
                    }
                }
            }
            drawText(band, font, r.x + 50, y + (kWeaponListRow - font.height()) / 2, kFull[weapon],
                     inkFor(font, dry ? kUiGrey : (inHand || under ? kUiWhite : kUiOrange)));
        }
    }

    void BottomScreen::showPad()
    {
        stopShown = false;
        create();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll(); // (a menu's colours of its own go)
        shown_ = Shown::Pad;
        padPending_ = false; // (drawn on the screen now, all of it)
        mirrored_ = settings.leftHanded;
        drawBar();
        drawPad();
        drawColumn();
        drawSlots();
        if (list_.open())
            drawWeaponList();
    }

    void BottomScreen::setStatus(int weapon, int clip, int spare, unsigned have, unsigned empty, bool canReload)
    {
        const bool bar = canReload != canReload_ || netGame.playing() != matchBar_;
        const bool slots = weapon != weapon_ || clip != clip_ || spare != spare_ || have != have_;
        empty_ = empty;
        canReload_ = canReload;
        weapon_ = weapon;
        clip_ = clip;
        spare_ = spare;
        have_ = have;
        if (shown_ != Shown::Pad)
            return;
        if (bar)
            drawBar();
        if (slots)
            drawSlots();
    }

    void BottomScreen::setPressed(int held)
    {
        if (held == held_)
            return;
        const int was = held_;
        held_ = held;
        if (shown_ != Shown::Pad)
            return;
        // (A slot shows what is in hand, not what is touched.)
        auto onBar = [](int on)
        { return on == kTouchMenu || on == kTouchUse || on == kTouchReload || on == kTouchSpray; };
        if (onBar(was) || onBar(held))
            drawBar();
        auto onColumn = [](int on) { return on == kTouchJump || on == kTouchDuck || on == kTouchLamp; };
        if (onColumn(was) || onColumn(held))
            drawColumn();
    }

    void BottomScreen::showMessage(const char *line1, const char *line2)
    {
        stopShown = false;
        create();
        touchCanvas.pageOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &title = touchCanvas.font(kFontTitle), &font = touchCanvas.font(kFontMedium);
        drawTextCentred(canvas, title, 128, 84, line1, inkFor(title, kUiOrange));
        // (As much of the second line as the screen is wide.)
        char cut[64];
        std::snprintf(cut, sizeof(cut), "%.*s", textFit(font, line2, 248), line2);
        drawTextCentred(canvas, font, 128, 112, cut, inkFor(font, kUiWhite));
        touchCanvas.pageOn();
    }

    void BottomScreen::drawScores(int top)
    {
        Canvas &canvas = touchCanvas.canvas();
        const NetMatch &match = netGame.match();
        // Who is here, the most frags first (then the fewest deaths).
        int order[kNetPlayers], count = 0;
        for (int p = 0; p < kNetPlayers; ++p)
        {
            if (!netHere(match, p))
                continue;
            int at = count++;
            for (; at > 0; --at)
            {
                const int q = order[at - 1];
                if (match.frags[q] > match.frags[p] ||
                    (match.frags[q] == match.frags[p] && match.deaths[q] <= match.deaths[p]))
                    break;
                order[at] = q;
            }
            order[at] = p;
        }
        // Whole rows of cells (each player's colours have cells of their
        // own): two to a player while they fit, one, in the plain letters,
        // when they do not.
        const int firstCell = (top + 7) / 8 + 1;
        const bool roomy = firstCell + count * 2 <= 24;
        const int rowCells = roomy ? 2 : 1;
        const FontView &font = roomy ? touchCanvas.font(kFontSmall) : touchCanvas.plain();
        canvas.fill(0, top, 256, 192 - top, kUiBack);
        const int headY = firstCell * 8 - font.height() - 1;
        constexpr int kNameX = 28, kFragsX = 196, kDeathsX = 236;
        char text[24];
        if (match.rules.teams())
        {
            std::snprintf(text, sizeof(text), "TEAMS  %d : %d", match.teamFrags[1], match.teamFrags[2]);
            drawText(canvas, font, kNameX, headY, text, inkFor(font, kUiOrange));
        }
        else
            drawText(canvas, font, kNameX, headY, "PLAYER", inkFor(font, kUiGrey));
        drawTextCentred(canvas, font, kFragsX, headY, "FRAGS", inkFor(font, kUiGrey));
        drawTextCentred(canvas, font, kDeathsX, headY, "DIED", inkFor(font, kUiGrey));
        for (int row = 0; row < count; ++row)
        {
            const int p = order[row], cell = firstCell + row * rowCells, y = cell * 8;
            const int textY = y + (rowCells * 8 - font.height()) / 2;
            if (p == match.me)
                canvas.fill(24, y, 232, rowCells * 8, kUiPanel);
            // The player's own colours (TouchCanvas::tint: its 1 and 2).
            canvas.fill(8, y, 8, 8, 1);
            canvas.fill(16, y, 8, 8, 2);
            const NetProfile &profile = match.profiles[p];
            const std::uint16_t three[3] = {hueColour(profile.known ? profile.top : 30),
                                            hueColour(profile.known ? profile.bottom : 6), uiColour(kUiEdge)};
            touchCanvas.tint(1, cell, 2, 1, three, 2 + row);
            const int ink = p == match.me ? kUiWhite : kUiOrange;
            std::snprintf(text, sizeof(text), "%.*s", textFit(font, match.names[p][0] != 0 ? match.names[p] : "...", 140),
                          match.names[p][0] != 0 ? match.names[p] : "...");
            drawText(canvas, font, kNameX + 2, textY, text, inkFor(font, ink));
            std::snprintf(text, sizeof(text), "%d", match.frags[p]);
            drawTextCentred(canvas, font, kFragsX, textY, text, inkFor(font, ink));
            std::snprintf(text, sizeof(text), "%d", match.deaths[p]);
            drawTextCentred(canvas, font, kDeathsX, textY, text, inkFor(font, ink));
        }
    }

    void BottomScreen::drawFinal(int top)
    {
        Canvas &canvas = touchCanvas.canvas();
        const NetMatch &match = netGame.match();
        const NetFinal &final = match.final;
        const bool totals = match.rules.totals;
        const FontView &small = touchCanvas.font(kFontSmall);
        // Whole rows of cells, as drawScores'; the last row is the facts'.
        const int firstCell = (top + 7) / 8 + 1;
        const bool roomy = firstCell + final.count * 2 <= 21;
        const int rowCells = roomy ? 2 : 1;
        const FontView &font = roomy ? small : touchCanvas.plain();
        const int headY = firstCell * 8 - font.height() - 1;
        const int nameX = 28, fragsX = totals ? 166 : 196, deathsX = totals ? 202 : 236, allX = 238;
        char text[48];
        if (match.rules.teams())
        {
            std::snprintf(text, sizeof(text), "TEAMS  %d : %d", final.teamFrags[1], final.teamFrags[2]);
            drawText(canvas, font, nameX, headY, text, inkFor(font, kUiOrange));
        }
        else
            drawText(canvas, font, nameX, headY, "PLAYER", inkFor(font, kUiGrey));
        drawTextCentred(canvas, font, fragsX, headY, "FRAGS", inkFor(font, kUiGrey));
        drawTextCentred(canvas, font, deathsX, headY, "DIED", inkFor(font, kUiGrey));
        if (totals)
            drawTextCentred(canvas, font, allX, headY, "ALL", inkFor(font, kUiGrey));
        const NetFinal::Row *first = final.count > 0 ? &final.rows[final.order[0]] : nullptr;
        for (int row = 0; row < final.count; ++row)
        {
            const int p = final.order[row], cell = firstCell + row * rowCells, y = cell * 8;
            const NetFinal::Row &line = final.rows[p];
            const int textY = y + (rowCells * 8 - font.height()) / 2;
            // The winner, or the winning team, stands out; who has gone is grey.
            const bool won = match.rules.teams() ? final.winnerTeam != 0 && line.team == final.winnerTeam
                                                 : line.frags == first->frags && line.deaths == first->deaths;
            if (won && !line.gone)
                canvas.fill(24, y, 232, rowCells * 8, kUiPanel);
            canvas.fill(8, y, 8, 8, 1);
            canvas.fill(16, y, 8, 8, 2);
            const std::uint16_t three[3] = {line.gone ? uiColour(kUiGrey) : hueColour(line.top),
                                            line.gone ? uiColour(kUiGrey) : hueColour(line.bottom), uiColour(kUiEdge)};
            touchCanvas.tint(1, cell, 2, 1, three, 2 + row);
            const int ink = line.gone ? kUiGrey : (won || p == match.me ? kUiWhite : kUiOrange);
            const char *name = line.name[0] != 0 ? line.name : "...";
            std::snprintf(text, sizeof(text), "%.*s", textFit(font, name, fragsX - nameX - 18), name);
            drawText(canvas, font, nameX + 2, textY, text, inkFor(font, ink));
            std::snprintf(text, sizeof(text), "%d", line.frags);
            drawTextCentred(canvas, font, fragsX, textY, text, inkFor(font, ink));
            std::snprintf(text, sizeof(text), "%d", line.deaths);
            drawTextCentred(canvas, font, deathsX, textY, text, inkFor(font, ink));
            if (totals)
            {
                std::snprintf(text, sizeof(text), "%d", match.sessionFrags[p]);
                drawTextCentred(canvas, font, allX, textY, text, inkFor(font, ink));
            }
        }
        // What comes next, and what was remarkable about it, along the bottom.
        if (match.nextName[0] != 0)
        {
            std::snprintf(text, sizeof(text), "NEXT: %s", match.nextName);
            drawTextCentred(canvas, small, 128, 192 - 2 * small.height() - 4, text, inkFor(small, kUiOrange));
        }
        char facts[64];
        netFinalFacts(final, facts, sizeof(facts));
        if (facts[0] != 0)
        {
            std::snprintf(text, sizeof(text), "%.*s", textFit(small, facts, 248), facts);
            drawTextCentred(canvas, small, 128, 192 - small.height() - 2, text, inkFor(small, kUiGrey));
        }
    }

    namespace
    {
        // The vote's big buttons, three to the screen: its picture on whole
        // cells, its name and its votes beside it.
        constexpr int kVoteTop = 3, kVoteCellsDown = 7, kVoteX = 3, kVoteW = 240;
        int voteRowY(int row) { return (kVoteTop + row * kVoteCellsDown) * 8; }
        constexpr int kLobbyX = 204, kLobbyY = 1, kLobbyW = 50, kLobbyH = 15;

        void drawLobbyButton(Canvas &canvas)
        {
#ifndef HLDS_CHILD
            if (!netGame.isHost())
                return;
            const FontView &small = touchCanvas.font(kFontSmall);
            canvas.button(kLobbyX, kLobbyY, kLobbyW, kLobbyH, false);
            drawTextCentred(canvas, small, kLobbyX + kLobbyW / 2, kLobbyY + (kLobbyH - small.height()) / 2, "LOBBY",
                            inkFor(small, kUiOrange));
#else
            (void)canvas;
#endif
        }
    } // namespace

    bool BottomScreen::lobbyButtonAt(int x, int y) const
    {
        return x >= kLobbyX && x < kLobbyX + kLobbyW && y >= kLobbyY && y < kLobbyY + kLobbyH;
    }

    int BottomScreen::voteRowMap(int row) const
    {
        if (row < 0 || row >= kNetVoteRows)
            return -1;
        switch (votePage_)
        {
        case VotePage::Confirm: return row == 1 ? voteAsked_ : -1;
        case VotePage::Result: return row == 0 ? netGame.match().vote.decided : -1;
        default: return voteTop_ + row < netGame.match().vote.count ? voteTop_ + row : -1;
        }
    }

    void BottomScreen::voteRow(int row)
    {
        Canvas &canvas = touchCanvas.canvas();
        const NetMatch &match = netGame.match();
        const NetVote &vote = match.vote;
        const int map = voteRowMap(row), y = voteRowY(row);
        const FontView &medium = touchCanvas.font(kFontMedium), &small = touchCanvas.font(kFontSmall);
        // (Not over its picture, which stays as it is: only what is beside it.)
        canvas.fill(kVoteX + 72, y - 3, kVoteW - 72, 54, kUiBack);
        if (map < 0)
            return;
        const bool mine = vote.choice[match.me] == map;
        const bool won = vote.decided == map;
        const int edge = mine || won ? kUiOrange + 2 : kUiEdge;
        canvas.fill(kVoteX, y - 3, kVoteW, 1, edge);
        canvas.fill(kVoteX, y + 50, kVoteW, 1, edge);
        canvas.fill(kVoteX, y - 3, 1, 54, edge);
        canvas.fill(kVoteX + kVoteW - 1, y - 3, 1, 54, edge);
        drawText(canvas, medium, kVoteX + 72, y + 8, vote.names[map], inkFor(medium, mine || won ? kUiWhite : kUiOrange));
        char text[24];
        std::snprintf(text, sizeof(text), "%d vote%s%s", vote.tally[map], vote.tally[map] == 1 ? "" : "s", mine ? " - YOURS" : "");
        drawText(canvas, small, kVoteX + 72, y + 28, text, inkFor(small, mine ? kUiOrange : kUiGrey));
    }

    void BottomScreen::voteList()
    {
        votePage_ = VotePage::List;
        stopShown = false;
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &medium = touchCanvas.font(kFontMedium);
        drawText(canvas, medium, 6, 4, "VOTE: THE NEXT MAP", inkFor(medium, kUiOrange));
        drawLobbyButton(canvas);
        voteClock_ = -1;
        const NetVote &vote = netGame.match().vote;
        for (int r = 0; r < kNetVoteRows; ++r)
        {
            if (voteRowMap(r) < 0)
                continue;
            canvas.fill(kVoteX + 4, voteRowY(r), 64, 48, kUiPanel);
            voteRow(r);
        }
        // Where it is in the list.
        if (vote.count > kNetVoteRows)
        {
            const int height = 168 * kNetVoteRows / vote.count;
            canvas.fill(249, 24, 3, 168, kUiPanel);
            canvas.fill(249, 24 + (168 - height) * voteTop_ / (vote.count - kNetVoteRows), 3, height, kUiOrange + 2);
        }
        netGame.voteRedrawn();
    }

    void BottomScreen::showVote()
    {
        voteTop_ = 0;
        voteHeld_ = false;
        voteList();
        netGame.setVoteShown(true);
    }

    void BottomScreen::voteConfirm(int map)
    {
        votePage_ = VotePage::Confirm;
        voteAsked_ = map;
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll();
        const FontView &medium = touchCanvas.font(kFontMedium);
        drawTextCentred(canvas, medium, 128, 28, "VOTE FOR THIS MAP?", inkFor(medium, kUiOrange));
        canvas.fill(kVoteX + 4, voteRowY(1), 64, 48, kUiPanel);
        voteRow(1);
        canvas.button(8, 140, 116, 46, false);
        drawTextCentred(canvas, medium, 66, 140 + (46 - medium.height()) / 2, "YES", inkFor(medium, kUiOrange));
        canvas.button(132, 140, 116, 46, false);
        drawTextCentred(canvas, medium, 190, 140 + (46 - medium.height()) / 2, "NO", inkFor(medium, kUiOrange));
        netGame.voteRedrawn();
    }

    void BottomScreen::showVoteResult()
    {
        votePage_ = VotePage::Result;
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll();
        const NetVote &vote = netGame.match().vote;
        const FontView &medium = touchCanvas.font(kFontMedium), &small = touchCanvas.font(kFontSmall);
        drawTextCentred(canvas, medium, 128, 4, "NEXT MAP", inkFor(medium, kUiOrange));
        canvas.fill(kVoteX + 4, voteRowY(0), 64, 48, kUiPanel);
        voteRow(0);
        int top[3];
        const int found = vote.top(top, 3);
        char text[40];
        int y = 92;
        for (int n = 0; n < found; ++n, y += 26)
        {
            std::snprintf(text, sizeof(text), "%d. %s", n + 1, vote.names[top[n]]);
            drawText(canvas, medium, 12, y, text, inkFor(medium, n == 0 ? kUiWhite : kUiOrange));
            std::snprintf(text, sizeof(text), "%d", vote.tally[top[n]]);
            drawTextCentred(canvas, medium, 232, y, text, inkFor(medium, n == 0 ? kUiWhite : kUiOrange));
        }
        if (vote.tally[vote.decided] == 0)
            drawTextCentred(canvas, small, 128, 170, "No votes: drawn at random", inkFor(small, kUiGrey));
        netGame.voteRedrawn();
    }

    void BottomScreen::voteUpdate()
    {
        if (votePage_ == VotePage::List)
        {
            for (int r = 0; r < kNetVoteRows; ++r)
                voteRow(r);
        }
    }

    void BottomScreen::voteClock(int seconds)
    {
        if (votePage_ != VotePage::List || seconds == voteClock_)
            return;
        voteClock_ = seconds;
        Canvas &canvas = touchCanvas.canvas();
        const FontView &small = touchCanvas.font(kFontSmall);
        char text[8];
        std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
        canvas.fill(166, 3, 36, small.height() + 2, kUiBack);
        drawTextCentred(canvas, small, 184, 5, text, inkFor(small, kUiGrey));
    }

    void BottomScreen::voteTouch(bool down, int x, int y)
    {
        const NetVote &vote = netGame.match().vote;
        if (votePage_ == VotePage::Result)
            return;
        if (down && !voteHeld_)
        {
            voteHeld_ = true;
            voteMoved_ = false;
            voteFromY_ = voteDownY_ = y;
            voteDownX_ = x;
            return;
        }
        if (down)
        {
            // A slide: a button at a time.
            if (votePage_ == VotePage::List && (y - voteFromY_ >= 28 || voteFromY_ - y >= 28))
            {
                const int to = netVoteScrolled(voteTop_, y < voteFromY_ ? 1 : -1, vote.count);
                voteFromY_ = y;
                voteMoved_ = true;
                if (to != voteTop_)
                {
                    voteTop_ = to;
                    voteList();
                }
            }
            return;
        }
        if (!voteHeld_)
            return;
        voteHeld_ = false;
        if (voteMoved_)
            return;
        if (votePage_ == VotePage::Confirm)
        {
            if (voteDownY_ >= 140 && voteDownY_ < 186 && voteDownX_ >= 8 && voteDownX_ < 124)
                netGame.vote(voteAsked_);
            if (voteDownY_ >= 140 && voteDownY_ < 186)
                voteList();
            return;
        }
        // (A vote made is made: the list is there to look at.)
        if (vote.choice[netGame.match().me] >= 0 || voteDownY_ < voteRowY(0) - 3)
            return;
        const int row = (voteDownY_ - (voteRowY(0) - 3)) / (kVoteCellsDown * 8);
        const int map = voteRowMap(row);
        if (map >= 0)
            voteConfirm(map);
    }

    void BottomScreen::voteCard(int row, const std::uint8_t *file, std::size_t size)
    {
        if (row < 0 || row >= kNetVoteRows || previewWidthOf(file, size) != kPreviewSmallWidth)
            return;
        const auto *pixels = reinterpret_cast<const std::uint16_t *>(file + sizeof(PreviewHeader));
        std::uint16_t palette[16];
        previewPalette(pixels, kPreviewSmallWidth * kPreviewSmallHeight, palette);
        Canvas &canvas = touchCanvas.canvas();
        const int x = kVoteX + 5, y = voteRowY(row);
        const int px0 = (x + 7) / 8 * 8;
        for (int py = 0; py < kPreviewSmallHeight; ++py)
        {
            for (int px = 0; px < kPreviewSmallWidth; ++px)
                canvas.fill(px0 + px, y + py, 1, 1, previewNearest(palette, pixels[py * kPreviewSmallWidth + px]));
        }
        touchCanvas.tintWhole(px0 / 8, y / 8, kPreviewSmallWidth / 8, kPreviewSmallHeight / 8, palette, 12 + row);
    }

    void BottomScreen::showScores()
    {
        stopShown = false;
        touchCanvas.pageOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        touchCanvas.untintAll();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &title = touchCanvas.font(kFontTitle);
        char said[32];
        netFinalTitle(netGame.match(), said, sizeof(said));
        drawTextCentred(canvas, title, 128, 6, said, inkFor(title, kUiOrange));
        drawFinal(6 + title.height() + 2);
        drawLobbyButton(canvas);
        touchCanvas.pageOn();
    }

    void BottomScreen::showProgress(const char *title, const char *step, const char *note, int done, int total,
                                    bool stop, bool percent)
    {
        create();
        touchCanvas.pageOff();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Message;
        padPending_ = false;
        const FontView &big = touchCanvas.font(kFontTitle), &medium = touchCanvas.font(kFontMedium),
                       &small = touchCanvas.font(kFontSmall);
        drawTextCentred(canvas, big, 128, 22, title, inkFor(big, kUiOrange));
        char cut[64];
        std::snprintf(cut, sizeof(cut), "%.*s", textFit(medium, step, 240), step);
        drawTextCentred(canvas, medium, 128, 70, cut, inkFor(medium, kUiWhite));
        if (note != nullptr && note[0] != '\0')
        {
            std::snprintf(cut, sizeof(cut), "%.*s", textFit(small, note, 240), note);
            drawTextCentred(canvas, small, 128, 70 + medium.height() + 4, cut, inkFor(small, kUiGrey));
        }
        // How far: a bar, and in words under it.
        constexpr int kBarX = 28, kBarY = 108, kBarW = 200, kBarH = 10;
        if (total > 0)
        {
            canvas.fill(kBarX, kBarY, kBarW, kBarH, kUiTrack);
            canvas.frame(kBarX - 1, kBarY - 1, kBarW + 2, kBarH + 2, kUiEdge);
            const int filled = kBarW * (done < total ? done : total) / total;
            canvas.fill(kBarX, kBarY, filled, kBarH, kUiOrange + 1);
            if (percent)
                std::snprintf(cut, sizeof(cut), "%d%%", done * 100 / total);
            else
                std::snprintf(cut, sizeof(cut), "%d of %d", done, total);
            drawTextCentred(canvas, small, 128, kBarY + kBarH + 6, cut, inkFor(small, kUiGrey));
        }
        if (stop)
        {
            canvas.button(kStopX, kStopY, kStopW, kStopH, stopHit);
            drawTextCentred(canvas, medium, kStopX + kStopW / 2, kStopY + (kStopH - medium.height()) / 2,
                            stopHit ? "STOPPING" : "STOP", inkFor(medium, stopHit ? kUiWhite : kUiOrange));
        }
        stopShown = stop;
        touchCanvas.pageOn();
    }

    bool BottomScreen::stopTouched()
    {
        const bool hit = stopHit;
        if (hit)
            stopShown = stopHit = false;
        return hit;
    }

    void BottomScreen::stopDone() { stopShown = stopHit = false; }

    void BottomScreen::onVblank()
    {
        if (!stopShown || stopHit || !(keysCurrent() & KEY_TOUCH))
            return;
        touchPosition touch;
        touchRead(&touch);
        stopHit = touch.px >= kStopX && touch.px < kStopX + kStopW && touch.py >= kStopY && touch.py < kStopY + kStopH;
    }

    void BottomScreen::showFatal(const char *line1, const char *line2, const char *line3)
    {
        stopShown = false;
        create();
        Canvas &canvas = touchCanvas.canvas();
        canvas.clear();
        shown_ = Shown::Nothing;
        padPending_ = false;
        const FontView &title = touchCanvas.font(kFontTitle), &font = touchCanvas.font(kFontSmall);
        drawTextCentred(canvas, title, 128, 20, "Half-Life DS", inkFor(title, kUiOrange));
        int y = 64;
        for (const char *line : {line1, line2, line3})
        {
            // Each on as many rows as it takes.
            while (line != nullptr && *line != '\0' && y < 180)
            {
                const int fits = textFit(font, line, 244);
                drawText(canvas, font, 6, y, line, inkFor(font, kUiWhite), fits);
                line += fits;
                while (*line == ' ')
                    ++line;
                y += font.height();
            }
            y += 6;
        }
    }

    void BottomScreen::readout(int row, const char *text)
    {
        if (!settings.debugReadout || shown_ == Shown::Nothing || row < 0 || row > 8)
            return;
        Canvas &canvas = touchCanvas.canvas();
        canvas.fill(0, row * 8, 256, 8, kUiBack);
        drawText(canvas, touchCanvas.plain(), 0, row * 8, text, inkPlain(kUiWhite + 2), 32);
    }
} // namespace hlrt
