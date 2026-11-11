#include "hlds/LevelReport.hpp"

#include <vector>

#include "HlLevelReader.hpp"

namespace hlds
{
    using namespace hldata;

    const char *sectionName(Section section)
    {
        switch (section)
        {
        case Section::Planes:        return "planes";
        case Section::PlaneTypes:    return "plane types";
        case Section::Nodes:         return "nodes";
        case Section::Leaves:        return "leaves";
        case Section::Marks:         return "marks";
        case Section::Vis:           return "vis";
        case Section::Surfaces:      return "surfaces";
        case Section::GxStream:      return "gx stream";
        case Section::Models:        return "models";
        case Section::ClipNodes:     return "clip nodes";
        case Section::Textures:      return "textures";
        case Section::Entities:      return "entities";
        case Section::EntityText:    return "entity text";
        case Section::LightPatch:    return "light patch";
        case Section::Brushes:       return "brushes";
        case Section::BrushLeaves:   return "brush leaves";
        case Section::Logic:         return "logic";
        case Section::LogicAux:      return "logic lists";
        case Section::PathCorners:   return "path corners";
        case Section::NameHashes:    return "names";
        case Section::Landmarks:     return "landmarks";
        case Section::Strings:       return "strings";
        case Section::ModelRefs:     return "model refs";
        case Section::Actors:        return "actors";
        case Section::PathNodes:     return "path nodes";
        case Section::PathNext:      return "path next";
        case Section::Sounds:        return "sounds";
        case Section::LogicSounds:   return "logic sounds";
        case Section::Sentences:     return "sentences";
        case Section::SentenceWords: return "sentence words";
        case Section::Rooms:         return "rooms";
        case Section::Sprites:       return "sprites";
        case Section::Speech:        return "speech";
        case Section::Messages:      return "messages";
        case Section::ModelSounds:   return "model sounds";
        case Section::NodeMarks:     return "node marks";
        case Section::NodeSpans:     return "node spans";
        case Section::Count:         break;
        }
        return "?";
    }

    namespace
    {
        /// The clip nodes under `root` not yet `seen`, marked; how many.
        std::uint32_t reach(const LevelView &view, std::int32_t root, std::vector<char> &seen)
        {
            std::uint32_t count = 0;
            std::vector<std::int32_t> open;
            if (root >= 0)
                open.push_back(root);
            while (!open.empty())
            {
                const std::int32_t at = open.back();
                open.pop_back();
                if (at < 0 || static_cast<std::uint32_t>(at) >= view.clipNodeCount || seen[at] != 0)
                    continue;
                seen[at] = 1;
                ++count;
                for (const std::int16_t child : view.clipNodes[at].children)
                {
                    if (child >= 0)
                        open.push_back(child);
                }
            }
            return count;
        }

        /// The leaves leaf `from`'s row lists (itself too), as BSP rows go:
        /// a non-zero byte is eight leaves, a zero byte is followed by how
        /// many zero bytes it stands for; bit 0 of the first is leaf 1.
        void rowOf(const LevelView &view, std::uint32_t from, std::vector<std::uint32_t> &out)
        {
            out.clear();
            const std::uint32_t rows = view.header->visLeafCount;
            const std::int32_t ofs = view.leaves[from].visOfs;
            if (ofs < 0 || view.vis == nullptr)
            {
                for (std::uint32_t l = 1; l <= rows && l < view.leafCount; ++l)
                    out.push_back(l);
                return;
            }
            const std::uint8_t *in = view.vis + ofs;
            const std::uint8_t *end = view.vis + view.visSize;
            std::uint32_t index = 1;
            while (index <= rows && in < end)
            {
                const std::uint8_t bits = *in++;
                if (bits == 0)
                {
                    if (in >= end)
                        break;
                    index += 8u * *in++;
                    continue;
                }
                for (std::uint32_t b = 0; b < 8; ++b)
                {
                    if (((bits >> b) & 1u) != 0 && index + b <= rows && index + b < view.leafCount)
                        out.push_back(index + b);
                }
                index += 8;
            }
            bool self = false;
            for (const std::uint32_t l : out)
                self = self || l == from;
            if (!self)
                out.push_back(from);
        }
    } // namespace

