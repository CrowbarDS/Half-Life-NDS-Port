#pragma once

// Drawing the world: which surfaces can be seen from here, and sending them.
//
// The cooked level already holds each surface as a finished run of
// geometry-engine commands, so "drawing" is choosing. Each frame:
//
//  1. Find the leaf the eye is in. If it changed, decode that leaf's PVS row
//     and stamp every visible leaf and the nodes above it.
//  2. Walk the BSP tree front to back, skipping branches with no stamp and
//     boxes outside the view. In each leaf reached, take the surfaces that face
//     the eye and are inside the view, until the frame's budget is spent.
//  3. Send the chosen surfaces in two goes, the near ones and then the far
//     ones (see THE ORDER below), each in index order -- they are stored
//     sorted by texture -- by copying their words to the geometry FIFO.
//
// THE BUDGET is the whole reason for the order of step 2. The hardware stops
// at 2048 polygons and 6144 vertices and simply drops the rest, in whatever
// order they were sent. Walking front to back and stopping at a budget of our
// own means that what gets left out is the farthest geometry, which the fog
// is already hiding, rather than whichever wall happened to come last.
//
// THE ORDER of step 3 is for when the budget is wrong. The walk counts a
// surface whole, before clipping; the hardware counts what is left of it
// after, and a polygon cut by the screen's edge can come out with more
// vertices than it went in with. When the hardware fills up, it is whatever
// was sent last that goes. Sent by texture alone, that could be a near
// ceiling. So the surfaces whose nearest point is past half the distance the
// view reaches (the fog's end, or the map's own farthest) go after all the
// others: what a full hardware drops is then far, and in the fog. (What is
// drawn after the world -- models, brushes, effects -- still comes after
// both; an overflow in the world's far half takes those too.)
//
// BRUSH ENTITIES -- doors, grates, glass -- are drawn after the world, each
// under its own translation, when one of the leaves it touches is visible and
// its box is in view. Opaque ones go straight out. The rest wait: the hardware
// draws every see-through polygon after every opaque one, in the order they
// were SENT and without sorting them, so they are sent far to near. That
// includes the cutout class, which looks opaque: a polygon with an A3I5
// texture is treated as see-through whatever its own alpha (measured, not
// assumed -- NEA's own header says otherwise).
//
// The walk and the send are in WorldDraw.itcm.arm.cpp: ARM code in ITCM.

#include <cstdint>

#include <nds.h>
#include <NEAMain.h>

#include "HlLogic.hpp"

#include "HlFog.hpp"
#include "Settings.hpp"

namespace hlrt
{
    struct WorldStats
    {
        int leaf = 0;        ///< the leaf the eye is in
        int surfaces = 0;    ///< surfaces sent
        int polygons = 0;    ///< as the level counts them, before clipping
        int vertices = 0;
        /// What the geometry engine says it used, read back once it had taken
        /// the frame's last command. Higher than the two above when polygons
        /// were clipped at the screen's edge.
        int hwPolygons = 0;
        int hwVertices = 0;
        int brushes = 0;        ///< brush entities sent
        bool budgetHit = false; ///< the walk stopped early
        /// A brush entity had no room. (It does not bring the fog in: what is
        /// behind a door left out is the room there, not nothing.)
        bool brushCut = false;
        /// How far away the first thing left out was (whole units), when the
        /// walk stopped early.
        int cutDistance = 0;
        /// Left out for being small and far (see WorldDraw::pressure), and
        /// what the surfaces among them would have cost.
        int dropped = 0;
        int droppedPolygons = 0;
        int droppedVertices = 0;
        int models = 0;         ///< actors drawn
        int modelsCut = 0;      ///< in view, but the pass had no room
        /// Surfaces in view that the walk had no room for (each time one did
        /// not fit; what was still to come when it gave up is not counted).
        int surfacesCut = 0;
        /// What of `polygons` was the world's far half, sent last (the
        /// part a full hardware drops).
        int farPolygons = 0;
        /// Surfaces the view's planes left out (each once a pass), and --
        /// in a build with HLDS_BOX_TEST -- those of the rest the hardware's
        /// box test left out; and whether the box test was not done this
        /// pass (the DSi's DMA was still feeding the geometry engine).
        int sphereOut = 0;
        int boxOut = 0;
        bool boxSkipped = false;
        /// Brush entities in view not drawn: no room in the pass for them, or
        /// (`brushesOverflow`, counted in both) more of them than the lists
        /// hold.
        int brushesDropped = 0;
        int brushesOverflow = 0;
    };

