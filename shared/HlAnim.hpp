#pragma once

// Posing a model: from a clip and a moment in it, each bone's matrix in the
// model's own space.
//
// A clip holds, per bone, keys of a rotation (a quaternion) and a place, both
// relative to the bone's parent. A pose is: for each bone find the two keys
// the moment falls between, blend them, turn the result into a matrix, and
// put it on top of the parent's -- parents always come first in the list, so
// one pass does it.
//
// Everything is fixed point: quaternions in Q14, matrices' rotations in Q14,
// places in Q12 units. Blending two rotations is a straight line between the
// quaternions, made unit length again: between keys that the converter has
// already put close together, that is as good as the long way round the
// sphere and a fraction of the work.
//
// std-only and inline: the host tests hold it against the game's own
// floating-point maths.

#include "HlDsp.hpp"
#include "HlFixed.hpp"
#include "HlModel.hpp"

namespace hldata
{
    /// A bone's matrix: the rotation's nine in Q14, row by row
    /// (model = r * bone), and where the bone's origin is, in Q12 units.
    struct BoneMatrix
    {
        std::int32_t r[9];
        fx t[3];
    };

    inline constexpr int kBoneRotShift = 14;

    namespace detail
    {
        /// The matrix of a unit quaternion (x y z w, Q14), in Q14.
        inline void quatToMatrix(const std::int32_t *q, std::int32_t *m)
        {
            // (Each part is at most one, 16384: 16 bits, and the product 32.)
            const auto mul = [](std::int32_t a, std::int32_t b) { return smulbb(a, b) >> 13; }; // 2ab, Q14
            const std::int32_t xx = mul(q[0], q[0]), yy = mul(q[1], q[1]), zz = mul(q[2], q[2]);
            const std::int32_t xy = mul(q[0], q[1]), xz = mul(q[0], q[2]), yz = mul(q[1], q[2]);
            const std::int32_t wx = mul(q[3], q[0]), wy = mul(q[3], q[1]), wz = mul(q[3], q[2]);
            constexpr std::int32_t one = 1 << 14;
            m[0] = one - yy - zz;
            m[1] = xy - wz;
            m[2] = xz + wy;
            m[3] = xy + wz;
            m[4] = one - xx - zz;
            m[5] = yz - wx;
            m[6] = xz - wy;
            m[7] = yz + wx;
            m[8] = one - xx - yy;
        }

        /// Row `row` of one Q14 rotation times column `column` of another, Q14.
        /// (Every entry of a rotation is at most one, 16384: 16 bits, and the
        /// three products' sum fits 32.)
        inline std::int32_t rotationDot(const std::int32_t *row, const std::int32_t *column)
        {
            return dotShort(row, 1, column, 3) >> kBoneRotShift;
        }

        /// Which two of a run of keys `frame` (Q8) falls between, and how
        /// far along (Q12). Keys sit at 0, step, 2 * step ... and the last at
        /// the clip's last frame, which is usually a shorter stretch.
        inline std::uint32_t keySpan(std::uint32_t count, std::uint32_t step, std::uint32_t frame,
                                     std::uint32_t lastFrame, std::int32_t &along)
        {
            // (Divided as 64 bits, so that the console's hardware divider does it.)
            std::uint32_t index = static_cast<std::uint32_t>(frame / (static_cast<std::uint64_t>(step) << 8));
            if (index >= count - 1)
                index = count - 2;
            const std::uint32_t from = index * step;
            const std::uint32_t to = index + 2 == count ? lastFrame : from + step;
            const std::uint32_t span = (to > from ? to - from : 1) << 8;
            std::uint32_t done = frame - (from << 8);
            if (done > span)
                done = span;
            along = static_cast<std::int32_t>((static_cast<std::uint64_t>(done) << 12) / span);
            return index;
        }

        /// keySpan's answers for one pose: every track of a clip shares its
        /// frame and last frame, so a track's span depends only on its key
        /// count and step -- and the converter keys at a handful of steps
        /// (ModelCook.cpp: 16, 8, 6, 4, 3, 2, 1). Each different one is
        /// worked out once, not twice a bone: the divides are calls on the
        /// DS.
        struct KeySpans
        {
            std::uint32_t frame, lastFrame;
            int known = 0;
            std::uint16_t count[8], step[8];
            std::uint32_t index[8];
            std::int32_t along[8];

