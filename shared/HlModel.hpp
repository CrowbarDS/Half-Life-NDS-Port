#pragma once

// A cooked model: mdl/<name>.hmd, made by the converter from one of the game's
// studio models, and its skin, mdl/<name>.htx.
//
// WHAT IS IN IT
//
//   Bones      the skeleton: each bone's parent.
//   Batches    the skin, as ready-made geometry-engine command streams (the
//              same idea as a level's surfaces). A batch is one sub-model of
//              one body part -- a head, a holster -- cut so that it uses at
//              most kModelBatchBones bones, and it lists them: the DS puts
//              each bone's matrix in a slot of the hardware's matrix stack,
//              and the stream says "use slot N" before the vertices that
//              belong to that bone. Every vertex of these models belongs to
//              exactly one bone, which is what makes this possible.
//   Clips      animations: for each bone a track of keys (a rotation and a
//              place relative to its parent), at a step chosen per bone by
//              how fast it moves. A map's models carry only the clips that
//              map uses.
//   Events     moments of a clip that fire a target by name.
//
// UNITS. A vertex is in its bone's space, 16-bit, 1/kModelVertexScale of a
// unit (or coarser for a model too big for that: `vertexShift`). The DS
// builds, for each bone, the matrix that takes such a vertex straight to view
// space, on the CPU and in 64-bit, so nothing is lost on the way.
//
// Like a level, the file is checked in full when it is opened
// (ModelView::open) so that drawing and animating need no bounds checks.

#include <string>
#include <cstdint>
#include <cstring>

#include "HlLevel.hpp"

namespace hldata
{
    inline constexpr char kModelMagic[4] = {'H', 'L', 'M', 'D'};
    /// 8: a talking model's mouth (its controller 4, kControlMouth) is kept.
    /// A model of 7 is read as well: it has none, and its mouth stays shut.
    /// 9: a first-person model may say how many of its polygons each clip
    /// can show at once (kModelScreenCounts); one of 7 or 8 says nothing.
    inline constexpr std::uint16_t kModelVersion = 9;
    inline constexpr std::uint16_t kModelMinVersion = 7;
    /// A model comes at up to this many levels of detail: 0 the finest the
    /// file has, each after it of fewer triangles. A level's error is how
    /// far it is from the model as the game has it, in sixteenths of a
    /// unit (the distance all but three in a hundred of the model's own
    /// vertices are within); kModelNoLevel where the file has no such level.
    inline constexpr int kModelLevels = 4;
    inline constexpr std::uint8_t kModelNoLevel = 0xFF;

    /// Bones a batch may use: the matrix stack has 31 slots, and the world's
    /// own drawing needs the bottom one.
    inline constexpr int kModelBatchBones = 30;
    inline constexpr int kModelMaxBones = 96;

    /// The file a model's skin is in, under mdl/: by the model's name and the
    /// skin's size, since a model may be cooked with a smaller skin for a map
    /// that is short of video memory.
    inline std::string modelSkinFile(const char *name, int width, int height)
    {
        return std::string(name) + "." + std::to_string(width) + "x" + std::to_string(height) + ".htx";
    }
    /// Vertex units to the Half-Life unit, before `vertexShift`.
    inline constexpr int kModelVertexShift = 7; // 128
    /// Key rotations are quaternions in Q14; key places are units in Q6.
    inline constexpr int kModelQuatShift = 14;
    inline constexpr int kModelPosShift = 6;

