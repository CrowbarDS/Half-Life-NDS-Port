#include "Hot.hpp"
#include "HlDrawPlan.hpp"
#include "render/ModelDraw.hpp"

#include <nds.h>

#include <NEAMain.h>

#include "DebugLog.hpp"
#include "Global.hpp"
#include "HlFog.hpp"
#include "HlLamp.hpp"
#include "HlPolygonIds.hpp"
#include "Settings.hpp"
#include "data/ModelCache.hpp"
#include "net/NetGame.hpp"
#include "game/Game.hpp"
#include "game/Player.hpp"
#include "render/Camera.hpp"
#include "render/EffectDraw.hpp"
#include "render/WorldDraw.hpp"

namespace hlrt
{
    ModelDraw modelDraw;

    namespace
    {
        using namespace hldata;

        /// Where the light on an actor comes from, in the world: from above
        /// and a little to one side, as most of the game's rooms are lit. Q12,
        /// unit length, the way the light TRAVELS.
        constexpr fx kLightDir[3] = {1024, 683, -3908};

        /// Both sides of a model's triangles are drawn. Its front faces are
        /// what the hardware calls back faces (the game winds them the other
        /// way from the level), so NEA_CULL_FRONT would be the saving -- but
        /// the hardware judges a triangle by its first three corners after
        /// projection, gets thin ones wrong, and what it gets wrong it throws
        /// away: bits of people went missing. Drawn both ways, the depth
        /// buffer does the hiding.
        constexpr NEA_CullingEnum kCull = NEA_CULL_NONE;

        /// An actor's place in the per-actor caches (ModelDraw::kActorSlots:
        /// one each; a level with more actors is not read).
        inline int slotOf(int actor)
        {
            return actor >= 0 && actor < ModelDraw::kActorSlots ? actor : ModelDraw::kActorSlots - 1;
        }

        /// A model fading away (a body, a piece of one): see-through, `alpha`
        /// of 31, under a polygon ID of its own -- the hardware does not blend
        /// a see-through pixel over one of the same ID, so its own faces one
        /// behind another do not darken each other, and the next fading model
        /// takes the next of the IDs set aside (HlPolygonIds.hpp), so that two
        /// that overlap show through each other -- with its back faces left
        /// out (a little of a thin triangle may go missing: it is going), and
        /// its depth kept, so that what is behind it stays behind it. 31: as
        /// every model is drawn.
        int nextFadeId = 0;
        void modelAlpha(int alpha)
        {
            const int kFadeId = kIdFadeFirst + (alpha >= 31 ? 0 : nextFadeId++ % kIdFadeCount);
            if (alpha >= 31)
            {
                NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(NEA_LIGHT_0 | NEA_LIGHT_1), kCull,
                               worldDraw.polygonMode());
                return;
            }
            NEA_PolyFormat(alpha < 1 ? 1 : alpha, kFadeId, static_cast<NEA_LightEnum>(NEA_LIGHT_0 | NEA_LIGHT_1),
                           NEA_CULL_FRONT,
                           static_cast<NEA_OtherFormatEnum>(static_cast<int>(worldDraw.polygonMode()) | NEA_TRANS_DEPTH_UPDATE));
        }

        /// A monster drawn see-through as the map has it (a hologram): `alpha`
        /// of 31, under IDs of its own, round and round, its back faces left
        /// out and its depth kept, as a fading body is. `pale`: with the toon
        /// table's grey added to its picture (highlight shading, lightOn) --
        /// the game adds a hologram to what is behind it, which a blend
        /// cannot: this is as near as it comes.
        int nextGlassId = 0;
        void glassAlpha(int alpha, bool pale)
        {
            NEA_PolyFormat(alpha < 1 ? 1 : alpha, kIdGlassFirst + nextGlassId++ % kIdGlassCount,
                           static_cast<NEA_LightEnum>(NEA_LIGHT_0 | NEA_LIGHT_1), NEA_CULL_FRONT,
                           static_cast<NEA_OtherFormatEnum>(static_cast<int>(worldDraw.polygonMode()) |
                                                            NEA_TRANS_DEPTH_UPDATE |
                                                            (pale ? NEA_TOON_HIGHLIGHT_SHADING : 0)));
        }

        /// The hologram's flicker: new each time it is asked.
        std::uint32_t flicker = 1;
        int flickerNoise()
        {
            flicker = flicker * 1664525u + 1013904223u;
            return static_cast<int>((flicker >> 16) & 63) - 32;
        }

