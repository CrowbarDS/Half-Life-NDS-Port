// hlds-convert: turns a Half-Life installation into the files the DS port
// reads. A thin driver -- everything it does is in hlds_convert_core.

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "HlLevelReader.hpp"
#include "hlds/TextureFamily.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/Converter.hpp"
#include "hlds/Cooker.hpp"
#include "hlds/EntityParser.hpp"
#include "hlds/GxStream.hpp"
#include "hlds/MapStats.hpp"
#include "hlds/Pack.hpp"
#include "hlds/TextureCook.hpp"
#include "hlds/WadFile.hpp"

namespace fs = std::filesystem;
using namespace hlds;

namespace
{
    // The geometry engine's per-frame limits (GBATEK, "DS 3D Polygon/Vertex RAM").
    constexpr int kPolygonLimit = 2048;
    constexpr int kVertexLimit = 6144;

    int usage()
    {
        std::fprintf(stderr,
                     "usage:\n"
                     "  hlds-convert --version\n"
                     "  hlds-convert info <map.bsp> [--valve <game folder>] [--entities]\n"
                     "      What the map holds and what it will cost the DS to draw.\n"
                     "      --valve also checks that every texture can be found.\n"
                     "  hlds-convert cook-map <game folder> <map name> -o <card root> [options]\n"
                     "  hlds-convert cook-all <game folder> -o <card root> [options]\n"
                     "      Cook one map, or every map, and put it on the card under\n"
                     "      <card root>/_nds/hl_nds/data/: one archive a map in maps/, one for\n"
                     "      what every map shares, the music and the manifest.\n"
                     "      --stage <folder>   where the converter keeps its own files, which\n"
                     "                         the archives are made from and the next run\n"
                     "                         finds again (default: <card root>-stage)\n"
                     "      --no-compress      archives with nothing in them compressed\n"
                     "      --no-textures      levels only; the DS draws them in light alone\n"
                     "      --ptexconv <path>  the texture encoder, if it is not found by\n"
                     "                         itself (it comes with BlocksDS)\n"
                     "      --light-scale <s>  how strong the baked light is (1.32, the game's)\n"
                     "      --light-power <p>  ... and the power it is lifted by (0.8, the game's)\n"
                     "      --geometry <mode>  guarded (default): merge faces and drop mid-edge\n"
                     "                         vertices where the picture provably does not\n"
                     "                         change. lossless: keep every vertex of the map\n"
                     "      --no-stream-textures  do not stream: by default plain textures only\n"
                     "                         the world wears are cooked at their own size and\n"
                     "                         streamed in as they come into view; without\n"
                     "                         streaming they are shrunk until the whole map fits\n"
                     "      --jobs <n>         maps cooked at once\n"
                     "                         (default: threads available, up to 6)\n"
                     "      --hotspots         list each map's ten worst leaves as seen\n"
                     "                         (where, which way, what they cost)\n"
                     "      --group-across-leaves  let a surface span two leaves (as before\n"
                     "                         the portals): fewer surfaces, drawn whole\n"
                     "                         wherever any of their leaves is seen\n"
                     "      --plain-tiles      do not vary the game's '-' textures face by\n"
                     "                         face: fewer textures and polygons, and\n"
                     "                         floors that repeat\n"
                     "      --start <map>      the map a new game starts in (c0a0)\n"
                     "  hlds-convert pack -o <card root> [--stage <folder>] [--no-compress]\n"
                     "      Make the card's archives again from the stage, cooking nothing.\n"
                     "  hlds-convert verify <level.hlm | map.npac> [--brushes]\n"
                     "      Open a cooked level with the DS's own reader, decode every\n"
                     "      surface, and print what it costs.\n");
        return 2;
    }

    int cmdInfo(const std::vector<std::string> &args)
    {
        fs::path bspPath;
        fs::path valveDir;
        bool listEntities = false;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--valve" && i + 1 < args.size())
                valveDir = args[++i];
            else if (args[i] == "--entities")
                listEntities = true;
            else if (bspPath.empty())
                bspPath = args[i];
            else
                return usage();
        }
        if (bspPath.empty())
            return usage();

