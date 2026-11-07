#include "hlds/LightBake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace hlds
{
    namespace
    {
        constexpr int kSampleSpacing = 16; // texels between lightmap samples
        constexpr int kFirstSwitchableStyle = 32;
        constexpr int kLightStartOff = 1; // spawnflag on light, light_spot
    } // namespace

    LightBake::LightBake(const BspFile &bsp, const std::vector<Entity> &entities, double scale, double power)
        : bsp_(bsp), styleOff_(256, 0)
    {
        for (int i = 0; i < 256; ++i)
        {
            gammaTable_[i] = static_cast<std::uint8_t>(
                std::min(255L, std::lround(255.0 * scale * std::pow(i / 255.0, power))));
        }

        // A switchable style is off only if EVERY light using it starts off:
        // several lights can share one style, and one of them lit is light.
        std::vector<char> seenOn(256, 0);
        std::vector<char> seenOff(256, 0);
        for (const Entity &entity : entities)
        {
            const std::string_view cls = entity.className();
            if (cls != "light" && cls != "light_spot" && cls != "light_environment")
                continue;
            const int style = std::atoi(std::string(entity.value("style", "0")).c_str());
            if (style < kFirstSwitchableStyle || style > 255)
                continue;
            const int flags = std::atoi(std::string(entity.value("spawnflags", "0")).c_str());
            ((flags & kLightStartOff) ? seenOff : seenOn)[style] = 1;
        }
        for (int style = kFirstSwitchableStyle; style < 256; ++style)
        {
            styleOff_[style] = (seenOff[style] && !seenOn[style]) ? 1 : 0;
        }
    }

    bool LightBake::styleIsOn(int style) const
    {
        return style >= 0 && style < 255 && !styleOff_[style];
    }

    void LightBake::texCoords(const BspFace &face, int i, double &s, double &t) const
    {
        const BspTexInfo &ti = bsp_.texInfos[face.texInfo];
        const float *p = bsp_.faceVertex(face, i).point;
        // Doubles, as the engine does it: the sums decide which sample a corner
        // lands on, and float error at a boundary shifts the whole lightmap.
        s = static_cast<double>(p[0]) * ti.vecs[0][0] + static_cast<double>(p[1]) * ti.vecs[0][1] +
            static_cast<double>(p[2]) * ti.vecs[0][2] + ti.vecs[0][3];
        t = static_cast<double>(p[0]) * ti.vecs[1][0] + static_cast<double>(p[1]) * ti.vecs[1][1] +
            static_cast<double>(p[2]) * ti.vecs[1][2] + ti.vecs[1][3];
    }

    LightExtents LightBake::extents(const BspFace &face) const
    {
        double lo[2] = {1e30, 1e30};
        double hi[2] = {-1e30, -1e30};
        for (int i = 0; i < face.edgeCount; ++i)
        {
            double st[2];
            texCoords(face, i, st[0], st[1]);
            for (int k = 0; k < 2; ++k)
            {
                lo[k] = std::min(lo[k], st[k]);
                hi[k] = std::max(hi[k], st[k]);
            }
        }
        LightExtents e;
        e.minS = static_cast<int>(std::floor(lo[0] / kSampleSpacing));
        e.minT = static_cast<int>(std::floor(lo[1] / kSampleSpacing));
        e.width = static_cast<int>(std::ceil(hi[0] / kSampleSpacing)) - e.minS + 1;
        e.height = static_cast<int>(std::ceil(hi[1] / kSampleSpacing)) - e.minT + 1;
        return e;
    }

    unsigned LightBake::changingLayers(const BspFace &face) const
    {
        const bool special = (bsp_.texInfos[face.texInfo].flags & kTexSpecial) != 0;
        if (special || bsp_.lighting.empty() || face.lightOfs < 0)
            return 0;
        unsigned layers = 0;
        for (int layer = 0; layer < 4 && face.styles[layer] != 255; ++layer)
        {
            if (face.styles[layer] != 0)
                layers |= 1u << layer;
        }
        return layers;
    }

    bool LightBake::brightest(const BspFace &face, int layer, double &s, double &t) const
    {
        if (layer < 0 || layer >= 4 || face.lightOfs < 0 || bsp_.lighting.empty())
            return false;
        for (int l = 0; l <= layer; ++l)
        {
            if (face.styles[l] == 255)
                return false;
        }
        const LightExtents e = extents(face);
        const std::size_t layerBytes = static_cast<std::size_t>(e.width) * e.height * 3;
        const std::size_t base = static_cast<std::size_t>(face.lightOfs) + layer * layerBytes;
        if (layerBytes == 0 || base + layerBytes > bsp_.lighting.size())
            return false;
        const std::uint8_t *px = bsp_.lighting.data() + base;
        int best = -1, at = 0;
        for (int i = 0; i < e.width * e.height; ++i)
        {
            const int v = std::max({px[i * 3], px[i * 3 + 1], px[i * 3 + 2]});
            if (v > best)
            {
                best = v;
                at = i;
            }
        }
        s = (e.minS + at % e.width) * static_cast<double>(kSampleSpacing);
        t = (e.minT + at / e.width) * static_cast<double>(kSampleSpacing);
        return true;
    }

    Rgb LightBake::sample(const BspFace &face, double s, double t) const
    {
        // (As the map starts: every layer whose style is on.)
        unsigned layers = 0;
        for (int layer = 0; layer < 4 && face.styles[layer] != 255; ++layer)
        {
            if (styleIsOn(face.styles[layer]))
                layers |= 1u << layer;
        }
        return sampleLayers(face, s, t, layers);
    }

    Rgb LightBake::sampleLayers(const BspFace &face, double s, double t, unsigned layers) const
    {
        const bool special = (bsp_.texInfos[face.texInfo].flags & kTexSpecial) != 0;
        if (special || bsp_.lighting.empty())
            return Rgb{255, 255, 255}; // water and the like, or a map compiled unlit
        if (face.lightOfs < 0)
            return Rgb{0, 0, 0}; // lit map, but no light reaches this face

        const LightExtents e = extents(face);
        const std::size_t layerBytes = static_cast<std::size_t>(e.width) * e.height * 3;

        // Position in samples, clamped onto the lightmap, then bilinear.
        const double fs = std::clamp(s / kSampleSpacing - e.minS, 0.0, e.width - 1.0);
        const double ft = std::clamp(t / kSampleSpacing - e.minT, 0.0, e.height - 1.0);
        const int s0 = static_cast<int>(fs);
        const int t0 = static_cast<int>(ft);
        const int s1 = std::min(s0 + 1, e.width - 1);
        const int t1 = std::min(t0 + 1, e.height - 1);
        const double ws = fs - s0;
        const double wt = ft - t0;

        double sum[3] = {0, 0, 0};
        for (int layer = 0; layer < 4; ++layer)
        {
            const int style = face.styles[layer];
            if (style == 255)
                break; // layers are packed: the first unused one ends the list
            const std::size_t base = static_cast<std::size_t>(face.lightOfs) + layer * layerBytes;
            if (base + layerBytes > bsp_.lighting.size())
                break; // a lightmap that runs off the lump: use what is there
            if (!(layers & (1u << layer)))
                continue;
            const std::uint8_t *px = bsp_.lighting.data() + base;
            for (int c = 0; c < 3; ++c)
            {
                const double top = px[(t0 * e.width + s0) * 3 + c] * (1 - ws) +
                                   px[(t0 * e.width + s1) * 3 + c] * ws;
                const double bottom = px[(t1 * e.width + s0) * 3 + c] * (1 - ws) +
                                      px[(t1 * e.width + s1) * 3 + c] * ws;
                sum[c] += top * (1 - wt) + bottom * wt;
            }
        }
        Rgb out;
        out.r = gammaTable_[std::min(255L, std::lround(sum[0]))];
        out.g = gammaTable_[std::min(255L, std::lround(sum[1]))];
        out.b = gammaTable_[std::min(255L, std::lround(sum[2]))];
        return out;
    }
} // namespace hlds
