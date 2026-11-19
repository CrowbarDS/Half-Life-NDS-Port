#include "hlds/TextureBudget.hpp"
#include "hlds/TextureImage.hpp"

#include "hlds/TextureFamily.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "hlds/MapStats.hpp"

namespace hlds
{
    namespace
    {
        double faceArea(const BspFile &bsp, const BspFace &face)
        {
            const float *a = bsp.faceVertex(face, 0).point;
            double area = 0;
            for (int i = 1; i + 1 < face.edgeCount; ++i)
            {
                const float *b = bsp.faceVertex(face, i).point;
                const float *c = bsp.faceVertex(face, i + 1).point;
                const double e1[3] = {double(b[0]) - a[0], double(b[1]) - a[1], double(b[2]) - a[2]};
                const double e2[3] = {double(c[0]) - a[0], double(c[1]) - a[1], double(c[2]) - a[2]};
                const double n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                     e1[0] * e2[1] - e1[1] * e2[0]};
                area += 0.5 * std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            }
            return area;
        }

        /// A 4x4 block needs two to four colours of its own before it starts
        /// sharing, so the useful palette grows with the texel count.
        int coloursFor(int texels, int divisor)
        {
            return std::clamp(texels / divisor, 16, 512) & ~1;
        }
        /// A name the streaming may take: not a liquid ('!', "water"), a belt
        /// ("scroll") or a hole ('{'). (The cycles and the tiled variants are
        /// the families' business: planTextures.)
        bool streamableName(const std::string &name)
        {
            if (name.empty())
                return false;
            const char c = name[0];
            return c != '!' && c != '{' && c != '*' && name.rfind("water", 0) != 0 && name.rfind("scroll", 0) != 0;
        }

        /// For each leaf a view has (1..visLeafCount): the streamed textures
        /// any surface in its PVS wears, each once.
        std::vector<std::vector<int>> streamedViews(const BspFile &bsp, const std::vector<char> &streamed,
                                                    const std::vector<int> &canon, const std::vector<int> &otherOf)
        {
            const int leaves = bsp.visLeafCount();
            std::vector<std::vector<int>> own(leaves + 1), views(leaves + 1);
            std::vector<int> stamp(bsp.textures.size(), -1);
            for (int l = 1; l <= leaves; ++l)
            {
                const BspLeaf &leaf = bsp.leaves[l];
                for (int k = 0; k < leaf.markSurfaceCount; ++k)
                {
                    const BspFace &face = bsp.faces[bsp.markSurfaces[leaf.firstMarkSurface + k]];
                    const int worn = wornTexture(bsp, face);
                    if (worn < 0 || !streamed[worn])
                        continue;
                    // (A cycle is one picture in a view -- the one shown -- and
                    // so is its other face's, which a brush may show instead.)
                    for (const int t : {canon[worn], otherOf[worn]})
                    {
                        if (t >= 0 && stamp[t] != l)
                        {
                            stamp[t] = l;
                            own[l].push_back(t);
                        }
                    }
                }
            }
            std::fill(stamp.begin(), stamp.end(), -1);
            for (int l = 1; l <= leaves; ++l)
            {
                const std::vector<std::uint8_t> row = bsp.decompressVis(l);
                for (int other = 1; other <= leaves; ++other)
                {
                    if (other != l && !(row[(other - 1) >> 3] & (1 << ((other - 1) & 7))))
                        continue;
                    for (const int t : own[other])
                    {
                        if (stamp[t] != l)
                        {
                            stamp[t] = l;
                            views[l].push_back(t);
                        }
                    }
                }
            }
            return views;
        }

