#include "hlds/ModelCook.hpp"

#include "HlRemap.hpp"
#include "hlds/ModelMesh.hpp"
#include "hlds/ViewModelTrim.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <map>

#include "HlLevelReader.hpp"

namespace hlds
{
    using namespace hldata;

    void mdlAngleQuaternion(const float *angles, double *quat)
    {
        const double sr = std::sin(angles[0] * 0.5), cr = std::cos(angles[0] * 0.5);
        const double sp = std::sin(angles[1] * 0.5), cp = std::cos(angles[1] * 0.5);
        const double sy = std::sin(angles[2] * 0.5), cy = std::cos(angles[2] * 0.5);
        quat[0] = sr * cp * cy - cr * sp * sy;
        quat[1] = cr * sp * cy + sr * cp * sy;
        quat[2] = cr * cp * sy - sr * sp * cy;
        quat[3] = cr * cp * cy + sr * sp * sy;
    }

    namespace
    {
        void quatMatrix(const double *q, double *m) // 3 x 3, row by row
        {
            m[0] = 1 - 2 * q[1] * q[1] - 2 * q[2] * q[2];
            m[1] = 2 * q[0] * q[1] - 2 * q[3] * q[2];
            m[2] = 2 * q[0] * q[2] + 2 * q[3] * q[1];
            m[3] = 2 * q[0] * q[1] + 2 * q[3] * q[2];
            m[4] = 1 - 2 * q[0] * q[0] - 2 * q[2] * q[2];
            m[5] = 2 * q[1] * q[2] - 2 * q[3] * q[0];
            m[6] = 2 * q[0] * q[2] - 2 * q[3] * q[1];
            m[7] = 2 * q[1] * q[2] + 2 * q[3] * q[0];
            m[8] = 1 - 2 * q[0] * q[0] - 2 * q[1] * q[1];
        }
    } // namespace

