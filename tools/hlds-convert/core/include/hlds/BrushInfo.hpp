#pragma once

// What each brush entity of a map is, as far as drawing and collision go.
//
// A brush entity is a piece of level geometry that belongs to an entity rather
// than to the world: a door, a grate, a pane of glass, a trigger volume. The
// map gives it a class and a handful of keys; this turns those into the few
// facts the DS needs -- is it drawn, how, and does the player bump into it.
//
// The drawing rules are the PC engine's (xash3d ref/gl/gl_rsurf.c,
// R_SetRenderMode):
//
//   rendermode 0  normal    opaque; "renderamt" is ignored
//   rendermode 1  colour    no texture: a flat "rendercolor" at renderamt
//   rendermode 2  texture   blended at renderamt; '{' texels with index 255
//   rendermode 3  glow        are holes
//   rendermode 5  additive  (the DS cannot add; drawn as a blend)
//   rendermode 4  solid     full strength, '{' holes cut out
//
// and, for every mode but 0, an amount of 0 means NOT DRAWN AT ALL. Mappers
// use that constantly -- 431 brushes in the campaign -- for walls that must
// stop the player and not be seen, so "invisible" and "not solid" are separate
// facts here.

#include <string>
#include <vector>

#include "HlLevel.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/EntityParser.hpp"

namespace hlds
{
    struct BrushInfo
    {
        int entity = -1; ///< index into the entity list
        int model = 0;   ///< the "*N" it names
        std::string className;
        hldata::RenderClass renderClass = hldata::RenderClass::Opaque;
        int alpha = 31;            ///< polygon alpha, 1..31
        std::uint16_t colour = 0;  ///< RGB555, for RenderClass::Colour
        bool solid = true;         ///< the player collides with it
        bool startsOff = false;    ///< a func_wall_toggle that begins switched off
        /// Unseen to begin with (an amount of 0) but named by an env_render
        /// that shows it: cooked as drawn, and kept unseen until then.
        bool startsUnseen = false;
        bool hasPivot = false;     ///< it has an "origin": it was built to turn
        /// The player can USE it: a button, a charger, a door that opens
        /// by hand. Kept as a brush when neither seen nor in the way, for
        /// its box is what USE looks for (c1a4f's lift button, inside the
        /// car, is both).
        bool usable = false;
        double origin[3] = {0, 0, 0};

        bool drawn() const { return renderClass != hldata::RenderClass::Hidden; }
        /// '{' textures on this brush are cooked with real holes.
        bool cutsHoles() const
        {
            return renderClass == hldata::RenderClass::Cutout ||
                   renderClass == hldata::RenderClass::Blend;
        }
        /// Its light is not baked: the engine draws blended and coloured
        /// brushes at full brightness.
        bool fullBright() const
        {
            return renderClass == hldata::RenderClass::Blend ||
                   renderClass == hldata::RenderClass::Colour;
        }
    };

    /// One entry per entity that has a brush model, in entity order.
    std::vector<BrushInfo> analyseBrushes(const BspFile &bsp, const std::vector<Entity> &entities);

    /// The brush that uses each model, by model index; null for the world and
    /// for a model no entity names. Points into `brushes`.
    std::vector<const BrushInfo *> brushOfModel(const BspFile &bsp,
                                                const std::vector<BrushInfo> &brushes);
} // namespace hlds
