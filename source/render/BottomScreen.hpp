#pragma once

#include "HlNetVote.hpp"
#include "EngineObject.hpp"

// The touch screen while the game is played: a bar along the top (MENU,
// USE, RELOAD), the framed aim pad with JUMP, the lamp and DUCK in a column
// beside it, and a slot for each weapon along the bottom, with the game's
// own picture of it; the messages of the moments the game is busy; and --
// when it is asked for -- the debug readout over the top. Drawn on the touch
// canvas (ui/TouchCanvas.hpp) in the suit's amber and the game's own fonts.
//
// Where everything is, is shared/HlTouchLayout.hpp's table, used here to
// draw and by Input to test a touch, so the two cannot disagree. (The pause
// menu has the whole screen to itself: ui/Menu.hpp.)

#include "HlWeaponList.hpp"

namespace hlrt
{
    class BottomScreen
    {
    public:
        /// Set up the sub engine. Idempotent, until reset().
        void create();
        /// Forget that the sub engine was set up: the video mode was changed
        /// under it. The next create() starts over.
        void reset();

        void showPad();
        /// What the slots say: what is in hand (`weapon` is a kWeapon*;
        /// `clip` and `spare` its rounds) and what is carried (`have`: a
        /// bit per kWeapon*); and whether there is anything to reload,
        /// which is whether RELOAD is lit. Redrawn only when something
        /// changes. (Health and the suit's charge are the top screen's.)
        void setStatus(int weapon, int clip, int spare, unsigned have, unsigned empty, bool canReload);
        /// What setStatus was last told is forgotten (nothing in hand or
        /// carried), for a new game: drawn so when the pad is next shown.
        void forgetStatus()
        {
            weapon_ = clip_ = spare_ = 0;
            have_ = empty_ = 0;
            canReload_ = false;
        }
        /// What setStatus was told: in hand, carried, carried with nothing to
        /// fire (a bit per kWeapon*).
        int weapon() const { return weapon_; }
        unsigned have() const { return have_; }
        unsigned empty() const { return empty_; }
        /// A slot's list of weapons (HlWeaponList.hpp), drawn up over the pad
        /// as far as it has slid -- each frame, as it moves: off the screen,
        /// to be put on it by present().
        void setWeaponList(const hldata::WeaponList &list);
        /// Right after the wait for the vertical blank, before anything is
        /// drawn: the pad as setWeaponList last drew it, put on the screen
        /// while the screen is not being shown (a pad half drawn, the list
        /// rubbed out and not yet drawn again, is never seen).
        void present();
        /// The lamp's button: whether there is a suit to have one, whether
        /// it is alight, and how much is in it (0..100).
        void setLamp(bool suit, bool on, int percent);
        /// The player is walking: WALK stays lit (it may be a toggle).
        void setWalking(bool walking);
        /// The player has the long jump module: the column has LONG JUMP.
        void setLongJump(bool has);
        /// Which control the stylus is on (a kTouch*).
        void setPressed(int held);
        /// Two lines in the middle of the screen, in place of the pad: for
        /// the moments the game is busy.
        void showMessage(const char *line1, const char *line2);
        /// Who has how many frags, the most first, from `top` to the screen's
        /// foot: for 8 players too (in the plain letters, then).
        void drawScores(int top);
        /// The scores as the match ended (NetFinal), from `top` down.
        void drawFinal(int top);
        /// The vote for the next map, all of the screen: every map, on big
        /// buttons in a list that slides; touched, one asks to be confirmed.
        void showVote();
        /// How it stands, drawn over: its pictures are left as they are.
        void voteUpdate();
        /// How long it has to go.
        void voteClock(int seconds);
        /// It is over: what it came to, and the maps with the most votes.
        void showVoteResult();
        /// The stylus, on those pages: on the screen at (x, y), or lifted.
        void voteTouch(bool down, int x, int y);
        /// A map's small picture (HlPreview.hpp), onto row `row`'s button
        /// (on the list, its slot's).
        void voteCard(int row, const std::uint8_t *file, std::size_t size);
        /// Which map is on row `row` of what is shown (-1: none), and the first
        /// of the list.
        int voteRowMap(int row) const;
        int voteTop() const;
        /// Whether the host's LOBBY button is there.
        bool lobbyButtonAt(int x, int y) const;
        /// A match over: that table, all of the screen, until showPad().
        void showScores();
        /// Three lines and nothing else: what is said when the game cannot
        /// go on. Sets the screen up itself if nothing has.
        void showFatal(const char *line1, const char *line2, const char *line3);
        /// A long job in steps (the map pictures; a console sent the game
        /// joining a match): its name, what it is on (`step`, said in white;
        /// `note`, if any, in grey under it), how far (`done` of `total`, as a
        /// bar, said as such or, `percent`, as a percentage; `total` 0: no
        /// bar), and, `stop`, a STOP to touch (stopTouched).
        void showProgress(const char *title, const char *step, const char *note, int done, int total,
                          bool stop = true, bool percent = false);
        /// Whether STOP has been touched since showProgress showed it (looked
        /// at in the vertical blank: a touch while a map loads counts too).
        static bool stopTouched();
        /// The job is over: STOP is no longer watched.
        static void stopDone();
        /// (Global's vertical blank.)
        static void onVblank();
        /// The screen is someone else's for now (the menu): nothing here
        /// draws until showPad() or showMessage().
        void release();
        /// One row of the debug readout (eight pixels high, from the top).
        /// Nothing, unless the readout is switched on and the pad or a
        /// message is what is shown.
        void readout(int row, const char *text);

        /// The weapons' pictures, read now rather than when first drawn (a
        /// console sent the game has them only while it starts up).
        void loadIcons();

    private:
        enum class VotePage
        {
            List,
            Confirm,
            Result,
        };
        void voteList();
        void voteSlots();
        void voteMarker();
        void voteSlideTo(int scroll);
        void voteRow(int row);
        void voteRowAt(int map, int y);
        void voteConfirm(int map);
        VotePage votePage_ = VotePage::List;
        int voteScroll_ = 0, voteSpeed_ = 0, voteAsked_ = -1, voteClock_ = -1;
        int voteSlotMap_[hldata::kNetVoteRows] = {-2, -2, -2, -2};
        bool voteHeld_ = false, voteMoved_ = false;
        int voteFromY_ = 0, voteDownX_ = 0, voteDownY_ = 0;
        enum class Shown
        {
            Nothing,
            Pad,
            Message,
        };
        Shown shown_ = Shown::Nothing;
        void drawBar();
        void drawPad();
        void drawColumn();
        void drawSlots();
        void drawWeaponList();
        hldata::WeaponList list_; ///< as last drawn
        int listHover_ = -1;
        unsigned empty_ = 0;
        bool canReload_ = false;
        bool lampSuit_ = false, lampOn_ = false;
        bool walking_ = false;
        bool longJump_ = false;
        int lampBar_ = -1; ///< the gauge as drawn, in pixels
        int weapon_ = 0;
        int clip_ = 0;
        int spare_ = 0;
        unsigned have_ = 0;
        int held_ = 0;
        bool mirrored_ = false; ///< as the pad was last drawn
        bool matchBar_ = false; ///< the bar was drawn with a match's buttons (SPRAY)
        bool padPending_ = false; ///< the pad drawn off the screen, not yet on it
        bool iconsTried_ = false;
    };

    HL_ENGINE_OBJECT(BottomScreen, bottomScreen);
} // namespace hlrt