    void mdlBoneMatrices(const MdlFile &mdl, const MdlPose *poses,
                         std::vector<std::array<double, 12>> &out)
    {
        out.resize(mdl.bones.size());
        for (std::size_t b = 0; b < mdl.bones.size(); ++b)
        {
            double q[4], r[9];
            mdlAngleQuaternion(poses[b].rot, q);
            quatMatrix(q, r);
            std::array<double, 12> local = {r[0], r[1], r[2], poses[b].pos[0], r[3], r[4],
                                            r[5], poses[b].pos[1], r[6], r[7], r[8], poses[b].pos[2]};
            const int parent = mdl.bones[b].parent;
            if (parent < 0)
            {
                out[b] = local;
                continue;
            }
            const std::array<double, 12> &p = out[parent];
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                    out[b][row * 4 + column] = p[row * 4] * local[column] +
                                               p[row * 4 + 1] * local[4 + column] +
                                               p[row * 4 + 2] * local[8 + column];
                out[b][row * 4 + 3] = p[row * 4] * local[3] + p[row * 4 + 1] * local[7] +
                                      p[row * 4 + 2] * local[11] + p[row * 4 + 3];
            }
        }
    }

    namespace
    {
        std::string lower(std::string text)
        {
            for (char &c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        // ------------------------------------------------------------ the skin

        struct AtlasRect
        {
            int x = 0, y = 0, w = 4, h = 4; ///< where the texture's pixels are, in the atlas
            bool flat = false;              ///< chrome: one colour, every corner at its middle
        };

        /// Shelf-pack rectangles (already padded) into `width` x `height`.
        bool shelfPack(const std::vector<std::pair<int, int>> &sizes, int width, int height,
                       std::vector<std::pair<int, int>> &at)
        {
            std::vector<int> order(sizes.size());
            for (std::size_t i = 0; i < order.size(); ++i)
                order[i] = static_cast<int>(i);
            std::stable_sort(order.begin(), order.end(),
                             [&](int a, int b) { return sizes[a].second > sizes[b].second; });
            at.assign(sizes.size(), {0, 0});
            int x = 0, y = 0, shelf = 0;
            for (int i : order)
            {
                if (sizes[i].first > width)
                    return false;
                if (x + sizes[i].first > width)
                {
                    x = 0;
                    y += shelf;
                    shelf = 0;
                }
                if (y + sizes[i].second > height)
                    return false;
                at[i] = {x, y};
                x += sizes[i].first;
                shelf = std::max(shelf, sizes[i].second);
            }
            return true;
        }

        /// All of a model's textures in one image. Each is scaled by the same
        /// factor -- the largest that lets them all fit the smallest atlas
        /// that does them justice -- and given a border of its own edge
        /// pixels, so that neither the 4 x 4 blocks of the compression nor a
        /// coordinate that lands a hair outside pick up a neighbour.
        void buildAtlas(const MdlFile &mdl, RgbaImage &atlas, std::vector<AtlasRect> &rects,
                        int mostTexels)
        {
            rects.assign(mdl.textures.size(), AtlasRect());
            if (mdl.textures.empty())
                return;
            constexpr int kBorder = 2;
            static const int kSizes[][2] = {{32, 32},   {64, 32},   {64, 64},  {128, 64},
                                            {128, 128}, {256, 128}, {256, 256}};
            static const double kScales[] = {1.0, 0.75, 0.625, 0.5, 0.375, 0.3125, 0.25, 0.1875, 0.125};

            int width = 0, height = 0;
            std::vector<std::pair<int, int>> at;
            std::vector<std::pair<int, int>> inner;
            bool placed = false;
            // A smaller atlas wins if it holds the textures at kAtlasMinScale
            // or better; the largest takes whatever scale fits.
            for (int pass = 0; pass < 2 && !placed; ++pass)
            {
                const int sizeCount = static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0]));
                for (int n = 0; n < sizeCount; ++n)
                {
                    // The first time round from the smallest up; the second,
                    // when nothing held them at half size, from the largest
                    // allowed down.
                    const int *size = kSizes[pass == 0 ? n : sizeCount - 1 - n];
                    // (The smallest is always allowed.)
                    if (size[0] * size[1] > mostTexels && size[0] * size[1] > 32 * 32)
                    {
                        if (pass == 0)
                            break;
                        continue;
                    }
                    for (double scale : kScales)
                    {
                        if (pass == 0 && scale < kAtlasMinScale)
                            break;
                        std::vector<std::pair<int, int>> padded;
                        inner.clear();
                        for (const MdlTexture &texture : mdl.textures)
                        {
                            const bool chrome = (texture.flags & kMdlTextureChrome) != 0;
                            int w = chrome ? 4 : std::max(4, static_cast<int>(std::lround(texture.width * scale)));
                            int h = chrome ? 4 : std::max(4, static_cast<int>(std::lround(texture.height * scale)));
                            inner.push_back({w, h});
                            // Whole 4 x 4 blocks, border included.
                            padded.push_back({(w + 2 * kBorder + 3) & ~3, (h + 2 * kBorder + 3) & ~3});
                        }
                        if (shelfPack(padded, size[0], size[1], at))
                        {
                            width = size[0];
                            height = size[1];
                            placed = true;
                            break;
                        }
                    }
                    if (placed)
                        break;
                }
            }
            if (!placed)
                return; // more textures than fit at an eighth: the model goes untextured

            atlas.width = width;
            atlas.height = height;
            atlas.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
            for (std::size_t t = 0; t < mdl.textures.size(); ++t)
            {
                const MdlTexture &texture = mdl.textures[t];
                RgbaImage source;
                source.width = texture.width;
                source.height = texture.height;
                source.rgba.resize(texture.pixels.size() * 4);
                std::uint64_t sum[3] = {0, 0, 0};
                for (std::size_t i = 0; i < texture.pixels.size(); ++i)
                {
                    for (int c = 0; c < 3; ++c)
                    {
                        source.rgba[i * 4 + c] = brightened(texture.palette[texture.pixels[i] * 3 + c]);
                        sum[c] += source.rgba[i * 4 + c];
                    }
                    source.rgba[i * 4 + 3] = 255;
                }
                AtlasRect &rect = rects[t];
                rect.w = inner[t].first;
                rect.h = inner[t].second;
                rect.x = at[t].first + kBorder;
                rect.y = at[t].second + kBorder;
                rect.flat = (texture.flags & kMdlTextureChrome) != 0;
                RgbaImage scaled = rect.flat ? RgbaImage() : resample(source, rect.w, rect.h);
                for (int y = -kBorder; y < rect.h + kBorder; ++y)
                {
                    for (int x = -kBorder; x < rect.w + kBorder; ++x)
                    {
                        std::uint8_t *to = atlas.at(rect.x + x, rect.y + y);
                        if (rect.flat)
                        {
                            // Chrome is a reflection the DS does not do: its
                            // average colour stands in.
                            for (int c = 0; c < 3; ++c)
                                to[c] = static_cast<std::uint8_t>(sum[c] / std::max<std::size_t>(1, texture.pixels.size()));
                        }
                        else
                        {
                            const std::uint8_t *from = scaled.at(std::clamp(x, 0, rect.w - 1),
                                                                 std::clamp(y, 0, rect.h - 1));
                            to[0] = from[0], to[1] = from[1], to[2] = from[2];
                        }
                        to[3] = 255;
                    }
                }
            }
        }

        /// The skin again as 256 colours (CookedModel::indexed), in the same
        /// places, where the model has a texture a player's colours go over.
        /// Texels are taken as they are, never blended: an index is a colour
        /// only of its own palette.
        void buildIndexedAtlas(const MdlFile &mdl, const RgbaImage &atlas, const std::vector<AtlasRect> &rects,
                               CookedModel::IndexedSkin &out)
        {
            out = CookedModel::IndexedSkin();
            int base = -1;
            for (std::size_t t = 0; t < mdl.textures.size(); ++t)
            {
                if (hldata::remapRunsOf(mdl.textures[t].name.c_str()).any)
                    base = static_cast<int>(t);
            }
            if (base < 0 || atlas.width == 0)
                return;
            const MdlTexture &remap = mdl.textures[static_cast<std::size_t>(base)];
            const hldata::RemapRuns runs = hldata::remapRunsOf(remap.name.c_str());
            out.width = atlas.width;
            out.height = atlas.height;
            out.texels.assign(static_cast<std::size_t>(atlas.width) * atlas.height, 0);
            for (int i = 0; i < 768; ++i)
                out.palette[static_cast<std::size_t>(i)] = brightened(remap.palette[static_cast<std::size_t>(i)]);
            // (Another texture's colour: the nearest of the remap texture's that
            // no player's colours go over.)
            auto nearest = [&](const std::uint8_t *rgb)
            {
                int best = 0;
                long bestOff = -1;
                for (int i = 0; i < 256; ++i)
                {
                    if ((i >= runs.topFirst && i <= runs.topLast) || (i >= runs.bottomFirst && i <= runs.bottomLast))
                        continue;
                    long off = 0;
                    for (int c = 0; c < 3; ++c)
                    {
                        const long d = static_cast<long>(rgb[c]) - out.palette[static_cast<std::size_t>(i * 3 + c)];
                        off += d * d;
                    }
                    if (bestOff < 0 || off < bestOff)
                    {
                        bestOff = off;
                        best = i;
                    }
                }
                return static_cast<std::uint8_t>(best);
            };
            constexpr int kBorder = 2;
            for (std::size_t t = 0; t < mdl.textures.size(); ++t)
            {
                const MdlTexture &texture = mdl.textures[t];
                const AtlasRect &rect = rects[t];
                if (rect.w <= 0 || rect.h <= 0 || texture.width <= 0 || texture.height <= 0)
                    continue;
                // (Its colours in the remap texture's: worked out once each.)
                std::array<std::uint8_t, 256> mapped{};
                for (int i = 0; i < 256; ++i)
                {
                    std::uint8_t rgb[3];
                    for (int c = 0; c < 3; ++c)
                        rgb[c] = brightened(texture.palette[static_cast<std::size_t>(i * 3 + c)]);
                    mapped[static_cast<std::size_t>(i)] = static_cast<int>(t) == base ? static_cast<std::uint8_t>(i) : nearest(rgb);
                }
                for (int y = -kBorder; y < rect.h + kBorder; ++y)
                {
                    for (int x = -kBorder; x < rect.w + kBorder; ++x)
                    {
                        const int ax = rect.x + x, ay = rect.y + y;
                        if (ax < 0 || ay < 0 || ax >= atlas.width || ay >= atlas.height)
                            continue;
                        const int sx = std::clamp(x, 0, rect.w - 1) * texture.width / rect.w;
                        const int sy = std::clamp(y, 0, rect.h - 1) * texture.height / rect.h;
                        const std::uint8_t index =
                            rect.flat ? mapped[0]
                                      : mapped[texture.pixels[static_cast<std::size_t>(sy) * texture.width + sx]];
                        out.texels[static_cast<std::size_t>(ay) * atlas.width + ax] = index;
                    }
                }
            }
        }

        // ------------------------------------------------------------ streams

        struct Command
        {
            std::uint8_t code;
            std::uint8_t count;
            std::uint32_t params[2];
        };

        void pack(const std::vector<Command> &commands, std::vector<std::uint32_t> &words)
        {
            for (std::size_t i = 0; i < commands.size(); i += 4)
            {
                std::uint32_t head = 0;
                for (std::size_t k = 0; k < 4 && i + k < commands.size(); ++k)
                    head |= static_cast<std::uint32_t>(commands[i + k].code) << (8 * k);
                words.push_back(head);
                for (std::size_t k = 0; k < 4 && i + k < commands.size(); ++k)
                {
                    for (int p = 0; p < commands[i + k].count; ++p)
                        words.push_back(commands[i + k].params[p]);
                }
            }
        }

        /// One sub-model's meshes as batches of at most kModelBatchBones bones.
        /// `family` is the skin the batches are made in; `tag` what they are
        /// marked for (ModelBatch::skin).
        void buildBatches(const MdlFile &mdl, const MdlSubModel &model, int bodyPart, int subModel,
                          const std::vector<AtlasRect> &rects, int vertexShift, CookedModel &out,
                          int family = 0, int tag = 0, int level = 0)
        {
            struct Open
            {
                std::vector<int> bones;
                std::vector<Command> commands;
                int polygons = 0;
                int vertices = 0;
            };
            Open batch;
            // (Which bone's matrix and which normal the hardware has: they
            // last from one strip to the next, so a strip that begins as the
            // last ended says neither again.)
            int currentBone = -1;
            std::uint32_t currentNormal = 0xFFFFFFFFu;
            auto flush = [&]()
            {
                currentBone = -1;
                currentNormal = 0xFFFFFFFFu;
                if (batch.commands.empty())
                    return;
                ModelBatch record{};
                record.gxOfs = static_cast<std::uint32_t>(out.gx.size());
                pack(batch.commands, out.gx);
                record.gxWords = static_cast<std::uint16_t>(out.gx.size() - record.gxOfs);
                record.polygons = static_cast<std::uint16_t>(batch.polygons);
                record.vertices = static_cast<std::uint16_t>(batch.vertices);
                record.bodyPart = static_cast<std::uint8_t>(bodyPart);
                record.subModel = static_cast<std::uint8_t>(subModel);
                record.skin = static_cast<std::uint8_t>(tag);
                record.level = static_cast<std::uint8_t>(level);
                record.boneCount = static_cast<std::uint8_t>(batch.bones.size());
                for (std::size_t i = 0; i < batch.bones.size(); ++i)
                    record.bones[i] = static_cast<std::uint8_t>(batch.bones[i]);
                out.batches.push_back(record);
                batch = Open();
            };

            const std::vector<int> &skin =
                mdl.skins.empty() ? std::vector<int>()
                                  : mdl.skins[family < static_cast<int>(mdl.skins.size()) ? family : 0];
            for (const MdlMesh &mesh : model.meshes)
            {
                const int textureIndex =
                    mesh.skinRef >= 0 && mesh.skinRef < static_cast<int>(skin.size()) ? skin[mesh.skinRef] : -1;
                const AtlasRect rect = textureIndex >= 0 ? rects[textureIndex] : AtlasRect();
                const MdlTexture *texture = textureIndex >= 0 ? &mdl.textures[textureIndex] : nullptr;

                for (const MdlStrip &strip : mesh.strips)
                {
                    if (strip.corners.size() < 3)
                        continue;
                    // The bones this strip needs that the batch does not have.
                    std::vector<int> fresh;
                    for (const MdlCorner &corner : strip.corners)
                    {
                        const int bone = model.vertexBone[corner.vertex];
                        if (std::find(batch.bones.begin(), batch.bones.end(), bone) == batch.bones.end() &&
                            std::find(fresh.begin(), fresh.end(), bone) == fresh.end())
                            fresh.push_back(bone);
                    }
                    if (batch.bones.size() + fresh.size() > static_cast<std::size_t>(kModelBatchBones))
                    {
                        flush();
                        fresh.clear();
                        for (const MdlCorner &corner : strip.corners)
                        {
                            const int bone = model.vertexBone[corner.vertex];
                            if (std::find(fresh.begin(), fresh.end(), bone) == fresh.end())
                                fresh.push_back(bone);
                        }
                        // A strip on more bones than a batch holds does not
                        // happen in the game's models; it would be left out.
                        if (fresh.size() > static_cast<std::size_t>(kModelBatchBones))
                            continue;
                    }
                    batch.bones.insert(batch.bones.end(), fresh.begin(), fresh.end());

                    auto emit = [&](const MdlCorner &corner)
                    {
                        const int bone = model.vertexBone[corner.vertex];
                        if (bone != currentBone)
                        {
                            currentBone = bone;
                            const auto slot = static_cast<std::uint32_t>(
                                std::find(batch.bones.begin(), batch.bones.end(), bone) -
                                batch.bones.begin() + kModelFirstSlot);
                            batch.commands.push_back(Command{kGxMtxRestore, 1, {slot, 0}});
                        }
                        const auto &n = model.normals[corner.normal];
                        const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                        std::uint32_t normal = 0;
                        for (int a = 0; a < 3; ++a)
                        {
                            const int v = static_cast<int>(
                                std::lround(std::clamp(length > 0 ? n[a] / length : 0.0, -1.0, 1.0) * 511));
                            normal |= static_cast<std::uint32_t>(v & 0x3FF) << (10 * a);
                        }
                        if (normal != currentNormal)
                        {
                            currentNormal = normal;
                            batch.commands.push_back(Command{kGxNormal, 1, {normal, 0}});
                        }
                        if (texture != nullptr)
                        {
                            double s = rect.x + rect.w * 0.5, t = rect.y + rect.h * 0.5;
                            if (!rect.flat)
                            {
                                s = rect.x + (corner.s + 0.5) * rect.w / texture->width;
                                t = rect.y + (corner.t + 0.5) * rect.h / texture->height;
                            }
                            const auto s16 = static_cast<std::int16_t>(std::lround(s * 16));
                            const auto t16 = static_cast<std::int16_t>(std::lround(t * 16));
                            batch.commands.push_back(
                                Command{kGxTexCoord, 1,
                                        {static_cast<std::uint16_t>(s16) |
                                             (static_cast<std::uint32_t>(static_cast<std::uint16_t>(t16)) << 16),
                                         0}});
                        }
                        const auto &v = model.vertices[corner.vertex];
                        std::uint16_t xyz[3];
                        for (int a = 0; a < 3; ++a)
                        {
                            const double raw = std::round(v[a] * (1 << kModelVertexShift) / (1 << vertexShift));
                            xyz[a] = static_cast<std::uint16_t>(
                                static_cast<std::int16_t>(std::clamp(raw, -32768.0, 32767.0)));
                        }
                        batch.commands.push_back(
                            Command{kGxVtx16, 2, {xyz[0] | (static_cast<std::uint32_t>(xyz[1]) << 16), xyz[2]}});
                        ++batch.vertices;
                    };

                    const int n = static_cast<int>(strip.corners.size());
                    if (strip.fan)
                    {
                        // The hardware has no fans: each triangle on its own.
                        batch.commands.push_back(Command{kGxBegin, 1, {0, 0}});
                        for (int i = 1; i + 1 < n; ++i)
                        {
                            emit(strip.corners[0]);
                            emit(strip.corners[i]);
                            emit(strip.corners[i + 1]);
                        }
                    }
                    else
                    {
                        batch.commands.push_back(Command{kGxBegin, 1, {2, 0}});
                        for (const MdlCorner &corner : strip.corners)
                            emit(corner);
                    }
                    batch.polygons += n - 2;
                }
            }
            flush();
        }

        // ------------------------------------------------------------ animation

        struct BoneFrame
        {
            double quat[4];
            double pos[3];
        };

        /// Reconstruct frame `f` from keys at every `step` frames (and the
        /// last), the way the DS will: a straight blend, made unit again.
        BoneFrame blendKeys(const std::vector<BoneFrame> &frames, int step, int f)
        {
            const int last = static_cast<int>(frames.size()) - 1;
            const int keyCount = (last + step - 1) / step + 1;
            int index = std::min(f / step, keyCount - 2);
            const int from = index * step;
            const int to = index + 2 == keyCount ? last : from + step;
            const double s = to > from ? double(f - from) / (to - from) : 0;
            const BoneFrame &a = frames[from];
            const BoneFrame &b = frames[to];
            BoneFrame out;
            double dot = 0;
            for (int i = 0; i < 4; ++i)
                dot += a.quat[i] * b.quat[i];
            const double sign = dot < 0 ? -1 : 1;
            double length = 0;
            for (int i = 0; i < 4; ++i)
            {
                out.quat[i] = a.quat[i] + (sign * b.quat[i] - a.quat[i]) * s;
                length += out.quat[i] * out.quat[i];
            }
            length = std::sqrt(length);
            for (int i = 0; i < 4; ++i)
                out.quat[i] /= length > 0 ? length : 1;
            for (int i = 0; i < 3; ++i)
                out.pos[i] = a.pos[i] + (b.pos[i] - a.pos[i]) * s;
            return out;
        }

        ModelKey rotKey(const BoneFrame &frame)
        {
            // q and -q are the same turn: stored as the one whose w is not
            // negative, so that w can be left out.
            const double sign = frame.quat[3] < 0 ? -1 : 1;
            ModelKey key{};
            for (int i = 0; i < 3; ++i)
                key.v[i] = static_cast<std::int16_t>(
                    std::clamp(std::lround(sign * frame.quat[i] * (1 << kModelQuatShift)), -16384L, 16384L));
            return key;
        }

        ModelKey posKey(const BoneFrame &frame)
        {
            ModelKey key{};
            for (int i = 0; i < 3; ++i)
                key.v[i] = static_cast<std::int16_t>(
                    std::clamp(std::lround(frame.pos[i] * (1 << kModelPosShift)), -32768L, 32767L));
            return key;
        }
    } // namespace

    std::string eventSoundName(std::string path)
    {
        for (char &c : path)
            c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        while (!path.empty() && (path[0] == '*' || path[0] == '/' || path[0] == '#' || path[0] == '@'))
            path.erase(0, 1);
        return path.size() > 4 && path.substr(path.size() - 4) == ".wav" ? path : std::string();
    }

    bool bakeHeldModel(MdlFile &mdl, std::string &error)
    {
        int hand = -1, spine = -1;
        for (std::size_t b = 0; b < mdl.bones.size(); ++b)
        {
            if (lower(mdl.bones[b].name) == "bip01 r hand")
                hand = static_cast<int>(b);
            if (lower(mdl.bones[b].name) == "bip01 spine")
                spine = static_cast<int>(b);
        }
        const bool onSpine = hand < 0;
        if (onSpine)
            hand = spine;
        if (hand < 0 || mdl.sequences.empty())
        {
            error = mdl.name + " has no right hand to be held in";
            return false;
        }
        std::vector<MdlPose> poses;
        if (!mdl.decode(0, poses, error))
            return false;
        std::vector<std::array<double, 12>> matrices(mdl.bones.size());
        mdlBoneMatrices(mdl, poses.data(), matrices);
        const std::array<double, 12> &h = matrices[static_cast<std::size_t>(hand)];
        auto place = [&](const std::array<double, 12> &m, const std::array<float, 3> &v, bool moves)
        {
            double world[3];
            for (int a = 0; a < 3; ++a)
                world[a] = m[a * 4] * v[0] + m[a * 4 + 1] * v[1] + m[a * 4 + 2] * v[2] + (moves ? m[a * 4 + 3] - h[a * 4 + 3] : 0.0);
            std::array<float, 3> out;
            for (int a = 0; a < 3; ++a)
                out[a] = static_cast<float>(h[a] * world[0] + h[4 + a] * world[1] + h[8 + a] * world[2]);
            return out;
        };
        for (MdlBodyPart &part : mdl.bodyParts)
        {
            for (MdlSubModel &model : part.models)
            {
                for (std::size_t v = 0; v < model.vertices.size(); ++v)
                {
                    model.vertices[v] = place(matrices[model.vertexBone[v]], model.vertices[v], true);
                    model.vertexBone[v] = 0;
                }
                for (std::size_t n = 0; n < model.normals.size(); ++n)
                {
                    model.normals[n] = place(matrices[model.normalBone[n]], model.normals[n], false);
                    model.normalBone[n] = 0;
                }
            }
        }
        MdlBone one;
        one.name = onSpine ? "Bip01 Spine" : "Bip01 R Hand";
        mdl.bones.assign(1, one);
        mdl.sequences.clear();
        mdl.controllers.clear();
        mdl.hitboxes.clear();
        mdl.attachments.clear();
        return true;
    }

    bool cookModel(const MdlFile &source, const std::vector<std::string> &clipNames, CookedModel &out,
                   ModelCookReport &report, std::string &error, int skinTexels, bool full,
                   const ModelLean &lean)
    {
        out = CookedModel();
        report = ModelCookReport();
        // A first-person model without what the screen never shows of it
        // (ViewModelTrim.hpp); every other as it is.
        bool viewModel = isViewModel(source.name);
        MdlFile trimmed;
        std::vector<std::vector<std::array<double, 12>>> viewPosesOf;
        const ViewCone view = widestView();
        int untrimmedTriangles = 0;
        if (viewModel)
        {
            trimmed = source;
            if (!viewPoses(trimmed, viewPosesOf, error))
                return false;
            int kept = 0;
            untrimmedTriangles = trimViewModel(trimmed, viewPosesOf, view, kept);
            // (One never on the screen at all is not held as the game's
            // weapons are: kept whole.)
            viewModel = kept > 0;
        }
        const MdlFile &mdl = viewModel ? trimmed : source;
        if (mdl.bones.empty() || mdl.bones.size() > static_cast<std::size_t>(kModelMaxBones))
        {
            error = mdl.name + " has more bones than a cooked model holds";
            return false;
        }
        for (const MdlBone &bone : mdl.bones)
            out.parents.push_back(static_cast<std::int16_t>(bone.parent));

        // ---- how coarse the vertices must be to fit sixteen bits
        double furthest = 0;
        for (const MdlBodyPart &part : mdl.bodyParts)
        {
            for (const MdlSubModel &model : part.models)
            {
                for (const auto &v : model.vertices)
                    furthest = std::max({furthest, std::fabs(double(v[0])), std::fabs(double(v[1])),
                                         std::fabs(double(v[2]))});
            }
        }
        int vertexShift = 0;
        while (furthest * (1 << kModelVertexShift) / (1 << vertexShift) > 32000 && vertexShift < 6)
            ++vertexShift;

        // ---- the skin
        std::vector<AtlasRect> rects;
        buildAtlas(mdl, out.atlas, rects, skinTexels);
        buildIndexedAtlas(mdl, out.atlas, rects, out.indexed);
        const bool textured = out.atlas.width != 0;
        if (!textured)
            rects.assign(mdl.textures.size(), AtlasRect());

        // ---- how it stands: every bone's matrix in its first sequence
        std::vector<std::array<double, 12>> standing(mdl.bones.size(),
                                                     {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
        if (!mdl.sequences.empty())
        {
            std::vector<MdlPose> poses;
            if (!mdl.decode(0, poses, error))
                return false;
            mdlBoneMatrices(mdl, poses.data(), standing);
        }

        // ---- how many triangles it may keep, at each level of detail
        //
        // The first level is what a model has always been thinned to; the
        // next two are a half and a quarter of that (a model already small
        // has the one). With `full`, the model as the game has it is a
        // level above them all -- for a machine with the memory for it.
        int sourceTriangles = 0;
        for (const MdlSubModel *model : mdl.selectBody(0))
            sourceTriangles += model->triangleCount();
        report.sourceTriangles = sourceTriangles;
        // (A trimmed first-person model is thinned as it was before: what is
        // left of it looks as it did -- the same share of it kept.)
        const int thinnedFrom = viewModel ? untrimmedTriangles : sourceTriangles;
        const int allowedOf = thinnedFrom > kModelTriangleTarget ? std::max(kModelTriangleTarget, thinnedFrom / 4)
                                                                 : thinnedFrom;
        const int allowed = viewModel && thinnedFrom > 0
                                ? static_cast<int>((static_cast<long>(allowedOf) * sourceTriangles + thinnedFrom - 1) /
                                                   thinnedFrom)
                                : allowedOf;
        report.untrimmedTriangles = viewModel ? untrimmedTriangles : sourceTriangles;
        std::vector<int> levelTargets = {allowed};
        if (sourceTriangles > kModelLevelLeast * 2 && !lean.oneLevel)
        {
            levelTargets.push_back(std::max(kModelLevelLeast, allowed / 2));
            levelTargets.push_back(std::max(kModelLevelLeast, allowed * 35 / 100));
        }
        // (A model always drawn: one level coarser still, below the rest.)
        if (lean.coarse && !lean.oneLevel)
        {
            const int coarse = std::max(kCoarseLeast, sourceTriangles * kCoarseShare / 100);
            if (coarse < levelTargets.back())
                levelTargets.push_back(coarse);
        }
        const bool withFull = full && allowed < sourceTriangles;

        // Every sub-model at every level, each in the same proportion.
        struct Made
        {
            std::vector<MeshLevel> levels;
            MdlSubModel whole; ///< as the game has it, in the best strips
        };
        std::vector<std::vector<Made>> made(mdl.bodyParts.size());
        std::vector<int> total(levelTargets.size(), 0), sent(levelTargets.size(), 0);
        std::vector<double> off(levelTargets.size(), 0);
        int wholeSent = 0;
        for (std::size_t p = 0; p < mdl.bodyParts.size(); ++p)
        {
            const MdlBodyPart &part = mdl.bodyParts[p];
            made[p].resize(part.models.size());
            for (std::size_t m = 0; m < part.models.size(); ++m)
            {
                const MdlSubModel &source = part.models[m];
                std::vector<std::array<double, 3>> rest;
                const MeshInput input = meshInput(mdl, source, standing, rest);
                const int had = source.triangleCount();
                std::vector<int> targets;
                for (int level : levelTargets)
                    targets.push_back(sourceTriangles > 0
                                          ? static_cast<int>(static_cast<long>(had) * level / sourceTriangles)
                                          : had);
                made[p][m].levels = meshLevels(input, targets);
                if (withFull)
                    made[p][m].whole = restripped(source);
                const bool ofBodyZero = m == static_cast<std::size_t>((0 / part.base) % part.models.size());
                for (std::size_t l = 0; l < levelTargets.size(); ++l)
                {
                    off[l] = std::max(off[l], made[p][m].levels[l].error);
                    if (ofBodyZero)
                    {
                        total[l] += made[p][m].levels[l].triangles;
                        sent[l] += sentVertices(made[p][m].levels[l].model);
                    }
                }
                if (ofBodyZero && withFull)
                    wholeSent += sentVertices(made[p][m].whole);
            }
        }
        // A level that would save less than a seventh over the one before
        // it is not worth its memory.
        std::vector<int> kept = {0};
        for (std::size_t l = 1; l < levelTargets.size(); ++l)
        {
            if (total[l] * 20 <= total[kept.back()] * 17)
                kept.push_back(static_cast<int>(l));
        }
        std::vector<double> levelError;
        if (withFull)
        {
            levelError.push_back(0);
            report.levels.push_back({sourceTriangles, wholeSent, 0.0});
        }
        for (int l : kept)
        {
            levelError.push_back(off[l]);
            report.levels.push_back({total[l], sent[l], off[l]});
        }
        report.triangles = report.levels.front().triangles;

        // ---- the meshes
        int worstPolygons = 0;
        // (Each level's meshes, by part, for a first-person model's counts
        // of what is on the screen: below, with the clips.)
        std::vector<std::vector<std::vector<const MdlSubModel *>>> meshesAt(
            levelError.size(), std::vector<std::vector<const MdlSubModel *>>(mdl.bodyParts.size()));
        for (std::size_t p = 0; p < mdl.bodyParts.size(); ++p)
        {
            const MdlBodyPart &part = mdl.bodyParts[p];
            if (part.models.empty())
                continue;
            ModelBodyPart record{};
            record.base = static_cast<std::uint16_t>(std::clamp(part.base, 1, 65535));
            record.count = static_cast<std::uint16_t>(part.models.size());
            const int index = static_cast<int>(out.bodyParts.size());
            out.bodyParts.push_back(record);
            int most = 0;
            for (std::size_t m = 0; m < part.models.size(); ++m)
            {
                for (std::size_t level = 0; level < levelError.size(); ++level)
                {
                    const std::size_t before = out.batches.size();
                    const MdlSubModel &cut = withFull && level == 0
                                                 ? made[p][m].whole
                                                 : made[p][m].levels[kept[level - (withFull ? 1 : 0)]].model;
                    // A part that some other skin of the model puts a
                    // different picture on is made once for each skin (up
                    // to four); the rest once, for all of them.
                    const int families = std::min<int>(4, static_cast<int>(mdl.skins.size()));
                    bool differs = false;
                    for (int f = 1; f < families; ++f)
                    {
                        for (const MdlMesh &mesh : cut.meshes)
                        {
                            const std::size_t ref = static_cast<std::size_t>(mesh.skinRef);
                            differs = differs || (mesh.skinRef >= 0 && ref < mdl.skins[0].size() &&
                                                  ref < mdl.skins[f].size() && mdl.skins[f][ref] != mdl.skins[0][ref]);
                        }
                    }
                    const int tag = static_cast<int>(level);
                    buildBatches(mdl, cut, index, static_cast<int>(m), rects, vertexShift, out, 0, differs ? 1 : 0, tag);
                    const std::size_t usual = out.batches.size();
                    for (int f = 1; differs && f < families; ++f)
                        buildBatches(mdl, cut, index, static_cast<int>(m), rects, vertexShift, out, f, f + 1, tag);
                    int polygons = 0;
                    for (std::size_t b = before; b < usual; ++b)
                        polygons += out.batches[b].polygons;
                    if (level == 0)
                        most = std::max(most, polygons);
                    meshesAt[level][p].push_back(&cut);
                }
            }
            worstPolygons += most;
        }
        for (int l = 0; l < kModelLevels; ++l)
        {
            out.levelError[l] = static_cast<std::size_t>(l) < levelError.size()
                                    ? static_cast<std::uint8_t>(std::clamp(std::lround(levelError[l] * 16), 0L, 254L))
                                    : kModelNoLevel;
        }
        report.batches = static_cast<int>(out.batches.size());

        // ---- its size, from how it stands in its first sequence
        double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9}, radius = 1;
        {
            const std::vector<std::array<double, 12>> &matrices = standing;
            for (const MdlBodyPart &part : mdl.bodyParts)
            {
                for (const MdlSubModel &model : part.models)
                {
                    for (std::size_t v = 0; v < model.vertices.size(); ++v)
                    {
                        const auto &m = matrices[model.vertexBone[v]];
                        const auto &p = model.vertices[v];
                        double w[3];
                        for (int a = 0; a < 3; ++a)
                        {
                            w[a] = m[a * 4] * p[0] + m[a * 4 + 1] * p[1] + m[a * 4 + 2] * p[2] + m[a * 4 + 3];
                            lo[a] = std::min(lo[a], w[a]);
                            hi[a] = std::max(hi[a], w[a]);
                        }
                        radius = std::max(radius, std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]));
                    }
                }
            }
        }
        if (lo[0] > hi[0])
            lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0;

        // ---- the clips
        // (By its label; or "@n", the model's n-th sequence, for one whose
        // label another has too -- both of a roach's are "run". And
        // "label>base": the sequence `label` as the game's code sees it --
        // its name, what it is for, how often it is chosen -- played with
        // the frames of the clip `base`, which is cooked too: a DS without
        // room for a turn's own frames still turns as the game does,
        // looking as it stands meanwhile.)
        auto sequenceOf = [&](const std::string &label) {
            if (label.size() > 1 && label[0] == '@')
            {
                const int at = std::atoi(label.c_str() + 1);
                return at >= 0 && at < static_cast<int>(mdl.sequences.size()) ? at : -1;
            }
            return label.empty() ? -1 : mdl.sequenceByName(label);
        };
        std::vector<std::pair<std::string, std::string>> aliases;
        std::vector<int> wanted;
        for (const std::string &name : clipNames)
        {
            const std::string::size_type to = name.find('>');
            if (to != std::string::npos)
            {
                aliases.emplace_back(lower(name.substr(0, to)), lower(name.substr(to + 1)));
                continue;
            }
            const std::string label = lower(name);
            int index = -1;
            if (label.size() > 1 && label[0] == '@')
            {
                index = std::atoi(label.c_str() + 1);
                if (index < 0 || index >= static_cast<int>(mdl.sequences.size()))
                    index = -1;
            }
            else if (!label.empty())
            {
                index = mdl.sequenceByName(label);
            }
            if (index < 0)
            {
                if (!label.empty())
                    report.missingClips.push_back(label);
                continue;
            }
            if (std::find(wanted.begin(), wanted.end(), index) == wanted.end())
                wanted.push_back(index);
        }
        std::vector<std::vector<double>> clipFirstZ;
        std::vector<int> clipSequence; ///< each clip's sequence, whose frames it plays
        for (const int index : wanted)
        {
            const MdlSequence &seq = mdl.sequences[index];
            const std::string label = lower(seq.label);
            std::vector<MdlPose> poses;
            if (!mdl.decode(index, poses, error))
                return false;
            const int boneCount = static_cast<int>(mdl.bones.size());
            // (How the clip holds each bone about z to begin with: for the
            // bone controllers, below.)
            clipFirstZ.emplace_back();
            for (int b = 0; b < boneCount; ++b)
                clipFirstZ.back().push_back(poses[b].rot[2]);
            // (A pose to hold a gun in: its one frame is enough, and what it
            // saves of its thirty-odd is what makes it affordable.)
            const int frameCount = label.rfind("ref_aim_", 0) == 0 ? 1 : seq.frameCount;

            ModelClip clip{};
            clip.name = nameHash(label.c_str());
            clip.frameCount = static_cast<std::uint16_t>(frameCount);
            clip.fps = static_cast<std::uint16_t>(
                std::clamp(std::lround(std::max(0.01f, seq.fps) * 256), 1L, 65535L));
            clip.flags = (seq.flags & kMdlSequenceLooping) ? kClipLoops : 0;
            // What it is for, and how often it is the one chosen for that
            // (HlAi.hpp, LookupActivity): bits 8.. and 2..7 of its flags.
            clip.flags = static_cast<std::uint16_t>(clip.flags | (std::clamp(seq.activity, 0, 255) << 8) |
                                                    (std::clamp(seq.actWeight, 0, 63) << 2));
            clip.firstTrack = static_cast<std::uint32_t>(out.tracks.size());
            for (int a = 0; a < 3; ++a)
                clip.travel[a] = fxFromDouble(seq.linearMovement[a]);
            clip.firstEvent = static_cast<std::uint16_t>(out.events.size());
            for (const MdlEvent &event : seq.events)
            {
                // 1000: the one playing it is dead from here on (a script's
                // fall, a dying guard): said of the clip. 1003: fire the
                // target named in the options. 1004 and 5004: a sound, 1008:
                // a sound from the mouth -- all three name a file. The rest
                // are for the monster's own code.
                if (event.event == 1000)
                {
                    clip.flags |= kClipDies;
                    continue;
                }
                ModelEvent record{};
                record.frame = static_cast<std::uint16_t>(std::clamp(event.frame, 0, frameCount - 1));
                // Below 1000: the monster's own (a claw, a zap), by number.
                if (event.event > 0 && event.event < 1000)
                {
                    record.kind = kEventMonster;
                    record.target = static_cast<std::uint32_t>(event.event);
                    out.events.push_back(record);
                    continue;
                }
                // 1001, 1002: the scene may not, or may again, be broken
                // off.
                if (event.event == 1001 || event.event == 1002)
                {
                    record.kind = kEventInterrupt;
                    record.target = event.event == 1002 ? 1 : 0;
                    out.events.push_back(record);
                    continue;
                }
                // 2001, 2002: the body hits the floor, light or heavy.
                if (event.event == 2001 || event.event == 2002)
                {
                    record.kind = kEventBodyDrop;
                    record.target = event.event == 2002 ? 1 : 0;
                    out.events.push_back(record);
                    continue;
                }
                if (event.options.empty())
                    continue;
                if (event.event == 1003)
                {
                    record.kind = kEventFire;
                    record.target = nameHash(lower(event.options).c_str());
                }
                else if (event.event == 1004 || event.event == 5004 || event.event == 1008)
                {
                    const std::string sound = eventSoundName(event.options);
                    if (sound.empty())
                        continue;
                    record.kind = event.event == 1008 ? kEventVoice : kEventSound;
                    record.target = nameHash(sound.c_str());
                    if (std::find(out.eventSounds.begin(), out.eventSounds.end(), sound) == out.eventSounds.end())
                        out.eventSounds.push_back(sound);
                }
                else
                {
                    continue;
                }
                out.events.push_back(record);
            }
            clip.eventCount = static_cast<std::uint16_t>(out.events.size() - clip.firstEvent);

            // Each bone's track of the clip: of its pose, or (an aim that
            // blends by its pitch) of the pose at each end of the aim, the
            // second's tracks after the first's.
            auto cookTracks = [&](const std::vector<MdlPose> &blendPoses, bool first)
            {
                for (int b = 0; b < boneCount; ++b)
                {
                    std::vector<BoneFrame> frames(frameCount);
                    for (int f = 0; f < frameCount; ++f)
                    {
                        const MdlPose &pose = blendPoses[static_cast<std::size_t>(f) * boneCount + b];
                        mdlAngleQuaternion(pose.rot, frames[f].quat);
                        for (int a = 0; a < 3; ++a)
                            frames[f].pos[a] = pose.pos[a];
                        // The sequence's own travel is the entity's to make: the
                        // bone that carries it stays put on those axes.
                        if (b == seq.motionBone)
                        {
                            for (int a = 0; a < 3; ++a)
                            {
                                if (seq.motionType & (1 << a))
                                    frames[f].pos[a] = 0;
                            }
                        }
                        // One turn, two quaternions: keep to the one nearer the
                        // frame before, so that blends go the short way.
                        if (f > 0)
                        {
                            double dot = 0;
                            for (int i = 0; i < 4; ++i)
                                dot += frames[f].quat[i] * frames[f - 1].quat[i];
                            if (dot < 0)
                            {
                                for (int i = 0; i < 4; ++i)
                                    frames[f].quat[i] = -frames[f].quat[i];
                            }
                        }
                    }
                    if (first && b == 0 && frameCount > 0)
                    {
                        // (Where the root is at the end, as the game finds it:
                        // the bone's own place, not how far it went.)
                        for (int a = 0; a < 2; ++a)
                            clip.endOffset[a] = fxFromDouble(frames[frameCount - 1].pos[a]);
                        clip.endUp = static_cast<std::int16_t>(
                            std::clamp(std::lround(frames[frameCount - 1].pos[2]), -32768L, 32767L));
                    }

                    // Turning and place are keyed apart, each at the largest
                    // step that gives every frame back -- to within three quarters
                    // of a degree, and
                    // a quarter of a unit -- or as one key if it never changes.
                    auto turnOff = [&](const BoneFrame &got, int f)
                    {
                        double dot = 0;
                        for (int i = 0; i < 4; ++i)
                            dot += got.quat[i] * frames[f].quat[i];
                        return 2 * std::acos(std::min(1.0, std::fabs(dot))) * 180 / M_PI;
                    };
                    auto placeOff = [&](const BoneFrame &got, int f)
                    {
                        double apart = 0;
                        for (int i = 0; i < 3; ++i)
                            apart = std::max(apart, std::fabs(got.pos[i] - frames[f].pos[i]));
                        return apart;
                    };
                    auto chooseStep = [&](bool rotation)
                    {
                        bool still = true;
                        for (int f = 1; f < frameCount && still; ++f)
                            still = rotation ? turnOff(frames[0], f) <= 0.5 : placeOff(frames[0], f) <= 0.1;
                        if (still || frameCount == 1)
                            return 0; // one key
                        static const int kSteps[] = {16, 8, 6, 4, 3, 2};
                        for (int candidate : kSteps)
                        {
                            if (candidate >= frameCount)
                                continue;
                            bool good = true;
                            for (int f = 0; f < frameCount && good; ++f)
                            {
                                const BoneFrame got = blendKeys(frames, candidate, f);
                                good = rotation ? turnOff(got, f) <= lean.turnOff : placeOff(got, f) <= lean.placeOff;
                            }
                            if (good)
                                return candidate;
                        }
                        return 1;
                    };
                    ModelTrack track{};
                    const int last = frameCount - 1;
                    const int rotStep = chooseStep(true);
                    track.firstRot = static_cast<std::uint32_t>(out.keys.size());
                    track.rotStep = static_cast<std::uint16_t>(std::max(1, rotStep));
                    if (rotStep == 0)
                    {
                        out.keys.push_back(rotKey(frames[0]));
                    }
                    else
                    {
                        for (int f = 0; f < last; f += rotStep)
                            out.keys.push_back(rotKey(frames[f]));
                        out.keys.push_back(rotKey(frames[last]));
                    }
                    track.rotCount = static_cast<std::uint16_t>(out.keys.size() - track.firstRot);
                    const int posStep = chooseStep(false);
                    track.firstPos = static_cast<std::uint32_t>(out.keys.size());
                    track.posStep = static_cast<std::uint16_t>(std::max(1, posStep));
                    if (posStep == 0)
                    {
                        out.keys.push_back(posKey(frames[0]));
                    }
                    else
                    {
                        for (int f = 0; f < last; f += posStep)
                            out.keys.push_back(posKey(frames[f]));
                        out.keys.push_back(posKey(frames[last]));
                    }
                    track.posCount = static_cast<std::uint16_t>(out.keys.size() - track.firstPos);
                    out.tracks.push_back(track);
                }
            };
            cookTracks(poses, true);
            clip.blends = 1;
            if (seq.blendCount >= 2 && seq.blendType == kMdlMotionXR)
            {
                std::vector<MdlPose> other;
                if (!mdl.decode(index, other, error, 1))
                    return false;
                cookTracks(other, false);
                clip.blends = 2;
                clip.blendLow = static_cast<std::int8_t>(std::clamp(std::lround(seq.blendStart), -128L, 127L));
                clip.blendHigh = static_cast<std::int8_t>(std::clamp(std::lround(seq.blendEnd), -128L, 127L));
            }
            report.sourceFrames += frameCount * boneCount;
            out.clips.push_back(clip);
            clipSequence.push_back(index);
        }
        for (const auto &[label, base] : aliases)
        {
            const int index = sequenceOf(label), from = sequenceOf(base);
            int baseClip = -1;
            for (std::size_t c = 0; from >= 0 && c < out.clips.size(); ++c)
            {
                if (out.clips[c].name == nameHash(lower(mdl.sequences[from].label).c_str()))
                    baseClip = static_cast<int>(c);
            }
            if (index < 0 || baseClip < 0)
            {
                report.missingClips.push_back(label + ">" + base);
                continue;
            }
            const MdlSequence &seq = mdl.sequences[index];
            ModelClip clip = out.clips[baseClip];
            clip.name = nameHash(lower(seq.label).c_str());
            clip.flags = static_cast<std::uint16_t>((clip.flags & kClipLoops) | (std::clamp(seq.activity, 0, 255) << 8) |
                                                    (std::clamp(seq.actWeight, 0, 63) << 2));
            // (None of the base's events: they are what the base does.)
            clip.firstEvent = static_cast<std::uint16_t>(out.events.size());
            clip.eventCount = 0;
            clipFirstZ.push_back(clipFirstZ[baseClip]);
            out.clips.push_back(clip);
            clipSequence.push_back(from); // (its frames are the base's)
        }
        report.clips = static_cast<int>(out.clips.size());

        // ---- a first-person model's clips: the most of it each can show at
        // once, at any level (each part's worst choice, added up), at the
        // usual view and the widest (kModelScreenCounts)
        if (viewModel)
        {
            const ViewCone views[2] = {narrowView(), widestView()};
            auto countOf = [&](const std::vector<std::vector<std::array<double, 12>>> &poses)
            {
                std::uint32_t counts = 0;
                for (int w = 0; w < 2; ++w)
                {
                    int most = 0;
                    for (const auto &parts : meshesAt)
                    {
                        int sum = 0;
                        for (const auto &models : parts)
                        {
                            int partMost = 0;
                            for (const MdlSubModel *model : models)
                                partMost = std::max(partMost, screenMost(*model, poses, views[w]));
                            sum += partMost;
                        }
                        most = std::max(most, sum);
                    }
                    // (Never nothing: 0 says "not said".)
                    counts |= static_cast<std::uint32_t>(std::clamp(most, 1, 65535)) << (16 * w);
                }
                return counts;
            };
            std::vector<std::vector<std::array<double, 12>>> clipPoses, lastPoses;
            for (std::size_t c = 0; c < clipSequence.size(); ++c)
            {
                if (!sequencePoses(mdl, clipSequence[c], clipPoses, error))
                    return false;
                // (Its last frame, of each blend: as many frames each as the
                // clip has, or as the sequence has -- the poses are the
                // sequence's.)
                const std::size_t frames = static_cast<std::size_t>(
                    mdl.sequences[static_cast<std::size_t>(clipSequence[c])].frameCount);
                const std::size_t shown = std::max<std::size_t>(1, out.clips[c].frameCount);
                lastPoses.clear();
                for (std::size_t at = 0; frames != 0 && at + frames <= clipPoses.size(); at += frames)
                    lastPoses.push_back(clipPoses[at + std::min(shown, frames) - 1]);
                out.screenCounts.push_back(countOf(clipPoses));
                out.screenCounts.push_back(lastPoses.empty() ? out.screenCounts.back() : countOf(lastPoses));
            }
        }
        report.keys = static_cast<int>(out.keys.size());

        // ---- what the game sets by hand, and what a shot is tried against
        std::vector<int> controlTypes;
        for (const MdlController &c : mdl.controllers)
        {
            // (The game's five: four set by number, and the mouth, set by
            // how loud what the model says is: kControlMouth.)
            if (c.index < 0 || c.index >= kModelControls)
                continue;
            ModelController control{};
            control.bone = static_cast<std::uint8_t>(c.bone);
            control.index = static_cast<std::uint8_t>(c.index);
            const int type = c.type & 0x7FFF;
            if (type == kMdlMotionX || type == kMdlMotionY || type == kMdlMotionZ)
                control.kind = kControlSlide;
            else if (type == kMdlMotionYR || type == kMdlMotionZR)
                control.kind = kControlTurn;
            else if (type == kMdlMotionXR)
                control.kind = kControlTwist;
            else
                continue;
            if (control.kind != kControlSlide)
            {
                control.flags = (c.type & kMdlMotionLoops) ? kControlLoops : 0;
                control.start = static_cast<std::int32_t>(std::lround(c.start * 65536.0 / 360.0));
                control.end = static_cast<std::int32_t>(std::lround(c.end * 65536.0 / 360.0));
            }
            else
            {
                control.start = fxFromDouble(c.start);
                control.end = fxFromDouble(c.end);
            }
            out.controllers.push_back(control);
            controlTypes.push_back(type);
        }
        // The axis of each, for each clip. A bone is turned about its x,
        // then y, then z. More about z is a turn about the parent's z, made
        // last; more about x is a turn about the bone's own x, made first;
        // more about y, the one in the middle, is a turn about y as the
        // bone's turn about z leaves it -- and that depends on how the clip
        // holds the bone (a sentry's gun is held by one set of angles
        // folded away and by another when it is up). It is taken from the
        // clip's first frame: the clips the game aims in hold it still.
        for (std::size_t clip = 0; clip < out.clips.size(); ++clip)
        {
            for (std::size_t n = 0; n < out.controllers.size(); ++n)
            {
                const int type = controlTypes[n];
                double axis[3] = {0, 0, 0};
                if (type == kMdlMotionYR)
                {
                    const double z = clipFirstZ[clip][out.controllers[n].bone];
                    axis[0] = -std::sin(z);
                    axis[1] = std::cos(z);
                }
                else if (type == kMdlMotionX || type == kMdlMotionXR)
                    axis[0] = 1;
                else if (type == kMdlMotionY)
                    axis[1] = 1;
                else
                    axis[2] = 1;
                ModelControlAxis a{};
                for (int k = 0; k < 3; ++k)
                    a.v[k] = static_cast<std::int16_t>(std::lround(axis[k] * 16384.0));
                out.controlAxes.push_back(a);
            }
        }
        for (const MdlHitbox &box : mdl.hitboxes)
        {
            if (out.hitboxes.size() >= 255)
                break;
            ModelHitbox hit{};
            hit.bone = static_cast<std::uint8_t>(box.bone);
            hit.group = static_cast<std::uint8_t>(std::clamp(box.group, 0, 255));
            for (int a = 0; a < 3; ++a)
            {
                hit.mins[a] = static_cast<std::int16_t>(
                    std::clamp(std::floor(double(box.mins[a]) * (1 << kHitboxShift)), -32768.0, 32767.0));
                hit.maxs[a] = static_cast<std::int16_t>(
                    std::clamp(std::ceil(double(box.maxs[a]) * (1 << kHitboxShift)), -32768.0, 32767.0));
            }
            out.hitboxes.push_back(hit);
        }

        for (const MdlFile::Attachment &a : mdl.attachments)
        {
            ModelAttachment attachment{};
            attachment.bone = static_cast<std::uint8_t>(a.bone);
            for (int k = 0; k < 3; ++k)
                attachment.at[k] = static_cast<std::int16_t>(
                    std::clamp(std::lround(double(a.at[k]) * (1 << kHitboxShift)), -32768L, 32767L));
            out.attachments.push_back(attachment);
        }

        ModelHeader &h = out.header;
        h.attachmentCount = static_cast<std::uint8_t>(out.attachments.size());
        h.controllerCount = static_cast<std::uint8_t>(out.controllers.size());
        h.hitboxCount = static_cast<std::uint8_t>(out.hitboxes.size());
        std::memcpy(h.magic, kModelMagic, 4);
        h.version = kModelVersion;
        h.boneCount = static_cast<std::uint16_t>(mdl.bones.size());
        h.handBone = h.spineBone = -1;
        for (std::size_t b = 0; b < mdl.bones.size() && b < 127; ++b)
        {
            if (lower(mdl.bones[b].name) == "bip01 r hand")
                h.handBone = static_cast<std::int8_t>(b);
            if (lower(mdl.bones[b].name) == "bip01 spine")
                h.spineBone = static_cast<std::int8_t>(b);
        }
        h.batchCount = static_cast<std::uint16_t>(out.batches.size());
        h.clipCount = static_cast<std::uint16_t>(out.clips.size());
        h.eventCount = static_cast<std::uint16_t>(out.events.size());
        h.bodyPartCount = static_cast<std::uint16_t>(out.bodyParts.size());
        h.gxWordCount = static_cast<std::uint32_t>(out.gx.size());
        h.trackCount = static_cast<std::uint32_t>(out.tracks.size());
        h.keyCount = static_cast<std::uint32_t>(out.keys.size());
        h.atlasWidth = static_cast<std::uint16_t>(out.atlas.width);
        h.atlasHeight = static_cast<std::uint16_t>(out.atlas.height);
        h.vertexShift = static_cast<std::uint8_t>(vertexShift);
        for (int a = 0; a < 3; ++a)
        {
            h.mins[a] = static_cast<std::int16_t>(std::clamp(std::floor(lo[a]), -32768.0, 32767.0));
            h.maxs[a] = static_cast<std::int16_t>(std::clamp(std::ceil(hi[a]), -32768.0, 32767.0));
        }
        // Half as much again: an animation reaches further than the rest pose.
        h.radius = static_cast<std::uint16_t>(std::min(65535.0, std::ceil(radius * 1.5)));
        h.polygons = static_cast<std::uint16_t>(std::min(worstPolygons, 65535));
        for (int l = 0; l < kModelLevels; ++l)
            h.levelError[l] = out.levelError[l];
        return true;
    }

    bool writeModel(const CookedModel &model, std::vector<std::uint8_t> &out, std::string &error)
    {
        out.assign(sizeof(ModelHeader), 0);
        auto append = [&](const void *data, std::size_t bytes)
        {
            const auto *p = static_cast<const std::uint8_t *>(data);
            out.insert(out.end(), p, p + bytes);
            while (out.size() & 3)
                out.push_back(0);
        };
        append(model.parents.data(), model.parents.size() * 2);
        append(model.bodyParts.data(), model.bodyParts.size() * sizeof(ModelBodyPart));
        append(model.batches.data(), model.batches.size() * sizeof(ModelBatch));
        append(model.gx.data(), model.gx.size() * 4);
        append(model.clips.data(), model.clips.size() * sizeof(ModelClip));
        append(model.tracks.data(), model.tracks.size() * sizeof(ModelTrack));
        append(model.keys.data(), model.keys.size() * sizeof(ModelKey));
        append(model.events.data(), model.events.size() * sizeof(ModelEvent));
        append(model.controllers.data(), model.controllers.size() * sizeof(ModelController));
        append(model.controlAxes.data(), model.controlAxes.size() * sizeof(ModelControlAxis));
        append(model.hitboxes.data(), model.hitboxes.size() * sizeof(ModelHitbox));
        append(model.attachments.data(), model.attachments.size() * sizeof(ModelAttachment));
        ModelHeader header = model.header;
        if (!model.screenCounts.empty())
        {
            append(model.screenCounts.data(), model.screenCounts.size() * 4);
            header.flags = static_cast<std::uint16_t>(header.flags | kModelScreenCounts);
        }

        header.fileSize = static_cast<std::uint32_t>(out.size());
        header.crc32 = crc32(out.data() + sizeof(ModelHeader),
                             static_cast<std::uint32_t>(out.size() - sizeof(ModelHeader)));
        std::memcpy(out.data(), &header, sizeof(header));

        ModelView view;
        if (!view.open(out.data(), static_cast<std::uint32_t>(out.size())))
        {
            error = "the cooked model does not pass the DS reader";
            out.clear();
            return false;
        }
        return true;
    }

    bool splitModelKeys(const std::vector<std::uint8_t> &file, std::vector<std::uint8_t> &model,
                        std::vector<std::uint8_t> &keys)
    {
        ModelView view;
        if (!view.open(file.data(), static_cast<std::uint32_t>(file.size())) ||
            (view.header->flags & kModelKeysApart) != 0)
            return false;
        // The keys' section, padded as the file has it, comes out whole.
        const std::size_t at = static_cast<std::size_t>(reinterpret_cast<const std::uint8_t *>(view.keys) - file.data());
        const std::size_t bytes = std::size_t{view.header->keyCount} * sizeof(ModelKey);
        const std::size_t padded = (bytes + 3) & ~std::size_t(3);
        keys.assign(file.begin() + static_cast<std::ptrdiff_t>(at),
                    file.begin() + static_cast<std::ptrdiff_t>(at + bytes));
        model.assign(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(at));
        model.insert(model.end(), file.begin() + static_cast<std::ptrdiff_t>(at + padded), file.end());
        ModelHeader header;
        std::memcpy(&header, model.data(), sizeof(header));
        header.flags = static_cast<std::uint16_t>(header.flags | kModelKeysApart);
        header.fileSize = static_cast<std::uint32_t>(model.size());
        header.crc32 = crc32(model.data() + sizeof(ModelHeader), static_cast<std::uint32_t>(model.size() - sizeof(ModelHeader)));
        std::memcpy(model.data(), &header, sizeof(header));
        ModelView apart;
        if (!apart.open(model.data(), static_cast<std::uint32_t>(model.size())))
        {
            model.clear();
            keys.clear();
            return false;
        }
        return true;
    }

    bool modelKeyBytes(const std::vector<std::uint8_t> &file, std::size_t &all, std::size_t &largestClip,
                       std::size_t &clips)
    {
        all = largestClip = clips = 0;
        ModelView view;
        if (!view.open(file.data(), static_cast<std::uint32_t>(file.size())))
            return false;
        all = std::size_t{view.header->keyCount} * sizeof(ModelKey);
        clips = view.header->clipCount;
        for (int c = 0; c < view.header->clipCount; ++c)
        {
            std::uint32_t first = 0, count = 0;
            view.clipKeys(c, first, count);
            largestClip = std::max(largestClip, std::size_t{count} * sizeof(ModelKey));
        }
        return true;
    }
} // namespace hlds
