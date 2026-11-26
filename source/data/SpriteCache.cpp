#include "data/SpriteCache.hpp"

#include <cstdlib>
#include <string>

#include "Global.hpp"
#include "HlSprite.hpp"
#include "data/LevelFile.hpp"
#include "data/TextureCache.hpp"
#include "render/GlowLayer.hpp"

namespace hlrt
{
    SpriteCache spriteCache;

    void SpriteCache::destroy()
    {
        for (int i = 0; i < count_; ++i)
        {
            if (materials_[i] != nullptr)
                NEA_MaterialDelete(materials_[i]);
            if (palettes_[i] != nullptr)
                NEA_PaletteDelete(palettes_[i]);
        }
        forget();
    }

    void SpriteCache::forget()
    {
        glowLayer.forget();
        std::free(materials_);
        std::free(palettes_);
        materials_ = nullptr;
        palettes_ = nullptr;
        count_ = 0;
    }

    bool SpriteCache::create()
    {
        destroy();
        const int count = static_cast<int>(level.view.spriteCount);
        materials_ = static_cast<NEA_Material **>(std::calloc(count + 1, sizeof(NEA_Material *)));
        palettes_ = static_cast<NEA_Palette **>(std::calloc(count + 1, sizeof(NEA_Palette *)));
        if (materials_ == nullptr || palettes_ == nullptr)
        {
            forget();
            return false;
        }
        count_ = count;
        // (The glows' 2D picture is made from one of them as it is read.)
        const int glow = glowLayer.chooseSprite();
        for (int i = 0; i < count; ++i)
        {
            const hldata::SpriteRef &ref = level.view.sprites[i];
            if (ref.frames == 0)
                continue;
            const std::string path =
                global.mapPath("spr/") + hldata::spriteFile(ref.name, ref.width, ref.height);
            TextureCache::loadFile(path.c_str(), false, ref.width, ref.height, 1, materials_[i], palettes_[i], nullptr,
                                   false, i == glow ? &GlowLayer::takePicture : nullptr, &glowLayer);
        }
        return true;
    }

    HL_ITCM const hldata::SpriteRef *SpriteCache::ref(unsigned index) const
    {
        // (A common sprite a level cooked before it has not got: that place
        // is one of the map's own.)
        if (index < static_cast<unsigned>(hldata::CommonSprite::Count) &&
            index >= hldata::commonSpritesOf(level.view.header->version))
            return nullptr;
        return material(index) != nullptr ? &level.view.sprites[index] : nullptr;
    }
} // namespace hlrt