    class WorldDraw
    {
    public:
        // Kept under the hardware's 2048 / 6144: clipping against the screen's
        // edges turns one polygon into one with more vertices, and that is
        // counted against the same RAM.
        //
        // ... and above the level's own pass (what the converter judges a
        // map heavy by: kPassPolygons) by this much, at run time: what the
        // hardware was measured to hold of a pass the walk had filled (see
        // docs/TECHNICAL.md, "The pass's budget"). 0 puts it back.
        static constexpr int kHeadroomPolygons = 200;
        static constexpr int kHeadroomVertices = 600;
        static constexpr int kPolygonBudget = hldata::kPassPolygons + kHeadroomPolygons;
        static constexpr int kVertexBudget = hldata::kPassVertices + kHeadroomVertices;

        // How every polygon of a level is sent.
        //
        // The hardware is NOT asked to cull back faces. Every surface is one
        // plane and is tested against the eye before it is sent, so there is
        // nothing left for the hardware to reject -- and its own test goes by
        // a polygon's first three vertices after projection, where a sliver
        // or a far-off polygon can come out with the wrong sign and vanish.
        //
        // Polygons that cross the far plane are clipped to it. The default is
        // to drop them whole, which takes a long wall away all at once.
        /// The size of the display list a pass's surfaces are put in on a
        /// DSi (32-bit words). More than a pass ever is; if one were, it
        /// would go in two pieces.
        static constexpr int kListWords = 24 * 1024;
        static constexpr int kRelaxWait = 120;
        /// The most relaxWait_ is let grow to (it doubles each time the
        /// pressure has to go straight back up).
        static constexpr int kRelaxWaitMost = kRelaxWait * 16;
        /// Near is nearer than the view's reach shifted down by this (1: a
        /// half): what is sent first (THE ORDER, above).
        static constexpr int kNearShift = 1;
        /// Nearer than this (whole units, along the view), nothing is left
        /// out for being small: under pressure, only what is farther goes.
        static constexpr int kDetailNearest = hldata::kDetailNearest;
        static constexpr int kNoFog = hldata::kFogNone; ///< a fog distance that is no fog
        static constexpr NEA_CullingEnum kCull = NEA_CULL_NONE;
        static constexpr NEA_OtherFormatEnum kPolygonMode =
            static_cast<NEA_OtherFormatEnum>(NEA_MODULATION | NEA_RENDER_FAR_CLIPPED);

        /// Allocate the per-level working arrays. Call after a level loads.
        bool create();
        void destroy();

        /// Choose and send the world's surfaces. Call inside the frame's draw
        /// callback, after Camera::apply().
        void draw();

        /// For what is drawn besides the world (models): whether a ball at
        /// `centre` (whole units) can be seen at all -- its leaf is one the
        /// eye's leaf can see, and it is in the view.
        bool ballVisible(const std::int16_t *centre, int radius, bool anyDistance = false) const;
        /// Take `polygons` and `vertices` from what is left of the pass.
        /// False, with nothing taken, if there is not that much.
        bool spend(int polygons, int vertices);

        const WorldStats &stats() const { return stats_; }
        /// The leaves the eye's leaf can see: changes (to a number not had
        /// just before) each time they are found again, for a new leaf.
        std::uint16_t visStamp() const { return visFrame_; }
        /// Whether `leaf` is one of them.
        bool leafVisible(int leaf) const { return leafFrame_ != nullptr && leafFrame_[leaf] == visFrame_; }

