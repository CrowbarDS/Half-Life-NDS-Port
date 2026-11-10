#pragma once

// A parsed .bsp to a cooked level.
//
// This is where a GoldSrc map stops being GoldSrc's and becomes the DS's:
// floats become fixed point, faces become geometry-engine command runs with
// their light baked in, and everything the console will not use is left behind
// -- tool faces, the third clip hull, planes nothing references.
//
// What it keeps is the map's own structure: the same BSP tree, the same leaves,
// the same visibility rows. The DS finds the camera's leaf and reads the PVS
// exactly as the PC engine does; it just has much less to do afterwards.

#include <map>
#include <string>
#include <vector>

#include "hlds/PathNodes.hpp"
#include "hlds/SoundCook.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/LevelWriter.hpp"
#include "hlds/Portals.hpp"
#include "hlds/LightBake.hpp"
#include "hlds/TextureBudget.hpp"
#include "hlds/TitlesFile.hpp"

namespace hlds
{
    struct CookOptions
    {
        double lightScale = LightBake::kDefaultScale;
        double lightPower = LightBake::kDefaultPower;
        /// Merge faces and drop mid-edge vertices where that provably changes
        /// nothing visible (MeshOptimize.hpp). Off leaves every vertex of the
        /// map in place and only shares them between neighbouring faces.
        bool guardedMerges = true;
        /// No surface of the world spans two leaves: a face is merged and
        /// grouped only with faces of the same leaf (the first that lists
        /// it), so that a surface is drawn only where its own leaf is seen
        /// (the portal flood: HlPortals.hpp). Off: across leaves, as before.
        bool groupByLeaf = true;
        /// The sizes a console sent the map has its textures at
        /// (guestTexturePlan), said in each TextureRef::guestHalvings; null:
        /// as the host has them.
        const TexturePlan *guestPlan = nullptr;
            /// The map's sky, when its six pictures are there to be cooked: the
        /// name they go by ("2desert"). Empty: the map is cooked without one.
        std::string skyName;
        /// The models that were cooked for this map (ModelNeed::name): the
        /// actors made of them are in the level, the rest are left out.
        std::vector<std::string> models;
        /// The game's own graph of the map's nodes (NodGraph.hpp); may be
        /// null, and then the ways between them are worked out.
        const NodGraph *nodGraph = nullptr;
        /// What the game's textures are made of (Materials.hpp); may be
        /// null, and then everything is concrete.
        const struct MaterialBook *materials = nullptr;
        /// The sounds those models make in their clips
        /// (CookedModel::eventSounds).
        std::vector<std::string> modelSounds;
        /// The weapons' view models the map was not cooked with, and the
        /// sounds of their clips: named in the level after `models`, to be
        /// loaded out of the common archive if one is taken up
        /// (kModelRefLazy, kSoundLazy).
        std::vector<std::string> lazyModels;
        std::vector<std::string> lazyModelSounds;
        /// Which of `models` is the one the player holds; empty for none.
        std::string viewModel;
        /// The game's sentences.txt, parsed; null if it was not read.
        const SentenceBook *sentences = nullptr;
        /// The game's titles.txt, parsed; null if it was not read.
        const TitlesBook *titles = nullptr;
        /// The name in titles.txt of the title of the chapter this map is
        /// part of; empty if it is not known.
        std::string chapter;
    };

    struct CookReport
    {
        std::vector<std::string> warnings;
        PathReport paths; ///< the nodes and the ways between them
        int brushesReachingFar = 0; ///< solid brushes whose collision reaches past their box (Brush::pad)
        int surfaces = 0;
        int polygons = 0;      ///< every surface of the map
        int vertices = 0;
        int faces = 0;         ///< drawn faces of the map, before any merging
        /// What one quad or strip per face would have cost: the baseline the
        /// grouping and merging are measured against.
        int facePolygons = 0;
        int faceVertices = 0;
        int merges = 0;          ///< faces merged into a neighbour
        int removedVertices = 0; ///< mid-edge vertices dropped
        int weldedVertices = 0;  ///< vertices given to sides that ran past them
        double sourceArea = 0;   ///< of every drawn face, square units, on the grid
        double lostArea = 0;     ///< of that, what no surface covers: should be nothing
        int droppedFaces = 0;  ///< tool and sky faces, and faces of undrawn brushes
        int brushes = 0;       ///< brush entities cooked
        int hiddenBrushes = 0; ///< of those, solid but never drawn
        int logicRecords = 0;  ///< entities that do something
        /// Classes the port does not handle yet, with how many the map has.
        std::map<std::string, int> unsupported;
        int droppedClipNodes = 0;
        int droppedPlanes = 0;
        int portals = 0;     ///< the holes between leaves (Portals.cpp)
        /// World surfaces made of faces of more than one leaf: none, unless
        /// CookOptions::groupByLeaf is off.
        int surfacesSpanningLeaves = 0;
        /// The worst a leaf's PVS asks for, in all directions: the header's
        /// worst before it was taken as it is seen (Portals.cpp's seenWorst).
        int pvsWorstPolygons = 0;
        int pvsWorstVertices = 0;
        int seenGivenUp = 0;               ///< floods of seenWorst that gave up
        std::vector<SeenWorst::Hotspot> hotspots; ///< the worst leaves as seen
    };

    /// `plan` is the texture plan (TextureBudget.hpp): the cooked size of each
    /// texture in each form, which geometry is built against, and -- as width
    /// 0 -- the textures left out, whose faces are not drawn. A default
    /// (empty) plan means every texture at its default size, which is what
    /// the tests that have no plan use.
    bool cookLevel(const BspFile &bsp, const CookOptions &options, const TexturePlan &plan,
                   CookedLevel &level, CookReport &report, std::string &error);
} // namespace hlds
