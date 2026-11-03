#pragma once

#include "EngineObject.hpp"

// What the game writes over the picture: a chapter's name, the names of the
// introduction, the game's own title (the level's Messages: HlLevel.hpp).
//
// It is not drawn in 3D. The letters are put, by the processor, on sprites of
// the top screen, in front of the 3D picture: video bank G -- which nothing
// else uses -- is a sheet of 256 by 128 pixels in sixteen colours, the same
// form as the touch screen's canvas (shared/HlFont.hpp), shown as four bands
// of 256 by 32, two for each message, one above the other where its writing
// goes (laid out by shared/HlTopLayout.hpp). So it costs
// no texture memory and is the same in every way of drawing the world; and
// since the sprites are added to what is behind them, as the game adds its
// writing, a fade is nothing but their colours.
//
// Two messages at once at most (a title and a name, say), each in 64 rows:
// as many lines as fit them, wrapped.

#include <cstdint>

#include "HlFont.hpp"
#include "HlLevel.hpp"
#include "HlTopLayout.hpp"

namespace hlrt
{
    class TopText
    {
    public:
        /// Show `message`, whose text is `text` (which must stay where it
        /// is: the level's Strings).
        void show(const hldata::MessageRecord &message, const char *text);
        /// A small word in the top left corner, faint (the game saving):
        /// only where nothing the map writes is, and never in its place --
        /// with both slots taken it is not shown at all. Held until
        /// endNotice(); a second notice() while it is up only changes the word.
        /// `text` must stay where it is.
        void notice(const char *text);
        /// The notice goes, `holdMs` from now, fading.
        void endNotice(int holdMs);
        /// Two lines in the top left corner, a trophy before them (an
        /// achievement earned), shown for `holdMs` and faded out. Only in
        /// a slot nothing is in: false, and nothing shown, if there is
        /// none. `text` must stay where it is.
        bool announce(const char *text, int holdMs);
        /// `ms` more milliseconds.
        void tick(int ms);
        /// Nothing written: the map is going.
        void clear();

        /// The game's title (ui/title.hic), read now rather than when first
        /// shown (a console sent the game has it only while it starts up).
        void loadTitle();

    private:
        static constexpr int kSlots = 2;
        static constexpr int kLines = hldata::kTopLines;
        struct Line
        {
            const char *text = nullptr;
            int count = 0;
            int x = 0;
            int row = 0; ///< from the top of the slot's rows
        };
        struct Slot
        {
            bool busy = false;
            hldata::MessageRecord message{};
            Line lines[kLines];
            int lineCount = 0;
            int font = 0;  ///< one face for all its lines (-1: the console's letters)
            int top = 0;   ///< on the screen
            int rows = 0;  ///< of its 64 that it takes
            bool badge = false; ///< the trophy before its first line (announce)
            int letters = 0;  ///< in all its lines
            int elapsed = 0;  ///< ms
            int written = 0;  ///< letters on the sheet
            int settled = 0;  ///< ... and of those, in their final colour
            std::uint32_t order = 0;
        };
        Slot slots_[kSlots];
        int noticeSlot_ = -1; ///< the slot the notice is in, if it is up
        int font_ = -1;       ///< for the next layout: this face, not the largest that fits
        void layout(Slot &slot, int index, const hldata::MessageRecord &message, const char *text);
        std::uint32_t orders_ = 0;
        bool ready_ = false;
        hldata::Canvas canvas_;

        void ensure();
        void place(const Slot &slot, int index);
        void colours(int index, const Slot &slot, int strength);
        void letter(const Slot &slot, int index, int n, bool fresh);
        void drawLogo(Slot &slot, int index);
        void drawBadge(const Slot &slot, int index);
    };

    HL_ENGINE_OBJECT(TopText, topText);
} // namespace hlrt