        /// The budget, corrected by what the hardware said of the last pass
        /// (GpuStats): call once a pass, after it is sent.
        ///
        /// It goes both ways. The constants above are what the hardware is
        /// meant to end up holding, but the count the walk keeps is made
        /// before the hardware throws away what is off the screen, and that
        /// is often a fifth of it: so most of the difference seen in the
        /// last pass of this half is added to the next one's allowance. And
        /// a pass the hardware could not hold, or could not finish its lines
        /// for, takes some away -- the far end of the view goes -- which
        /// comes back slowly while the hardware has room to spare.
        ///
        /// `slow`: the pass took longer than a frame to make. What is added
        /// is for a console with time to draw it: a slow pass takes a
        /// quarter of it away, and a second of passes that are not slow
        /// gives a quarter back.
        void govern(bool overflow, bool underflow, int lines, bool slow);
        /// How hard the view is to fit, 0 to 3: raised while passes run out
        /// of room, let down slowly when they have room to spare. What it
        /// costs first is what is least seen -- the far and small: surfaces
        /// and brushes a few pixels across, and the share of a pass the
        /// models may have. Only what is still too much is cut off at a
        /// distance, and that is faded into fog (below), not left as an edge.
        /// (0..3, for the models and effects: World detail's own steps, two
        /// to each of these.)
        int pressure() const { return (pressure_ + 1) / 2; }
        /// The ratio of distance to size past which something is left out,
        /// at this pressure (0: nothing is).
        int detailRatio() const
        {
            // (Normally nothing with no pressure, then about 2.5, 4 and 6
            // pixels across; the player may have it sooner, or never.)
            return hldata::worldDetailRatio(settings.graphics.worldDetail, pressure_);
        }
        /// Where the fog is whole (whole units), or 0 with no fog.
        int fogDistance() const { return fogOn_ ? fogEnd_ : 0; }
        /// What the world, the models and the brushes are drawn with: with
        /// the fog on them while there is fog.
        NEA_OtherFormatEnum polygonMode() const
        {
            return static_cast<NEA_OtherFormatEnum>(kPolygonMode | (fogOn_ ? NEA_FOG_ENABLE : 0));
        }
        /// Set the hardware's fog for the pass about to be drawn.
        void applyFog();
        int polygonBudget() const { return kPolygonBudget + budget_.extra; }
        int vertexBudget() const { return kVertexBudget + budget_.extraVertices; }
        /// For the overlay: what is being held back (negative: added).
        int heldBack() const { return -budget_.extra; }

        /// Where the brush entities are: one hldata::BrushState per Brush
        /// record, owned and moved by the game (Game::brushStates). Null
        /// draws them where the map built them.
        void setBrushStates(const hldata::BrushState *states) { brushState_ = states; }
        /// Whether brush entity `i` can be seen through (glass, a grate, one
        /// an env_render has made so): what is behind it is not hidden by it.
        bool brushSeeThrough(int i) const;

    private:
        void markVisible(int leaf);
        void walk();
        void emit();
        void emitList();
        void finishList();
        /// The brush entities this pass draws, chosen before the world is
        /// walked and their cost set aside (up to a third of the pass): a
        /// crate in front of the eye is not what the far end of a heavy view
        /// leaves no room for.
        void planBrushes();
        /// ... and drawn, each whole or not at all.
        void drawBrushes();
        void drawBrush(std::uint32_t index, int polygonId);
        /// The eye in brush `index`'s own space (and its turn, if it is
        /// turned: true).
        bool brushEye(std::uint32_t index, hldata::fx *eye, std::int32_t *axes) const;
        /// What brush `index` costs to draw from where the eye is: the
        /// surfaces that face it.
        void brushCost(std::uint32_t index, int &polygons, int &vertices) const;
        int brushPolygons_ = 0; ///< what the world leaves for the brushes this pass
        int brushVertices_ = 0;
        /// What the world leaves for the lamp's light this pass: what it
        /// lit the pass before, and a little (it is counted as it is drawn,
        /// so this is only room kept, not a cost).
        int lampPolygons_ = 0;
        int lampVertices_ = 0;
        int lampLastPolygons_ = -1; ///< what it lit last pass; -1: it was not lit
        int lampLastVertices_ = 0;
        /// The texture matrix as liquids want it (it is also what slides a
        /// conveyor's belt, which puts this back when it has done).
        void sway();
        /// The lamp's light, projected onto what of the world is in its
        /// beam (HlLamp.hpp): after everything solid, before what is
        /// see-through.
        void lampPass();
        /// The solid brushes drawn this pass: the lamp's pass lights them
        /// as it does the world.
        static constexpr int kMaxLampBrushes = 24;
        std::uint16_t lampBrushes_[kMaxLampBrushes];
        int lampBrushCount_ = 0;
        void drawSky();

