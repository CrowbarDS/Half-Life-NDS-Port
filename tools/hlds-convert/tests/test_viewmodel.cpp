// The first-person models without what is never on the screen
// (core/src/ViewModelTrim.cpp), on the game's own weapons:
//
//  - no triangle that can be seen is left out: every one on the screen in
//    the widest view with no margin -- in any frame of any sequence, and
//    halfway between frames -- is kept;
//  - the cooked model (a DS's and a DSi's) has no more polygons than it did,
//    and says, for each clip, how many can be on the screen at once at the
//    usual view and the widest -- no fewer than are, with no margin, in any
//    frame of the clip or halfway between two;
//  - a model the game also puts in the world (the tripmine's) is as it was;
//  - the file still opens, and says the counts.
//
// It prints, for each weapon, its polygons (the most any choice of its body
// draws, at its finest level) before and after the trim, and what the pass's
// budget counts for it held at rest: its first idle clip at the usual view
// and the widest, and the last frame of its draw at the usual view (where
// the crowbar, which has no idle in the game, stays). Needs HLDS_VALVE_DIR; skipped without it.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "TestBspBuilder.hpp"
#include "HlModel.hpp"
#include "hlds/MdlFile.hpp"
#include "hlds/ModelCook.hpp"
#include "hlds/ModelMesh.hpp"
#include "hlds/ViewModelTrim.hpp"

using namespace hlds;
using namespace hldstest;
namespace fs = std::filesystem;

namespace
{
    using Key = std::vector<int>;

    /// Each triangle of a sub-model, told apart by its mesh and corners.
    Key keyOf(std::size_t part, std::size_t model, const MeshTriangle &t)
    {
        Key key = {static_cast<int>(part), static_cast<int>(model), t.mesh};
        for (const MdlCorner &c : t.c)
            key.insert(key.end(), {c.vertex, c.normal, c.s, c.t});
        return key;
    }

    struct Cooked
    {
        int polygons = 0;
        std::vector<int> narrow, wide; ///< per clip (a clip for each sequence, in order)
        std::vector<int> narrowEnd, wideEnd; ///< ... on its last frame
        bool opens = false;
    };

    Cooked cook(const MdlFile &mdl, bool full)
    {
        std::vector<std::string> clips;
        for (std::size_t s = 0; s < mdl.sequences.size(); ++s)
            clips.push_back("@" + std::to_string(s));
        CookedModel cooked;
        ModelCookReport report;
        std::string error;
        Cooked out;
        if (!cookModel(mdl, clips, cooked, report, error, 64 * 64, full))
        {
            std::printf("test_viewmodel: %s: %s\n", mdl.name.c_str(), error.c_str());
            return out;
        }
        out.polygons = cooked.header.polygons;
        std::vector<std::uint8_t> bytes;
        hldata::ModelView view;
        out.opens = writeModel(cooked, bytes, error) && view.open(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        for (int c = 0; out.opens && view.screenCounts != nullptr && c < view.header->clipCount; ++c)
        {
            out.narrow.push_back(view.screenPolygons(c, false));
            out.wide.push_back(view.screenPolygons(c, true));
            out.narrowEnd.push_back(view.screenPolygons(c, false, true));
            out.wideEnd.push_back(view.screenPolygons(c, true, true));
        }
        return out;
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || !fs::is_directory(fs::path(dir) / "models"))
    {
        std::printf("test_viewmodel: HLDS_VALVE_DIR not set: skipped\n");
        return 77;
    }
    std::vector<fs::path> files;
    for (const auto &entry : fs::directory_iterator(fs::path(dir) / "models"))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("v_", 0) == 0 && entry.path().extension() == ".mdl")
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    check(!files.empty(), "the game has first-person models");

