#pragma once

// A first-person model (v_*) without what is never on the screen.
//
// The game's weapons are modelled with the arms that hold them, most of
// which is below and beside the picture: of the crowbar's 329 polygons, the
// hardware kept 48 (melonDS, c1a0). Every one of them was still counted in
// the pass's budget, sent and clipped away. So a triangle is kept only if it
// can be on the screen: in some frame of some sequence (and blend) of the
// model, posed as the game poses it, it is not wholly outside one edge of
// the widest view the model is ever seen through.
//
// THE VIEW. The model is drawn at the eye, turned as the view is
// (shared/HlView.hpp): what moves the picture moves it too, but for the
// walk's bob pushing it forward (kViewHeldPushMost; forward only brings it
// nearer the middle). The widest picture is the widest field of view
// (kViewFovMost across on 4:3) on the widest screen (16:10, as high, wider).
// Every edge is then opened by kViewTrimMargin degrees more, and the model
// pushed kViewTrimPush units further: for a sway or lag the view may come to
// have, the console's keys being within a degree of the frames, its
// projection's whole degrees, and frames played between frames.
//
// WHAT IS COUNTED. Even trimmed, a weapon's swing shows much more of the
// arm than it is held at: the crowbar idles with 48 of its 329 polygons on
// the screen at the usual view, and swings with all of them. So each clip
// says the most of the model on the screen in any of its frames, at any of
// the model's levels, in two views: the usual one (kViewFovNarrow or less,
// on 4:3) and the widest (kModelScreenCounts). The pass's budget counts the
// clip in hand's, at the view there is; a profiling build counts what the
// hardware keeps of it and says in perf.txt if that ever runs over -- the
// sign of a count too low, or of a triangle left out that is seen.

#include <array>
#include <string>
#include <vector>

#include "hlds/MdlFile.hpp"

namespace hlds
{
    /// Degrees each edge of the widest picture is opened by, and units the
    /// model is pushed forward by, over what the view is known to do.
    inline constexpr double kViewTrimMargin = 4.0;
    inline constexpr double kViewTrimPush = 2.0;

    /// A view: the eye at the origin of the model's space, looking along x,
    /// y to the left and z up; an edge is where |y| = x * tanAcross, or
    /// |z| = x * tanHigh, with x pushed `push` further.
    struct ViewCone
    {
        double tanAcross = 1, tanHigh = 0.75, push = 0;
    };
    /// A field of view `fovAcross` across on 4:3 (as the fov command has
    /// it), on a 16:10 screen if `wide` (as high, wider), opened by `margin`
    /// degrees each way and pushed `push` units more than the bob does.
    ViewCone viewCone(int fovAcross, bool wide, double margin = kViewTrimMargin, double push = kViewTrimPush);
    /// The widest the model is ever seen through.
    ViewCone widestView(double margin = kViewTrimMargin, double push = kViewTrimPush);
    /// The usual view, the widest of them (kViewFovNarrow on 4:3).
    ViewCone narrowView(double margin = kViewTrimMargin, double push = kViewTrimPush);

    /// Every bone's matrix (mdlBoneMatrices) in every frame of sequence
    /// `sequence`, of each of its blends.
    bool sequencePoses(const MdlFile &mdl, int sequence, std::vector<std::vector<std::array<double, 12>>> &out,
                       std::string &error);

    /// Every bone's matrix (mdlBoneMatrices) in every frame of every
    /// sequence and blend; with `between`, also halfway from each frame to
    /// the next (the console plays them in between).
    bool viewPoses(const MdlFile &mdl, std::vector<std::vector<std::array<double, 12>>> &out, std::string &error,
                   bool between = false);

    /// Whether a triangle (its corners in the model's space) is not wholly
    /// outside one edge of `view`, nor wholly behind the eye.
    bool triangleOnScreen(const std::array<double, 3> *corners, const ViewCone &view);

    /// For each triangle of `model` (meshTriangles' order): whether it is
    /// on the screen in any of `poses`.
    std::vector<char> seenTriangles(const MdlSubModel &model,
                                    const std::vector<std::vector<std::array<double, 12>>> &poses,
                                    const ViewCone &view);

    /// Leave out of every sub-model of `mdl` the triangles on the screen in
    /// none of its poses. Returns how many body 0 had, and has (by `kept`).
    int trimViewModel(MdlFile &mdl, const std::vector<std::vector<std::array<double, 12>>> &poses,
                      const ViewCone &view, int &kept);

    /// The most triangles of `model` on the screen in any one of `poses`.
    int screenMost(const MdlSubModel &model, const std::vector<std::vector<std::array<double, 12>>> &poses,
                   const ViewCone &view);

    /// Whether a model is only ever seen in first person, by its name (lower
    /// case). Not the tripmine's: the game puts that same model in the world,
    /// to be picked up and stuck to walls (EntityCook, the thrown models).
    inline bool isViewModel(const std::string &name)
    {
        return name.rfind("v_", 0) == 0 && name != "v_tripmine";
    }
} // namespace hlds
