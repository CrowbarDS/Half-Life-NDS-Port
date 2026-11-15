#pragma once

// What is seen for a moment in a fight: an alien slave's bolt, spit and
// grenades in the air, a houndeye's blast, an explosion.
//
// Plain shapes in flat colour, facing the camera -- a handful of polygons a
// frame, with no textures (the game's own sprites are not converted). The
// things themselves are the game's (ActorWorld::effects and ::shots, kept
// by shared/HlMonsters.hpp); this only draws them.

#include <cstdint>

#include "HlActors.hpp"
#include "HlFixed.hpp"

namespace hldata
{
    struct NetMatch;
}

namespace hlrt
{
    class EffectDraw
    {
    public:
        /// List what there is to draw this pass, farthest first, and add
        /// what it will cost. Call once the parts of the map in view are
        /// known, before the world is chosen.
        void gather(int &polygons, int &vertices);
        /// How many things are in the list (the glows the models asked for
        /// since gather() among them). Then each is drawn by its place,
        /// under the polygon ID given; and end() when all are.
        int begin();
        std::int32_t distance(int n) const { return items_[n].distance; }
        void drawItem(int n, int id);
        /// Leaves the modelview as the camera's, no texture bound and the
        /// level's polygon format.
        void end();

        /// The marks on the walls in view (ActorWorld::decals): each a flat
        /// picture in its wall's plane, drawn only where the wall's own
        /// depth is. After everything solid, before what is see-through.
        /// Returns how many it drew.
        int drawDecals();
        /// The most marks drawn in a pass (half of it on a DS).
        static constexpr int kDecalPolygons = 48;

        /// A match's sprays (HlNetGame.hpp): each player's made into a
        /// picture of its own, in its colour -- once the map is loaded, and
        /// let go with it -- and drawn on its wall as the marks are, after
        /// them. Returns how many it drew.
        void loadSprays(const hldata::NetMatch &match);
        void dropSprays();
        int drawSprays();
        /// The small things that are solid (specks, casings, the pieces of
        /// what is not glass), in as few runs as there are textures among
        /// them. With the marks. Returns how many triangles it drew.
        int drawSolid();
        /// Add what the marks and the solid small things may cost this
        /// pass: what there is of them, and nothing when there are none.
        void reserve(int &polygons, int &vertices) const;

        /// A glow to be drawn with this frame's effects: at `at`, `alpha`
        /// solid (1..31), `half` units across either way. For whoever draws
        /// something that glows somewhere only it knows (a turret's eye).
        void glow(const hldata::fx *at, int alpha, std::uint16_t colour, int half);

    private:
        struct Glow
        {
            hldata::fx at[3];
            std::uint16_t colour;
            std::uint8_t alpha, half;
        };
        static constexpr int kMaxGlows = 40; ///< (the nihilanth's spheres are twenty of them)
        Glow glows_[kMaxGlows];
        int glowCount_ = 0;

        static constexpr int kMaxItems = 96;
        enum : std::uint8_t { kRecord, kShot, kEffect, kGlow, kMine, kPiece };
        struct Item
        {
            std::int32_t distance;
            std::uint16_t index;
            std::uint8_t type;
        };
        Item items_[kMaxItems];
        int count_ = 0;
        static constexpr int kMaxSprays = 8; ///< (kNetPlayers)
        void *sprayMaterials_[kMaxSprays] = {};
        void *sprayPalettes_[kMaxSprays] = {};
        int refused_ = 0; ///< this pass: what was in view and found the list full

    public:
        /// What this pass had in view and could not take (the list was full).
        int refused() const { return refused_; }

    private:
        void drawRecord(int index);
        void drawShot(int index);
        void drawEffect(int index);
        void drawGlow(int index);
        void drawMine(int index);
        void drawPiece(int index);
        bool pieceSeen(const hldata::Piece &piece) const;
        void pieceCorners(const hldata::Piece &piece, bool textured);

        /// The marks drawn the pass before (a bit each, by their place):
        /// those keep the room they had, a little more of it than one new.
        std::uint64_t decalShown_ = 0;
        /// How far the marks have given way to a frame short of room for its
        /// enemies (0..8 eighths of a quarter): a step a pass, not at once.
        int decalYield_ = 0;
    };

    extern EffectDraw effectDraw;
} // namespace hlrt