            std::uint32_t get(std::uint32_t keys, std::uint32_t every, std::int32_t &out)
            {
                for (int k = 0; k < known; ++k)
                {
                    if (count[k] == keys && step[k] == every)
                    {
                        out = along[k];
                        return index[k];
                    }
                }
                const std::uint32_t at = keySpan(keys, every, frame, lastFrame, out);
                if (known < 8)
                {
                    count[known] = static_cast<std::uint16_t>(keys);
                    step[known] = static_cast<std::uint16_t>(every);
                    index[known] = at;
                    along[known] = out;
                    ++known;
                }
                return at;
            }
        };

        /// The sum of a quaternion's four squares, each part at most 16384
        /// in size (Q14, near unit length): within 32 bits.
        inline std::int32_t squaresShort(const std::int32_t *q)
        {
            return smlabb(q[3], q[3], smlabb(q[2], q[2], smlabb(q[1], q[1], smulbb(q[0], q[0]))));
        }

        /// A stored rotation, whole: the w that was left out put back.
        inline void fullQuat(const ModelKey &key, std::int32_t *q)
        {
            q[0] = key.v[0];
            q[1] = key.v[1];
            q[2] = key.v[2];
            const std::int32_t rest = (1 << 28) - q[0] * q[0] - q[1] * q[1] - q[2] * q[2];
            q[3] = rest > 0 ? static_cast<std::int32_t>(isqrt64(static_cast<std::uint64_t>(rest))) : 0;
        }

        /// One bone's rotation and place at `frame` (Q8 frames) of a track.
        HL_HOT void sampleTrack(const ModelTrack &track, const ModelKey *keys, KeySpans &spans,
                                std::int32_t *quat, fx *pos)
        {
            const ModelKey *rot = keys + track.firstRot;
            if (track.rotCount == 1)
            {
                fullQuat(*rot, quat);
            }
            else
            {
                std::int32_t s;
                const std::uint32_t index = spans.get(track.rotCount, track.rotStep, s);
                std::int32_t a[4], b[4];
                fullQuat(rot[index], a);
                fullQuat(rot[index + 1], b);
                // The same turn is two quaternions, q and -q: blend towards
                // the nearer.
                std::int32_t dot = 0;
                for (int i = 0; i < 4; ++i)
                    dot += a[i] * b[i];
                const std::int32_t sign = dot < 0 ? -1 : 1;
                for (int i = 0; i < 4; ++i)
                    quat[i] = a[i] + (((sign * b[i] - a[i]) * s) >> 12);
                // (Each part between two unit ones, at most 16384: the four
                // squares fit 32 bits.)
                const std::int32_t length2 = squaresShort(quat);
                // Back to unit length (Q14).
                const auto length = static_cast<std::int32_t>(isqrt64(static_cast<std::uint64_t>(length2)));
                if (length > 0)
                {
                    // One divide for the four of them: by its reciprocal
                    // (Q14; the length is never less than 0.7, two unit
                    // quaternions being never more than a right angle apart
                    // once the nearer is taken, so this fits 32 bits).
                    const auto reciprocal = static_cast<std::int32_t>((std::int64_t(1) << 28) / length);
                    for (int i = 0; i < 4; ++i)
                        quat[i] = (quat[i] * reciprocal) >> 14;
                }
            }

            const ModelKey *place = keys + track.firstPos;
            if (track.posCount == 1)
            {
                for (int i = 0; i < 3; ++i)
                    pos[i] = static_cast<fx>(place->v[i]) << (kFxShift - kModelPosShift);
                return;
            }
            std::int32_t s;
            const std::uint32_t index = spans.get(track.posCount, track.posStep, s);
            for (int i = 0; i < 3; ++i)
            {
                const std::int32_t p =
                    place[index].v[i] + (((place[index + 1].v[i] - place[index].v[i]) * s) >> 12);
                pos[i] = static_cast<fx>(p) << (kFxShift - kModelPosShift);
            }
        }

        /// `quat` and `pos` taken `mix` (Q12) of the way to the same bone of
        /// the other pose, `track` (the game's StudioSlerpBones).
        inline void blendPose(const ModelTrack &track, const ModelKey *keys, KeySpans &spans, std::int32_t mix,
                              std::int32_t *quat, fx *pos)
        {
            std::int32_t q[4];
            fx p[3];
            sampleTrack(track, keys, spans, q, p);
            std::int32_t dot = 0;
            for (int i = 0; i < 4; ++i)
                dot += quat[i] * q[i];
            const std::int32_t sign = dot < 0 ? -1 : 1;
            for (int i = 0; i < 4; ++i)
                quat[i] += ((sign * q[i] - quat[i]) * mix) >> 12;
            const std::int32_t length2 = squaresShort(quat); // (as sampleTrack's)
            const auto length = static_cast<std::int32_t>(isqrt64(static_cast<std::uint64_t>(length2)));
            if (length > 0)
            {
                // (Two poses of an aim are well within a right angle of each
                // other, bone by bone: as sampleTrack's, this fits.)
                const auto reciprocal = static_cast<std::int32_t>((std::int64_t(1) << 28) / length);
                for (int i = 0; i < 4; ++i)
                    quat[i] = (quat[i] * reciprocal) >> 14;
            }
            for (int i = 0; i < 3; ++i)
                pos[i] += static_cast<fx>((static_cast<std::int64_t>(p[i] - pos[i]) * mix) >> 12);
        }
    } // namespace detail

