#pragma once

// From a studio model to what the DS draws: mdl/<name>.hmd (shared/HlModel.hpp)
// and its skin, one texture.
//
// Three things are worked out here so that the DS does not have to:
//
//   THE SKIN AS COMMAND STREAMS. The model's strips and fans become triangle
//   strips and triangles of geometry-engine commands, cut into batches that
//   each use few enough bones to fit the hardware's matrix stack, with a
//   "use this bone's matrix" command wherever the bone changes.
//
//   ONE TEXTURE. A model comes with twenty or thirty little textures of any
//   size; the DS wants few, and powers of two. They are packed into one atlas
//   and the triangles' coordinates moved to match.
//
//   ANIMATION AS KEYS. Each sequence the map uses becomes, per bone, a short
//   list of keys: a bone that does not move in the sequence gets one, a bone
//   that moves slowly gets one every several frames, and the step is the
//   largest that reproduces every original frame to within a degree and a
//   quarter of a unit.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "HlModel.hpp"
#include "hlds/MdlFile.hpp"
#include "hlds/TextureImage.hpp"

namespace hlds
{
    /// Palette colours a model's skin is encoded with.
    inline constexpr int kModelSkinColours = 512;
    /// What a model's skin is thinned to: this many triangles, or a quarter of
    /// what it had if that is more (a loader is not a scientist). A model at
    /// or under it is left alone. Three or four people at this fit a pass
    /// with the world; at their original 600 to 800 triangles, one did.
    inline constexpr int kModelTriangleTarget = 350;
    /// Below that there are two coarser levels, of a half and of about a
    /// third of it (a person in fewer than that has lost half of their
    /// outline: measured) -- but none of fewer triangles than this, and none
    /// at all for a model of not twice as many.
    inline constexpr int kModelLevelLeast = 60;
    /// The coarse level of a model always drawn (ModelLean::coarse): this
    /// share of its triangles, in hundredths, and no fewer than this many.
    inline constexpr int kCoarseShare = 35;
    inline constexpr int kCoarseLeast = 8;
    /// Whether the model called `name` is always to be drawn, coarser if need
    /// be: a casing out of a gun, something lying about to pick up, or the
    /// pieces something breaks into ("woodgibs", "hgibs").
    inline bool alwaysDrawnModel(const std::string &name)
    {
        return name == "shell" || name == "shotgunshell" || name.rfind("w_", 0) == 0 ||
               (name.size() > 4 && name.compare(name.size() - 4, 4, "gibs") == 0);
    }

    /// The first-person model every map is given.
    inline constexpr const char *kViewModelName = "v_crowbar";

    struct CookedModel
    {
        hldata::ModelHeader header{};
        std::vector<std::int16_t> parents;
        std::vector<hldata::ModelBodyPart> bodyParts;
        std::vector<hldata::ModelBatch> batches;
        std::vector<std::uint32_t> gx;
        std::vector<hldata::ModelClip> clips;
        std::vector<hldata::ModelTrack> tracks;
        std::vector<hldata::ModelKey> keys;
        std::vector<hldata::ModelEvent> events;
        /// The sounds its kept clips make (the game's names, lower-case,
        /// without the mark in front that says how to stream them): not
        /// written with the model -- the level says where each is.
        std::vector<std::string> eventSounds;
        std::vector<hldata::ModelController> controllers;
        std::vector<hldata::ModelControlAxis> controlAxes;
        std::vector<hldata::ModelHitbox> hitboxes;
        std::vector<hldata::ModelAttachment> attachments;
        /// A first-person model's most polygons on the screen at once, two
        /// per clip (all of it, then its last frame), at the usual view (low
        /// half) and the widest (high half): kModelScreenCounts. Empty for
        /// any other.
        std::vector<std::uint32_t> screenCounts;
        /// The skin, before it is encoded: empty for a model with no textures.
        RgbaImage atlas;
        /// The same skin as 256 colours, for a model a player's colours are
        /// laid over (one with a "DM_Base" texture): that texture's own
        /// colours, at their own places -- the runs a player's colours go
        /// over among them -- and the rest of the skin in the nearest of the
        /// others. Empty for any other model.
        struct IndexedSkin
        {
            int width = 0, height = 0;
            std::vector<std::uint8_t> texels; ///< width * height, a byte each
            std::array<std::uint8_t, 768> palette{}; ///< RGB, as the skin is lit
        } indexed;
        /// Each level's error, as the header has it (ModelHeader::levelError).
        std::uint8_t levelError[hldata::kModelLevels] = {0, hldata::kModelNoLevel, hldata::kModelNoLevel,
                                                         hldata::kModelNoLevel};
    };

