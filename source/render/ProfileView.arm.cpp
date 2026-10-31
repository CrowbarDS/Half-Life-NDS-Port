#include "render/ProfileView.hpp"

#include <nds.h>

#include <NEAMain.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Global.hpp"
#include "HlAnim.hpp"
#include "HlModel.hpp"
#include "HlRemap.hpp"
#include "HlUi.hpp"
#include "data/CardRead.hpp"
#include "data/TextureCache.hpp"

#include "HlCharacters.hpp"

namespace hlrt
{
    ProfileView profileView;

    namespace
    {
        using namespace hldata;

        /// A model is tens of kilobytes; one that claims to be megabytes is
        /// not one (ModelCache's bound).
        constexpr long kMostBytes = 1024 * 1024;
        /// Room left after it, for whatever the title does next.
        constexpr std::size_t kSpare = 256 * 1024;

        /// Where it stands, in view space (whole units: 128 of them a unit
        /// of the view, as the near plane has it): a little below the middle
        /// of the screen, as far off as shows all of it.
        constexpr int kAtY = -6, kAtZ = -84;
        /// How fast it turns: a whole turn in about seven seconds.
        constexpr int kTurnPerBlank = 156;

        BoneMatrix pose[kModelMaxBones];
#ifndef HLDS_CHILD
        BoneMatrix aimPose[kModelMaxBones];
        /// What they hold (a console with the game: the pistol).
        constexpr const char *kLobbyGun = "p_9mmhandgun";
#endif
    } // namespace

    bool ProfileView::open(int top, int bottom, int character)
    {
        close();
        character_ = character;
        hidden_ = false;
        const bool other = character > 0 && character < kCharacterCount;
        const std::string own = std::string("chr/") + kCharacterFiles[other ? character : 0];
        // (At its finest on a DSi, where it is there.)
        std::string modelFile = other ? own + ".hmd" : std::string(kProfileModelFile);
        std::string skinFile = other ? own + ".htx" : std::string(kProfileSkinFile);
        if (global.isDSi)
        {
            const std::string fineModel = other ? own + ".dsi.hmd" : std::string(kProfileFineModelFile);
            if (std::FILE *fine = std::fopen(global.commonPath(fineModel).c_str(), "rb"))
            {
                std::fclose(fine);
                modelFile = fineModel;
                skinFile = other ? own + ".dsi.htx" : std::string(kProfileFineSkinFile);
            }
        }
        std::FILE *file = std::fopen(global.commonPath(modelFile).c_str(), "rb");
        if (file == nullptr)
            return false;
        std::fseek(file, 0, SEEK_END);
        const long length = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        std::uint8_t *block = nullptr;
        if (length > 0 && length <= kMostBytes && global.heapHeadroom() >= static_cast<std::size_t>(length) + kSpare)
            block = static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(length)));
        const bool got =
            block != nullptr && cardRead(block, static_cast<std::size_t>(length), file) == static_cast<std::size_t>(length);
        std::fclose(file);
        auto *view = new ModelView();
        if (!got || !view->open(block, static_cast<std::uint32_t>(length)) || view->header->atlasWidth == 0 ||
            view->header->clipCount == 0)
        {
            std::free(block);
            delete view;
            return false;
        }
        // Its skin, and the colours it came with (a 256-colour one has them
        // to lay a player's over).
        NEA_Material *material = nullptr;
        NEA_Palette *palette = nullptr;
        if (!TextureCache::loadFile(global.commonPath(skinFile).c_str(), false, view->header->atlasWidth,
                                    view->header->atlasHeight, 0, material, palette) ||
            !TextureCache::takeRemapBase(base_))
        {
            if (material != nullptr)
                NEA_MaterialDelete(material);
            if (palette != nullptr)
                NEA_PaletteDelete(palette);
            std::free(block);
            delete view;
            return false;
        }
        data_ = block;
        view_ = view;
        material_ = material;
        palette_ = palette;
        // Standing about, as a player does in a match with nothing to do.
        clip_ = view->findClip(nameHash("look_idle"));
        clip_ = clip_ >= 0 ? clip_ : 0;
        frame_ = 0;
        yaw_ = 0;
        since_ = Global::vblanks();
        setColours(top, bottom);