    // (Strictly: the widest view with nothing opened, the bob's push only.)
    const ViewCone strict = widestView(0, 0);
    const ViewCone wide = widestView();
    int sumBefore = 0, sumAfter = 0, sumIdle = 0;
    std::printf("%-16s %7s %7s %8s %8s %8s %9s %9s\n", "model", "DS was", "DS now", "idle 90", "idle 110w",
                "drawn 90", "DSi was", "DSi now");
    for (const fs::path &file : files)
    {
        MdlFile mdl;
        std::string error;
        if (!mdl.load(file, error))
        {
            std::printf("test_viewmodel: %s\n", error.c_str());
            check(false, "a first-person model loads");
            continue;
        }
        const bool trims = isViewModel(mdl.name);

        // ---- nothing seen is left out
        std::vector<std::vector<std::array<double, 12>>> poses, between;
        check(viewPoses(mdl, poses, error) && viewPoses(mdl, between, error, true), "its poses decode");
        MdlFile trimmed = mdl;
        int kept = 0;
        const int had = trimViewModel(trimmed, poses, wide, kept);
        std::set<Key> keptKeys;
        for (std::size_t p = 0; p < trimmed.bodyParts.size(); ++p)
        {
            for (std::size_t m = 0; m < trimmed.bodyParts[p].models.size(); ++m)
            {
                for (const MeshTriangle &t : meshTriangles(trimmed.bodyParts[p].models[m]))
                    keptKeys.insert(keyOf(p, m, t));
            }
        }
        int seenLost = 0;
        for (std::size_t p = 0; p < mdl.bodyParts.size(); ++p)
        {
            for (std::size_t m = 0; m < mdl.bodyParts[p].models.size(); ++m)
            {
                const MdlSubModel &model = mdl.bodyParts[p].models[m];
                const std::vector<MeshTriangle> triangles = meshTriangles(model);
                const std::vector<char> seen = seenTriangles(model, between, strict);
                for (std::size_t t = 0; t < triangles.size(); ++t)
                    seenLost += seen[t] && keptKeys.count(keyOf(p, m, triangles[t])) == 0;
            }
        }
        if (seenLost != 0 && kept != 0)
            std::printf("test_viewmodel: %s: %d triangles seen and left out\n", mdl.name.c_str(), seenLost);
        check(seenLost == 0 || kept == 0, "no triangle on the screen is left out");
        check(kept <= had, "trimming adds nothing");

        // ---- the cooked model, as it was (cooked under another name) and now
        MdlFile asWas = mdl;
        asWas.name = "x" + mdl.name;
        const Cooked wasDs = cook(asWas, false), nowDs = cook(mdl, false);
        const Cooked wasDsi = cook(asWas, true), nowDsi = cook(mdl, true);
        check(wasDs.opens && nowDs.opens && wasDsi.opens && nowDsi.opens, "the cooked files open");
        check(nowDs.polygons <= wasDs.polygons && nowDsi.polygons <= wasDsi.polygons, "no more polygons than before");
        // (Kept whole: the tripmine's, and one never on the screen.)
        const bool counted = trims && kept > 0;
        check(counted ? !nowDs.narrow.empty() && !nowDsi.narrow.empty() : nowDs.narrow.empty(),
              "a first-person model, and only one, says what its clips show");
        if (!counted)
            check(nowDs.polygons == wasDs.polygons, "a model kept whole is as it was");
        int idle = 0, draw = -1;
        for (std::size_t s = mdl.sequences.size(); s-- > 0;)
        {
            if (mdl.sequences[s].label.find("idle") != std::string::npos)
                idle = static_cast<int>(s);
            if (mdl.sequences[s].label.find("draw") != std::string::npos ||
                mdl.sequences[s].label.find("deploy") != std::string::npos)
                draw = static_cast<int>(s);
        }
        for (std::size_t c = 0; counted && c < nowDs.narrow.size(); ++c)
        {
            check(nowDs.narrow[c] <= nowDs.wide[c] && nowDs.wide[c] <= nowDs.polygons &&
                      nowDsi.narrow[c] <= nowDsi.wide[c] && nowDsi.wide[c] <= nowDsi.polygons,
                  "a clip shows no more than the model has, and the usual view no more than the widest");
            // The DSi's finest level is the trimmed model itself: no fewer
            // counted than it shows with no margin, halfway between frames
            // too.
            std::vector<std::vector<std::array<double, 12>>> clip, halves;
            check(sequencePoses(trimmed, static_cast<int>(c), clip, error), "a clip's poses decode");
            halves = clip;
            for (std::size_t f = 0; f + 1 < clip.size(); ++f)
            {
                std::vector<std::array<double, 12>> half(clip[f].size());
                for (std::size_t b = 0; b < half.size(); ++b)
                    for (std::size_t k = 0; k < 12; ++k)
                        half[b][k] = (clip[f][b][k] + clip[f + 1][b][k]) * 0.5;
                halves.push_back(std::move(half));
            }
            int shows[2] = {0, 0};
            const ViewCone strictViews[2] = {narrowView(0, 0), widestView(0, 0)};
            for (int w = 0; w < 2; ++w)
            {
                for (const MdlBodyPart &part : trimmed.bodyParts)
                {
                    int most = 0;
                    for (const MdlSubModel &model : part.models)
                        most = std::max(most, screenMost(model, halves, strictViews[w]));
                    shows[w] += most;
                }
            }
            if (shows[0] > nowDsi.narrow[c] || shows[1] > nowDsi.wide[c])
                std::printf("test_viewmodel: %s clip %zu shows %d/%d, counted %d/%d\n", mdl.name.c_str(), c, shows[0],
                            shows[1], nowDsi.narrow[c], nowDsi.wide[c]);
            check(shows[0] <= nowDsi.narrow[c] && shows[1] <= nowDsi.wide[c], "a clip counts all it shows");
            // ... and on its last frame (of each blend), where it stays
            // once done if it does not loop.
            const std::size_t frames = static_cast<std::size_t>(mdl.sequences[c].frameCount);
            std::vector<std::vector<std::array<double, 12>>> last;
            for (std::size_t at = 0; frames != 0 && at + frames <= clip.size(); at += frames)
                last.push_back(clip[at + frames - 1]);
            int showsEnd[2] = {0, 0};
            for (int w = 0; w < 2; ++w)
            {
                for (const MdlBodyPart &part : trimmed.bodyParts)
                {
                    int most = 0;
                    for (const MdlSubModel &model : part.models)
                        most = std::max(most, screenMost(model, last, strictViews[w]));
                    showsEnd[w] += most;
                }
            }
            check(nowDsi.narrowEnd[c] <= nowDsi.narrow[c] && nowDsi.wideEnd[c] <= nowDsi.wide[c] &&
                      nowDs.narrowEnd[c] <= nowDs.narrow[c] && nowDs.wideEnd[c] <= nowDs.wide[c],
                  "a clip's last frame shows no more than all of it");
            check(showsEnd[0] <= nowDsi.narrowEnd[c] && showsEnd[1] <= nowDsi.wideEnd[c],
                  "a clip's last frame counts all it shows");
        }
        const int idleNarrow = counted ? nowDs.narrow[static_cast<std::size_t>(idle)] : nowDs.polygons;
        const int idleWide = counted ? nowDs.wide[static_cast<std::size_t>(idle)] : nowDs.polygons;
        const int drawEnd = counted && draw >= 0 ? nowDs.narrowEnd[static_cast<std::size_t>(draw)] : nowDs.polygons;
        std::printf("%-16s %7d %7d %8d %8d %8d %9d %9d\n", mdl.name.c_str(), wasDs.polygons, nowDs.polygons, idleNarrow,
                    idleWide, drawEnd, wasDsi.polygons, nowDsi.polygons);
        if (counted)
        {
            sumBefore += wasDs.polygons;
            sumAfter += nowDs.polygons;
            sumIdle += idleNarrow;
        }
    }
    if (!files.empty())
        std::printf("trimmed, DS: %d polygons were %d; counted held at rest, usual view: %d\n", sumAfter, sumBefore,
                    sumIdle);
    return g_failures == 0 ? 0 : 1;
}