        /// How many steps to pose an actor; lower is better.
        /// 256: full frame, 128: half frame, 64: quarter frame.
        constexpr std::uint32_t kPoseStep = 64;
    } // namespace

    namespace
    {
        struct Posed
        {
            int id = -2; ///< the actor's place in the map's list; -1: what is held
            unsigned model = 0;
            int clip = 0;
            std::uint32_t frame = 0;
            std::int32_t controls[2] = {0, 0}; ///< what its bone controllers are set to
            int blend = 0;                     ///< where between a blending clip's poses
            int aim = -1;                      ///< (a player) the pose of holding what they hold; and how far up
            std::uint32_t used = 0;
            BoneMatrix bones[kModelMaxBones];
        };
        Posed posedSlots[ModelDraw::kMaxDrawn + 1];
        std::uint32_t posedClock = 0;

        /// How a player stands holding their gun: which clip, and its blend
        /// by how far up they look (in `aim`: clip * 256 + blend). -1: not one.
        [[gnu::noinline]] int playerAim(const ActorState &actor, const ModelView &model, int id)
        {
            const NetMatch &match = netGame.match();
            const int p = id - match.firstActor;
            if (actor.kind != static_cast<std::uint8_t>(ActorKind::Player) || p < 0 || p >= kNetPlayers ||
                model.header->spineBone < 0)
                return -1;
            const NetPlayer &player = netPlayer(match, p);
            if ((player.flags & kNetAlive) == 0 || player.weapon >= kWeaponCount)
                return -1;
            const int clip = model.findClip(nameHash(kAimClips[kAimOf[player.weapon]]));
            if (clip < 0)
                return -1;
            int blend = 128 - player.pitch * 3;
            // (Having just fired: the arms thrown up a little, for a moment.)
            static std::uint32_t firedAt[kNetPlayers];
            if ((player.flags & kNetFiring) != 0)
                firedAt[p] = Global::vblanks();
            const std::uint32_t since = Global::vblanks() - firedAt[p];
            if (since < 10)
                blend -= static_cast<int>(10 - since) * 5;
            blend = blend < 0 ? 0 : (blend > 255 ? 255 : blend);
            return clip * 256 + blend;
        }

        void overlayAim(const ModelView &model, BoneMatrix *bones, int aim)
        {
            static BoneMatrix held[kModelMaxBones];
            overlayAimPose(model, bones, aim / 256, aim & 255, held);
        }
    } // namespace

    void ModelDraw::forgetPoses()
    {
        for (Posed &slot : posedSlots)
            slot.id = -2;
    }

    HL_ITCM const BoneMatrix *ModelDraw::posed(const ActorState &actor, const ModelView &model,
                                       unsigned modelIndex, int id)
    {
        // Needed for proper 60fps blending between animations
        constexpr std::uint32_t kStep = kPoseStep;
        const std::uint32_t frame = (actor.frame + kStep / 2) & ~(kStep - 1);

        // What the game sets by hand on top of the clip: a barnacle's tongue
        // let out, a turret's gun turned (HlActors.hpp).
        std::int32_t controls[kModelControls];
        const bool controlled = actorControls(actor, controls);
        const int aim = playerAim(actor, model, id);
        Posed *oldest = &posedSlots[0];
        for (Posed &slot : posedSlots)
        {
            if (slot.id == id && slot.model == modelIndex && slot.clip == actor.clip &&
                slot.frame == frame && slot.controls[0] == controls[0] &&
                slot.controls[1] == controls[1] && slot.blend == actor.blend.value && slot.aim == aim)
            {
                slot.used = ++posedClock;
                return slot.bones;
            }
            if (slot.used < oldest->used)
                oldest = &slot;
        }
        const DebugLog::Perf::Scope scope(DebugLog::Section::Pose);
        Posed &slot = *oldest;
        slot.id = id;
        slot.model = modelIndex;
        slot.clip = actor.clip;
        slot.frame = frame;
        slot.controls[0] = controls[0];
        slot.controls[1] = controls[1];
        slot.blend = actor.blend.value;
        slot.aim = aim;
        slot.used = ++posedClock;
        const int bones = model.header->boneCount;
        if (actor.clip >= 0)
        {
            poseModel(model, actor.clip, frame, slot.bones, controlled ? controls : nullptr, actor.blend.value);
            if (aim >= 0)
                overlayAim(model, slot.bones, aim);
        }
        else
        {
            // No clip at all: every bone at the origin, unturned. (Only a
            // model cooked with no sequences gets here.)
            for (int b = 0; b < bones; ++b)
            {
                for (int i = 0; i < 9; ++i)
                    slot.bones[b].r[i] = (i % 4 == 0) ? (1 << kBoneRotShift) : 0;
                slot.bones[b].t[0] = slot.bones[b].t[1] = slot.bones[b].t[2] = 0;
            }
        }
        return slot.bones;
    }

    void ModelDraw::levelsText(char *out, int size) const
    {
        int n = 0;
        for (; n < found_ && n + 1 < size; ++n)
            out[n] = static_cast<char>('0' + level_[n]);
        if (size > 0)
            out[n] = '\0';
    }

    void ModelDraw::drawGun(const ActorState &actor, const ModelView &model, const fx *turn, int id)
    {
        const NetMatch &match = netGame.match();
        const int p = id - match.firstActor;
        if (actor.kind != static_cast<std::uint8_t>(ActorKind::Player) || p < 0 || p >= kNetPlayers ||
            worldDraw.pressure() >= 2 || handsCut_)
            return;
        const NetPlayer &player = netPlayer(match, p);
        const int m = modelCache.heldModel(player.weapon);
        if ((player.flags & kNetAlive) == 0 || m < 0 || m >= game.actors.modelCount ||
            game.actors.models[m].header == nullptr)
            return;
        // What it is held by (a pack on the back: the spine). (Copied: the
        // next pose asked for may take this one's place.)
        const int by = game.actors.models[m].header->spineBone == 0 ? model.header->spineBone : model.header->handBone;
        if (by < 0)
            return;
        const BoneMatrix hand = posed(actor, model, actor.model, id)[by];
        ActorState gun;
        gun.active = true;
        gun.kind = static_cast<std::uint8_t>(ActorKind::Generic);
        gun.model = static_cast<std::uint16_t>(m);
        gun.light = actor.light;
        gun.clip = -1;
        fx gunTurn[9];
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
                gunTurn[r * 3 + c] = static_cast<fx>(
                    (static_cast<std::int64_t>(turn[r * 3]) * hand.r[c] + static_cast<std::int64_t>(turn[r * 3 + 1]) * hand.r[3 + c] +
                     static_cast<std::int64_t>(turn[r * 3 + 2]) * hand.r[6 + c]) >>
                    kBoneRotShift);
            gun.pos[r] = actor.pos[r] + fxMul(turn[r * 3], hand.t[0]) + fxMul(turn[r * 3 + 1], hand.t[1]) +
                         fxMul(turn[r * 3 + 2], hand.t[2]);
        }
        drawActor(gun, game.actors.models[m], static_cast<unsigned>(m), gunTurn, -3 - m,
                  game.actors.models[m].levelCount() - 1);
    }

    HL_ITCM void ModelDraw::drawActor(const ActorState &actor, const ModelView &model, unsigned modelIndex,
                              const fx *turn, int id, int level, int shrink)
    {
        // Weapons whose skins are not in video memory yet are not shown.
        if (!modelCache.drawable(modelIndex))
            return;
        // The pose is kept: one that is holding still (a body, someone
        // waiting on a frame) is the same from frame to frame.
        const BoneMatrix *pose = posed(actor, model, modelIndex, id);
        const DebugLog::Perf::Scope scope(DebugLog::Section::Models);

        const int *view = camera.view.m; // row k: where world axis k goes, times 8; row 3: translation
        const int shift = 5 - model.header->vertexShift; // see below

        // The actor's turn and the view together, once: row m is where the
        // MODEL's axis m goes in view space (times 8, Q12, as the view's
        // rows are). And where the actor's own origin goes.
        //
        // As camera.apply does for the level: a Q12 unit is 1024 of the
        // level's vertex units.
        int turned[9];
        int base[3];
        for (int j = 0; j < 3; ++j)
        {
            for (int m = 0; m < 3; ++m)
                turned[m * 3 + j] = static_cast<int>(
                    (static_cast<std::int64_t>(turn[m]) * view[j] +
                     static_cast<std::int64_t>(turn[3 + m]) * view[3 + j] +
                     static_cast<std::int64_t>(turn[6 + m]) * view[6 + j]) >>
                    12);
            const std::int64_t sum = static_cast<std::int64_t>(actor.pos[0] >> 10) * view[0 + j] +
                                     static_cast<std::int64_t>(actor.pos[1] >> 10) * view[3 + j] +
                                     static_cast<std::int64_t>(actor.pos[2] >> 10) * view[6 + j];
            base[j] = (static_cast<int>(sum >> 12) + view[9 + j]) >> shrink;
        }

        const NEA_Material *skin = modelCache.skin(modelIndex);
        if (skin != nullptr)
        {
            NEA_MaterialUse(skin);
            // (Another player in a game between consoles: in their colours.)
            if (const std::uint32_t palette = modelCache.playerPalette(modelIndex, id - netGame.match().firstActor))
                GFX_PAL_FORMAT = palette;
        }
        else
        {
            GFX_TEX_FORMAT = 0;
        }
        // Lit by the one light: how bright the actor stands sets its colour.
        const int bright = actor.light > 31 ? 31 : actor.light;
        // The lamp is a light of the hardware's own on whatever is in its
        // beam -- the second: it shines the way the view looks -- and what
        // it falls on shines back a little where it is turned to the eye
        // (the material's specular colour, by the shininess table). Not on
        // what is in hand, which is behind it.
        int lamp = 0;
        if (game.lamp.on && id != -1)
        {
            // The game's lamp shines on whoever the view meets as it does
            // on a wall: so the light is where THEY are, if the middle of
            // the view is near them (their chest, more or less, is what
            // counts) -- or where it has fallen on the wall, if they stand
            // in that.
            const int d[3] = {fxToInt(actor.pos[0] - camera.pos[0]), fxToInt(actor.pos[1] - camera.pos[1]),
                              fxToInt(actor.pos[2] - camera.pos[2]) +
                                  (actor.kind == static_cast<std::uint8_t>(ActorKind::Player) ? 0 : 36)};
            const int along = (d[0] * camera.forward[0] + d[1] * camera.forward[1] + d[2] * camera.forward[2]) >> 12;
            if (along > 0 && along < kLampReach)
            {
                const int square = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - along * along;
                const int aside = static_cast<int>(isqrt64(static_cast<std::uint64_t>(square > 0 ? square : 0)));
                lamp = lampOn(along, aside, along);
                if (game.lampLit)
                {
                    const int onWall = lampOn(along, aside, game.lampDistance);
                    lamp = onWall > lamp ? onWall : lamp;
                }
            }
        }
        GFX_LIGHT_COLOR = RGB15(bright, bright, bright); // light 0
        GFX_LIGHT_COLOR = RGB15(lamp, lamp * 30 / 31, lamp * 25 / 31) | (1u << 30); // light 1
        GFX_DIFFUSE_AMBIENT = RGB15(20, 20, 20) | (RGB15(13, 13, 13) << 16);
        // (Bit 15: the specular light goes by the shininess table.)
        GFX_SPECULAR_EMISSION = lamp > 0 ? (RGB15(lamp * 2 / 3, lamp * 2 / 3, lamp * 5 / 9) | 0x8000u) : 0u;
        // A hologram is its own light, whatever is round it (the game draws
        // it added to the picture, unlit): its picture as it is, pale.
        if (glow_)
        {
            GFX_LIGHT_COLOR = RGB15(31, 31, 31);
            GFX_LIGHT_COLOR = RGB15(31, 31, 31) | (1u << 30);
            GFX_DIFFUSE_AMBIENT = RGB15(31, 31, 31) << 16;
            GFX_SPECULAR_EMISSION = RGB15(31, 31, 31);
        }

        for (int n = 0; n < model.header->batchCount; ++n)
        {
            const ModelBatch &batch = model.batches[n];
            if (!model.batchInBody(batch, actor.body, actor.skin, level))
                continue;
            for (int k = 0; k < batch.boneCount; ++k)
            {
                const BoneMatrix &bone = pose[batch.bones[k]];
                // The matrix the hardware multiplies a ROW vector by: row i is
                // where the bone's axis i goes in view space.
                //
                // A vertex is 1/128 unit (coarser by vertexShift); the view
                // matrix's rows take a level vertex (1/4 unit) to view space.
                // So a model vertex needs the same rows, 32 times smaller:
                // that is the ">> 5", less the vertexShift.
                //
                // The same rotation at full length (the view's rows are 8
                // long; ">> 3") is the matrix for NORMALS, which must not
                // shrink with the vertices or the model goes dark.
                //
                // The sums fit 32 bits: a bone's column is a unit vector in
                // Q14 and a row of `turned` one of length 8 in Q12, so their
                // product is at most 2^29.
                m4x3 position, direction;
                for (int i = 0; i < 3; ++i)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        const int sum = bone.r[i] * turned[j] + bone.r[3 + i] * turned[3 + j] +
                                        bone.r[6 + i] * turned[6 + j]; // Q14 * (Q12 * 8)
                        position.m[i * 3 + j] = sum >> (kBoneRotShift + shift + shrink);
                        direction.m[i * 3 + j] = sum >> (kBoneRotShift + 3);
                    }
                }
                for (int j = 0; j < 3; ++j)
                {
                    // The bone's origin: Q12 units against rows that are for
                    // 1/1024ths of one.
                    const std::int64_t sum = static_cast<std::int64_t>(bone.t[0]) * turned[j] +
                                             static_cast<std::int64_t>(bone.t[1]) * turned[3 + j] +
                                             static_cast<std::int64_t>(bone.t[2]) * turned[6 + j];
                    position.m[9 + j] = base[j] + static_cast<int>(sum >> (22 + shrink));
                    direction.m[9 + j] = 0;
                }
                // Both matrices at once takes the directions; then the
                // positions alone are replaced; the slot keeps the pair.
                MATRIX_CONTROL = GL_MODELVIEW;
                glLoadMatrix4x3(&direction);
                MATRIX_CONTROL = GL_POSITION;
                glLoadMatrix4x3(&position);
                MATRIX_STORE = static_cast<std::uint32_t>(kModelFirstSlot + k);
            }
            MATRIX_CONTROL = GL_MODELVIEW;
            const std::uint32_t *word = model.gx + batch.gxOfs;
            for (int count = batch.gxWords; count > 0; --count)
                GFX_FIFO = *word++;
        }

        // The big turret's eye: a red glow where the model has it (its
        // second attachment, which turns with the gun), as bright as the
        // turret says. Drawn with the effects.
        fx eye[3];
        if (actor.kind == static_cast<std::uint8_t>(ActorKind::Turret) && actor.reach > 0 &&
            attachmentPoint(model, pose, 1, eye))
        {
            fx at[3];
            for (int k = 0; k < 3; ++k)
                at[k] = actor.pos[k] + fxMul(turn[k * 3], eye[0]) + fxMul(turn[k * 3 + 1], eye[1]) +
                        fxMul(turn[k * 3 + 2], eye[2]);
            effectDraw.glow(at, static_cast<int>(actor.reach) * 31 / 255, RGB15(31, 2, 2), 12);
        }
        // A light of Xen's glow, while it shines: at its second attachment
        // (the top of its stalk), in the colour the map gave it.
        else if (actor.kind == static_cast<std::uint8_t>(ActorKind::Xen) &&
                 actor.item == static_cast<std::uint8_t>(XenPlant::Light) && actor.reach > 0 && id >= 0 &&
                 id < game.actors.recordCount && attachmentPoint(model, pose, 1, eye))
        {
            fx at[3];
            for (int k = 0; k < 3; ++k)
                at[k] = actor.pos[k] + fxMul(turn[k * 3], eye[0]) + fxMul(turn[k * 3 + 1], eye[1]) +
                        fxMul(turn[k * 3 + 2], eye[2]);
            const auto colour = static_cast<std::uint16_t>(game.actors.records[id].item >> 16);
            effectDraw.glow(at, static_cast<int>(actor.reach) * 31 / 255, colour, 16);
        }
    }

    void ModelDraw::plan(int &polygons, int &vertices)
    {
        found_ = 0;
        cut_ = 0;
        heldDrawn_ = false;
        polygons = 0;
        vertices = 0;
        int &found = found_;
        int &cut = cut_;
        // How many at once: the player's say, or what the machine is good
        // for.
        const int limit = settings.graphics.modelLimit != 0
                              ? (settings.graphics.modelLimit > kMaxDrawn ? kMaxDrawn : settings.graphics.modelLimit)
                              : limit_;
        int *order = order_;
        const ActorWorld &actors = game.actors;

        // Who can be seen, in the order they are to be had: enemies, then
        // the people, then what there is to pick up, then the rest; each
        // kind nearest first (shared/HlDrawPlan.hpp). More are looked at
        // than can be drawn, so that it is the right ones that are.
        constexpr int kLooked = kDrawMost * 2;
        int seen[kLooked];
        std::uint8_t seenClass[kLooked];
        std::int32_t reach[kLooked];
        int looked = 0;
        for (int c = 0; c < kDrawClasses; ++c)
            cutClass_[c] = 0;
        // (The lines of sight this frame may cost: HlWalk.hpp's lineClear.)
        ++planFrame_;
        hidden_ = 0;
        int linesLeft = isDSiMode() ? 10 : 6;
        for (int i = 0; actors.states != nullptr && i < actors.count; ++i)
        {
            const ActorState &actor = actors.states[i];
            const ModelView *model = actor.active ? actorModel(actors, actor) : nullptr;
            if (model == nullptr)
                continue;
            // In view if any part of it is: a ball round its middle and one
            // round its head, for something standing; all the way down its
            // tongue, for a barnacle.
            // (A clip that carries it far down -- a fall -- counts all the way.)
            const int clipDown = actor.clip >= 0 && actor.clip < static_cast<int>(model->header->clipCount) &&
                                         model->clips[actor.clip].endUp < 0
                                     ? model->clips[actor.clip].endUp
                                     : 0;
            DrawPoints points =
                actorViewPoints(actor, model->header->maxs[2], model->header->radius, clipDown);
            // An enemy is seen however far off it is (the fog has it); and if
            // its middle and head are in places the eye cannot see, its sides
            // may be in one that it can (a wide one at a doorway).
            const bool enemy = drawClass(actor) == kDrawEnemy;
            const int ownPoints = points.count; // (its own: middle, head, down its tongue)
            if (enemy && points.count + 4 <= DrawPoints::kMost)
            {
                const int half = model->header->radius / 2;
                const std::int16_t mid = points.at[0][2];
                for (int k = 0; k < 4; ++k)
                {
                    std::int16_t *p = points.at[points.count++];
                    p[0] = static_cast<std::int16_t>(points.at[0][0] + ((k & 1) ? half : -half));
                    p[1] = static_cast<std::int16_t>(points.at[0][1] + ((k & 2) ? half : -half));
                    p[2] = mid;
                }
            }
            bool visible = false;
            for (int k = 0; k < points.count && !visible; ++k)
                visible = worldDraw.ballVisible(points.at[k], model->header->radius, enemy);
            if (!visible)
                continue;
            // (How far off: the longest of the three ways and half the
            // rest, which is near enough the true distance, and is what a
            // level of detail is chosen from. To the nearest of its points:
            // a barnacle is as near as its tongue is.)
            std::int32_t far = 0x7FFFFFFF;
            for (int k = 0; k < points.count; ++k)
            {
                const std::int32_t dx = points.at[k][0] - fxToInt(camera.pos[0]);
                const std::int32_t dy = points.at[k][1] - fxToInt(camera.pos[1]);
                const std::int32_t dz = points.at[k][2] - fxToInt(camera.pos[2]);
                const std::int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, az = dz < 0 ? -dz : dz;
                const std::int32_t most = ax > ay ? (ax > az ? ax : az) : (ay > az ? ay : az);
                const std::int32_t one = most + (ax + ay + az - most) / 2;
                far = one < far ? one : far;
            }
            // Behind a wall, though in a place the eye's can see: not drawn.
            // A line from the eye to its middle, its top and its bottom (a
            // barnacle's tongue end) -- seen if any is clear. A few lines a
            // frame, kept per actor: one hidden is looked at again every
            // frame (it shows the frame it comes into sight), the rest every
            // eighth; and hidden only after two looks in a row. Never what is
            // close to the eye, nor what a script has.
            if (far > static_cast<std::int32_t>(model->header->radius) * kDrawClose &&
                !(actor.script >= 0 && actor.scriptPhase != kActorFree))
            {
                auto clear = [&]()
                {
                    const int ends[3] = {0, ownPoints > 1 ? 1 : 0, ownPoints - 1};
                    for (int k = 0; k < 3; ++k)
                    {
                        if (k > 0 && ends[k] == ends[k - 1])
                            continue;
                        const std::int16_t *p = points.at[ends[k]];
                        const fx to[3] = {fxFromInt(p[0]), fxFromInt(p[1]), fxFromInt(p[2])};
                        if (lineClear(game.world, camera.pos, to))
                            return true;
                    }
                    return false;
                };
                if (drawHidden(hiddenRun_[slotOf(i)], hiddenLook_[slotOf(i)], planFrame_, linesLeft, clear))
                {
                    ++hidden_;
                    continue;
                }
            }
            else
            {
                hiddenRun_[slotOf(i)] = 0;
            }
            const int kind = drawClass(actor);
            int at = looked < kLooked ? looked : kLooked - 1;
            if (looked >= kLooked && !drawBefore(kind, far, seenClass[at], reach[at]))
            {
                ++cutClass_[kind];
                continue;
            }
            if (looked >= kLooked)
                ++cutClass_[seenClass[at]]; // the last so far gives up its place
            while (at > 0 && drawBefore(kind, far, seenClass[at - 1], reach[at - 1]))
            {
                seen[at] = seen[at - 1];
                seenClass[at] = seenClass[at - 1];
                reach[at] = reach[at - 1];
                --at;
            }
            seen[at] = i;
            seenClass[at] = static_cast<std::uint8_t>(kind);
            reach[at] = far;
            if (looked < kLooked)
                ++looked;
        }

        // What the player holds is always in view.
        // (Not while a gun's controls are in hand: the game's holstered weapon.)
        const ModelView *held = game.held.active && !player.noclip && tankManned(game.logic) < 0
                                    ? actorModel(actors, game.held)
                                    : nullptr;
        // (The player's say: with model detail high, a step of pressure is
        // borne before any model is made coarser for it.)
        const int weight = modelDetailWeight(settings.graphics.modelDetail);
        const int pressure = settings.graphics.modelDetail == 2 ? (worldDraw.pressure() > 0 ? worldDraw.pressure() - 1 : 0)
                                                                : worldDraw.pressure();
        DrawHeld inHand;
        if (held != nullptr)
        {
            inHand.active = true;
            const int levels = held->levelCount();
            inHand.levels = static_cast<std::uint8_t>(levels > kDrawLevels ? kDrawLevels : levels);
            for (int b = 0; b < held->header->batchCount; ++b)
            {
                const int level = held->batches[b].level;
                if (level >= kDrawLevels)
                    continue;
                inHand.polygons[level] += held->batches[b].polygons;
                inHand.vertices[level] += held->batches[b].vertices;
            }
            // (At its finest, unless that is the game's own mesh and the
            // frame is already too full: then the level under it.)
            inHand.want = pressure > 0 && levels > 1 && held->header->polygons > kWholeModel ? 1 : 0;
        }

        // Each actor at a level of detail: the coarsest whose error is
        // under a pixel and a half at the model's distance (a unit is 127
        // pixels over the distance in units, so a sixteenth of a unit of
        // error is good from five and a third times as many units away).
        // What is then made of them -- who is drawn, who coarser, what
        // gives way -- is HlDrawPlan's.
        DrawCandidate candidates[kLooked];
        for (int n = 0; n < looked; ++n)
        {
            const ActorState &actor = actors.states[seen[n]];
            const ModelView &model = *actorModel(actors, actor);
            const int levels = model.levelCount() > kDrawLevels ? kDrawLevels : model.levelCount();
            int level = 0;
            for (int l = levels - 1; l > 0; --l)
            {
                if (model.header->levelError[l] * weight <= reach[n] * 3)
                {
                    level = l;
                    break;
                }
            }
            // (A model that was at a coarser level a moment ago stays at
            // it until it is a tenth nearer than where it would take it up:
            // otherwise one standing at the line flickers between two.)
            const int last = lastLevel_[slotOf(seen[n])] - 1;
            if (last > level && last < levels && model.header->levelError[last] * weight * 9 <= reach[n] * 3 * 10)
                level = last;
            DrawCandidate &one = candidates[n];
            one.kind = seenClass[n];
            one.levels = static_cast<std::uint8_t>(levels);
            one.want = static_cast<std::uint8_t>(level);
            // (Close to the eye: within five times its own size, where it
            // fills much of the picture and a coarse mesh shows.)
            one.far = reach[n];
            one.close = reach[n] <= static_cast<std::int32_t>(model.header->radius) * kDrawClose;
            // (Once not seen, not yet twice: the first to give way if room
            // is short.)
            one.hidden = !one.close && hiddenRun_[slotOf(seen[n])] != 0;
            for (int l = 0; l < levels; ++l)
            {
                int p = 0, v = 0;
                for (int b = 0; b < model.header->batchCount; ++b)
                {
                    if (!model.batchInBody(model.batches[b], actor.body, actor.skin, l))
                        continue;
                    p += model.batches[b].polygons;
                    v += model.batches[b].vertices;
                }
                one.polygons[l] = static_cast<std::uint16_t>(p);
                one.vertices[l] = static_cast<std::uint16_t>(v);
            }
            // (A player's: and the gun in their hand, drawn at its coarsest.)
            {
                const NetMatch &match = netGame.match();
                const int player = seen[n] - match.firstActor;
                if (actor.kind == static_cast<std::uint8_t>(ActorKind::Player) && player >= 0 && player < kNetPlayers)
                {
                    const int m = modelCache.heldModel(netPlayer(match, player).weapon);
                    if (m >= 0 && m < actors.modelCount && actors.models[m].header != nullptr)
                    {
                        const ModelView &gun = actors.models[m];
                        const int coarse = gun.levelCount() - 1;
                        int p = 0, v = 0;
                        for (int b = 0; b < gun.header->batchCount; ++b)
                        {
                            if (!gun.batchInBody(gun.batches[b], 0, 0, coarse))
                                continue;
                            p += gun.batches[b].polygons;
                            v += gun.batches[b].vertices;
                        }
                        for (int l = 0; l < levels; ++l)
                        {
                            one.polygons[l] = static_cast<std::uint16_t>(one.polygons[l] + p);
                            one.vertices[l] = static_cast<std::uint16_t>(one.vertices[l] + v);
                        }
                    }
                }
            }
        }
        // The models' share of this pass: as much of it as it always was,
        // but no more than seven tenths of what the pass now has (when the
        // hardware has had to be held back, the world must keep some); and
        // what it may grow by for the enemies, up to nine tenths in all.
        const int passPolygons = worldDraw.polygonBudget(), passVertices = worldDraw.vertexBudget();
        const int sharePolygons = kMaxPolygons < passPolygons * 7 / 10 ? kMaxPolygons : passPolygons * 7 / 10;
        const int shareVertices = kMaxVertices < passVertices * 13 / 20 ? kMaxVertices : passVertices * 13 / 20;
        const int growPolygons = passPolygons * 9 / 10 - sharePolygons;
        const int growVertices = passVertices * 9 / 10 - shareVertices;
        const DrawPlan chosen = planModels(candidates, looked, limit, inHand, pressure, sharePolygons, shareVertices,
                                           kWholeModel, isDSiMode() ? kDrawMost : 12, growPolygons > 0 ? growPolygons : 0,
                                           growVertices > 0 ? growVertices : 0);
        heldLevel_ = chosen.heldLevel;
        heldDrawn_ = held != nullptr;
        polygons = chosen.polygons;
        vertices = chosen.vertices;
        grown_ = chosen.grown;
        strained_ = chosen.strained;
        // (What is in someone's hand goes only when people have gone too.)
        handsCut_ = chosen.cut[kDrawPerson] > 0 || chosen.cut[kDrawEnemy] > 0;
        int kept = 0;
        for (int n = 0; n < looked; ++n)
        {
            if (!chosen.drawn[n] || kept >= kMaxDrawn)
            {
                lastLevel_[slotOf(seen[n])] = 0;
                continue;
            }
            // A step at a time: one drawn a frame ago goes no more than a
            // level finer or coarser in a frame -- what the frame's pressure
            // asks of it is seen as a softening, not as a jump from the
            // finest mesh to the coarsest. (What that costs over the plan
            // is counted below.)
            int level = chosen.level[n];
            const int last = lastLevel_[slotOf(seen[n])] - 1;
            if (last >= 0 && level > last + 1)
                level = last + 1;
            else if (last >= 0 && level < last - 1)
                level = last - 1;
            if (level != chosen.level[n])
            {
                polygons += candidates[n].polygons[level] - candidates[n].polygons[chosen.level[n]];
                vertices += candidates[n].vertices[level] - candidates[n].vertices[chosen.level[n]];
            }
            lastLevel_[slotOf(seen[n])] = static_cast<std::uint8_t>(level + 1);
            level_[kept] = static_cast<std::uint8_t>(level);
            order[kept++] = seen[n];
        }
        // (What was left out while they were looked over is not known to be
        // hidden: counted as seen.)
        seenCut_ = cutClass_[kDrawEnemy] + chosen.cutSeen;
        for (int c = 0; c < kDrawClasses; ++c)
        {
            cutClass_[c] += chosen.cut[c];
            cut += cutClass_[c];
        }
        found = kept;

        // The pieces of what has come apart: those in view, for as long as
        // there is room in the models' share (they are the first thing to
        // go when there is not: nothing under pressure, and never more than
        // a third of the share).
        // (... and nothing when an enemy has had to be made room for.)
        gibFound_ = 0;
        if (worldDraw.pressure() < 2 && !strained_)
        {
            int gibPolygons = 0;
            for (int i = 0; i < kMaxGibs; ++i)
            {
                const Gib &gib = actors.gibs[i];
                if (!gib.active || gib.model >= actors.modelCount || actors.models[gib.model].header == nullptr)
                    continue;
                const ModelView &model = actors.models[gib.model];
                const std::int16_t centre[3] = {static_cast<std::int16_t>(fxToInt(gib.pos[0])),
                                                static_cast<std::int16_t>(fxToInt(gib.pos[1])),
                                                static_cast<std::int16_t>(fxToInt(gib.pos[2]))};
                if (!worldDraw.ballVisible(centre, 12))
                    continue;
                int p = 0, v = 0;
                for (int b = 0; b < model.header->batchCount; ++b)
                {
                    // (A piece is small and soon gone: the coarsest there is.)
                    if (!model.batchInBody(model.batches[b], gib.body, 0, model.levelCount() - 1))
                        continue;
                    p += model.batches[b].polygons;
                    v += model.batches[b].vertices;
                }
                if (polygons + p > kMaxPolygons || vertices + v > kMaxVertices ||
                    gibPolygons + p > kMaxPolygons / 3)
                    continue;
                polygons += p;
                vertices += v;
                gibPolygons += p;
                gibOrder_[gibFound_++] = static_cast<std::uint8_t>(i);
            }
        }
        // The spent casings, as the game's models of them where the map has
        // them: by the same rules as the pieces above, in a sixth of the
        // share of their own. (What is left out is drawn as a triangle:
        // EffectDraw.)
        pieceFound_ = 0;
        pieceModelled_ = 0;
        if (worldDraw.pressure() < 2 && !strained_)
        {
            int casingPolygons = 0;
            for (int i = 0; i < kMaxPieces && i < 64; ++i)
            {
                const Piece &piece = actors.pieces[i];
                const int m = piece.kind != 0 ? game.shellModel(piece.kind) : -1;
                if (m < 0 || m >= actors.modelCount || actors.models[m].header == nullptr)
                    continue;
                const ModelView &model = actors.models[m];
                const std::int16_t centre[3] = {static_cast<std::int16_t>(fxToInt(piece.pos[0])),
                                                static_cast<std::int16_t>(fxToInt(piece.pos[1])),
                                                static_cast<std::int16_t>(fxToInt(piece.pos[2]))};
                if (!worldDraw.ballVisible(centre, 4))
                    continue;
                int p = 0, v = 0;
                for (int b = 0; b < model.header->batchCount; ++b)
                {
                    if (!model.batchInBody(model.batches[b], 0, 0, model.levelCount() - 1))
                        continue;
                    p += model.batches[b].polygons;
                    v += model.batches[b].vertices;
                }
                if (polygons + p > kMaxPolygons || vertices + v > kMaxVertices || casingPolygons + p > kMaxPolygons / 6)
                    continue;
                polygons += p;
                vertices += v;
                casingPolygons += p;
                pieceOrder_[pieceFound_++] = static_cast<std::uint8_t>(i);
                pieceModelled_ |= std::uint64_t{1} << i;
            }
        }
        // The RPG's laser, where it falls (while it is in hand and on), and
        // a lit rocket's flame.
        if (player.state.inv.current == kWeaponRpg && (player.arsenal.have & kArsenalLaserOff) == 0 &&
            player.state.health > 0 && !player.noclip)
        {
            fx dot[3];
            laserDot(game.world, camera.pos, camera.forward, dot);
            const fx at[3] = {dot[0] - camera.forward[0] * 2, dot[1] - camera.forward[1] * 2,
                              dot[2] - camera.forward[2] * 2};
            effectDraw.glow(at, 31, RGB15(31, 3, 3), 3);
        }
        for (int i = 0; i < kMaxProjectiles; ++i)
        {
            const Projectile &shot = actors.shots[i];
            if (shot.active && (shot.kind == kShotRocket || shot.kind == kShotRocketFoe) && shot.sprite == 1)
                effectDraw.glow(shot.pos, 26, RGB15(31, 20, 6), 8);
            // The nihilanth's balls: its zap, the one that takes the player
            // away, and what it throws off dying.
            else if (shot.active && shot.kind == kShotNihilZap)
                effectDraw.glow(shot.pos, 31, RGB15(24, 28, 31), 16);
            else if (shot.active && (shot.kind == kShotNihilPortal || shot.kind == kShotNihilGreen))
                effectDraw.glow(shot.pos, 28, RGB15(8, 31, 8), shot.kind == kShotNihilPortal ? 24 : 10);
        }
        // ... and its spheres, going round it (`reach` of them), and, open,
        // the light at its head (`mood`: HlAliens.hpp's nihilanth).
        for (int i = 0; i < actors.count; ++i)
        {
            const ActorState &boss = actors.states[i];
            if (!boss.active || boss.kind != static_cast<std::uint8_t>(ActorKind::Nihilanth))
                continue;
            const int spheres = boss.reach < 0 ? 0 : (boss.reach > 20 ? 20 : static_cast<int>(boss.reach));
            for (int n = 0; n < spheres; ++n)
            {
                fx sine, cosine;
                fxSinCos(static_cast<std::uint16_t>(Global::drawClock() * 400 + n * 65536 / 20), sine, cosine);
                const fx at[3] = {boss.pos[0] + cosine * 360, boss.pos[1] + sine * 360,
                                  boss.pos[2] + fxFromInt(240 + ((n & 3) - 2) * 40)};
                effectDraw.glow(at, 28, RGB15(31, 28, 24), 12);
            }
            if (boss.mood >= 2)
            {
                const fx head[3] = {boss.pos[0], boss.pos[1], boss.pos[2] + fxFromInt(400)};
                effectDraw.glow(head, 31, RGB15(31, 31, 31), 40);
            }
        }
        // What the player has thrown or put down: few, small, and wanted
        // (a tripmine is a thing to see) -- in what room there is.
        shotFound_ = 0;
        for (int i = 0; i < kMaxProjectiles; ++i)
        {
            const Projectile &shot = actors.shots[i];
            const int m = shot.active ? game.thrownModel(shot.kind) : -1;
            if (m < 0 || m >= actors.modelCount || actors.models[m].header == nullptr)
                continue;
            const ModelView &model = actors.models[m];
            const std::int16_t centre[3] = {static_cast<std::int16_t>(fxToInt(shot.pos[0])),
                                            static_cast<std::int16_t>(fxToInt(shot.pos[1])),
                                            static_cast<std::int16_t>(fxToInt(shot.pos[2]))};
            if (!worldDraw.ballVisible(centre, 24))
                continue;
            const int body = shot.kind == kShotMine ? 3 : 0;
            int p = 0, v = 0;
            for (int b = 0; b < model.header->batchCount; ++b)
            {
                if (!model.batchInBody(model.batches[b], body, 0, model.levelCount() - 1))
                    continue;
                p += model.batches[b].polygons;
                v += model.batches[b].vertices;
            }
            // (A room of their own over the share, taken from the world's:
            // they are few and small, and a big monster in view must not
            // hide the satchel on the floor in front of it.)
            if (polygons + p > kMaxPolygons + kThrownPolygons || vertices + v > kMaxVertices + kThrownPolygons * 3)
                continue;
            polygons += p;
            vertices += v;
            shotOrder_[shotFound_++] = static_cast<std::uint8_t>(i);
        }
    }

    void ModelDraw::prepare()
    {
        const ActorWorld &actors = game.actors;
        for (int n = 0; n < found_; ++n)
        {
            const ActorState &actor = actors.states[order_[n]];
            posed(actor, *actorModel(actors, actor), actor.model, order_[n]);
        }
        const ActorState &held = game.held;
        const ModelView *model = held.active ? actorModel(game.actors, held) : nullptr;
        if (model != nullptr && heldDrawn_)
            posed(held, *model, held.model, -1);
        // (A gib model has one pose, shared by all its pieces: nothing to
        // work out worth doing ahead.)
    }

    HL_ITCM void ModelDraw::draw(int &drawn, int &cut)
    {
        const ActorWorld &actors = game.actors;
        cut += cut_;
        if (found_ == 0 && gibFound_ == 0 && shotFound_ == 0 && pieceFound_ == 0)
            return;
        const int found = found_;
        const int *order = order_;

        lightOn();

        for (int n = 0; n < found; ++n)
        {
            const ActorState &actor = actors.states[order[n]];
            const ModelView &model = *actorModel(actors, actor);
            // Turned about the vertical, the way it faces.
            fx sine, cosine;
            fxSinCos(actor.yaw, sine, cosine);
            fx turn[9] = {cosine, -sine, 0, sine, cosine, 0, 0, 0, kFxOne};
            std::int32_t tiltPitch = 0, tiltRoll = 0;
            if (actorTilt(actor, tiltPitch, tiltRoll))
            {
                // ... and, swimming or flying, nosed up or down and rolled
                // (actorTilt): its forward, left and up are the columns.
                fx sp, cp, sr, cr;
                fxSinCos(static_cast<std::uint16_t>(tiltPitch), sp, cp);
                fxSinCos(static_cast<std::uint16_t>(tiltRoll), sr, cr);
                const fx forward[3] = {fxMul(cp, cosine), fxMul(cp, sine), sp};
                const fx left0[3] = {-sine, cosine, 0};
                const fx up0[3] = {-fxMul(sp, cosine), -fxMul(sp, sine), cp};
                for (int r = 0; r < 3; ++r)
                {
                    turn[r * 3 + 0] = forward[r];
                    turn[r * 3 + 1] = fxMul(left0[r], cr) + fxMul(up0[r], sr);
                    turn[r * 3 + 2] = fxMul(up0[r], cr) - fxMul(left0[r], sr);
                }
            }
            // A turret under a ceiling is the same model upside down: turned
            // over about the way it faces (the game's half turn of pitch,
            // and of yaw to make up for it).
            const fx over[9] = {cosine, sine, 0, sine, -cosine, 0, 0, 0, -kFxOne};
            const bool ceiling = isTurret(actor.kind) && (actor.flags & kActorCeiling) != 0;
            // (A body the game fades: as much of it as is left. An
            // assassin cloaked: its renderamt, of 255.)
            const int fading = corpseFadeLeft(actor);
            const int cloaked = actor.cloak.value != 0 ? (255 - actor.cloak.value) * 31 / 255 : -1;
            // (A monster_generic the map has see-through, or a hologram:
            // fading with distance and flickering, as the game's does.)
            const std::uint32_t look =
                actor.kind == static_cast<std::uint8_t>(ActorKind::Generic) && order[n] < actors.recordCount
                    ? (actors.records[order[n]].item >> kActorLookShift) & kActorLookMask
                    : 0;
            int glass = -1;
            if (look == kActorLookHologram)
            {
                const int d[3] = {fxToInt(actor.pos[0] - camera.pos[0]), fxToInt(actor.pos[1] - camera.pos[1]),
                                  fxToInt(actor.pos[2] - camera.pos[2])};
                const int along = (d[0] * camera.forward[0] + d[1] * camera.forward[1] + d[2] * camera.forward[2]) >> 12;
                glass = hologramBlend(along, flickerNoise()) >> 3;
                if (glass < 1)
                    continue;
                glow_ = true;
            }
            else if (look != 0)
            {
                glass = static_cast<int>(look) * 31 / 7;
            }
            if (fading >= 0)
                modelAlpha(1 + (fading * 30) / kCorpseFadeMs);
            else if (cloaked >= 0)
                modelAlpha(cloaked);
            else if (glass >= 0)
                glassAlpha(glass, glow_);
            drawActor(actor, model, actor.model, ceiling ? over : turn, order[n], level_[n]);
            drawGun(actor, model, ceiling ? over : turn, order[n]);
            glow_ = false;
            if (fading >= 0 || cloaked >= 0 || glass >= 0)
                modelAlpha(31);
            ++drawn;
        }
        // The pieces: each a model of its own for the moment, turned as it
        // has tumbled. (One pose serves all the pieces of a model: they are
        // asked for under one number.)
        for (int n = 0; n < gibFound_; ++n)
        {
            const Gib &gib = actors.gibs[gibOrder_[n]];
            if (!gib.active || gib.model >= actors.modelCount || actors.models[gib.model].header == nullptr)
                continue;
            ActorState piece;
            piece.active = true;
            piece.kind = static_cast<std::uint8_t>(ActorKind::Generic);
            piece.model = gib.model;
            piece.body = gib.body;
            piece.light = 22;
            piece.clip = actors.models[gib.model].header->clipCount != 0 ? 0 : -1;
            for (int k = 0; k < 3; ++k)
                piece.pos[k] = gib.pos[k];
            fx sy, cy, sp, cp;
            fxSinCos(gib.yaw, sy, cy);
            fxSinCos(gib.pitch, sp, cp);
            const fx turn[9] = {fxMul(cy, cp), -sy, fxMul(cy, sp), fxMul(sy, cp), cy, fxMul(sy, sp), -sp, 0, cp};
            // (Its last two seconds it fades: SUB_StartFadeOut.)
            const bool fading = gib.leftMs < 2000;
            if (fading)
                modelAlpha(1 + (gib.leftMs * 30) / 2000);
            drawActor(piece, actors.models[gib.model], gib.model, turn, -3 - static_cast<int>(gib.model),
                      actors.models[gib.model].levelCount() - 1);
            if (fading)
                modelAlpha(31);
        }
        // The casings: each the game's model of it, turned about as it
        // tumbles in the air, and lying flat once it has come to rest.
        for (int n = 0; n < pieceFound_; ++n)
        {
            const Piece &piece = actors.pieces[pieceOrder_[n]];
            const int m = piece.kind != 0 ? game.shellModel(piece.kind) : -1;
            if (m < 0 || m >= actors.modelCount || actors.models[m].header == nullptr)
                continue;
            ActorState casing;
            casing.active = true;
            casing.kind = static_cast<std::uint8_t>(ActorKind::Generic);
            casing.model = static_cast<std::uint16_t>(m);
            casing.light = 22;
            casing.clip = actors.models[m].header->clipCount != 0 ? 0 : -1;
            for (int k = 0; k < 3; ++k)
                casing.pos[k] = piece.pos[k];
            fx sy, cy, sp, cp;
            fxSinCos(piece.spin, sy, cy);
            fxSinCos(piece.bounces >= 2 ? 0 : static_cast<std::uint16_t>(piece.spin * 3), sp, cp);
            const fx turn[9] = {fxMul(cy, cp), -sy, fxMul(cy, sp), fxMul(sy, cp), cy, fxMul(sy, sp), -sp, 0, cp};
            drawActor(casing, actors.models[m], static_cast<unsigned>(m), turn, -3 - m, actors.models[m].levelCount() - 1);
        }
        // The later weapons' things, each turned the way it faces: a bolt
        // along its flight, a tripmine out of its wall, a satchel and a
        // snark the way they go (on the floor: they move as a crouched
        // hull's middle, 18 units up).
        for (int n = 0; n < shotFound_; ++n)
        {
            const Projectile &shot = actors.shots[shotOrder_[n]];
            const int m = shot.active ? game.thrownModel(shot.kind) : -1;
            if (m < 0 || m >= actors.modelCount || actors.models[m].header == nullptr)
                continue;
            const ModelView &model = actors.models[m];
            ActorState thing;
            thing.active = true;
            thing.kind = static_cast<std::uint8_t>(ActorKind::Generic);
            thing.model = static_cast<std::uint16_t>(m);
            thing.body = shot.kind == kShotMine ? 3 : 0;
            thing.light = 22;
            thing.clip = model.header->clipCount != 0 ? 0 : -1;
            const bool snark = shot.kind == kShotSnark || shot.kind == kShotSnarkFoe;
            if (snark && thing.clip == 0 && model.clips[0].frameCount > 1)
            {
                // (Its legs going, at the clip's own rate.)
                const std::uint32_t whole = static_cast<std::uint32_t>(model.clips[0].frameCount - 1) << 8;
                thing.frame = static_cast<std::uint32_t>(Global::drawClock() * model.clips[0].fps / 60) % whole;
            }
            fx dir[3] = {shot.velocity[0], shot.velocity[1], shot.velocity[2]};
            // (Each its own way through its spin, by where it is in the list.)
            const std::uint32_t own = static_cast<std::uint32_t>(shotOrder_[n]) * 37u;
            if (shot.kind == kShotGrenade)
            {
                // A hand grenade (ShootTimed, TumbleThink): spinning through
                // its "toss" in the air; once it has all but stopped
                // (HlMonsters.hpp marks it so), its "roll1", as fast as it
                // rolls (its speed / 200, none below half of that).
                const bool rolling = shot.sprite == 0xFFFE;
                const int clip = model.findClip(nameHash(rolling ? "roll1" : "toss"));
                if (clip >= 0 && model.clips[clip].frameCount > 1)
                {
                    thing.clip = static_cast<std::int16_t>(clip);
                    const fx speed = fxLength(shot.velocity);
                    int rate = 256;
                    if (rolling)
                    {
                        rate = static_cast<int>((static_cast<std::int64_t>(speed) * 256) / fxFromInt(200));
                        rate = rate > 256 ? 256 : (rate < 128 ? 0 : rate);
                    }
                    const std::uint32_t whole = static_cast<std::uint32_t>(model.clips[clip].frameCount - 1) << 8;
                    thing.frame = static_cast<std::uint32_t>(
                                      ((static_cast<std::uint64_t>(Global::drawClock()) + own) * model.clips[clip].fps * rate) /
                                      (60 * 256)) %
                                  whole;
                }
                // (Facing the way it goes, flat: the clip does the turning.)
                dir[2] = 0;
            }
            for (int k = 0; k < 3; ++k)
                thing.pos[k] = shot.pos[k];
            if (shot.kind == kShotSatchel || snark)
            {
                dir[2] = 0;
                // (A monstermaker's snark is where its feet are: HlMonsters.hpp.)
                thing.pos[2] -= shot.kind == kShotSnarkFoe ? fxFromInt(4) : kCrouchedFeet;
            }
            else if (shot.kind == kShotMine)
            {
                for (int k = 0; k < 3; ++k)
                    thing.pos[k] -= dir[k] * 4; // (back onto the wall)
            }
            // Its forward along `dir`: yaw and pitch as the pieces have them.
            fx forward[3] = {kFxOne, 0, 0};
            const fx length = fxLength(dir);
            if (length > fxFromInt(1) / 16)
            {
                for (int k = 0; k < 3; ++k)
                    forward[k] = static_cast<fx>((static_cast<std::int64_t>(dir[k]) << kFxShift) / length);
            }
            const fx level[3] = {forward[0], forward[1], 0};
            const fx cp = fxLength(level);
            fx cy = kFxOne, sy = 0;
            if (cp > kFxOne / 64)
            {
                cy = static_cast<fx>((static_cast<std::int64_t>(forward[0]) << kFxShift) / cp);
                sy = static_cast<fx>((static_cast<std::int64_t>(forward[1]) << kFxShift) / cp);
            }
            fx sp = -forward[2];
            fx cpp = cp;
            if (shot.kind == kShotContact)
            {
                // The launcher's grenade: pointed along its flight, and
                // tumbling end over end (ShootContact: 100 to 500 degrees a
                // second; here 300).
                const std::uint16_t pitch = static_cast<std::uint16_t>(
                    fxAtan2Turns(-forward[2], cp) +
                    static_cast<std::uint16_t>((static_cast<std::uint32_t>(Global::drawClock()) + own) * 910u));
                fxSinCos(pitch, sp, cpp);
            }
            const fx turn[9] = {fxMul(cy, cpp), -sy, fxMul(cy, sp), fxMul(sy, cpp), cy, fxMul(sy, sp), -sp, 0, cpp};
            drawActor(thing, model, static_cast<unsigned>(m), turn, -3 - m, model.levelCount() - 1);
        }
        lightOff();
    }

    void ModelDraw::lightOn()
    {
        // One light, set in view space: with the direction matrix at identity
        // the hardware takes the vector as it is given.
        MATRIX_CONTROL = GL_MODELVIEW;
        MATRIX_IDENTITY = 0;
        {
            const int *view = camera.view.m;
            int v[3];
            for (int j = 0; j < 3; ++j)
            {
                // The view's rows are 8 long: >> 15 leaves Q12; a light
                // vector is 10 bits, 1.0 = 512 (and may not quite be 1).
                const std::int64_t sum = static_cast<std::int64_t>(kLightDir[0]) * view[0 + j] +
                                         static_cast<std::int64_t>(kLightDir[1]) * view[3 + j] +
                                         static_cast<std::int64_t>(kLightDir[2]) * view[6 + j];
                v[j] = static_cast<int>(sum >> (15 + 3));
                v[j] = v[j] > 511 ? 511 : (v[j] < -511 ? -511 : v[j]);
            }
            GFX_LIGHT_VECTOR = (v[0] & 0x3FF) | ((v[1] & 0x3FF) << 10) | ((v[2] & 0x3FF) << 20);
        }
        // The lamp's: straight along the view, which in view space is
        // always the same way. (Its colour is each model's own: none, for
        // one that is not in the beam.)
        GFX_LIGHT_VECTOR = ((static_cast<std::uint32_t>(-511) & 0x3FF) << 20) | (1u << 30);
        // How the shine falls off as a face turns from the eye: steeply (a
        // highlight, not a wash). The table is 128 steps of 0..255.
        if (game.lamp.on)
        {
            for (int w = 0; w < 32; ++w)
            {
                std::uint32_t word = 0;
                for (int b = 0; b < 4; ++b)
                {
                    const std::uint32_t x = static_cast<std::uint32_t>(w * 4 + b) * 2; // 0..254
                    const std::uint32_t x2 = x * x >> 8, x4 = x2 * x2 >> 8;
                    word |= (x4 * x2 >> 8) << (b * 8);
                }
                GFX_SHININESS = word;
            }
        }
        // What a hologram's picture has added to it (glassAlpha): a pale grey,
        // the same however it is lit. (Nothing else is drawn highlighted.)
        NEA_ToonHighlightEnable(true);
        NEA_ToonTableFill(RGB15(5, 5, 5));
        NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(NEA_LIGHT_0 | NEA_LIGHT_1), kCull, worldDraw.polygonMode());
    }

    void ModelDraw::lightOff()
    {
        // As it was: the camera's matrix, no lights, the level's polygons.
        camera.apply();
        NEA_PolyFormat(31, 0, static_cast<NEA_LightEnum>(0), WorldDraw::kCull, worldDraw.polygonMode());
        GFX_COLOR = 0x7FFF;
    }

    void ModelDraw::drawHeld()
    {
        const ActorState &held = game.held;
        const ModelView *model = held.active ? actorModel(game.actors, held) : nullptr;
        // (Not in a dead hand: the camera has fallen from it; nor while the
        // view is from a trigger_camera.)
        if (model == nullptr || !heldDrawn_ || player.state.health <= 0 || game.logic.camera >= 0)
            return;
        // At the eye, turned as the view is: the model's forward, left and up
        // are the camera's.
        // (... and with the walk's bob, pushed a little forward by it, as
        // the game's is.)
        ActorState at = held;
        for (int a = 0; a < 3; ++a)
            at.pos[a] = camera.pos[a] + static_cast<fx>((static_cast<std::int64_t>(camera.forward[a]) * player.view.bob * 2 / 5) >> 12);
        const fx turn[9] = {camera.forward[0], -camera.right[0], camera.up[0],
                            camera.forward[1], -camera.right[1], camera.up[1],
                            camera.forward[2], -camera.right[2], camera.up[2]};
        // In the game it has a depth range of its own, and no wall comes in
        // front of it. Here the depth is the world's: so it is drawn a
        // quarter of its size about the eye -- exactly the same picture, a
        // quarter as far away: in front of any wall the player can stand
        // against (16 units) -- through a projection whose near plane is a
        // quarter as far too, or its nearest parts would be cut by that
        // instead. (In this projection the near plane goes with the
        // translation row's z: a quarter of it is a quarter as near. Depth
        // is distance -- W-buffering -- and does not change with it.)
        constexpr int kShrink = 2;
        int projection[16];
        glGetFixed(GL_GET_MATRIX_PROJECTION, projection);
        m4x4 nearer;
        for (int k = 0; k < 16; ++k)
            nearer.m[k] = projection[k];
        nearer.m[14] = projection[14] >> kShrink;
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadMatrix4x4(&nearer);
        glMatrixMode(GL_MODELVIEW);
        lightOn();
        drawActor(at, *model, held.model, turn, -1, heldLevel_, kShrink);
        lightOff();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix(1);
        glMatrixMode(GL_MODELVIEW);
    }
} // namespace hlrt