#ifndef HLDS_CHILD
        // In their hand, if there is one to have.
        aim_ = view->findClip(nameHash("ref_aim_onehanded"));
        if (std::FILE *gun = std::fopen(global.commonPath(std::string("wpn/") + kLobbyGun + ".hmd").c_str(), "rb"))
        {
            std::fseek(gun, 0, SEEK_END);
            const long bytes = std::ftell(gun);
            std::fseek(gun, 0, SEEK_SET);
            auto *gunBlock = bytes > 0 && bytes <= kMostBytes ? static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(bytes))) : nullptr;
            const bool whole = gunBlock != nullptr && cardRead(gunBlock, static_cast<std::size_t>(bytes), gun) == static_cast<std::size_t>(bytes);
            std::fclose(gun);
            auto *gunView = new ModelView();
            NEA_Material *gunSkin = nullptr;
            NEA_Palette *gunColours = nullptr;
            if (whole && gunView->open(gunBlock, static_cast<std::uint32_t>(bytes)) && gunView->header->atlasWidth != 0 &&
                view->header->handBone >= 0 &&
                TextureCache::loadFile(global.commonPath("wpn/" + modelSkinFile(kLobbyGun, gunView->header->atlasWidth, gunView->header->atlasHeight)).c_str(),
                                       false, gunView->header->atlasWidth, gunView->header->atlasHeight, 0, gunSkin, gunColours))
            {
                gunData_ = gunBlock;
                gunView_ = gunView;
                gunMaterial_ = gunSkin;
                gunPalette_ = gunColours;
            }
            else
            {
                std::free(gunBlock);
                delete gunView;
            }
        }
