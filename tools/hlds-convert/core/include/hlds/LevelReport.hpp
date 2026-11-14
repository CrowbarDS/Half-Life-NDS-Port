#pragma once

// What a cooked level is made of, as a DS would hold it: each section's
// bytes, the clip hulls one by one, and what reading the world's geometry
// from the card a page at a time (instead of holding all of it) would leave
// in memory. So that the sections can be ranked, and paging judged, without
// a console.
//
// The paging figures are of the simplest layout: the world's surfaces in
// the order the leaves first need them (leaf 1 first; a node's own surfaces
// before the leaves under it), cut into pages of `pageBytes`, a surface never
// across two. What a leaf needs is what its visibility row lists: those
// leaves' surfaces and those of every node above them (WorldDraw draws a
// node's own surfaces whenever it passes through it). A better layout can
// only do better; the exact figure says by how much at most.

#include <cstdint>
#include <vector>

#include "HlLevel.hpp"

namespace hldata
{
    struct LevelView;
}

namespace hlds
{
    /// A short name for each section ("gx stream", "clip nodes"...).
    const char *sectionName(hldata::Section section);

    struct GxPaging
    {
        std::uint32_t pageBytes = 0;
        std::uint32_t worldBytes = 0;  ///< GxStream of the world's own surfaces (model 0)
        std::uint32_t brushBytes = 0;  ///< ... of the brush entities', which stay in memory
        std::uint32_t pages = 0;       ///< the world's, as laid out
        /// Surfaces bigger than a page (each given pages of its own).
        std::uint32_t oversize = 0;
        /// The worst view (the leaf whose row lists the most): its surfaces'
        /// own bytes, and the pages they are on. And the mean over the leaves.
        std::uint32_t worstExactBytes = 0;
        std::uint32_t worstPages = 0;
        std::uint32_t meanPageBytes = 0;
        std::int32_t worstLeaf = -1;
        /// The light patches that land in the world's geometry (what could
        /// travel with its pages instead of staying in memory).
        std::uint32_t worldPatchBytes = 0;
        /// About what a DS would hold less of: the world's geometry, less a
        /// cache of the worst view's pages and a quarter more for what is
        /// read ahead. Negative: paging would cost memory.
        std::int32_t savingBytes() const
        {
            const std::uint32_t cache = worstPages * pageBytes + worstPages * pageBytes / 4;
            return static_cast<std::int32_t>(worldBytes) - static_cast<std::int32_t>(cache);
        }
    };

    struct LevelReport
    {
        std::uint32_t sectionBytes[hldata::kSectionCount] = {};
        /// ClipNodes reached from every model's hull of each kind: standing
        /// (1), crouched (3), large (2, the world's only), in bytes.
        std::uint32_t clipHullBytes[3] = {};
        GxPaging paging;
    };

    /// The report of a level `view` opened. `pageBytes`: a power of two.
    LevelReport reportLevel(const hldata::LevelView &view, std::uint32_t pageBytes = 4096);

    /// The GxStream cut for a DS that reads it a page at a time (HlDsCut.hpp):
    /// as it lies, never split inside a surface, a new page begun at a
    /// surface that would take the page past `pageWords`.
    struct DsGxPages
    {
        std::vector<std::uint32_t> starts; ///< each page's first word, then the stream's end
        std::uint32_t slotWords = 0;       ///< the longest page
        std::uint32_t worstWorldPages = 0; ///< the world's pages the worst view needs
        std::uint32_t brushPages = 0;      ///< pages holding the brush entities' (kept in)
        std::uint32_t slotCount = 0;       ///< what a DS keeps: the worst view's, a quarter more, the brushes'
        std::uint32_t pages() const { return starts.empty() ? 0 : static_cast<std::uint32_t>(starts.size() - 1); }
    };
    DsGxPages dsGxPages(const hldata::LevelView &view, std::uint32_t pageWords);

    /// The surfaces in the order a DS that pages the geometry wants them in
    /// the stream: the world's where the first leaf that sees them is (the
    /// nodes above a leaf before it), so that what one view needs lies
    /// together; then the brush entities', together at the end. Empty if
    /// the stream is not made of its surfaces' words alone, end to end.
    std::vector<std::uint32_t> dsGxOrder(const hldata::LevelView &view);
} // namespace hlds