    struct ModelHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t boneCount;
        std::uint32_t fileSize;
        std::uint32_t crc32; ///< of every byte after this header
        std::uint16_t batchCount;
        std::uint16_t clipCount;
        std::uint16_t eventCount;
        std::uint16_t bodyPartCount;
        std::uint32_t gxWordCount;
        std::uint32_t trackCount;
        std::uint32_t keyCount;
        std::uint16_t atlasWidth; ///< of its skin, mdl/<name>.htx; 0 = no skin
        std::uint16_t atlasHeight;
        /// How much coarser than 1/128 unit its vertices are: 0 for nearly
        /// everything, more for a model too large to fit 16 bits otherwise.
        std::uint8_t vertexShift;
        std::uint8_t controllerCount;
        std::uint8_t hitboxCount;
        std::uint8_t attachmentCount;
        std::int16_t mins[3]; ///< its box at rest, whole units, model space
        std::int16_t maxs[3];
        std::uint16_t radius; ///< of a ball round its origin that holds it in any pose
        std::uint16_t polygons; ///< the most any choice of body draws (at level 0)
        std::uint8_t levelError[kModelLevels];
        /// A person's bones, by what the game calls them (-1: it has none):
        /// the right hand, what they hold is drawn at; the spine, from which
        /// up they take the pose of holding it.
        std::int8_t handBone;
        std::int8_t spineBone;
        std::uint16_t flags; ///< kModel* (0 in models cooked before there were any)
    };

    /// The model's animation keys are not in the file but beside it, in
    /// mdl/<name>.hmk (kModelKeysFile): `keyCount` of them, as they would
    /// have been. A DS too short of memory for a map's models whole reads a
    /// clip's keys from there as it is played (source/data/KeyCache.hpp).
    /// The file is otherwise the same, and its checksum is of what it holds.
    inline constexpr std::uint16_t kModelKeysApart = 1;
    /// A first-person model (v_*) has what is never on the screen left out
    /// (the converter's ViewModelTrim), and says, in one more section after
    /// the rest -- two 32-bit words for each clip -- the most polygons of it
    /// the hardware can keep at once while the clip plays, at any of its
    /// levels; and in the second, on the clip's last frame alone, where one
    /// that does not loop stays once it is done (the crowbar's draw: the
    /// game gives it no idle). Each in the low half at the usual view
    /// (kViewFovNarrow or less, on 4:3), in the high half at the widest.
    /// What the pass's budget counts for it (ModelDraw::plan), and what a
    /// profiling build holds the hardware's count to (perf.txt).
    inline constexpr std::uint16_t kModelScreenCounts = 2;
    inline constexpr char kModelKeysFile[] = ".hmk";

    /// One body part: how an entity's "body" number picks a sub-model of it,
    /// (body / base) % count.
    struct ModelBodyPart
    {
        std::uint16_t base;
        std::uint16_t count;
    };

    struct ModelBatch
    {
        std::uint32_t gxOfs; ///< first word in the stream
        std::uint16_t gxWords;
        std::uint16_t polygons;
        std::uint16_t vertices;
        std::uint8_t bodyPart;
        std::uint8_t subModel;
        std::uint8_t boneCount;
        /// Which of the model's skins it is for: 0 for every one of them
        /// (nearly always), n for the n-th alone (1 is the usual skin).
        /// Where a second skin puts another picture on some part of the
        /// model, that part is here once for each.
        std::uint8_t skin;
        std::uint8_t level; ///< which level of detail it is of
        std::uint8_t pad;
        /// The bones it uses. The stream's "use slot N" is N-th of these.
        std::uint8_t bones[kModelBatchBones + 2];
    };

    inline constexpr std::uint16_t kClipLoops = 1;
    /// The clip has the game's "dead" event (1000): whoever plays it to its
    /// end in a script is a body, held on its last frame where it lies.
    inline constexpr std::uint16_t kClipDies = 2;
    /// What the clip is for (the game's activity: idle, run, a melee
    /// attack...) in bits 8..15, and how often it is the one chosen among
    /// those for it (its weight) in bits 2..7. 0: a model cooked before
    /// these were kept, or a clip that is for nothing in particular.
    inline constexpr int kClipActivityShift = 8;
    inline constexpr int kClipWeightShift = 2;

    struct ModelClip
    {
        std::uint32_t name;       ///< nameHash of the sequence's label
        std::uint16_t frameCount; ///< frames of the original; the clip runs 0..frameCount-1
        std::uint16_t fps;        ///< frames a second, Q8
        std::uint16_t flags;      ///< kClip*
        std::uint16_t firstEvent;
        std::uint16_t eventCount;
        /// Where the root is, up from the model's origin, on the clip's last
        /// frame (whole units): with endOffset, where a scripted actor is
        /// when its clip is done. (0 in models converted before it was kept:
        /// read as before.)
        std::int16_t endUp;
        std::uint32_t firstTrack; ///< boneCount tracks from here, a bone each (of each blend)
        /// How far one run of the clip carries the model along its own
        /// forward, left and up (Q12 units): a walk's stride. The animation
        /// itself stays where it is.
        fx travel[3];
        /// Where the root is on the clip's last frame, forward and left of
        /// the model's own origin (Q12 units): where the game puts a scripted
        /// actor when its clip is done (GoldSrc's CineCleanup). (Models
        /// converted before held how far it went from the first frame: near
        /// enough the same.)
        fx endOffset[2];
        /// Two for a clip that blends between two poses by the aim's pitch
        /// (a guard's or a soldier's shooting): the second's tracks follow
        /// the first's. One for every other. `blendLow` and `blendHigh` are
        /// the pitch of each, in degrees (the game's SetBlending).
        std::uint8_t blends;
        std::uint8_t pad;
        std::int8_t blendLow;
        std::int8_t blendHigh;
    };
    inline std::uint8_t clipActivity(const ModelClip &clip)
    {
        return static_cast<std::uint8_t>(clip.flags >> kClipActivityShift);
    }
    inline int clipWeight(const ModelClip &clip)
    {
        return (clip.flags >> kClipWeightShift) & 63;
    }

    /// One bone's part of a clip: its turning and its place, each as keys at
    /// frames 0, step, 2 * step, ... and a last one at the clip's last frame.
    /// One key: it does not change. (Nearly every bone only turns: its place
    /// against its parent is one key for the whole clip.)
    struct ModelTrack
    {
        std::uint32_t firstRot; ///< into Keys
        std::uint32_t firstPos;
        std::uint16_t rotCount;
        std::uint16_t rotStep;
        std::uint16_t posCount;
        std::uint16_t posStep;
    };

    /// Three numbers: a place in Q6 units, or the x y z of a unit quaternion
    /// in Q14 whose w is not negative (and so need not be stored).
    struct ModelKey
    {
        std::int16_t v[3];
    };

    /// Something that happens as a clip passes a frame of it.
    struct ModelEvent
    {
        std::uint16_t frame;
        std::uint16_t kind;   ///< kEvent*
        /// kEventFire: nameHash of a name to fire. The other two: nameHash
        /// of a sound's name as the game has it ("common/npc_step1.wav"),
        /// which the level says how to find (Section::ModelSounds).
        std::uint32_t target;
    };
    inline constexpr std::uint16_t kEventFire = 0;  ///< the game's 1003: only in a scripted scene
    inline constexpr std::uint16_t kEventSound = 1; ///< 1004, 5004: a sound from where the actor is
    inline constexpr std::uint16_t kEventVoice = 2; ///< 1008: ... from its mouth
    /// One for the monster's own code (its HandleAnimEvent, HlAi.hpp): the
    /// game's event number (below 1000) is in `target` -- a claw landing, a
    /// zap let go. A model cooked before has none.
    inline constexpr std::uint16_t kEventMonster = 3;
    /// The game's 1001 (`target` 0: the scene playing may no longer be
    /// interrupted) and 1002 (1: it may again). A model cooked before has
    /// none: a build before skips it (no sound has its "name").
    inline constexpr std::uint16_t kEventInterrupt = 4;
    /// The game's 2001 (`target` 0: a light body) and 2002 (1: a heavy one)
    /// hitting the floor: bodydrop3 or 4, the heavy one lower. A build
    /// before skips it (no sound has its "name").
    inline constexpr std::uint16_t kEventBodyDrop = 5;

    /// The most texels a weapon's skin has that is loaded when it is taken
    /// up (HlLevel.hpp's kModelRefLazy): one size, whichever map it is
    /// loaded in, so that every map's slots for weapons' skins fit it.
    inline constexpr int kLazySkinTexels = 128 * 128;

    /// A bone controller: something about a bone that the game sets, on top
    /// of what the clip being played has -- which way a turret's gun points,
    /// how far a barnacle's tongue is let out. `index` is the number the game
    /// sets it by (0..3).
    ///
    /// A turn is about an axis: of the bone's parent (`kControlTurn`, made
    /// after the clip's own turning) or of the bone itself (`kControlTwist`,
    /// made before it). The game adds to one of the bone's three angles; the
    /// cooker works out which axis that comes to, for each clip (a clip may
    /// hold the bone by quite different angles from another: ModelControlAxis,
    /// one for each clip and controller, clip by clip). A slide is along an
    /// axis of the parent.
    struct ModelController
    {
        std::uint8_t bone;
        std::uint8_t kind;  ///< kControl*
        std::uint8_t index;
        std::uint8_t flags; ///< kControlLoops
        /// The ends of its travel: for a turn, 65536 to the whole circle; for
        /// a slide, units in Q12. What it is set to is kept between them (a
        /// turn that goes all the way round is wrapped, not stopped).
        std::int32_t start;
        std::int32_t end;
    };
    struct ModelControlAxis
    {
        std::int16_t v[3]; ///< Q14, unit
        std::int16_t pad;
    };
    inline constexpr std::uint8_t kControlSlide = 0;
    inline constexpr std::uint8_t kControlTurn = 1;
    inline constexpr std::uint8_t kControlTwist = 2;
    inline constexpr std::uint8_t kControlLoops = 1;
    inline constexpr int kModelControls = 5; ///< how many the game sets by number
    /// The game's controller 4: the mouth, set not to an angle but to how far
    /// open it is, 0 shut .. 64 as far as it goes (the game's mouthopen / 64,
    /// between the controller's start and end: poseModel).
    inline constexpr int kControlMouth = 4;

    /// A box round part of a bone, in the bone's own space, that a shot is
    /// tried against; `group` is the part of the body it stands for.
    struct ModelHitbox
    {
        std::uint8_t bone;
        std::uint8_t group; ///< kHit*
        std::int16_t mins[3]; ///< sixteenths of a unit
        std::int16_t maxs[3];
        std::int16_t pad;
    };
    inline constexpr std::uint8_t kHitBody = 0;
    inline constexpr std::uint8_t kHitHead = 1;
    inline constexpr std::uint8_t kHitArmour = 10; ///< a turret's plating
    inline constexpr int kHitboxShift = 4;

    /// A place on a bone that something is fixed to or comes out of (a
    /// muzzle, a turret's eye): by number, as the game knows them.
    struct ModelAttachment
    {
        std::uint8_t bone;
        std::uint8_t pad;
        std::int16_t at[3]; ///< in the bone's space, sixteenths of a unit
    };
    static_assert(sizeof(ModelAttachment) == 8);
    static_assert(sizeof(ModelController) == 12);
    static_assert(sizeof(ModelControlAxis) == 8);
    static_assert(sizeof(ModelHitbox) == 16);
    static_assert(sizeof(ModelHeader) == 68);
    static_assert(sizeof(ModelBatch) == 48);
    static_assert(sizeof(ModelClip) == 44);
    static_assert(sizeof(ModelTrack) == 16);
    static_assert(sizeof(ModelKey) == 6);
    static_assert(sizeof(ModelEvent) == 8);

    // The geometry-engine commands a model's stream is made of.
    inline constexpr std::uint8_t kGxMtxRestore = 0x14;
    inline constexpr std::uint8_t kGxNormal = 0x21;
    inline constexpr std::uint8_t kGxTexCoord = 0x22;
    inline constexpr std::uint8_t kGxVtx16 = 0x23;
    inline constexpr std::uint8_t kGxVtx10 = 0x24;
    inline constexpr std::uint8_t kGxBegin = 0x40;
    /// The first matrix-stack slot bones go in: slot 0 is the world's.
    inline constexpr int kModelFirstSlot = 1;

    /// A model file, opened: pointers into the block it was read into.
    struct ModelView
    {
        const ModelHeader *header = nullptr;
        const std::int16_t *parents = nullptr; ///< per bone; -1 for a root
        const ModelBodyPart *bodyParts = nullptr;
        const ModelBatch *batches = nullptr;
        const std::uint32_t *gx = nullptr;
        const ModelClip *clips = nullptr;
        const ModelTrack *tracks = nullptr;
        const ModelKey *keys = nullptr;
        const ModelEvent *events = nullptr;
        const ModelController *controllers = nullptr;
        const ModelControlAxis *controlAxes = nullptr; ///< [clip * controllerCount + controller]
        const ModelHitbox *hitboxes = nullptr;
        const ModelAttachment *attachments = nullptr;
        const std::uint32_t *screenCounts = nullptr; ///< two per clip (kModelScreenCounts); none: not said

        /// What of the model clip `clip` can show at once, at the usual view
        /// or the widest -- on its last frame alone, `ended`; 0 when the
        /// model does not say.
        int screenPolygons(int clip, bool wide, bool ended = false) const
        {
            if (screenCounts == nullptr || clip < 0 || clip >= header->clipCount)
                return 0;
            const std::uint32_t counts = screenCounts[clip * 2 + (ended ? 1 : 0)];
            return static_cast<int>(wide ? counts >> 16 : counts & 0xFFFF);
        }

        /// False for anything that is not a whole, consistent model of this
        /// version.
        bool open(const std::uint8_t *data, std::uint32_t size)
        {
            *this = ModelView();
            if (size < sizeof(ModelHeader) || (reinterpret_cast<std::uintptr_t>(data) & 3) != 0)
                return false;
            const auto *h = reinterpret_cast<const ModelHeader *>(data);
            if (std::memcmp(h->magic, kModelMagic, 4) != 0 || h->version > kModelVersion ||
                h->version < kModelMinVersion ||
                h->fileSize != size || h->boneCount == 0 || h->boneCount > kModelMaxBones ||
                crc32(data + sizeof(ModelHeader), size - sizeof(ModelHeader)) != h->crc32)
                return false;
            // The sections follow the header in this order, each padded to
            // four bytes.
            std::uint64_t at = sizeof(ModelHeader);
            auto take = [&](std::uint64_t bytes) -> const std::uint8_t *
            {
                const std::uint8_t *p = data + at;
                at += (bytes + 3) & ~std::uint64_t(3);
                return p;
            };
            const auto *p = reinterpret_cast<const std::int16_t *>(take(h->boneCount * 2ull));
            const auto *bp = reinterpret_cast<const ModelBodyPart *>(
                take(h->bodyPartCount * sizeof(ModelBodyPart)));
            const auto *b = reinterpret_cast<const ModelBatch *>(take(h->batchCount * sizeof(ModelBatch)));
            const auto *g = reinterpret_cast<const std::uint32_t *>(take(h->gxWordCount * 4ull));
            const auto *c = reinterpret_cast<const ModelClip *>(take(h->clipCount * sizeof(ModelClip)));
            const auto *t = reinterpret_cast<const ModelTrack *>(take(h->trackCount * sizeof(ModelTrack)));
            // (Keys apart: none here, and no pointer to them -- poseModel
            // asks for a clip's.)
            const bool keysApart = (h->flags & kModelKeysApart) != 0;
            const auto *k = keysApart ? nullptr
                                      : reinterpret_cast<const ModelKey *>(take(h->keyCount * sizeof(ModelKey)));
            const auto *e = reinterpret_cast<const ModelEvent *>(take(h->eventCount * sizeof(ModelEvent)));
            const auto *cs = reinterpret_cast<const ModelController *>(
                take(h->controllerCount * sizeof(ModelController)));
            const auto *ca = reinterpret_cast<const ModelControlAxis *>(take(
                static_cast<std::uint64_t>(h->controllerCount) * h->clipCount * sizeof(ModelControlAxis)));
            const auto *hb = reinterpret_cast<const ModelHitbox *>(
                take(h->hitboxCount * sizeof(ModelHitbox)));
            const auto *att = reinterpret_cast<const ModelAttachment *>(
                take(h->attachmentCount * sizeof(ModelAttachment)));
            const auto *screen = (h->flags & kModelScreenCounts) != 0
                                     ? reinterpret_cast<const std::uint32_t *>(take(h->clipCount * 8ull))
                                     : nullptr;
            if (at != size || (h->flags & ~(kModelKeysApart | kModelScreenCounts)) != 0)
                return false;
            for (int i = 0; i < h->attachmentCount; ++i)
            {
                if (att[i].bone >= h->boneCount)
                    return false;
            }
            for (int i = 0; i < h->controllerCount; ++i)
            {
                if (cs[i].bone >= h->boneCount || cs[i].index >= kModelControls ||
                    cs[i].kind > kControlTwist)
                    return false;
            }
            for (int i = 0; i < h->hitboxCount; ++i)
            {
                if (hb[i].bone >= h->boneCount)
                    return false;
            }

            for (int i = 0; i < h->boneCount; ++i)
            {
                if (p[i] >= i || p[i] < -1) // parents first
                    return false;
            }
            for (int i = 0; i < h->bodyPartCount; ++i)
            {
                if (bp[i].base == 0 || bp[i].count == 0)
                    return false;
            }
            for (int i = 0; i < h->batchCount; ++i)
            {
                if (static_cast<std::uint64_t>(b[i].gxOfs) + b[i].gxWords > h->gxWordCount ||
                    b[i].boneCount > kModelBatchBones || b[i].bodyPart >= h->bodyPartCount ||
                    b[i].subModel >= bp[b[i].bodyPart].count || b[i].level >= kModelLevels ||
                    h->levelError[b[i].level] == kModelNoLevel)
                    return false;
                for (int n = 0; n < b[i].boneCount; ++n)
                {
                    if (b[i].bones[n] >= h->boneCount)
                        return false;
                }
            }
            for (int i = 0; i < h->clipCount; ++i)
            {
                if (c[i].frameCount == 0 || c[i].fps == 0 || c[i].blends == 0 || c[i].blends > 2 ||
                    static_cast<std::uint64_t>(c[i].firstTrack) + std::uint64_t{h->boneCount} * c[i].blends >
                        h->trackCount ||
                    static_cast<std::uint32_t>(c[i].firstEvent) + c[i].eventCount > h->eventCount)
                    return false;
            }
            for (std::uint32_t i = 0; i < h->trackCount; ++i)
            {
                if (t[i].rotCount == 0 || t[i].rotStep == 0 || t[i].posCount == 0 ||
                    t[i].posStep == 0 ||
                    static_cast<std::uint64_t>(t[i].firstRot) + t[i].rotCount > h->keyCount ||
                    static_cast<std::uint64_t>(t[i].firstPos) + t[i].posCount > h->keyCount)
                    return false;
            }
            header = h;
            parents = p;
            bodyParts = bp;
            batches = b;
            gx = g;
            clips = c;
            tracks = t;
            keys = k;
            events = e;
            controllers = cs;
            controlAxes = ca;
            hitboxes = hb;
            attachments = att;
            screenCounts = screen;
            return true;
        }

        /// The clip called `name` (a nameHash), or -1.
        int findClip(std::uint32_t name) const
        {
            for (int i = 0; i < header->clipCount; ++i)
            {
                if (clips[i].name == name)
                    return i;
            }
            return -1;
        }

        /// Which of a body part's choices this "body" number takes: body /
        /// base % count (0 for a body under 0). The divides only where they
        /// change the answer: most parts have one choice, most bodies are
        /// under the part's base or within its count of it. (One copy: it is
        /// called from many places.)
        __attribute__((noinline)) static int bodyChoice(const ModelBodyPart &part, int body)
        {
            if (part.count <= 1 || body < static_cast<int>(part.base))
                return 0;
            const int quotient = part.base == 1 ? body : body / part.base;
            return quotient < part.count ? quotient : quotient % part.count;
        }

        /// Whether `batch` is part of the model for this "body" number, in
        /// this skin (0: the usual one), at this level. (The level and the
        /// skin first: they rule out most batches, without the body's sum.)
        bool batchInBody(const ModelBatch &batch, int body, int skin = 0, int level = 0) const
        {
            return batch.level == level && (batch.skin == 0 || batch.skin == skin + 1) &&
                   bodyChoice(bodyParts[batch.bodyPart], body) == batch.subModel;
        }
        /// How many levels of detail the file has (at least one).
        int levelCount() const
        {
            int count = 1;
            while (count < kModelLevels && header->levelError[count] != kModelNoLevel)
                ++count;
            return count;
        }

        /// The keys clip `clip` reads, of every blend: from `first`,
        /// `count` of them. (The converter writes a clip's keys together,
        /// clip after clip; this holds for any layout, as the run that
        /// covers them all.)
        void clipKeys(int clip, std::uint32_t &first, std::uint32_t &count) const
        {
            const ModelClip &c = clips[clip];
            const ModelTrack *t = tracks + c.firstTrack;
            const std::uint32_t n = std::uint32_t{header->boneCount} * c.blends;
            std::uint32_t low = 0xFFFFFFFFu, high = 0;
            for (std::uint32_t i = 0; i < n; ++i)
            {
                const std::uint32_t r = t[i].firstRot, p = t[i].firstPos;
                low = r < low ? r : low;
                low = p < low ? p : low;
                high = r + t[i].rotCount > high ? r + t[i].rotCount : high;
                high = p + t[i].posCount > high ? p + t[i].posCount : high;
            }
            first = n != 0 ? low : 0;
            count = n != 0 ? high - low : 0;
        }
    };

    /// mdl/<name>.hmd and mdl/<name>.htx: the names a model's files go by.
    inline constexpr int kModelNameMax = 24;
} // namespace hldata