        /// The most, over every view, of `value(texture)` summed over the
        /// textures the view lists; and which view that is.
        template <class F>
        long worstView(const std::vector<std::vector<int>> &views, F value, int *which = nullptr)
        {
            long worst = 0;
            for (std::size_t l = 1; l < views.size(); ++l)
            {
                long sum = 0;
                for (const int t : views[l])
                    sum += value(t);
                if (sum > worst)
                {
                    worst = sum;
                    if (which != nullptr)
                        *which = static_cast<int>(l);
                }
            }
            return worst;
        }
    } // namespace

    std::vector<char> findHiddenTextures(const BspFile &bsp, const WadSet &wads)
    {
        std::vector<char> hidden(bsp.textures.size(), 0);
        for (std::size_t t = 0; t < bsp.textures.size(); ++t)
        {
            const BspTexture &tex = bsp.textures[t];
            if (tex.missing || tex.name.empty())
                continue;
            IndexedImage wadImage;
            std::string ignored;
            const std::vector<std::uint8_t> *pixels = nullptr;
            const std::vector<std::uint8_t> *palette = nullptr;
            if (tex.embedded)
            {
                pixels = &tex.pixels;
                palette = &tex.palette;
            }
            else if (wads.read(tex.name, wadImage, ignored))
            {
                pixels = &wadImage.pixels;
                palette = &wadImage.palette;
            }
            if (pixels == nullptr || pixels->empty())
                continue; // not found: reported as missing elsewhere
            const bool keyed = tex.name[0] == '{';
            if (keyed && std::all_of(pixels->begin(), pixels->end(), [](std::uint8_t p) { return p == 255; }))
            {
                hidden[t] = kHiddenEverywhere;
                continue;
            }
            // Dark all over: what it shows as added light is nothing (less
            // than the A3I5 form's faintest alpha).
            std::uint8_t most = 0;
            for (const std::uint8_t p : *pixels)
            {
                if ((keyed && p == 255) || static_cast<std::size_t>(p) * 3 + 2 >= palette->size())
                    continue;
                most = std::max({most, (*palette)[p * 3], (*palette)[p * 3 + 1], (*palette)[p * 3 + 2]});
            }
            if (brightened(most) < 16)
                hidden[t] = kHiddenAsLight;
        }
        return hidden;
    }

    TextureNeeds textureNeeds(const BspFile &bsp, const std::vector<BrushInfo> &brushes,
                              const std::vector<char> &hidden)
    {
        TextureNeeds needs;
        needs.opaque.assign(bsp.textures.size(), 0);
        needs.alpha.assign(bsp.textures.size(), 0);
        needs.light.assign(bsp.textures.size(), 0);
        needs.opaqueArea.assign(bsp.textures.size(), 0.0);
        needs.alphaArea.assign(bsp.textures.size(), 0.0);
        needs.lightArea.assign(bsp.textures.size(), 0.0);

        const std::vector<const BrushInfo *> owner = brushOfModel(bsp, brushes);
        for (std::size_t m = 0; m < bsp.models.size(); ++m)
        {
            const BrushInfo *brush = owner[m];
            // The world is always drawn. Any other model is drawn as its
            // entity says, and a model no entity names is not drawn at all.
            if (m != 0 && (brush == nullptr || !brush->drawn() ||
                           brush->renderClass == hldata::RenderClass::Colour))
                continue;
            const BspModel &model = bsp.models[m];
            for (int f = model.firstFace; f < model.firstFace + model.faceCount; ++f)
            {
                const BspFace &face = bsp.faces[f];
                const int t = wornTexture(bsp, face);
                const BspTexture &tex = bsp.textures[t];
                const bool light = brush != nullptr && brush->asLight();
                if (tex.missing || isUndrawnTexture(tex.name) ||
                    (!hidden.empty() && (hidden[t] == kHiddenEverywhere || (light && hidden[t] == kHiddenAsLight))))
                    continue;
                const bool holes = brush != nullptr && brush->cutsHoles() && !tex.name.empty() &&
                                   tex.name[0] == '{';
                (light ? needs.light : holes ? needs.alpha : needs.opaque)[t] = 1;
                (light ? needs.lightArea : holes ? needs.alphaArea : needs.opaqueArea)[t] += faceArea(bsp, face);
            }
        }
        // A texture that is one picture of several needs the others too,
        // each as much as itself: the rest of its cycle, and its other face.
        for (std::size_t t = 0; t < bsp.textures.size(); ++t)
        {
            if (bsp.textures[t].missing || bsp.textures[t].name.empty() || bsp.textures[t].name[0] != '+')
                continue;
            if (!needs.opaque[t] && !needs.alpha[t] && !needs.light[t])
                continue;
            const TextureFamily family = textureFamily(bsp, static_cast<int>(t));
            for (const std::vector<int> *part : {&family.frames, &family.other})
            {
                for (int member : *part)
                {
                    needs.opaque[member] = needs.opaque[member] || needs.opaque[t];
                    needs.alpha[member] = needs.alpha[member] || needs.alpha[t];
                    needs.light[member] = needs.light[member] || needs.light[t];
                    needs.opaqueArea[member] = std::max(needs.opaqueArea[member], needs.opaqueArea[t]);
                    needs.alphaArea[member] = std::max(needs.alphaArea[member], needs.alphaArea[t]);
                    needs.lightArea[member] = std::max(needs.lightArea[member], needs.lightArea[t]);
                }
            }
        }
        return needs;
    }

    namespace
    {
        /// Halve whichever texture is doing least with its memory until the
        /// total fits. Returns false if it cannot be made to.
        bool shrinkToFit(std::vector<TextureSize> &sizes, const std::vector<double> &area,
                         int budget, int &total, std::vector<char> &shrunk)
        {
            auto texelsOf = [&](std::size_t t)
            {
                return static_cast<int>(sizes[t].width) * sizes[t].height;
            };
            total = 0;
            for (std::size_t t = 0; t < sizes.size(); ++t)
                total += texelsOf(t);
            while (total > budget)
            {
                int pick = -1;
                double worst = 0;
                for (std::size_t t = 0; t < sizes.size(); ++t)
                {
                    const TextureSize &size = sizes[t];
                    if (size.width == 0 ||
                        (size.width <= kMinTextureSide && size.height <= kMinTextureSide))
                        continue;
                    const double density = area[t] / texelsOf(t);
                    if (pick < 0 || density < worst)
                    {
                        pick = static_cast<int>(t);
                        worst = density;
                    }
                }
                if (pick < 0)
                    return false;
                TextureSize &size = sizes[pick];
                total -= texelsOf(pick);
                if (size.width >= size.height && size.width > kMinTextureSide)
                    size.width /= 2;
                else if (size.height > kMinTextureSide)
                    size.height /= 2;
                else
                    size.width /= 2;
                total += texelsOf(pick);
                shrunk[pick] = 1;
            }
            return true;
        }
    } // namespace

    TexturePlan planTextures(const BspFile &bsp, const TextureNeeds &needs, int texelBudget,
                             int colourBudget, int alphaBudget, const StreamOptions &stream)
    {
        TexturePlan plan;
        plan.sizes.assign(bsp.textures.size(), TextureSize{});
        plan.alphaSizes.assign(bsp.textures.size(), TextureSize{});
        plan.lightSizes.assign(bsp.textures.size(), TextureSize{});
        plan.paletteColours.assign(bsp.textures.size(), 0);
        // Which are streamed (if any are): plain opaque ones, one picture and
        // one face.
        std::vector<char> streamed;
        std::vector<int> canon, otherOf;       // see below
        std::vector<std::vector<int>> cycleOf; // by a cycle's first picture: its pictures
        if (stream.enabled)
        {
            streamed.assign(bsp.textures.size(), 0);
            for (std::size_t t = 0; t < bsp.textures.size(); ++t)
            {
                const TextureFamily family = textureFamily(bsp, static_cast<int>(t));
                const bool isFamily = family.frames.size() > 1 || !family.other.empty();
                if (isFamily && !stream.families)
                    continue;
                // (All of a family's pictures or none: they are wanted together.)
                bool ok = true;
                for (const std::vector<int> *part : {&family.frames, &family.other})
                {
                    for (const int member : *part)
                        ok = ok && needs.opaque[member] && !needs.alpha[member] && !needs.light[member] &&
                             streamableName(bsp.textures[member].name);
                }
                streamed[t] = ok;
            }
            // Each picture of a cycle stands for its cycle's first, and the
            // first of a cycle that has another face has that cycle's first.
            canon.resize(bsp.textures.size());
            otherOf.assign(bsp.textures.size(), -1);
            cycleOf.assign(bsp.textures.size(), std::vector<int>());
            for (std::size_t t = 0; t < bsp.textures.size(); ++t)
            {
                canon[t] = static_cast<int>(t);
                const TextureFamily family = textureFamily(bsp, static_cast<int>(t));
                const std::string &name = bsp.textures[t].name;
                const bool inFrames = std::find(family.frames.begin(), family.frames.end(), static_cast<int>(t)) != family.frames.end();
                if (inFrames)
                {
                    canon[t] = family.frames[0];
                    if (!family.other.empty())
                        otherOf[t] = family.other[0];
                }
                else if (std::find(family.other.begin(), family.other.end(), static_cast<int>(t)) != family.other.end())
                    canon[t] = family.other[0];
                (void)name;
            }
            for (std::size_t t = 0; t < bsp.textures.size(); ++t)
            {
                if (streamed[t])
                    cycleOf[canon[t]].push_back(static_cast<int>(t));
            }
        }
        for (std::size_t t = 0; t < bsp.textures.size(); ++t)
        {
            const BspTexture &tex = bsp.textures[t];
            if (needs.opaque[t])
            {
                plan.sizes[t] = streamed.empty() || !streamed[t] ? defaultTextureSize(tex.width, tex.height)
                                                                 : streamedTextureSize(tex.width, tex.height);
                ++plan.usedTextures;
            }
            if (needs.alpha[t])
            {
                plan.alphaSizes[t] = defaultTextureSize(tex.width, tex.height);
                ++plan.usedTextures;
            }
            if (needs.light[t])
            {
                plan.lightSizes[t] = defaultTextureSize(tex.width, tex.height);
                ++plan.usedTextures;
            }
        }

        std::vector<char> shrunk(bsp.textures.size(), 0);
        if (streamed.empty())
        {
            if (!shrinkToFit(plan.sizes, needs.opaqueArea, texelBudget, plan.texels, shrunk))
                plan.fits = false;
        }
        else
        {
            // What stays in video memory has its share of the budget; the
            // streamed ones are planned below, once that is known.
            std::vector<TextureSize> resident = plan.sizes;
            for (std::size_t t = 0; t < resident.size(); ++t)
            {
                if (streamed[t])
                    resident[t] = TextureSize{};
            }
            if (!shrinkToFit(resident, needs.opaqueArea, texelBudget * stream.residentPercent / 100, plan.texels, shrunk))
                plan.fits = false;
            for (std::size_t t = 0; t < resident.size(); ++t)
            {
                if (!streamed[t])
                    plan.sizes[t] = resident[t];
            }
        }
        // (Both A3I5 forms share the one budget: shrunk as one list, the
        // light form after the other.)
        {
            const std::size_t count = bsp.textures.size();
            std::vector<TextureSize> both(plan.alphaSizes);
            both.insert(both.end(), plan.lightSizes.begin(), plan.lightSizes.end());
            std::vector<double> area(needs.alphaArea);
            area.insert(area.end(), needs.lightArea.begin(), needs.lightArea.end());
            std::vector<char> bothShrunk(2 * count, 0);
            if (!shrinkToFit(both, area, alphaBudget, plan.alphaTexels, bothShrunk))
                plan.fits = false;
            for (std::size_t t = 0; t < count; ++t)
            {
                plan.alphaSizes[t] = both[t];
                plan.lightSizes[t] = both[count + t];
                if (bothShrunk[t] || bothShrunk[count + t])
                    shrunk[t] = 1;
            }
        }
        plan.shrunk = static_cast<int>(std::count(shrunk.begin(), shrunk.end(), 1));
        // The pictures of one texture are all one size: the smallest any of
        // them was given. (One that changed size as it changed would jump.)
        for (std::vector<TextureSize> *sizes : {&plan.sizes, &plan.alphaSizes, &plan.lightSizes})
        {
            for (std::size_t t = 0; t < bsp.textures.size(); ++t)
            {
                const std::string &name = bsp.textures[t].name;
                if ((*sizes)[t].width == 0 || name.size() < 2 || name[0] != '+' || name[1] != '0')
                    continue;
                const TextureFamily family = textureFamily(bsp, static_cast<int>(t));
                TextureSize least = (*sizes)[t];
                for (const std::vector<int> *part : {&family.frames, &family.other})
                {
                    for (int member : *part)
                    {
                        if ((*sizes)[member].width == 0)
                            continue;
                        least.width = std::min(least.width, (*sizes)[member].width);
                        least.height = std::min(least.height, (*sizes)[member].height);
                    }
                }
                for (const std::vector<int> *part : {&family.frames, &family.other})
                {
                    for (int member : *part)
                    {
                        if ((*sizes)[member].width != 0)
                            (*sizes)[member] = least;
                    }
                }
            }
        }
        plan.texels = plan.alphaTexels = 0;
        for (std::size_t t = 0; t < bsp.textures.size(); ++t)
        {
            if (streamed.empty() || !streamed[t])
                plan.texels += static_cast<int>(plan.sizes[t].width) * plan.sizes[t].height;
            plan.alphaTexels += static_cast<int>(plan.alphaSizes[t].width) * plan.alphaSizes[t].height +
                                static_cast<int>(plan.lightSizes[t].width) * plan.lightSizes[t].height;
        }
        std::vector<std::vector<int>> views;
        if (!streamed.empty())
        {
            plan.streamed = streamed;
            plan.streamedTextures = static_cast<int>(std::count(streamed.begin(), streamed.end(), 1));
            plan.residentTexels = plan.texels;
            // (A tenth is left over: the faces of brush entities are not in a
            // view's count.)
            plan.regionTexels = std::max((texelBudget - plan.residentTexels) * 9 / 10, 16 * 1024);
            views = streamedViews(bsp, streamed, canon, otherOf);
            auto texelsOf = [&](int t) { return static_cast<long>(plan.sizes[t].width) * plan.sizes[t].height; };
            // The worst view must fit what is left of the budget: halve,
            // in that view, whatever does the least with its memory -- but
            // every texture of the view once before any is halved twice, and
            // so on. (Without that, small things that are all detail -- a
            // sign -- always lose: they cover the least wall for their texels
            // and would be halved down to nothing while the walls stay as
            // they were.)
            std::vector<int> halvings(bsp.textures.size(), 0);
            for (;;)
            {
                int which = 0;
                const long worst = worstView(views, texelsOf, &which);
                if (worst <= plan.regionTexels || which == 0)
                    break;
                int pick = -1;
                double least = 0;
                for (const int t : views[which])
                {
                    const TextureSize &size = plan.sizes[t];
                    if (size.width <= kMinTextureSide && size.height <= kMinTextureSide)
                        continue;
                    const double density = needs.opaqueArea[t] / static_cast<double>(texelsOf(t));
                    if (pick < 0 || halvings[t] < halvings[pick] || (halvings[t] == halvings[pick] && density < least))
                    {
                        pick = t;
                        least = density;
                    }
                }
                if (pick < 0)
                {
                    plan.fits = false;
                    break;
                }
                TextureSize &size = plan.sizes[pick];
                if (size.width >= size.height && size.width > kMinTextureSide)
                    size.width /= 2;
                else if (size.height > kMinTextureSide)
                    size.height /= 2;
                else
                    size.width /= 2;
                shrunk[pick] = 1;
                ++halvings[pick];
                // (The pictures of a cycle are all one size.)
                for (const int member : cycleOf[pick])
                {
                    plan.sizes[member] = size;
                    shrunk[member] = 1;
                }
            }
            plan.worstViewTexels = static_cast<int>(worstView(views, texelsOf));
            plan.shrunk = static_cast<int>(std::count(shrunk.begin(), shrunk.end(), 1));
            // (What the report calls the map's texels: all that is ever in
            // video memory at once, at the most.)
            plan.texels += plan.worstViewTexels;
        }

        // The palette bank is shared: every A3I5 texture takes 32 colours of
        // it, and the 4x4 textures divide what is left. One colour per 16
        // texels is generous; fall back to fewer for the whole map until the
        // total fits.
        int alphaColours = 0;
        for (std::size_t t = 0; t < plan.alphaSizes.size(); ++t)
        {
            if (plan.alphaSizes[t].width != 0)
                alphaColours += hldata::kA3I5Colours;
            if (t < plan.lightSizes.size() && plan.lightSizes[t].width != 0)
                alphaColours += hldata::kA3I5Colours;
        }
        static constexpr int kDivisors[] = {16, 24, 32, 48, 64, 96, 128};
        for (int divisor : kDivisors)
        {
            plan.colours = alphaColours;
            for (std::size_t t = 0; t < plan.sizes.size(); ++t)
            {
                if (plan.sizes[t].width == 0)
                    continue;
                plan.paletteColours[t] = coloursFor(
                    static_cast<int>(plan.sizes[t].width) * plan.sizes[t].height, divisor);
                // (A streamed texture's palette is in video memory only as
                // long as the texture is: the worst view's, below.)
                if (streamed.empty() || !streamed[t])
                    plan.colours += plan.paletteColours[t];
            }
            if (!views.empty())
                plan.colours += static_cast<int>(worstView(views, [&](int t) { return plan.paletteColours[t]; }));
            if (plan.colours <= colourBudget)
                break;
        }
        if (plan.colours > colourBudget)
            plan.fits = false;
        return plan;
    }

    TexturePlan planTextures(const BspFile &bsp, int texelBudget, int colourBudget,
                             const std::vector<char> &hidden)
    {
        // No entities: every model is a plain opaque one.
        std::vector<BrushInfo> everyModel;
        for (std::size_t m = 1; m < bsp.models.size(); ++m)
        {
            BrushInfo info;
            info.model = static_cast<int>(m);
            everyModel.push_back(info);
        }
        return planTextures(bsp, textureNeeds(bsp, everyModel, hidden), texelBudget, colourBudget,
                            kAlphaTexelBudget);
    }
    TexturePlan guestTexturePlan(const BspFile &bsp, const TexturePlan &plan, int texelBudget)
    {
        TexturePlan guest = plan;
        guest.streamed.clear();
        guest.streamedTextures = 0;
        guest.shrunk = 0;
        guest.fits = true;
        // The textures in groups that shrink together: a cycle's pictures,
        // or one texture alone.
        std::map<std::string, std::vector<std::size_t>> groups;
        long total = 0;
        for (std::size_t t = 0; t < guest.sizes.size() && t < bsp.textures.size(); ++t)
        {
            const TextureSize &size = guest.sizes[t];
            if (size.width == 0)
                continue;
            total += static_cast<long>(size.width) * size.height;
            const std::string &name = bsp.textures[t].name;
            if (name.empty() || name[0] == '!' || name.rfind("water", 0) == 0)
                continue; // (liquid: its own ways of being drawn)
            const bool cycle = name.size() > 2 && (name[0] == '+' || name[0] == '-');
            groups[cycle ? name.substr(0, 1) + name.substr(2) : "=" + name].push_back(t);
        }
        while (total > texelBudget)
        {
            // The group that takes the most.
            const std::vector<std::size_t> *largest = nullptr;
            long most = 0;
            for (const auto &[key, members] : groups)
            {
                long texels = 0;
                bool can = true;
                for (std::size_t t : members)
                {
                    const TextureSize &size = guest.sizes[t];
                    texels += static_cast<long>(size.width) * size.height;
                    can = can && std::max(size.width, size.height) > kMinTextureSide;
                }
                if (can && texels > most)
                {
                    most = texels;
                    largest = &members;
                }
            }
            if (largest == nullptr)
            {
                guest.fits = false;
                break;
            }
            // Its longer side halved (both, if square).
            for (std::size_t t : *largest)
            {
                TextureSize &size = guest.sizes[t];
                const long before = static_cast<long>(size.width) * size.height;
                const bool across = size.width >= size.height && size.width > kMinTextureSide;
                const bool down = size.height >= size.width && size.height > kMinTextureSide;
                if (across)
                    size.width = static_cast<std::uint16_t>(size.width / 2);
                if (down)
                    size.height = static_cast<std::uint16_t>(size.height / 2);
                total -= before - static_cast<long>(size.width) * size.height;
            }
        }
        guest.texels = static_cast<int>(total);
        for (std::size_t t = 0; t < guest.sizes.size(); ++t)
        {
            if (guest.sizes[t].width != plan.sizes[t].width || guest.sizes[t].height != plan.sizes[t].height)
                ++guest.shrunk;
        }
        return guest;
    }
} // namespace hlds