    /// The setting (0..255) of a blending clip's blend for an aim of
    /// `pitch` (65536 to the circle, up positive): SetBlending's, for the
    /// pitch blends the game's monsters have. 0 for a clip that does not
    /// blend.
    inline int clipBlend(const ModelClip &clip, std::int32_t pitch)
    {
        if (clip.blends < 2 || clip.blendLow == clip.blendHigh)
            return 0;
        // In degrees, Q8; the game's "ugly hack": a range written backwards
        // turns the value round.
        // (pitch * 360 * 256 / 65536, rounded: the same as (pitch * 45 + 16) >> 5,
        // which needs only 32 bits.)
        std::int32_t value = (static_cast<std::int16_t>(pitch) * 45 + 16) >> 5;
        const std::int32_t low = clip.blendLow * 256, high = clip.blendHigh * 256;
        if (high < low)
            value = -value;
        const std::int32_t setting = 255 * (value - low) / (high - low);
        return setting < 0 ? 0 : (setting > 255 ? 255 : setting);
    }

    /// What a controller set to `value` comes to: kept within its travel,
    /// as the game's SetController keeps it. (There, a travel written
    /// backwards turns the value round; a turn that has the whole circle is
    /// wrapped into it, and one that has not is first brought to within half
    /// a circle of its middle.)
    inline std::int32_t controlValue(const ModelController &control, std::int32_t value)
    {
        std::int32_t low = control.start, high = control.end;
        if (control.kind != kControlSlide)
        {
            if (high < low)
                value = -value;
            if (control.flags & kControlLoops)
                return low + static_cast<std::int32_t>(static_cast<std::uint32_t>(value - low) & 0xFFFF);
            const std::int32_t middle = (low + high) / 2;
            value = middle + static_cast<std::int16_t>(static_cast<std::uint32_t>(value - middle));
        }
        if (high < low)
        {
            const std::int32_t swap = low;
            low = high;
            high = swap;
        }
        return value < low ? low : (value > high ? high : value);
    }

    namespace detail
    {
        /// The matrix (Q14) of a turn of `angle` (65536 to the circle) about
        /// the unit `axis` (Q14).
        inline void axisTurn(const std::int16_t *axis, std::int32_t angle, std::int32_t *m)
        {
            std::int32_t sine, cosine; // Q30
            fxSinCosFine(static_cast<std::uint16_t>(angle), sine, cosine);
            const std::int32_t s = sine >> 16, c = cosine >> 16; // Q14
            const std::int32_t k = (1 << 14) - c;
            // (The axis's parts and s are 16-bit: SMULBB. k reaches 32768, a
            // half turn, which is not; but a unit axis's products times it
            // stay within 2^29, so those are 32-bit multiplies.)
            const auto mul = [](std::int32_t a, std::int32_t b) { return smulbb(a, b) >> 14; };
            const auto byK = [k](std::int32_t a) { return (a * k) >> 14; };
            const std::int32_t x = axis[0], y = axis[1], z = axis[2];
            m[0] = c + byK(mul(x, x));
            m[1] = byK(mul(x, y)) - mul(z, s);
            m[2] = byK(mul(x, z)) + mul(y, s);
            m[3] = byK(mul(y, x)) + mul(z, s);
            m[4] = c + byK(mul(y, y));
            m[5] = byK(mul(y, z)) - mul(x, s);
            m[6] = byK(mul(z, x)) - mul(y, s);
            m[7] = byK(mul(z, y)) + mul(x, s);
            m[8] = c + byK(mul(z, z));
        }
    } // namespace detail