#endif
        return true;
    }

    void ProfileView::setColours(int top, int bottom)
    {
        if (data_ == nullptr)
            return;
        std::uint8_t base[256][3];
        for (int i = 0; i < 256; ++i)
        {
            base[i][0] = static_cast<std::uint8_t>((base_[i] & 31) << 3);
            base[i][1] = static_cast<std::uint8_t>(((base_[i] >> 5) & 31) << 3);
            base[i][2] = static_cast<std::uint8_t>(((base_[i] >> 10) & 31) << 3);
        }
        std::uint16_t colours[256];
        playerPalette(base, remapRunsOf("DM_Base"), top, bottom, colours);
        if (colours_ == nullptr)
            colours_ = NEA_PaletteCreate();
        if (colours_ != nullptr && NEA_PaletteLoad(static_cast<NEA_Palette *>(colours_), colours, 256, NEA_PAL256) != 1)
        {
            NEA_PaletteDelete(static_cast<NEA_Palette *>(colours_));
            colours_ = nullptr;
        }
    }

    void ProfileView::close()
    {
#ifndef HLDS_CHILD
        if (gunMaterial_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(gunMaterial_));
        if (gunPalette_ != nullptr)
            NEA_PaletteDelete(static_cast<NEA_Palette *>(gunPalette_));
        delete static_cast<ModelView *>(gunView_);
        std::free(gunData_);
        gunData_ = nullptr;
        gunView_ = gunMaterial_ = gunPalette_ = nullptr;
#endif
        if (colours_ != nullptr)
            NEA_PaletteDelete(static_cast<NEA_Palette *>(colours_));
        if (material_ != nullptr)
            NEA_MaterialDelete(static_cast<NEA_Material *>(material_));
        if (palette_ != nullptr)
            NEA_PaletteDelete(static_cast<NEA_Palette *>(palette_));
        delete static_cast<ModelView *>(view_);
        std::free(data_);
        data_ = nullptr;
        view_ = material_ = palette_ = colours_ = nullptr;
    }

    // (The pose worked out in here, not called: a copy of poseModel of its
    // own, so that ModelDraw's stays where it is, in its caller.)
    __attribute__((flatten)) void ProfileView::draw()
    {
        if (data_ == nullptr || hidden_)
            return;
        const ModelView &model = *static_cast<const ModelView *>(view_);
        // On by the blanks gone by, however many frames that was.
        const std::uint32_t now = Global::vblanks();
        const int blanks = static_cast<int>(now - since_);
        since_ = now;
        bool finished = false;
        frame_ = advanceClip(model.clips[clip_], frame_, blanks * 1000 / 60, finished);
        if (finished)
            frame_ = 0;
        yaw_ = static_cast<std::uint16_t>(yaw_ + blanks * kTurnPerBlank);
        poseModel(model, clip_, frame_, pose);
#ifndef HLDS_CHILD
        if (aim_ >= 0)
            overlayAimPose(model, pose, aim_, 128, aimPose);
#endif

        // Where each of the model's axes goes in view space (Q12): its
        // forward (x) turned about its up out of the screen at first, its
        // left to the screen's right, its up the screen's up.
        fx sine, cosine;
        fxSinCos(yaw_, sine, cosine);
        const int turned[9] = {sine, 0, cosine, cosine, 0, -sine, 0, kFxOne, 0};

        NEA_MaterialUse(static_cast<const NEA_Material *>(material_));
        if (colours_ != nullptr)
            GFX_PAL_FORMAT = NEA_PaletteGetBase(static_cast<NEA_Palette *>(colours_));
        NEA_PolyFormat(31, 0, NEA_LIGHT_0, NEA_CULL_NONE, static_cast<NEA_OtherFormatEnum>(0));
        // Lit from above and in front, a little to one side, as most of the
        // game's rooms are.
        MATRIX_CONTROL = GL_MODELVIEW;
        MATRIX_IDENTITY = 0;
        GFX_LIGHT_VECTOR = static_cast<std::uint32_t>(floattov10(-0.3f) & 0x3FF) |
                           (static_cast<std::uint32_t>(floattov10(-0.75f) & 0x3FF) << 10) |
                           (static_cast<std::uint32_t>(floattov10(-0.55f) & 0x3FF) << 20);
        GFX_LIGHT_COLOR = RGB15(26, 26, 26);
        GFX_DIFFUSE_AMBIENT = RGB15(20, 20, 20) | (RGB15(13, 13, 13) << 16);
        GFX_SPECULAR_EMISSION = 0;

        auto batches = [&](const ModelView &what, const BoneMatrix *poses)
        {
        const ModelView &model = what;
        const int coarser = model.header->vertexShift;
        for (int n = 0; n < model.header->batchCount; ++n)
        {
            const ModelBatch &batch = model.batches[n];
            if (!model.batchInBody(batch, 0))
                continue;
            for (int k = 0; k < batch.boneCount; ++k)
            {
                const BoneMatrix &bone = poses[batch.bones[k]];
                // As ModelDraw does, row i where the bone's axis i goes in
                // view space. A vertex is 1/128 unit (coarser by vertexShift)
                // and the hardware reads it as Q12; a unit of the view is 128
                // units: so a vertex of 128 must come to 32 of the view's
                // Q12 -- a quarter. The bone's turn (Q14) by the model's
                // (Q12) is Q26: 16 down, less vertexShift. Normals keep
                // their length.
                m4x3 position, direction;
                for (int i = 0; i < 3; ++i)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        const int sum = bone.r[i] * turned[j] + bone.r[3 + i] * turned[3 + j] +
                                        bone.r[6 + i] * turned[6 + j]; // Q14 * Q12
                        position.m[i * 3 + j] =
                            static_cast<int>((static_cast<std::int64_t>(sum) << coarser) >> (kBoneRotShift + 2));
                        direction.m[i * 3 + j] = sum >> kBoneRotShift;
                    }
                }
                // The bone's origin (Q12 units, by Q12): to the view's Q12,
                // a 128th.
                const int at[3] = {atX_ << 5, kAtY << 5, kAtZ << 5};
                for (int j = 0; j < 3; ++j)
                {
                    const std::int64_t sum = static_cast<std::int64_t>(bone.t[0]) * turned[j] +
                                             static_cast<std::int64_t>(bone.t[1]) * turned[3 + j] +
                                             static_cast<std::int64_t>(bone.t[2]) * turned[6 + j];
                    position.m[9 + j] = at[j] + static_cast<int>(sum >> 19);
                    direction.m[9 + j] = 0;
                }
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
        };
        batches(model, pose);
#ifndef HLDS_CHILD
        if (gunView_ != nullptr)
        {
            NEA_MaterialUse(static_cast<const NEA_Material *>(gunMaterial_));
            BoneMatrix hand = pose[model.header->handBone];
            batches(*static_cast<const ModelView *>(gunView_), &hand);
        }
#endif
    }
} // namespace hlrt
