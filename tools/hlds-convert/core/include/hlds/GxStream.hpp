#pragma once

// Geometry-engine command streams: the packer that writes them and a decoder
// that reads them back.
//
// A surface in a cooked level is not a list of vertices for the DS to turn into
// commands -- it IS the commands, packed the way the hardware's FIFO takes
// them, so that drawing a surface is copying its words to one register. This
// file is the only place that knows the encoding.
//
// PACKED FORMAT (GBATEK, "GXFIFO / Packed Commands"): one word holds up to four
// command bytes, lowest byte first, and is followed by the parameter words of
// each in order. A zero byte ends the word's commands. The hardware has one
// rule that bites: the top-most command of a packed word must take parameters.
// Every command emitted here does, so the rule cannot be broken from outside.
//
// VERTEX POSITIONS are sent in the shortest form that is EXACT. The full form,
// VTX_16, is two parameter words; the hardware has four one-word forms, and a
// level's command stream is half its size on the card and in RAM, so they are
// worth having:
//
//   VTX_XY/XZ/YZ  two coordinates; the third is the previous vertex's
//   VTX_DIFF      three signed 10-bit differences from the previous vertex
//                 (+-512 raw: 128 units at our quarter-unit scale)
//   VTX_10        three 10-bit coordinates: exact only for multiples of 64 raw
//
// The relative forms need a previous vertex, so a stream's FIRST vertex is
// always absolute. That is what makes each surface's stream self-contained:
// the DS sends surfaces in whatever subset and order the frame calls for.
//
// The decoder exists so that the converter can check its own output (`verify`
// and the tests) by reading streams the way the console will, instead of
// trusting the code that wrote them.

#include <cstdint>
#include <string>
#include <vector>

namespace hlds
{
    enum class GxPrimitive : std::uint8_t
    {
        Triangles = 0,
        Quads = 1,
        TriangleStrip = 2,
        QuadStrip = 3,
    };

    struct GxVertex
    {
        std::int16_t x = 0, y = 0, z = 0; ///< quarter units (HlFixed.hpp)
        std::int16_t s = 0, t = 0;        ///< texels, 12.4
        std::uint16_t colour = 0;         ///< RGB555

        bool operator==(const GxVertex &) const = default;
    };

    struct GxRun
    {
        GxPrimitive type = GxPrimitive::Triangles;
        std::vector<GxVertex> vertices;

        /// Polygon RAM entries the run uses, or -1 if the vertex count does not
        /// fit the primitive type.
        int polygons() const;
    };

    class GxPacker
    {
    public:
        void begin(GxPrimitive type);
        /// Colour, texture coordinate and position. The colour command is left
        /// out when it would repeat the previous vertex's.
        void vertex(const GxVertex &v);
        /// The packed words. The packer is empty afterwards.
        std::vector<std::uint32_t> finish();
        /// Every vertex is given a colour command of its own from here on,
        /// whether or not it repeats the last: for vertices whose colours
        /// the console changes one by one (the level's LightPatch).
        void colourEvery(bool on) { everyColour_ = on; }
        /// After finish(): which word of what it returned is each colour
        /// command's parameter, in the order the vertices were given.
        const std::vector<std::uint32_t> &colourWords() const { return colourWords_; }

    private:
        struct Command
        {
            std::uint8_t code;
            std::uint8_t paramCount;
            std::uint32_t params[2];
        };

        void position(const GxVertex &v);

        std::vector<Command> commands_;
        bool haveColour_ = false;
        bool everyColour_ = false;
        std::vector<std::uint32_t> colourWords_;
        std::uint16_t colour_ = 0;
        bool havePrevious_ = false;
        std::int16_t previous_[3] = {0, 0, 0};
    };

    /// Read a stream back into runs. Fails on any command the packer does not
    /// emit, on a truncated stream, and on a vertex before the first begin.
    bool decodeGxStream(const std::uint32_t *words, std::size_t count, std::vector<GxRun> &out,
                        std::string &error);

    /// What is wrong with the SHAPE of the polygons in a surface's runs, as
    /// the hardware will meet them.
    ///
    /// The DS decides which way a polygon faces from the first three vertices
    /// of its ring -- for a quad of a strip, sent as rows (L[k], R[k]),
    /// (L[k+1], R[k+1]), those are (L[k], R[k], R[k+1]). A polygon it gets
    /// wrong is not only culled when culling is on: drawn, it is filled with
    /// its left and right edges swapped, which leaves pixels out along its
    /// flatter sides. A
    /// quad that is a triangle with a vertex in the middle of one side is fine
    /// to draw, but if that vertex is the middle one of those three the facing
    /// is the sign of rounding noise, and the polygon comes and goes with the
    /// camera. So: the leading three must turn, and turn the way the surface
    /// faces; and no corner may turn the other way.
    struct GxShapeAudit
    {
        int polygons = 0;
        int flatLead = 0;  ///< leading three vertices in one line
        int reversed = 0;  ///< leading three wound against the surface
        int reflex = 0;    ///< some corner turns the wrong way
        int noArea = 0;    ///< nothing to draw at all
        // The quads the hardware may draw wrongly (GBATEK's "prohibited
        // quads": crossed sides, a corner of more than 180 degrees), judged
        // with no allowance for the grid: a corner exactly in line with its
        // neighbours is fine (it stays in line however it is seen); one off
        // its line by the grid's rounding is not.
        int concaveQuads = 0; ///< a corner turns against the facing
        int crossedQuads = 0; ///< two opposite corners do: its sides cross
        int nearQuads = 0;    ///< not flat on the grid, and a corner turns the right way by no more than it can blur

        bool clean() const { return flatLead == 0 && reversed == 0 && reflex == 0 && noArea == 0; }
        /// No quad the hardware may draw wrongly.
        bool quadsSafe() const { return concaveQuads == 0 && crossedQuads == 0 && nearQuads == 0; }
        GxShapeAudit &operator+=(const GxShapeAudit &o);
    };

    /// `normal` is the side the surface is seen from, any length.
    GxShapeAudit auditGxShapes(const std::vector<GxRun> &runs, const double normal[3]);
} // namespace hlds
