#pragma once

// The map's path nodes, and the ways between them.
//
// The game's maps mark where a monster may stand with info_node entities and
// leave the engine to work out, when the map is first run, which can be
// walked between; it writes what it found beside the map
// (maps/graphs/<map>.nod), and those files come with the game. Where there
// is one, the nodes and links are taken from it (NodGraph.hpp) -- with the
// doors and breakable things each link passes through, and which sizes of
// creature each is for. (Each link is also tried with the very walk the DS
// makes, over the world alone, and those the trial cannot make are counted:
// they are kept, being nearly always over a floor that is a brush entity.)
// Where there is none, the links are worked out here as they always were.
//
// Either way one thing is added that the game has not got: a way DOWN, from
// a node at the top of a ledge to one at its foot, one way only.
//
// No table of routes is made. The console finds its way when it needs one
// (HlWalk.hpp, pathStep), over the links as they are at that moment.

#include <vector>

#include "hlds/EntityParser.hpp"
#include "hlds/LevelWriter.hpp"
#include "hlds/NodGraph.hpp"

namespace hlds
{
    struct PathReport
    {
        bool fromGraph = false; ///< the game's own graph was used
        int nodes = 0;
        int links = 0;
        int doorLinks = 0;      ///< ... of them, through a door or something that breaks
        int refused = 0;        ///< links of the game's a trial walk over the world alone could not make (kept)
        int dropLinks = 0;      ///< ways down added (none on the game's graph)
        int nodesLeftOut = 0;   ///< in the air, or more than the format holds
        int linksTooLong = 0;   ///< longer than a link's length holds (4,095 units): left out
        double longestLink = 0; ///< units, of those kept
    };

    /// Fills `level.pathNodes` and `level.pathLinks`. The level's planes,
    /// clip nodes, models and brushes must be in place. `graph`: the game's
    /// own, or null.
    PathReport cookPathNodes(const std::vector<Entity> &entities, CookedLevel &level, const NodGraph *graph = nullptr);
} // namespace hlds
