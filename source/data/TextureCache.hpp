#pragma once

// The loaded level's textures, as NEA materials.
//
// One material per entry of the level's texture table, in the same order, so
// the renderer goes from a surface's texture index to something it can bind
// with one array read. Each comes from its own .htx file on the card
// (shared/HlTexture.hpp), already in one of the hardware's own formats -- 4x4
// compressed, or A3I5 for textures with holes -- so the file's pieces are
// handed to NEA as they are.
//
// A texture that is missing or unreadable is not an error. Its material stays
// null and the renderer draws those surfaces in their baked light alone --
// a grey wall is a better thing to show a player than a refusal to start.
//
// Streamed textures are not loaded here. They are drawn as a flat colour until
// TextureStream has read the real one in, and again after it has made way for another.
// Either way what a surface binds is two words (bind()), which is why the surfaces
// that name them can all be drawn alike.

#include <cstddef>
#include <cstdint>

#include <NEAMain.h>

#include "HlTexture.hpp"

namespace hlrt
{
    class TextureCache
    {
    public:
        /// Load every texture of the loaded level. False only when there is no
        /// memory for the table itself.
        bool create();
        void destroy();
        /// Drop the table WITHOUT deleting the materials: for when NEA itself
        /// has just been torn down and has already freed them.
        void forget();

        /// Null for a texture that could not be loaded,
        /// and for a streamed one that is not currently in video memory.
        NEA_Material *material(unsigned index) const { return materials_[index]; }
        /// Whether `index` is one the streaming looks after.
        bool streamed(unsigned index) const { return streamed_ != nullptr && streamed_[index] != 0; }
        /// Whether there is anything to bind for `index`:
        /// a material, or (for a streamed texture) its flat colour.
        bool present(unsigned index) const { return materials_[index] != nullptr || streamed(index); }
        /// Do what NEA_MaterialUse does for `index`, for what is drawn next:
        /// its texture, or if streamed and not in memory, its flat colour.
        /// Returns false, with no texture, if missing.
        bool use(unsigned index) const;
        /// TextureStream's: the texture has been read into video memory,
        /// or has been let go (back to its flat colour).
        /// (Attaching over a texture still attached lets that one go first.)
        void attachStreamed(unsigned index, NEA_Material *material, NEA_Palette *palette);
        /// Back to the flat colour at once; what it had is handed to
        /// TextureStream::retire, which frees it once no picture on the
        /// screen can still be drawing it.
        void detachStreamed(unsigned index);
        int streamedCount() const { return streamedCount_; }
        /// The same choice as two command parameters, for a display list
        /// (where a register cannot be written): the texture's (0: none) and
        /// its palette's.
        const std::uint32_t *bind(unsigned index) const { return binds_ + index * 2; }
        /// The texture's own colour, taken all together (the mean of its
        /// palette), as an RGB555: what a wall of it is the colour of at
        /// full light. White for one that is not there.
        std::uint32_t tint(unsigned index) const
        {
            return tints_ != nullptr && index < static_cast<unsigned>(count_) ? tints_[index] : 0x7FFFu;
        }
        /// Which texture is shown now where a surface names `index`: itself,
        /// or -- for one that is more than one picture -- the picture of
        /// its cycle that is due; and with `switched`, of its other face's
        /// (a brush whose "frame" is set), if it has one.
        unsigned shown(unsigned index, bool switched = false) const;
        /// Once a frame: which picture of every cycle is due (ten a second,
        /// as the game has them).
        void frame();
        int count() const { return count_; }
        int missing() const { return missing_; }

        /// Load one cooked texture file into a new material and palette of
        /// its own. `expect*` are what the file has to be to be the right
        /// one (0 / -1: anything). False, with nothing left allocated, if it
        /// is missing, wrong, or does not fit in video memory.
        /// `ramBacked`: the texture is kept in main memory, and goes into video
        /// memory only when NEA_MaterialTexVramLoad is asked. `peek`, if
        /// given, is shown the file as read (its header, and what follows
        /// it), with `context`, before it goes.
        using Peek = void (*)(const hldata::TextureHeader &header, const std::uint8_t *body, void *context);
        static bool loadFile(const char *path, bool liquid, int expectWidth, int expectHeight,
                             int expectA3I5, NEA_Material *&material, NEA_Palette *&palette,
                             std::uint16_t *tint = nullptr, bool ramBacked = false, Peek peek = nullptr,
                             void *context = nullptr);

        /// The two halves of loadFile. readFile puts a file's header and
        /// the rest of it (texels, index, palette) in `body`, which has room
        /// for `capacity` bytes, after checking it against what is expected;
        /// false if it is missing, wrong or too big. create makes the
        /// material and palette from them.
        static bool readFile(const char *path, int expectWidth, int expectHeight, int expectA3I5,
                             hldata::TextureHeader &header, std::uint8_t *body, std::size_t capacity);
        static bool create(const hldata::TextureHeader &header, const std::uint8_t *body, bool liquid,
                           NEA_Material *&material, NEA_Palette *&palette, std::uint16_t *tint = nullptr,
                           bool ramBacked = false);

        /// The palette of the last 256-colour skin loaded that has a player's
        /// colours laid over it (hldata::kTextureRemapRuns), into `out` (256
        /// colours), once: what the players' own palettes are made from.
        static bool takeRemapBase(std::uint16_t *out)
        {
            if (!remapReady_)
                return false;
            for (int i = 0; i < 256; ++i)
                out[i] = remapBase_[i];
            remapReady_ = false;
            return true;
        }

        TextureCache() = default;
        TextureCache(const TextureCache &) = delete;
        TextureCache &operator=(const TextureCache &) = delete;

    private:
        static std::uint16_t remapBase_[256];
        static bool remapReady_;
        bool loadOne(unsigned index);
        bool makeFlat(unsigned index, std::uint16_t colour);

        NEA_Material **materials_ = nullptr;
        NEA_Palette **palettes_ = nullptr;
        NEA_Palette **flatPalettes_ = nullptr;
        NEA_Material *flatTexture_ = nullptr;
        std::uint8_t *streamed_ = nullptr;
        int streamedCount_ = 0;
        std::uint32_t *binds_ = nullptr;
        std::uint16_t *tints_ = nullptr;
        int count_ = 0;
        int missing_ = 0;
        std::uint32_t tick_ = 0;
    };

    extern TextureCache textureCache;
} // namespace hlrt