    /// The model's pose at `frame` (Q8, in the clip's own frames; clamped to
    /// its last) of clip `clip`: `out[bone]` for every bone, in model space.
    ///
    /// `controls`, if given, is what the model's bone controllers are set to
    /// (HlModel.hpp ModelController), by their number: kModelControls
    /// values, a turn as 65536 to the circle and a slide as units in Q12.
    /// As the game sets them, except that there a setting is kept as one
    /// byte: here it is as fine as it is given.
    ///
    /// `blend` is where between a blending clip's two poses it is (0 the
    /// first, 255 the second: the game's pev->blending[0], clipBlend); a
    /// clip with one pose has no use for it.
#ifndef HL_DSL
    HL_HOT void poseModel(const ModelView &model, int clip, std::uint32_t frame, BoneMatrix *out,
                          const std::int32_t *controls, int blend);
    /// `bones` from the spine up as they are in clip `aim`'s first frame (its
    /// blend `blend`): the upper body of someone holding something, the rest
    /// going on walking under it. `held`: room for a pose, to work in.
    inline void overlayAimPose(const ModelView &model, BoneMatrix *bones, int aim, int blend, BoneMatrix *held)
    {
        const int spine = model.header->spineBone, count = model.header->boneCount;
        const int under = spine >= 0 ? model.parents[spine] : -1;
        if (under < 0)
            return;
        poseModel(model, aim, 0, held, nullptr, blend);
        // What takes the aim's "under the spine" to this pose's.
        const BoneMatrix &to = bones[under];
        const BoneMatrix &from = held[under];
        std::int32_t turn[9];
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
                turn[r * 3 + c] = dotShort(to.r + r * 3, 1, from.r + c * 3, 1) >> kBoneRotShift;
        }
        // (Copied back after: `to` stays what it was, being under the spine.)
        std::uint8_t up[kModelMaxBones];
        const fx toAt[3] = {to.t[0], to.t[1], to.t[2]};
        for (int b = 0; b < count; ++b)
        {
            const int parent = model.parents[b];
            up[b] = b == spine || (parent >= 0 && up[parent]) ? 1 : 0;
            if (!up[b])
                continue;
            const BoneMatrix &m = held[b];
            BoneMatrix &o = bones[b];
            fx away[3];
            for (int a = 0; a < 3; ++a)
                away[a] = m.t[a] - from.t[a];
            for (int r = 0; r < 3; ++r)
            {
                for (int c = 0; c < 3; ++c)
                    o.r[r * 3 + c] = detail::rotationDot(turn + r * 3, m.r + c);
                o.t[r] = toAt[r] + static_cast<fx>((static_cast<std::int64_t>(turn[r * 3]) * away[0] +
                                                    static_cast<std::int64_t>(turn[r * 3 + 1]) * away[1] +
                                                    static_cast<std::int64_t>(turn[r * 3 + 2]) * away[2]) >>
                                                   kBoneRotShift);
            }
        }
    }
#endif
#ifdef HL_DSL
    // (In the game library, the engine's own, in ITCM: game/GameApi.hpp.)
    void poseModel(const ModelView &model, int clip, std::uint32_t frame, BoneMatrix *out,
                          const std::int32_t *controls = nullptr, int blend = 0);
