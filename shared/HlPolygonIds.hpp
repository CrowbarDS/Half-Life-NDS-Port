#pragma once

// Which polygon ID each see-through thing is drawn under (6 bits; 0 is what
// is solid). The hardware does not blend a see-through pixel over one of the
// same ID: two things drawn one over the other must have different ones, and
// one thing's own faces the same one -- a glass box's back does not darken
// its front. The world, the effects and the models all take theirs from here
// (the head-up display is sprites: it has none). 63 is the screen's fade, the
// last polygon drawn (and the clear plane's, which is solid: a see-through
// polygon is held back only over a see-through pixel of its own ID).

namespace hldata
{
    /// See-through brushes and effects, far to near, round and round.
    inline constexpr int kIdSeeThroughFirst = 1;
    inline constexpr int kIdSeeThroughCount = 47; ///< 1..47
    /// Models fading away (ModelDraw), round and round: two overlapping
    /// show through each other.
    inline constexpr int kIdFadeFirst = 48;
    inline constexpr int kIdFadeCount = 4; ///< 48..51
    inline constexpr int kIdMarks = 52;    ///< marks on the walls (EffectDraw)
    /// See-through monsters (a hologram), round and round.
    inline constexpr int kIdGlassFirst = 53;
    inline constexpr int kIdGlassCount = 4; ///< 53..56
    /// Marks on the walls: each one none of those it overlaps has, so that
    /// two that overlap blend (the hardware does not blend two polygons of
    /// one ID). Chosen once, as its shape is made (EffectDraw).
    inline constexpr int kIdMarksFirst = 57;
    inline constexpr int kIdMarksCount = 5; ///< 57..61
    inline constexpr int kIdLamp = 62;     ///< the lamp's light on the world
    inline constexpr int kIdScreenFade = 63; ///< a fade's colour over the picture (ScreenFade)

    static_assert(kIdSeeThroughFirst + kIdSeeThroughCount <= kIdFadeFirst);
    static_assert(kIdFadeFirst + kIdFadeCount <= kIdMarks);
    static_assert(kIdMarks < kIdGlassFirst && kIdGlassFirst + kIdGlassCount <= kIdLamp);
    static_assert(kIdGlassFirst + kIdGlassCount <= kIdMarksFirst && kIdMarksFirst + kIdMarksCount <= kIdLamp);
    static_assert(kIdLamp < kIdScreenFade && kIdScreenFade <= 63);
} // namespace hldata
