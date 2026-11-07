#pragma once

// Lightmaps to vertex colours.
//
// GoldSrc lights a face with a small texture, one RGB sample every 16 units.
// The DS has no second texture stage and nowhere near the texture memory to
// bake light into the first, so each vertex takes the lightmap's value at that
// vertex and the hardware interpolates across the polygon. Faces are at most
// 240 units across (the map compiler splits them for the lightmap's sake), so
// what is lost is detail inside a face, not the overall light.
//
// LIGHT STYLES. A face carries up to four lightmap layers, one per style. Style
// 0 is plain light. 1..31 flicker or pulse; 32 and up belong to lights a
// button switches. What is baked into a vertex is its light as the map
// starts (a switched light that starts off left out, a flickering one at its
// steady strength); and for every vertex such light reaches, the level's
// LightPatch section says what each style adds, so that the console can work
// its colour out again as they change (sampleLayers, changingLayers).

#include <cstdint>
#include <vector>

#include "hlds/BspFile.hpp"
#include "hlds/EntityParser.hpp"

namespace hlds
{
    struct Rgb
    {
        std::uint8_t r = 0, g = 0, b = 0;
    };

    /// A face's lightmap rectangle in texture space (xash3d's
    /// Mod_CalcSurfaceExtents): which 16-texel sample its corner sits on, and how
    /// many samples it spans.
    struct LightExtents
    {
        int minS = 0, minT = 0;   ///< in samples
        int width = 0, height = 0; ///< in samples
    };

    class LightBake
    {
    public:
        /// What is made of every lightmap value v (0..1) on its way to a
        /// vertex: scale * v^power, cut off at full. This is the game's own
        /// way with its lightmaps as MEASURED -- the real game photographed
        /// beside the port at the same places with tools/ref/measure.py,
        /// and each patch of wall solved for its texture and its light: the
        /// game shows light a third stronger than the file has it, and
        /// lifts it by the same power it lifts its textures by (0.8: its
        /// "texture gamma" of 2 on a screen of 2.5). The plain power that
        /// was here before (v^(1/1.8)) came out a quarter too dark all the
        /// way up; a plain scale, which is what a reimplementation of the
        /// renderer suggests, no better.
        static constexpr double kDefaultScale = 338.0 / 256.0;
        static constexpr double kDefaultPower = 0.8;

        LightBake(const BspFile &bsp, const std::vector<Entity> &entities,
                  double scale = kDefaultScale, double power = kDefaultPower);

        LightExtents extents(const BspFace &face) const;
        /// A face's texture coordinates at vertex i, in source texels.
        void texCoords(const BspFace &face, int i, double &s, double &t) const;
        /// The light at texture coordinate (s, t) of `face`, scaled.
        Rgb sample(const BspFace &face, double s, double t) const;

        bool styleIsOn(int style) const;

        /// The same, of some of the face's (up to four) layers only: bit n of
        /// `layers` is its n-th. Whether a layer's style starts on is not
        /// asked.
        Rgb sampleLayers(const BspFace &face, double s, double t, unsigned layers) const;
        /// Which of a face's layers are of light that changes -- any style
        /// but 0 -- as bits (0: all of its light is steady).
        unsigned changingLayers(const BspFace &face) const;
        /// Where a layer of a face is brightest: the texture coordinates
        /// of its brightest sample. False if the face has no such layer.
        bool brightest(const BspFace &face, int layer, double &s, double &t) const;

    private:
        const BspFile &bsp_;
        std::vector<char> styleOff_; ///< by style number; 1 = switched off at map start
        std::uint8_t gammaTable_[256];
    };
} // namespace hlds
