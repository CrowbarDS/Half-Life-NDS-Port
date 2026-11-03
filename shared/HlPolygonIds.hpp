#pragma once

// Which polygon ID each see-through thing is drawn under (6 bits; 0 is what
// is solid). The hardware does not blend a see-through pixel over one of the
// same ID: two things drawn one over the other must have different ones, and
// one thing's own faces the same one -- a glass box's back does not darken
// its front. The world, the effects and the models all take theirs from here
// (the head-up display is sprites: it has none). 57..61 and 63 are free.

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
    /// Marks on the walls, round and round by their place, so that two that
    /// overlap blend (the hardware does not blend two polygons of one ID).
    inline constexpr int kIdMarksFirst = 57;
    inline constexpr int kIdMarksCount = 3; ///< 57..59
    inline constexpr int kIdLamp = 62;     ///< the lamp's light on the world

    static_assert(kIdSeeThroughFirst + kIdSeeThroughCount <= kIdFadeFirst);
    static_assert(kIdFadeFirst + kIdFadeCount <= kIdMarks);
    static_assert(kIdMarks < kIdGlassFirst && kIdGlassFirst + kIdGlassCount <= kIdLamp);
    static_assert(kIdGlassFirst + kIdGlassCount <= kIdMarksFirst && kIdMarksFirst + kIdMarksCount <= kIdLamp);
    static_assert(kIdLamp <= 63);
} // namespace hldata
