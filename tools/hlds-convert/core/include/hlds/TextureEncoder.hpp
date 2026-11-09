#pragma once

// The DS's texture formats as files: the encoder interface, the encoder used
// today, and a decoder.
//
// Two formats are cooked (shared/HlTexture.hpp says why each): 4x4 compressed
// for everything opaque, A3I5 for textures with holes.
//
// ENCODING is not done here. Choosing a good palette for thousands of 4x4
// blocks at once is the hard part of that format, and ptexconv (BSD-2, part of
// a BlocksDS installation) already does it well, so it is run as a child
// process behind ITextureEncoder. The interface is the seam: replacing it with
// a linked-in encoder, so that a player needs no BlocksDS install, changes
// nothing above it.
//
// DECODING is here because it is small and lets the converter check what the
// encoder produced -- that every block points inside its palette -- and show a
// texture as the DS will draw it.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hlds/TextureImage.hpp"

namespace hlds
{
    enum class TextureFormat
    {
        Tex4x4,
        A3I5,
    };

    struct CookedTexture
    {
        int width = 0;
        int height = 0;
        TextureFormat format = TextureFormat::Tex4x4;
        std::vector<std::uint8_t> texels;  ///< 4x4: 4 bytes a block. A3I5: a byte a texel
        std::vector<std::uint8_t> index;   ///< 4x4: 2 bytes a block. A3I5: empty
        std::vector<std::uint8_t> palette; ///< 2 bytes a colour, RGB555

        int paletteColours() const { return static_cast<int>(palette.size() / 2); }
    };

    class ITextureEncoder
    {
    public:
        virtual ~ITextureEncoder() = default;
        /// `image` must already be the cooked size. `maxColours` caps the
        /// palette of a 4x4 texture; an A3I5 palette is always 32. For A3I5 the
        /// image's alpha is used at full depth; for 4x4 only as there / not
        /// there. Called from several threads at once: an implementation
        /// must not share unguarded state between calls.
        virtual bool encode(const RgbaImage &image, int maxColours, TextureFormat format,
                            CookedTexture &out, std::string &error) = 0;
        /// Changes whenever the encoder's output for the same input might:
        /// part of what a cached texture is checked against.
        virtual std::uint32_t version() const = 0;
    };

    /// How many textures are encoded at once in this process, by every
    /// encoder together (one ptexconv each): `threads`, or every thread the
    /// machine has for 0. Set before a cook begins.
    void setEncodeThreads(unsigned threads);
    unsigned encodeThreads();

    class PtexconvEncoder : public ITextureEncoder
    {
    public:
        /// `tempDir` is created if needed and holds the files exchanged with
        /// the tool; they are removed after each texture. `cancel`, if given,
        /// makes each texture from then on fail without being encoded.
        PtexconvEncoder(std::filesystem::path executable, std::filesystem::path tempDir,
                        const std::atomic<bool> *cancel = nullptr);

        bool encode(const RgbaImage &image, int maxColours, TextureFormat format,
                    CookedTexture &out, std::string &error) override;
        std::uint32_t version() const override { return 2; }

    private:
        std::filesystem::path executable_;
        std::filesystem::path tempDir_;
        const std::atomic<bool> *cancel_ = nullptr;
        std::atomic<int> counter_{0}; ///< names the temp files: one set per call
    };

    /// Where ptexconv is: `explicitPath` if given, else under $BLOCKSDSEXT,
    /// $WONDERFUL_TOOLCHAIN and /opt/wonderful, else on the PATH. Empty when it
    /// cannot be found.
    std::filesystem::path findPtexconv(const std::filesystem::path &explicitPath);

    /// Decode to true colour. Fails if the sizes are inconsistent or a block
    /// points outside the palette.
    bool decodeTexture(const CookedTexture &texture, RgbaImage &out, std::string &error);
} // namespace hlds
