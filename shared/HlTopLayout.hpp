#pragma once

// Where what the map writes over the picture goes (the console's TopText): its
// lines, wrapped to the screen, in one face for all of them, and where on the
// screen each is.
//
// The game writes in a small face on a screen 640 wide; the DS's is 256. So a
// line is wrapped where it is too wide, and the face is the largest of those
// given in which all of it fits the room a message has with its lines whole;
// if none will, the smallest of the game's that fits it wrapped; the
// console's 8x8 letters only for what fits nothing else.
// What still does not fit is left out.
//
// Across and down, the game's rule (its client's message.cpp) on the DS's
// screen: x and y are 0..256 of the screen's width and height from its left
// and top, -1 the middle; each line is placed across on its own.
//
// std-only and inline.

#include "HlFont.hpp"

namespace hldata
{
    inline constexpr int kTopLines = 8;

    struct TopLine
    {
        int start = 0; ///< into the text
        int count = 0;
        int x = 0;     ///< on the screen
        int row = 0;   ///< from the block's top
    };

    struct TopBlock
    {
        int face = 0;  ///< which of the faces given
        int pitch = 0; ///< from one line's top to the next's
        int top = 0;   ///< the block's, on the screen
        int lineCount = 0;
        TopLine lines[kTopLines];
    };

    namespace detail
    {
        /// The text wrapped to `width` in `font`: how many lines it takes,
        /// and those that fit into `block` (up to kTopLines).
        inline int topWrap(const FontView &font, const char *text, int width, TopBlock &block)
        {
            int lines = 0;
            const char *at = text;
            while (*at != '\0')
            {
                if (*at == '\n')
                {
                    ++at; // (an empty line is no line here)
                    continue;
                }
                const int count = textFit(font, at, width);
                if (lines < kTopLines)
                {
                    block.lines[lines].start = static_cast<int>(at - text);
                    block.lines[lines].count = count;
                }
                ++lines;
                at += count;
                while (*at == ' ')
                    ++at;
            }
            return lines;
        }
    } // namespace detail

    /// Lay `text` out in one of `faces` (largest first, the last only for what
    /// fits none of the others), within `width` pixels and `rows` rows. A
    /// `forced` face (0 or more) is used whatever it comes to. `x` and `y` are
    /// a MessageRecord's.
    inline TopBlock topLayout(const FontView *const *faces, int faceCount, int forced, const char *text, int x, int y,
                              int width = 252, int rows = 64, int screenWidth = 256, int screenHeight = 192)
    {
        TopBlock block;
        // (Each face's lines a pixel or two apart; the console's 8x8
        // letters, which have no edge round them, one; a game face with no
        // edge -- its console's, its credits' -- none: its rows have the
        // room between lines in them.)
        auto pitchOf = [&](int face)
        {
            if (face == faceCount - 1 && faceCount > 1)
                return faces[face]->height() + 1;
            const bool edged = faces[face]->header == nullptr || faces[face]->header->outlined != 0;
            return faces[face]->height() + (edged ? 2 : 0);
        };
        // The largest face that has every line whole, if any; else the
        // smallest of the game's that fits it wrapped (the game writes it
        // small); else the last, cut short.
        int whole = 0;
        for (const char *c = text; *c != '\0'; ++c)
            whole += (*c != '\n' && (c == text || c[-1] == '\n')) ? 1 : 0;
        int face = forced;
        int lines = 0;
        for (int pass = 0; pass < 2 && face < 0; ++pass)
        {
            for (int n = 0; n < faceCount - 1 && face < 0; ++n)
            {
                const int f = pass == 0 ? n : faceCount - 2 - n;
                lines = detail::topWrap(*faces[f], text, width, block);
                if (lines <= kTopLines && lines * pitchOf(f) <= rows && (pass == 1 || lines == whole))
                    face = f;
            }
        }
        if (face < 0)
            face = faceCount - 1;
        lines = detail::topWrap(*faces[face], text, width, block);
        const int pitch = pitchOf(face);
        const int most = rows / pitch < kTopLines ? rows / pitch : kTopLines;
        block.face = face;
        block.pitch = pitch;
        block.lineCount = lines < most ? lines : most;
        const int high = pitch * block.lineCount;
        int top = y < 0 ? (screenHeight - high) / 2 : y * screenHeight / 256;
        top = top > screenHeight - high ? screenHeight - high : top;
        block.top = top < 0 ? 0 : top;
        for (int l = 0; l < block.lineCount; ++l)
        {
            TopLine &line = block.lines[l];
            const int wide = textWidth(*faces[face], text + line.start, line.count);
            int across = x < 0 ? (screenWidth - wide) / 2 : x * screenWidth / 256;
            across = across > screenWidth - 2 - wide ? screenWidth - 2 - wide : across;
            line.x = across < 0 ? 0 : across;
            line.row = l * pitch;
        }
        return block;
    }
} // namespace hldata