#else
    HL_HOT void poseModel(const ModelView &model, int clip, std::uint32_t frame, BoneMatrix *out,
                          const std::int32_t *controls = nullptr, int blend = 0)
    {
        const int controllers = controls != nullptr ? model.header->controllerCount : 0;
        const ModelClip &c = model.clips[clip];
        const std::uint32_t lastFrame = c.frameCount - 1u;
        if (frame > (lastFrame << 8))
            frame = lastFrame << 8;
        const int bones = model.header->boneCount;
        // (A blend at either end is that end's pose alone.)
        if (c.blends < 2 || blend < 0)
            blend = 0;
        else if (blend > 255)
            blend = 255;
        const ModelTrack *tracks = model.tracks + c.firstTrack + (blend == 255 ? bones : 0);
        const ModelTrack *others = blend > 0 && blend < 255 ? model.tracks + c.firstTrack + bones : nullptr;
        const std::int32_t mix = (blend << 12) / 255; // Q12, towards `others`
        detail::KeySpans spans;
        spans.frame = frame;
        spans.lastFrame = lastFrame;
        for (int b = 0; b < bones; ++b)
        {
            std::int32_t quat[4];
            fx pos[3];
            detail::sampleTrack(tracks[b], model.keys, spans, quat, pos);
            if (others != nullptr)
                detail::blendPose(others[b], model.keys, spans, mix, quat, pos);
            std::int32_t local[9];
            detail::quatToMatrix(quat, local);
            for (int n = 0; n < controllers; ++n)
            {
                const ModelController &control = model.controllers[n];
                if (control.bone != b)
                    continue;
                const std::int32_t value = controlValue(control, controls[control.index]);
                const std::int16_t *axis = model.controlAxes[clip * controllers + n].v;
                if (control.kind == kControlSlide)
                {
                    for (int i = 0; i < 3; ++i)
                        pos[i] += static_cast<fx>((static_cast<std::int64_t>(value) * axis[i]) >> 14);
                    continue;
                }
                std::int32_t turn[9], turned[9];
                detail::axisTurn(axis, value, turn);
                const std::int32_t *first = control.kind == kControlTurn ? turn : local;
                const std::int32_t *second = control.kind == kControlTurn ? local : turn;
                for (int row = 0; row < 3; ++row)
                {
                    for (int column = 0; column < 3; ++column)
                        turned[row * 3 + column] = detail::rotationDot(first + row * 3, second + column);
                }
                for (int i = 0; i < 9; ++i)
                    local[i] = turned[i];
            }
            const int parent = model.parents[b];
            BoneMatrix &m = out[b];
            if (parent < 0)
            {
                for (int i = 0; i < 9; ++i)
                    m.r[i] = local[i];
                for (int i = 0; i < 3; ++i)
                    m.t[i] = pos[i];
                continue;
            }
            const BoneMatrix &p = out[parent];
            // Most bones' place is still a key's, a 16-bit number << 6: then
            // (r * (key << 6)) >> 14 is (r * key) >> 8, three 16-bit products.
            // (Blended or slid, it may not be: then through 64 bits.)
            const std::int32_t key[3] = {pos[0] >> 6, pos[1] >> 6, pos[2] >> 6};
            const bool keyed = ((pos[0] | pos[1] | pos[2]) & 63) == 0 && key[0] == static_cast<std::int16_t>(key[0]) &&
                               key[1] == static_cast<std::int16_t>(key[1]) && key[2] == static_cast<std::int16_t>(key[2]);
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                    m.r[row * 3 + column] = detail::rotationDot(p.r + row * 3, local + column);
                m.t[row] = p.t[row] + (keyed ? dotShort(p.r + row * 3, 1, key, 1) >> 8
                                             : static_cast<fx>((static_cast<std::int64_t>(p.r[row * 3]) * pos[0] +
                                                                static_cast<std::int64_t>(p.r[row * 3 + 1]) * pos[1] +
                                                                static_cast<std::int64_t>(p.r[row * 3 + 2]) * pos[2]) >>
                                                               kBoneRotShift));
            }
        }
    }
#endif

    /// Where the model's attachment `which` is, in the model's space (Q12
    /// units), posed as `bones` have it. False if it has no such.
    inline bool attachmentPoint(const ModelView &model, const BoneMatrix *bones, int which, fx *at)
    {
        if (which < 0 || which >= model.header->attachmentCount)
            return false;
        const ModelAttachment &a = model.attachments[which];
        const BoneMatrix &bone = bones[a.bone];
        // (The point is 16-bit, << (kFxShift - kHitboxShift): the bone's
        // turn of it is three 16-bit products, shifted the rest of the way.)
        static_assert(kBoneRotShift > kFxShift - kHitboxShift);
        const std::int32_t local[3] = {a.at[0], a.at[1], a.at[2]};
        for (int row = 0; row < 3; ++row)
            at[row] = bone.t[row] + (dotShort(bone.r + row * 3, 1, local, 1) >> (kBoneRotShift - (kFxShift - kHitboxShift)));
        return true;
    }

    /// How far into a clip `ms` more milliseconds takes `frame` (Q8). A clip
    /// that loops goes round; one that does not stops on its last frame, and
    /// `finished` says so.
    inline std::uint32_t advanceClip(const ModelClip &clip, std::uint32_t frame, int ms, bool &finished)
    {
        finished = false;
        const std::uint32_t end = (clip.frameCount - 1u) << 8;
        // fps is Q8 frames a second, so frames * 256 a millisecond is fps / 1000.
        // (Up to 65535 ms the product fits 32 bits, where a divide by a
        // constant is a multiply; beyond, it takes the divider.)
        const std::uint32_t msU = static_cast<std::uint32_t>(ms);
        std::uint64_t next = frame + (msU <= 0xFFFFu ? (clip.fps * msU) / 1000u
                                                     : (static_cast<std::uint64_t>(clip.fps) * msU) / 1000);
        if (end == 0)
        {
            finished = !(clip.flags & kClipLoops);
            return 0;
        }
        if (next >= end)
        {
            if (clip.flags & kClipLoops)
                return static_cast<std::uint32_t>(next % end);
            finished = true;
            return end;
        }
        return static_cast<std::uint32_t>(next);
    }
} // namespace hldata