    LevelReport reportLevel(const LevelView &view, std::uint32_t pageBytes)
    {
        LevelReport r;
        const auto *dir = reinterpret_cast<const SectionDir *>(reinterpret_cast<const std::uint8_t *>(view.header) +
                                                               sizeof(LevelHeader));
        for (int i = 0; i < kSectionCount; ++i)
            r.sectionBytes[i] = dir[i].size;

        // The hulls, each kind from every model's root of it.
        {
            std::vector<char> seen(view.clipNodeCount, 0);
            for (std::uint32_t m = 0; m < view.modelCount; ++m)
                r.clipHullBytes[0] += reach(view, view.models[m].clipStanding, seen) * sizeof(ClipNode);
            for (std::uint32_t m = 0; m < view.modelCount; ++m)
                r.clipHullBytes[1] += reach(view, view.models[m].clipCrouched, seen) * sizeof(ClipNode);
            for (std::uint32_t m = 0; m < view.modelCount; ++m)
                r.clipHullBytes[2] += reach(view, view.models[m].clipLarge, seen) * sizeof(ClipNode);
        }

        GxPaging &p = r.paging;
        p.pageBytes = pageBytes;
        if (view.modelCount == 0 || view.surfaceCount == 0 || pageBytes < 4)
            return r;
        const std::uint32_t first = view.models[0].firstSurface;
        const std::uint32_t last = first + view.models[0].surfaceCount;
        auto isWorld = [&](std::uint32_t s) { return s >= first && s < last && s < view.surfaceCount; };
        for (std::uint32_t s = 0; s < view.surfaceCount; ++s)
            (isWorld(s) ? p.worldBytes : p.brushBytes) += view.surfaces[s].gxWords * 4u;

        // Which words of the stream are the world's: the light patches there.
        {
            std::vector<char> world(view.gxWordCount, 0);
            for (std::uint32_t s = first; s < last && s < view.surfaceCount; ++s)
            {
                const Surface &surface = view.surfaces[s];
                for (std::uint32_t w = 0; w < surface.gxWords; ++w)
                    world[surface.gxOfs + w] = 1;
            }
            for (std::uint32_t i = 0; i < view.lightPatchCount; ++i)
            {
                if (view.lightPatches[i].word < view.gxWordCount && world[view.lightPatches[i].word] != 0)
                    p.worldPatchBytes += sizeof(LightPatch);
            }
        }

        // The nodes above each leaf (root last).
        auto above = [&](std::uint32_t leaf, std::vector<std::uint32_t> &out)
        {
            out.clear();
            std::uint32_t node = view.leaves[leaf].parent;
            while (node != kNoParent && node < view.nodeCount && out.size() <= view.nodeCount)
            {
                out.push_back(node);
                node = view.nodes[node].parent;
            }
        };
        auto nodeSurfaces = [&](std::uint32_t node, auto &&each)
        {
            if (node >= view.nodeSpanCount)
                return;
            const NodeSpan &span = view.nodeSpans[node];
            for (std::uint32_t k = 0; k < span.count; ++k)
                each(view.nodeMarks[span.first + k]);
        };
        auto leafSurfaces = [&](std::uint32_t leaf, auto &&each)
        {
            const Leaf &l = view.leaves[leaf];
            for (std::uint32_t k = 0; k < l.markCount; ++k)
                each(view.marks[l.firstMark + k]);
        };

        // The layout: each world surface where the first leaf needing it is.
        constexpr std::uint32_t kNone = 0xFFFFFFFFu;
        std::vector<std::uint32_t> pageOf(view.surfaceCount, kNone);
        std::uint32_t page = 0;
        std::uint32_t used = 0;
        auto place = [&](std::uint32_t s)
        {
            if (!isWorld(s) || pageOf[s] != kNone)
                return;
            const std::uint32_t bytes = view.surfaces[s].gxWords * 4u;
            if (bytes > pageBytes)
            {
                // (Pages of its own, the first of them named for it.)
                ++p.oversize;
                page += used != 0 ? 1 : 0;
                pageOf[s] = page;
                page += (bytes + pageBytes - 1) / pageBytes;
                used = 0;
                return;
            }
            if (used + bytes > pageBytes)
            {
                ++page;
                used = 0;
            }
            pageOf[s] = page;
            used += bytes;
        };
        std::vector<std::uint32_t> nodes;
        for (std::uint32_t leaf = 1; leaf < view.leafCount; ++leaf)
        {
            above(leaf, nodes);
            for (auto n = nodes.rbegin(); n != nodes.rend(); ++n)
                nodeSurfaces(*n, place);
            leafSurfaces(leaf, place);
        }
        for (std::uint32_t s = first; s < last && s < view.surfaceCount; ++s)
            place(s); // (any no leaf names)
        p.pages = page + (used != 0 ? 1 : 0);
        const auto pagesOf = [&](std::uint32_t s) -> std::uint32_t
        {
            const std::uint32_t bytes = view.surfaces[s].gxWords * 4u;
            return bytes > pageBytes ? (bytes + pageBytes - 1) / pageBytes : 1;
        };

        // Each leaf's view: its surfaces' bytes and their pages.
        std::vector<std::uint32_t> pageStamp(p.pages + 1, 0);
        std::vector<std::uint32_t> surfaceStamp(view.surfaceCount, 0);
        std::vector<std::uint32_t> nodeStamp(view.nodeCount, 0);
        std::vector<std::uint32_t> row;
        std::uint64_t pageSum = 0;
        std::uint32_t leaves = 0;
        const std::uint32_t rows = view.header->visLeafCount;
        for (std::uint32_t leaf = 1; leaf <= rows && leaf < view.leafCount; ++leaf)
        {
            if (view.leaves[leaf].contents == kContentsSolid)
                continue;
            const std::uint32_t stamp = leaf;
            std::uint32_t bytes = 0;
            std::uint32_t pages = 0;
            auto take = [&](std::uint32_t s)
            {
                if (!isWorld(s) || surfaceStamp[s] == stamp)
                    return;
                surfaceStamp[s] = stamp;
                bytes += view.surfaces[s].gxWords * 4u;
                for (std::uint32_t k = 0; k < pagesOf(s); ++k)
                {
                    if (pageStamp[pageOf[s] + k] != stamp)
                    {
                        pageStamp[pageOf[s] + k] = stamp;
                        ++pages;
                    }
                }
            };
            rowOf(view, leaf, row);
            for (const std::uint32_t seen : row)
            {
                leafSurfaces(seen, take);
                above(seen, nodes);
                for (const std::uint32_t n : nodes)
                {
                    if (nodeStamp[n] == stamp)
                        break; // (and every node above it)
                    nodeStamp[n] = stamp;
                    nodeSurfaces(n, take);
                }
            }
            pageSum += pages;
            ++leaves;
            if (pages > p.worstPages)
            {
                p.worstPages = pages;
                p.worstLeaf = static_cast<std::int32_t>(leaf);
            }
            if (bytes > p.worstExactBytes)
                p.worstExactBytes = bytes;
        }
        if (leaves != 0)
            p.meanPageBytes = static_cast<std::uint32_t>(pageSum * pageBytes / leaves);
        return r;
    }
} // namespace hlds