        std::uint16_t *nodeFrame_ = nullptr; ///< vis stamp per node
        std::uint16_t *leafFrame_ = nullptr; ///< vis stamp per leaf
        std::uint32_t *chosen_ = nullptr;    ///< one bit per surface
        std::uint32_t *far_ = nullptr;       ///< ... of those, the far ones: sent last
#ifdef HLDS_BOX_TEST
        /// THE BOX TEST (an experiment, off unless built with HLDS_BOX_TEST):
        /// after a surface's ball is found in the view, the geometry
        /// engine's own BOX_TEST is asked whether the box round its
        /// vertices is -- against the view as the hardware clips it, under
        /// the world's own matrix. A ball round a flat surface is far bigger
        /// than the surface; the box is not.
        ///
        /// The box is in the vertices' own numbers (quarter-units, read as
        /// 4.12: HlGxBounds.hpp), worked out at load from each surface's own
        /// commands, so the cooked level is as it was. Its corners are
        /// vertices, so they fit the test's 16 bits; its size could not,
        /// and such a surface (none on the game's maps: test_gxbounds) has
        /// no box and is judged by its ball alone.
        ///
        /// The test waits for the geometry engine's answer, so it is not
        /// asked while the DSi's DMA is still feeding it the last list
        /// (emitList): that pass is judged by balls alone. (finishList has
        /// always waited for the list before the walk comes round again;
        /// this only makes sure of it.)
        std::int16_t (*boxes_)[6] = nullptr; ///< per surface: mins, maxs; mins[0] > maxs[0]: none
        std::uint32_t *out_ = nullptr;       ///< left out this pass (tested once)
        bool boxTest_ = true;
#endif
        /// ... whether World detail kept it the last time it was asked (kept
        /// while it is out of view: turned away from and back to, it is as
        /// it was). All kept again whenever detail is off.
        std::uint32_t *detailKept_ = nullptr;
        bool detailWasOn_ = false;
        /// On a DSi: a node gone through has its own surfaces drawn too (the
        /// game's rule: NodeMarks), not only those its seen leaves list.
        bool nodeSurfaces_ = false;
        std::uint32_t *skipped_ = nullptr;   ///< ... of those left out as small and far
        /// On a DSi: where a pass's surfaces are put together as one display
        /// list, which its other DMA (NDMA) feeds to the geometry engine
        /// while this processor gets on with something else. Null on a DS.
        std::uint32_t *list_ = nullptr;
        bool listSent_ = false; ///< this pass's world went that way
        /// The DMA once did not finish what it was given (WorldDraw.itcm's
        /// listDone): the world is written to the geometry engine by this
        /// processor from then on, as on a DS.
        bool listStuck_ = false;
        const hldata::BrushState *brushState_ = nullptr;
        std::uint32_t chosenWords_ = 0;
        std::uint16_t visFrame_ = 0;
        int visLeaf_ = -1; ///< the leaf visFrame_ was built for
        int modelPolygons_ = 0; ///< what this pass's models cost (ModelDraw::plan)
        int modelVertices_ = 0;
        int pressure_ = 0;
        int cutPasses_ = 0;   ///< passes on end that ran out of room
        int easyPasses_ = 0;  ///< ... that had room to spare
        int relaxWait_ = kRelaxWait; ///< how many of those let the pressure down
        int sinceRise_ = 0;           ///< passes since the pressure last went up
        int sinceRelax_ = 0;
        /// Where the fog is whole: just short of where the view is being cut
        /// off. Brought in at once, let out slowly.
        int fogEnd_ = kNoFog;
        bool fogSolid_ = false; ///< it hides an edge: whole at its end (fogTable)
        bool fogOn_ = false;
        std::uint16_t behind_ = 0; ///< the colour behind everything, as last set
        hldata::BudgetState budget_; ///< held back from the budget, and spare added (govern)
        int spareQuarters_ = 4; ///< how much of what could be added is (0..4)
        int quickPasses_ = 0;
        WorldStats stats_;
    };

    extern WorldDraw worldDraw;
} // namespace hlrt