        BspFile bsp;
        std::string error;
        if (!bsp.load(bspPath, error))
        {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        const MapStats stats = computeMapStats(bsp);

        std::printf("%s\n", bspPath.filename().string().c_str());
        std::printf("  bounds      (%.0f %.0f %.0f) .. (%.0f %.0f %.0f)\n", stats.mins[0],
                    stats.mins[1], stats.mins[2], stats.maxs[0], stats.maxs[1], stats.maxs[2]);
        std::printf("  lumps       %zu planes, %zu vertices, %zu faces, %zu nodes, %zu leaves,\n"
                    "              %zu clipnodes, %zu models, %zu KB lighting, %zu KB vis\n",
                    bsp.planes.size(), bsp.vertices.size(), bsp.faces.size(), bsp.nodes.size(),
                    bsp.leaves.size(), bsp.clipNodes.size(), bsp.models.size(),
                    bsp.lighting.size() / 1024, bsp.visibility.size() / 1024);
        std::printf("  world       %d faces, %d drawn -> %d triangles, or %d polygons / %d "
                    "vertices as quads\n",
                    stats.worldFaces, stats.drawnWorldFaces, stats.worldCost.triangles,
                    stats.worldCost.polygons, stats.worldCost.vertices);
        std::printf("  brush ents  %d drawn faces in %zu models\n", stats.brushModelFaces,
                    bsp.models.size() - 1);
        std::printf("  textures    %d (%d embedded), %d used by drawn faces\n",
                    stats.textureCount, stats.embeddedTextures, stats.usedTextures);

        if (!valveDir.empty())
        {
            std::vector<Entity> entities;
            if (!parseEntities(bsp.entities, entities, error))
            {
                std::fprintf(stderr, "error: %s\n", error.c_str());
                return 1;
            }
            std::vector<std::string> missingWads;
            WadSet wads;
            wads.openForMap(valveDir, entities.empty() ? "" : entities[0].value("wad"),
                            missingWads);
            int unresolved = 0;
            for (const BspTexture &tex : bsp.textures)
            {
                if (tex.missing || tex.embedded || isUndrawnTexture(tex.name))
                    continue;
                if (!wads.contains(tex.name))
                {
                    std::printf("  MISSING texture '%s'\n", tex.name.c_str());
                    ++unresolved;
                }
            }
            std::printf("  wads        %zu opened, %zu not found, %d textures unresolved\n",
                        wads.wadCount(), missingWads.size(), unresolved);
            for (const std::string &name : missingWads)
                std::printf("              not found: %s\n", name.c_str());
        }

        std::printf("\n  PVS load per leaf (upper bound: no frustum or distance culling)\n");
        std::printf("    leaves    %d\n", stats.visLeaves);
        std::printf("    median    %d polygons\n", stats.medianPolygons);
        std::printf("    90%%       %d polygons\n", stats.p90Polygons);
        std::printf("    worst     %d polygons / %d vertices (%d triangles) from leaf %d\n",
                    stats.worst.cost.polygons, stats.worst.cost.vertices,
                    stats.worst.cost.triangles, stats.worst.leaf);
        std::printf("    limit     %d polygons / %d vertices per pass\n", kPolygonLimit,
                    kVertexLimit);
        for (std::size_t b = 0; b < stats.histogram.size(); ++b)
        {
            if (stats.histogram[b] == 0)
                continue;
            std::printf("    %5zu..%-5zu %5d leaves\n", b * MapStats::kBucket,
                        (b + 1) * MapStats::kBucket - 1, stats.histogram[b]);
        }

        if (listEntities)
        {
            std::printf("\n  entities\n");
            for (const auto &entry : stats.classCounts)
                std::printf("    %4d  %s\n", entry.second, entry.first.c_str());
        }
        return 0;
    }

    void printArchive(const MapResult &r)
    {
        if (r.archiveMembers != 0)
            std::printf("  on the card: %d KB in one archive of %d files (%d KB inflated)\n", r.archiveKb,
                        r.archiveMembers, r.archiveRawKb);
    }