    struct ModelCookReport
    {
        int sourceTriangles = 0; ///< body 0, as the model had them (a first-person one's, trimmed)
        int untrimmedTriangles = 0; ///< ... before what is never on the screen was left out
        int triangles = 0;     ///< body 0, as cooked, at its finest level
        /// Body 0 at each level of detail the file has, finest first.
        struct Level
        {
            int triangles = 0;
            int vertices = 0; ///< as sent: what its strips cost
            double error = 0; ///< units (MeshLevel::error)
        };
        std::vector<Level> levels;
        int batches = 0;
        int clips = 0;
        int sourceFrames = 0;  ///< frames * bones of the clips, as the model had them
        int keys = 0;          ///< ... and as cooked
        std::vector<std::string> missingClips; ///< asked for, not in the model
    };

    /// A skin's atlas is the smallest that holds its textures at this fraction
    /// of their width and height, or better.
    inline constexpr double kAtlasMinScale = 0.75;
    /// Smallest extra worth taking; below this the skins keep kMapSkinTexels.
    inline constexpr int kSkinSpareMargin = 16 * 1024;
    inline constexpr int kMaxExtraSkinTexels = 256 * 1024;
    inline constexpr int kMinExtraSkinTexels = 16 * 1024;
    /// The most texels a model's skin may take, unless told less.
    inline constexpr int kModelSkinTexels = 256 * 256;
    /// What a map's models may take of the video memory between them: the
    /// largest skins are made smaller until they do (Converter).
    /// The weapons' (v_*) are kept in main memory until one is taken up,
    /// so only the largest of those counts. (Raised by what bank D gave
    /// banks A to C: kSinglePassTexelBudget.)
    inline constexpr int kMapSkinTexels = 320 * 1024;
    /// ... on a map a console may be sent, as before: its guest's plan
    /// counts the skins against what it has (guestTexturePlan).
    inline constexpr int kSentMapSkinTexels = 208 * 1024;

    /// How closely a model is cooked: as the console's own (the defaults), or
    /// lighter, for a console sent it with no card (a guest: its weapons,
    /// held up to the eye, never need the thinner levels for distance, and
    /// the hand moves quickly enough that a looser key is not seen).
    struct ModelLean
    {
        bool oneLevel = false;   ///< only the finest level of detail
        double turnOff = 0.75;   ///< how far a key may turn a bone off a frame, degrees
        double placeOff = 0.25;  ///< ... and move it, units
        /// And one level coarser than the rest: kCoarseShare of its triangles,
        /// kCoarseLeast at fewest -- for what is always to be drawn, however
        /// tight the frame (a casing, a pickup): at worst, coarser.
        bool coarse = false;
    };
    /// A guest's weapons.
    inline constexpr ModelLean kGuestLean = {true, 1.5, 0.5};

    /// Cook `mdl` with the sequences named in `clipNames` (labels, any case).
    /// Its skin is at most `skinTexels` texels (a power of two each way; the
    /// smallest is 32 x 32).
    /// `full`: with the model as the game has it as its finest level, above
    /// the thinned ones (where it was thinned at all) -- a DSi's file.
    /// A sound's name as an event has it, as the level knows it.
    std::string eventSoundName(std::string path);

    /// What a player holds (a game's p_*.mdl): every vertex put in the space
    /// of its right hand, and the model made one bone, with nothing that moves
    /// it. False if it has no such hand.
    bool bakeHeldModel(MdlFile &mdl, std::string &error);

    bool cookModel(const MdlFile &mdl, const std::vector<std::string> &clipNames, CookedModel &out,
                   ModelCookReport &report, std::string &error, int skinTexels = kModelSkinTexels,
                   bool full = false, const ModelLean &lean = ModelLean());

    /// Serialize, then open the result with the DS's reader: fails rather
    /// than return a file the console would refuse.
    bool writeModel(const CookedModel &model, std::vector<std::uint8_t> &out, std::string &error);

    /// A model file (`file`, as writeModel made it) with its animation keys
    /// apart (HlModel.hpp's kModelKeysApart): `model` the file without them,
    /// `keys` the keys, keyCount * 6 bytes. False if `file` does not open, or
    /// has them apart already.
    bool splitModelKeys(const std::vector<std::uint8_t> &file, std::vector<std::uint8_t> &model,
                        std::vector<std::uint8_t> &keys);
    /// Of a model file: its keys' bytes, all told, and the most any one clip
    /// reads (ModelView::clipKeys). False if it does not open.
    bool modelKeyBytes(const std::vector<std::uint8_t> &file, std::size_t &all, std::size_t &largestClip,
                       std::size_t &clips);

    /// The game's own way from three angles (radians, about x y z) to a
    /// quaternion (x y z w): AngleQuaternion. Here for the cooker and for the
    /// tests that hold the DS's maths against the game's.
    void mdlAngleQuaternion(const float *angles, double *quat);

    /// Every bone's matrix in model space for one frame's poses (bones.size()
    /// of them), in doubles, the way the game builds them: twelve numbers a
    /// bone, three rows of rotation-then-place.
    void mdlBoneMatrices(const MdlFile &mdl, const MdlPose *poses,
                         std::vector<std::array<double, 12>> &out);
} // namespace hlds