    /// --hotspots: each map's worst leaves as seen, where and which way.
    bool verboseHotspots = false;

    void printResult(const MapResult &r)
    {
        for (const std::string &warning : r.warnings)
            std::printf("  warning: %s\n", warning.c_str());
        if (!r.ok)
        {
            std::printf("  FAILED: %s\n", r.error.c_str());
            return;
        }
        std::printf("  %zu KB (stream %d KB); %d faces -> %d surfaces, %d -> %d vertices (%d%%)\n"
                    "  worst leaf %d polygons / %d vertices; %d brush entities, %d logic "
                    "records, %d entities of %d classes not handled yet\n"
                    "  %d textures (%d encoded): %d KB compressed texels, %d KB A3I5\n",
                    r.levelBytes / 1024, r.streamKb, r.faces, r.surfaces, r.faceVertices,
                    r.vertices, r.faceVertices ? r.vertices * 100 / r.faceVertices : 100,
                    r.worstPolygons, r.worstVertices, r.brushes, r.logicRecords,
                    r.unsupportedEntities, r.unsupportedClasses, r.texturesUsed,
                    r.texturesEncoded, r.texelKb, r.alphaTexelKb);
        if (!r.unsupported.empty())
        {
            std::printf("    not handled:");
            for (const auto &entry : r.unsupported)
                std::printf(" %s (%d)", entry.first.c_str(), entry.second);
            std::printf("\n");
        }
        if (r.streamedTextures != 0)
            std::printf("  %d streamed: %d KB texels total; single view up to %d KB (%d KB free), %d KB resident\n",
                        r.streamedTextures, r.streamTotalKb, r.streamWorstViewKb, r.streamRegionKb, r.streamResidentKb);
        // What can be seen, as the portals have it: the worst a view asks
        // for (and the PVS's figure it replaced), how far it reaches, and
        // whether that makes the map heavy (its fog always on).
        std::printf("  %d portals; seen worst %d polygons / %d vertices (PVS %d / %d); far %d; %s\n", r.portals,
                    r.worstPolygons, r.worstVertices, r.pvsWorstPolygons, r.pvsWorstVertices, r.farUnits,
                    hldata::levelIsHeavy(r.worstPolygons, r.worstVertices) ? "heavy (fog always)" : "light");
        if (verboseHotspots)
        {
            for (const auto &spot : r.hotspots)
                std::printf("    hotspot leaf %d at %d %d %d yaw %d pitch %d: %d polygons / %d vertices\n", spot.leaf,
                            spot.at[0], spot.at[1], spot.at[2], spot.yaw, spot.pitch, spot.polygons, spot.vertices);
        }
        printArchive(r);
        if (r.stoodIn != 0)
            std::printf("  a multiplayer map: %d of its pickups are of weapons not in the port yet, and are "
                        "given as ones that are\n",
                        r.stoodIn);
        if (r.pathNodes != 0)
            std::printf("  %d nodes, %d ways between them (%s; %d through doors and what breaks, %d not walked by a "
                        "trial over the world alone, %d ways down added)\n",
                        r.pathNodes, r.pathLinks, r.pathFromGraph ? "the game's graph" : "worked out", r.pathDoorLinks,
                        r.pathRefused, r.pathDrops);
        if (r.models != 0)
        {
            std::printf("  %d actors of %d models: %d KB, skins %dK texels\n", r.actors, r.models,
                        r.modelKb, r.modelTexelKb);
            // (One of each model, added up: how much a level of detail saves.)
            std::printf("    levels of detail: %d triangles near, %d far (the game's own: %d; a DSi has those too, in %d KB)\n",
                        r.modelTriangles, r.modelTrianglesFar, r.modelTrianglesFull, r.modelKbDsi);
        }
        if (r.guestTexelKb != 0)
            std::printf("  sent to a console: textures %dK texels, all at once (%d of them smaller there)\n",
                        r.guestTexelKb, r.guestShrunk);
        if (r.sprites != 0 || r.spritesMissing != 0)
            std::printf("  %d sprites: %dK texels (%d not found)\n", r.sprites, r.spriteTexelKb, r.spritesMissing);
        if (r.sounds != 0 || r.soundsMissing != 0)
        {
            std::printf("  %d sounds: %d KB (%d at half rate, %d not found); %d music tracks\n",
                        r.sounds, r.soundKb, r.soundsHalved, r.soundsMissing, r.musicTracks);
            // (What a DSi is given over a DS.)
            if (r.soundsTwinned != 0)
                std::printf("    on a DSi: %d KB, %d of them at a better rate\n", r.soundKbDsi, r.soundsTwinned);
        }
    }

    /// cook-map and cook-all: the same run, over one map or over all of them.
    int cmdCook(const std::vector<std::string> &args, bool all)
    {
        ConvertOptions options;
        std::string mapName;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == "-o" && i + 1 < args.size())
                options.cardRoot = args[++i];
            else if (args[i] == "--no-textures")
                options.textures = false;
            else if (args[i] == "--ptexconv" && i + 1 < args.size())
                options.ptexconv = args[++i];
            else if (args[i] == "--light-power" && i + 1 < args.size())
                options.lightPower = std::atof(args[++i].c_str());
            else if ((args[i] == "--light-scale" || args[i] == "--light-gamma") && i + 1 < args.size())
                options.lightScale = std::atof(args[++i].c_str());
            else if (args[i] == "--geometry" && i + 1 < args.size())
            {
                const std::string &mode = args[++i];
                if (mode != "guarded" && mode != "lossless")
                    return usage();
                options.guardedMerges = mode == "guarded";
            }
            else if (args[i] == "--stream-textures")
                options.streamTextures = true;
            else if (args[i] == "--no-stream-textures")
                options.streamTextures = false;
            else if (args[i] == "--jobs" && i + 1 < args.size())
                options.jobs = std::atoi(args[++i].c_str());
            else if (args[i] == "--plain-tiles")
                hlds::setRandomTiling(false);
            else if (args[i] == "--group-across-leaves")
                options.groupByLeaf = false;
            else if (args[i] == "--hotspots")
                verboseHotspots = true;
            else if (args[i] == "--start" && i + 1 < args.size())
                options.startMap = args[++i];
            else if (args[i] == "--stage" && i + 1 < args.size())
                options.stageRoot = args[++i];
            else if (args[i] == "--no-compress")
                options.compress = false;
            else if (options.gameDir.empty())
                options.gameDir = args[i];
            else if (!all && mapName.empty())
                mapName = args[i];
            else
                return usage();
        }
        if (options.gameDir.empty() || options.cardRoot.empty() || options.lightScale <= 0 ||
            (!all && mapName.empty()))
            return usage();

        Converter converter(options);
        const std::string problem = converter.preflight();
        if (!problem.empty())
        {
            std::fprintf(stderr, "error: %s\n", problem.c_str());
            return 1;
        }

        const std::vector<std::string> names =
            all ? converter.listMaps() : std::vector<std::string>{mapName};
        std::string error;
        const int failed = converter.cookMaps(
            names,
            [&](std::size_t index, const std::string &name)
            {
                std::printf("[%zu/%zu] %s\n", index + 1, names.size(), name.c_str());
                std::fflush(stdout);
            },
            printResult, nullptr, error);
        if (!error.empty())
            std::fprintf(stderr, "error: %s\n", error.c_str());
        // What was made for the card as a whole.
        const CommonResult &common = converter.common();
        std::printf("for the card: %d of 3 fonts, %s, %s, %s, %s, %s, %d menu sounds; %s, %s, %s\n", common.fonts,
                    common.hud ? "the head-up display" : "no head-up display",
                    common.icons ? "the weapons' pictures" : "no weapons' pictures",
                    common.title ? "the game's title" : "no title",
                    common.decals ? "the marks" : "no marks",
                    common.loading ? "the loading picture" : "no loading picture", common.sounds,
                    common.menu ? "the menu's background" : "no menu background",
                    common.logo ? "the animated title" : "no animated title",
                    common.intro ? "the Valve intro" : "no intro");
        for (const std::string &warning : common.warnings)
            std::printf("  warning: %s\n", warning.c_str());
        if (!common.guest.empty())
            std::printf("sent to a console with no card (its memory, textures aside):\n");
        for (const std::string &line : common.guest)
            std::printf("%s\n", line.c_str());
        if (all)
            std::printf("%zu maps, %d failed\n", names.size(), failed);
        return failed == 0 ? 0 : 1;
    }

    /// pack: the card's archives again, from the stage.
    int cmdPack(const std::vector<std::string> &args)
    {
        ConvertOptions options;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == "-o" && i + 1 < args.size())
                options.cardRoot = args[++i];
            else if (args[i] == "--stage" && i + 1 < args.size())
                options.stageRoot = args[++i];
            else if (args[i] == "--no-compress")
                options.compress = false;
            else if (args[i] == "--start" && i + 1 < args.size())
                options.startMap = args[++i];
            else
                return usage();
        }
        if (options.cardRoot.empty())
            return usage();
        Converter converter(options);
        std::string error;
        int maps = 0;
        long cardKb = 0, rawKb = 0;
        const int failed = converter.packAll(
            [&](const MapResult &r)
            {
                ++maps;
                cardKb += r.archiveKb;
                rawKb += r.archiveRawKb;
                if (!r.ok)
                    std::printf("%s: FAILED: %s\n", r.name.c_str(), r.error.c_str());
            },
            error);
        if (!error.empty())
            std::fprintf(stderr, "error: %s\n", error.c_str());
        for (const std::string &warning : converter.common().warnings)
            std::printf("  warning: %s\n", warning.c_str());
        // What each map takes of the memory of a console sent it.
        if (!converter.common().guest.empty())
            std::printf("sent to a console with no card (its memory, textures aside):\n");
        for (const std::string &line : converter.common().guest)
            std::printf("%s\n", line.c_str());
        std::printf("%d maps packed, %d failed: %ld MB on the card for %ld MB of files\n", maps, failed,
                    cardKb / 1024, rawKb / 1024);
        return failed == 0 ? 0 : 1;
    }

    int cmdVerify(const std::vector<std::string> &allArgs)
    {
        std::vector<std::string> args;
        bool listBrushes = false;
        for (const std::string &arg : allArgs)
        {
            if (arg == "--brushes")
                listBrushes = true;
            else
                args.push_back(arg);
        }
        if (args.size() != 1)
            return usage();
        std::vector<std::uint8_t> bytes;
        std::string error;
        // (A map's archive: the level in it, put together as the console
        // puts it together.)
        std::size_t archiveBytes = 0;
        if (fs::path(args[0]).extension() == ".npac" ? !unpackLevel(args[0], bytes, archiveBytes, error)
                                                     : !readFile(args[0], bytes, error))
        {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
        hldata::LevelView view;
        const hldata::LevelError result =
            view.open(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        if (result != hldata::LevelError::Ok)
        {
            std::fprintf(stderr, "error: %s\n", hldata::levelErrorText(result));
            return 1;
        }

        // Read every surface the way the console will, and hold each to the
        // costs its record claims: the renderer budgets from those.
        long polygons = 0;
        long vertices = 0;
        GxShapeAudit shapes;
        for (std::uint32_t s = 0; s < view.surfaceCount; ++s)
        {
            const hldata::Surface &surface = view.surfaces[s];
            std::vector<GxRun> runs;
            if (!decodeGxStream(view.gxStream + surface.gxOfs, surface.gxWords, runs, error))
            {
                std::fprintf(stderr, "error: surface %u: %s\n", s, error.c_str());
                return 1;
            }
            int p = 0;
            int v = 0;
            for (const GxRun &run : runs)
            {
                p += run.polygons();
                v += static_cast<int>(run.vertices.size());
            }
            if (p != surface.polygons || v != surface.vertices)
            {
                std::fprintf(stderr, "error: surface %u claims %d/%d but holds %d/%d\n", s,
                             surface.polygons, surface.vertices, p, v);
                return 1;
            }
            polygons += p;
            vertices += v;

            // And to a shape the hardware draws from every side it is seen.
            const hldata::Plane &plane =
                view.planes[surface.planeRef & hldata::kSurfacePlaneMask];
            const double sign = (surface.planeRef & hldata::kSurfacePlaneBack) ? -1.0 : 1.0;
            const double normal[3] = {sign * plane.normal[0], sign * plane.normal[1],
                                      sign * plane.normal[2]};
            const GxShapeAudit one = auditGxShapes(runs, normal);
            if (!one.clean())
            {
                std::fprintf(stderr,
                             "error: surface %u: %d polygons lead with three vertices in line, "
                             "%d are wound backwards, %d have a reflex corner, %d have no area\n",
                             s, one.flatLead, one.reversed, one.reflex, one.noArea);
            }
            shapes += one;
        }
        if (!shapes.clean())
            return 1;

        const auto *dir = reinterpret_cast<const hldata::SectionDir *>(
            bytes.data() + sizeof(hldata::LevelHeader));
        static const char *const kNames[hldata::kSectionCount] = {
            "planes",    "plane types",  "nodes",    "leaves",     "marks",
            "vis",       "surfaces",     "gx stream", "models",    "clip nodes",
            "textures",  "entities",     "entity text", "light patch", "brushes",
            "brush leaves", "logic",     "logic lists", "path corners", "names",
            "landmarks", "strings",   "models",     "actors",      "path nodes",
            "path next", "sounds",    "logic sounds", "sentences", "sentence words",
            "rooms",     "sprites",   "speech",     "messages",    "model sounds"};
        std::printf("%s: ok, version %u, %zu KB\n", fs::path(args[0]).filename().string().c_str(),
                    view.header->version, bytes.size() / 1024);
        for (int i = 0; i < hldata::kSectionCount; ++i)
        {
            std::printf("  %-12s %7u records %8.1f KB\n", kNames[i], dir[i].count,
                        dir[i].size / 1024.0);
        }
        std::printf("  every surface decodes: %ld polygons, %ld vertices in all\n", polygons,
                    vertices);
        std::printf("  every polygon leads with a turn, the way its surface faces\n");
        std::printf("  worst leaf by PVS: %u polygons, %u vertices (limit %d / %d a pass)\n",
                    view.header->worstPolygons, view.header->worstVertices, kPolygonLimit,
                    kVertexLimit);
        if (listBrushes)
        {
            static const char *const kClasses[] = {"opaque", "cutout", "blend", "colour", "hidden"};
            std::printf("  brush entities (model, class, alpha, solid, bounds, leaves, surfaces)\n");
            for (std::uint32_t i = 0; i < view.brushCount; ++i)
            {
                const hldata::Brush &b = view.brushes[i];
                std::printf("    *%-3u %-6s a%-2u %s (%d %d %d)..(%d %d %d) leaves %u surfaces %u\n",
                            b.model, kClasses[b.renderClass], b.alpha,
                            (b.flags & hldata::kBrushSolid) ? "solid" : "     ", b.mins[0],
                            b.mins[1], b.mins[2], b.maxs[0], b.maxs[1], b.maxs[2], b.leafCount,
                            view.models[b.model].surfaceCount);
            }
        }
        std::printf("  resident on the DS: about %zu KB\n", bytes.size() / 1024);
        return 0;
    }
} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2)
        return usage();
    const std::string command = argv[1];
    if (command == "--version")
    {
        std::printf("hlds-convert %s\n", hlds::kConverterVersion);
        return 0;
    }
    const std::vector<std::string> args(argv + 2, argv + argc);
    if (command == "info")
        return cmdInfo(args);
    if (command == "cook-map")
        return cmdCook(args, false);
    if (command == "cook-all")
        return cmdCook(args, true);
    if (command == "pack")
        return cmdPack(args);
    if (command == "verify")
        return cmdVerify(args);
    return usage();
}
