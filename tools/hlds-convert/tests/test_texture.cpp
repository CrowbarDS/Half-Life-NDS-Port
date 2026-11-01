// Textures: from the game's indexed pixels to the DS's compressed blocks.
//
// The decoder is checked against blocks written out by hand from the hardware
// documentation, because it is what everything else here is measured with: the
// encoder is an external program, and the only way to know it produced the
// texture that was asked for is to decode its output and look.
//
// The encoder tests need ptexconv and are skipped, loudly, without it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "TestBspBuilder.hpp"
#include "hlds/AtomicWrite.hpp"
#include "hlds/TextureEncoder.hpp"
#include "hlds/TextureBudget.hpp"
#include "hlds/TextureCook.hpp"

namespace fs = std::filesystem;
using namespace hlds;
using namespace hldstest;

namespace
{
    void testIndexedAndResample()
    {
        IndexedImage img;
        img.width = 4;
        img.height = 2;
        img.pixels = {0, 1, 255, 255, 1, 1, 255, 0};
        img.palette.assign(768, 0);
        img.palette[0] = 200; // index 0: red 200
        img.palette[3 + 1] = 100; // index 1: green 100
        img.palette[255 * 3 + 2] = 255; // index 255: pure blue, the key colour

        RgbaImage opaque = toRgba(img, false);
        check(opaque.at(2, 0)[2] == 255 && opaque.at(2, 0)[3] == 255,
              "without a colour key, index 255 is just blue");
        RgbaImage keyed = toRgba(img, true);
        check(keyed.at(2, 0)[3] == 0 && keyed.at(2, 0)[2] == 0, "with one, it is a hole");
        check(keyed.at(0, 0)[0] == 200 && keyed.at(0, 0)[3] == 255, "other indices are colours");

        // Halve: each output pixel is a 2x2 average.
        RgbaImage half = resample(keyed, 2, 1);
        // Left: red, green, green, green -> opaque, R 50, G 75.
        check(half.at(0, 0)[3] == 255 && half.at(0, 0)[0] == 50 && half.at(0, 0)[1] == 75,
              "a 2x2 block averages");
        // Right: hole, hole, hole, red -> three quarters empty: a hole.
        check(half.at(1, 0)[3] == 0, "a mostly-empty block stays a hole");

        // Half empty counts as there, and takes its colour from the opaque
        // half only -- no blue fringe from the key colour.
        IndexedImage edge = img;
        edge.pixels = {0, 255, 0, 255, 0, 255, 0, 255};
        RgbaImage edgeHalf = resample(toRgba(edge, true), 2, 1);
        check(edgeHalf.at(0, 0)[3] == 255 && edgeHalf.at(0, 0)[0] == 200 &&
                  edgeHalf.at(0, 0)[2] == 0,
              "an edge pixel keeps the opaque colour, not a mix with the key");

        RgbaImage same = resample(opaque, 4, 2);
        check(same.rgba == opaque.rgba, "resampling to the same size changes nothing");
        RgbaImage doubled = resample(opaque, 8, 4);
        check(doubled.at(1, 1)[0] == 200 && doubled.at(2, 0)[1] == 100,
              "enlarging repeats pixels");
    }

    void put16(std::vector<std::uint8_t> &v, std::size_t at, std::uint16_t x)
    {
        v[at] = static_cast<std::uint8_t>(x);
        v[at + 1] = static_cast<std::uint8_t>(x >> 8);
    }

    void testDecoder()
    {
        // 16 x 4: four blocks, one in each of the format's four modes, sharing
        // a 6-colour palette.
        CookedTexture tex;
        tex.width = 16;
        tex.height = 4;
        tex.texels.assign(16, 0);
        tex.index.assign(8, 0);
        tex.palette.assign(12, 0);
        put16(tex.palette, 0, hldata::rgb555(248, 0, 0));   // 0 red
        put16(tex.palette, 2, hldata::rgb555(0, 0, 248));   // 1 blue
        put16(tex.palette, 4, hldata::rgb555(0, 248, 0));   // 2 green
        put16(tex.palette, 6, hldata::rgb555(248, 248, 248)); // 3 white
        // Every block: texels 0,1,2,3 across the top row, then all zero.
        for (int b = 0; b < 4; ++b)
            tex.texels[b * 4] = 0xE4; // 11 10 01 00
        put16(tex.index, 0, 0x0000 | 0); // mode 0: three colours + transparent
        put16(tex.index, 2, 0x4000 | 0); // mode 1: two colours, their mean, transparent
        put16(tex.index, 4, 0x8000 | 0); // mode 2: four colours
        put16(tex.index, 6, 0xC000 | 0); // mode 3: two colours and two blends

        RgbaImage out;
        std::string error;
        check(decodeTexture(tex, out, error), "the four-mode texture decodes");
        auto is = [&](int x, int r, int g, int b, int a)
        {
            const std::uint8_t *p = out.at(x, 0);
            return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
        };
        check(is(0, 248, 0, 0, 255) && is(1, 0, 0, 248, 255) && is(2, 0, 248, 0, 255) &&
                  is(3, 0, 0, 0, 0),
              "mode 0: colours 0, 1, 2 and transparent");
        check(is(6, 124, 0, 124, 255) && is(7, 0, 0, 0, 0),
              "mode 1: the third value is the mean, the fourth transparent");
        check(is(10, 0, 248, 0, 255) && is(11, 248, 248, 248, 255), "mode 2: four colours");
        check(is(14, 155, 0, 93, 255) && is(15, 93, 0, 155, 255),
              "mode 3: 5/8 - 3/8 blends both ways");
        check(out.at(0, 1)[0] == 248, "texel order is row by row");

        // The palette offset counts PAIRS of colours.
        put16(tex.index, 0, 0x8000 | 1);
        check(decodeTexture(tex, out, error) && is(0, 0, 248, 0, 255),
              "a palette offset of 1 starts at colour 2");
        put16(tex.index, 0, 0x8000 | 2);
        check(!decodeTexture(tex, out, error), "a block reaching past the palette is refused");
        tex.index.pop_back();
        check(!decodeTexture(tex, out, error), "data that does not match the size is refused");
    }

    void testA3I5Decoder()
    {
        // 4 x 1: alpha 0, 3 and 7 (of 7) on colour 1, and colour 2 at alpha 7.
        CookedTexture tex;
        tex.width = 4;
        tex.height = 1;
        tex.format = TextureFormat::A3I5;
        tex.texels = {1 | (0 << 5), 1 | (3 << 5), 1 | (7 << 5), 2 | (7 << 5)};
        tex.palette.assign(6, 0);
        put16(tex.palette, 2, hldata::rgb555(248, 0, 0));
        put16(tex.palette, 4, hldata::rgb555(0, 248, 0));

        RgbaImage out;
        std::string error;
        check(decodeTexture(tex, out, error), "an A3I5 texture decodes");
        check(out.at(0, 0)[3] == 0, "alpha 0 is a hole");
        // 3 widens to 3*4 + 3/2 = 13 of 31.
        check(out.at(1, 0)[3] == 13 * 255 / 31 && out.at(1, 0)[0] == 248,
              "alpha 3 is 13/31, as the hardware widens it");
        check(out.at(2, 0)[3] == 255 && out.at(3, 0)[1] == 248, "alpha 7 is opaque; colour index");

        tex.texels[0] = 3; // colour 3 of a 3-colour palette
        check(!decodeTexture(tex, out, error), "a texel past the palette is refused");
        tex.texels = {1, 1, 1};
        check(!decodeTexture(tex, out, error), "data that does not match the size is refused");

        // Packed, it is marked as A3I5 whatever the caller passes, and comes
        // back as A3I5.
        tex.texels.assign(16 * 16, 1 | (7 << 5));
        tex.width = tex.height = 16;
        const auto bytes = packTexture(tex, 0, 7);
        CookedTexture back;
        hldata::TextureHeader header;
        check(unpackTexture(bytes, back, header), "an A3I5 file unpacks");
        check((header.flags & hldata::kTextureA3I5) && header.indexBytes == 0 &&
                  header.texelBytes == 256 && back.format == TextureFormat::A3I5 &&
                  back.texels == tex.texels,
              "with its format, a byte a texel and no index piece");
        auto bad = bytes;
        bad[16] = 0; // clear the format flag: now the sizes are a 4x4 texture's, and wrong
        bad[17] = bad[18] = bad[19] = 0;
        std::memcpy(bad.data() + 24, "\0\0\0\0", 4);
        check(!unpackTexture(bad, back, header), "a file whose sizes do not fit its format is refused");
    }

    void testSoftResample()
    {
        // A 4x4 image whose left half is a hole, halved: the middle column's
        // pixels are half covered.
        RgbaImage image;
        image.width = 4;
        image.height = 2;
        image.rgba.assign(4 * 2 * 4, 0);
        for (int y = 0; y < 2; ++y)
            for (int x = 1; x < 4; ++x)
            {
                image.at(x, y)[0] = 200;
                image.at(x, y)[3] = 255;
            }
        const RgbaImage hard = resample(image, 2, 1);
        const RgbaImage soft = resample(image, 2, 1, true);
        check(hard.at(0, 0)[3] == 255 && soft.at(0, 0)[3] == 128,
              "a half-covered pixel is opaque when hard, half alpha when soft");
        check(soft.at(0, 0)[0] == 200, "and takes its colour from the covered half only");
        check(soft.at(1, 0)[3] == 255, "a fully covered pixel is opaque either way");
    }

    void testPackAndNames()
    {
        CookedTexture tex;
        tex.width = 16;
        tex.height = 32;
        tex.texels.assign(16 * 32 / 4, 0x11);
        tex.index.assign(16 * 32 / 8, 0);
        tex.palette.assign(6, 0x22); // three colours: padded to 8 bytes in the file
        for (std::size_t i = 0; i + 1 < tex.index.size(); i += 2)
            put16(tex.index, i, 0x0000); // mode 0 needs three colours
        const std::vector<std::uint8_t> bytes = packTexture(tex, hldata::kTextureColourKey, 0xABCD);
        check(bytes.size() == 32 + 128 + 64 + 8, "header, texels, index, padded palette");

        CookedTexture back;
        hldata::TextureHeader header;
        check(unpackTexture(bytes, back, header), "unpacks");
        check(back.width == 16 && back.height == 32 && back.texels == tex.texels &&
                  back.index == tex.index && back.palette == tex.palette,
              "round trip, with the padding dropped again");
        check(header.sourceHash == 0xABCD && header.flags == hldata::kTextureColourKey &&
                  header.paletteColours == 3,
              "header fields");

        auto bad = bytes;
        bad.pop_back();
        check(!unpackTexture(bad, back, header), "a truncated file is refused");
        bad = bytes;
        bad[8] = 20; // width 20: not a power of two
        check(!unpackTexture(bad, back, header), "a size the DS cannot use is refused");
        bad = bytes;
        bad[0] = 'X';
        check(!unpackTexture(bad, back, header), "wrong magic is refused");

        char name[40];
        hldata::textureFileName("{grate1", 64, 128, false, name);
        check(std::string(name) == "{grate1_64x128.htx", "file name carries the size");
        hldata::textureFileName("a*b?c:d", 16, 16, false, name);
        check(std::string(name) == "a_b_c_d_16x16.htx", "characters FAT refuses are replaced");
        hldata::textureFileName("sixteencharsname", 1024, 512, true, name);
        check(std::string(name) == "sixteencharsname_1024x512a.htx",
              "the longest name fits, with the A3I5 mark");
        hldata::textureFileName("{grate1", 64, 128, true, name);
        check(std::string(name) == "{grate1_64x128a.htx", "the A3I5 form has its own file");
    }

    void testBudget()
    {
        BspFile bsp;
        std::string error;
        BoxRoom room;
        room.externalTextureName = "unused";
        check(bsp.parse(buildBoxRoom(room), error), "box room parses");

        TexturePlan plan = planTextures(bsp);
        check(plan.fits && plan.usedTextures == 1 && plan.shrunk == 0, "one texture, in budget");
        check(plan.sizes[0].width == 16 && plan.sizes[0].height == 16 && plan.texels == 256,
              "at its default size");
        check(plan.sizes[1].width == 0 && plan.paletteColours[1] == 0,
              "a texture no face uses is not planned");
        check(plan.paletteColours[0] == 16, "256 texels get 16 colours");

        // Two used textures: a big one on five walls, a big one on one small
        // wall. Give the second face its own texinfo and shrink that face.
        bsp.textures[0].width = bsp.textures[0].height = 256; // cooks to 128 x 128
        bsp.textures[1].width = 256;
        bsp.textures[1].height = 128; // cooks to 128 x 64
        BspTexInfo second = bsp.texInfos[bsp.faces[5].texInfo];
        second.miptex = 1;
        bsp.texInfos.push_back(second);
        bsp.faces[5].texInfo = static_cast<std::int16_t>(bsp.texInfos.size() - 1);

        plan = planTextures(bsp);
        check(plan.usedTextures == 2 && plan.texels == 128 * 128 + 128 * 64, "both planned");

        // Room for the first and half the second: the one covering less wall
        // per texel gives way, along its longer side, and only as far as needed.
        plan = planTextures(bsp, 128 * 128 + 64 * 64);
        check(plan.fits && plan.shrunk == 1, "over budget: one texture shrinks");
        check(plan.sizes[0].width == 128 && plan.sizes[0].height == 128,
              "the texture on five walls is left alone");
        check(plan.sizes[1].width == 64 && plan.sizes[1].height == 64,
              "the one on a single wall is halved along its longer side");

        plan = planTextures(bsp, 100);
        check(!plan.fits && plan.sizes[0].width == 16 && plan.sizes[1].height == 16,
              "an impossible budget stops at the minimum size and says so");

        plan = planTextures(bsp, kWorldTexelBudget, 40);
        check(plan.colours <= 40 || !plan.fits, "the palette budget scales colours down");
        // 16384 and 8192 texels. At one colour per 16 texels both would sit at
        // the 512 ceiling (1024 in all); 800 forces one per 32: 512 and 256.
        plan = planTextures(bsp, kWorldTexelBudget, 800);
        check(plan.fits && plan.colours == 768 && plan.paletteColours[0] == 512 &&
                  plan.paletteColours[1] == 256,
              "a tight palette budget scales every texture, the bigger keeping more");
    }

    /// A texture that is all holes is left out of the plan, and with it the
    /// faces that wear it; the retail maps' "{blue" is one.
    void testHiddenTextures()
    {
        BoxRoom room;
        room.textureName = "{blue";
        BspFile bsp;
        std::string error;
        check(bsp.parse(buildBoxRoom(room), error), "parses");
        WadSet none;
        check(findHiddenTextures(bsp, none)[0] == 0, "a keyed texture with pixels is not hidden");

        std::fill(bsp.textures[0].pixels.begin(), bsp.textures[0].pixels.end(), 255);
        const std::vector<char> hidden = findHiddenTextures(bsp, none);
        check(hidden[0] == 1, "a keyed texture that is all key is hidden");
        const TexturePlan plan = planTextures(bsp, kWorldTexelBudget, kPaletteColourBudget, hidden);
        check(plan.usedTextures == 0 && plan.sizes[0].width == 0, "and left out of the plan");

        bsp.textures[0].name = "blue"; // no '{': 255 is a colour, not a hole
        check(findHiddenTextures(bsp, none)[0] == 0, "without the key prefix it is just a colour");

        RgbaImage empty;
        empty.width = empty.height = 16;
        empty.rgba.assign(16 * 16 * 4, 0);
        CookedTexture tex;
        PtexconvEncoder encoder("unused", fs::temp_directory_path() / "hlds_unused");
        check(!encoder.encode(empty, 16, TextureFormat::Tex4x4, tex, error) &&
                  error.find("transparent") != std::string::npos,
              "the encoder refuses an empty image instead of running the tool on it");
    }

    /// Mean squared error between two images, opaque pixels only; -1 if their
    /// alpha differs anywhere.
    double difference(const RgbaImage &a, const RgbaImage &b)
    {
        double sum = 0;
        int count = 0;
        for (int i = 0; i < a.width * a.height; ++i)
        {
            if (a.rgba[i * 4 + 3] != b.rgba[i * 4 + 3])
                return -1;
            if (a.rgba[i * 4 + 3] == 0)
                continue;
            for (int c = 0; c < 3; ++c)
            {
                const double d = double(a.rgba[i * 4 + c]) - b.rgba[i * 4 + c];
                sum += d * d;
            }
            ++count;
        }
        return count ? sum / (count * 3) : 0;
    }

    void testEncoder()
    {
        const fs::path exe = findPtexconv({});
        if (exe.empty())
        {
            std::printf("test_texture: ptexconv not found, encoder checks SKIPPED\n");
            return;
        }
        const fs::path dir = fs::temp_directory_path() / "hlds_test_texture";
        fs::remove_all(dir);
        PtexconvEncoder encoder(exe, dir / "tmp");
        std::string error;

        // A smooth gradient with a hole in one corner.
        RgbaImage image;
        image.width = 32;
        image.height = 32;
        image.rgba.resize(32 * 32 * 4);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
            {
                std::uint8_t *p = image.at(x, y);
                const bool hole = x < 8 && y < 8;
                p[0] = hole ? 0 : static_cast<std::uint8_t>(x * 8);
                p[1] = hole ? 0 : static_cast<std::uint8_t>(y * 8);
                p[2] = hole ? 0 : 128;
                p[3] = hole ? 0 : 255;
            }

        CookedTexture tex;
        check(encoder.encode(image, 64, TextureFormat::Tex4x4, tex, error),
              "ptexconv encodes a 32x32 texture");
        check(tex.width == 32 && tex.texels.size() == 256 && tex.index.size() == 128,
              "with the sizes the format dictates");
        check(tex.paletteColours() > 0 && tex.paletteColours() <= 64, "within the palette cap");

        RgbaImage decoded;
        check(decodeTexture(tex, decoded, error), "and what it wrote decodes");
        const double mse = difference(image, decoded);
        check(mse >= 0, "the hole comes back as a hole, exactly");
        check(mse >= 0 && mse < 150, "and the picture comes back recognisably (MSE < 150)");
        check(fs::is_empty(dir / "tmp"), "no temporary files are left behind");

        // The same picture as A3I5, with a soft edge to its hole.
        RgbaImage softImage = image;
        for (int y = 0; y < 32; ++y)
            softImage.at(8, y)[3] = 128;
        CookedTexture alphaTex;
        check(encoder.encode(softImage, 0, TextureFormat::A3I5, alphaTex, error),
              "ptexconv encodes an A3I5 texture");
        check(alphaTex.format == TextureFormat::A3I5 && alphaTex.texels.size() == 32 * 32 &&
                  alphaTex.index.empty() && alphaTex.paletteColours() == 32,
              "a byte a texel, 32 colours, no index piece");
        RgbaImage alphaBack;
        check(decodeTexture(alphaTex, alphaBack, error), "and it decodes");
        check(alphaBack.at(2, 2)[3] == 0 && alphaBack.at(20, 20)[3] == 255, "hole and solid");
        check(alphaBack.at(8, 20)[3] > 60 && alphaBack.at(8, 20)[3] < 200,
              "and the half-alpha column comes back half alpha");

        // The whole path for a map, twice: the second run must reuse the file.
        BspFile bsp;
        check(bsp.parse(buildBoxRoom(), error), "box room parses");
        const TexturePlan plan = planTextures(bsp);
        WadSet none;
        TextureCookReport report;
        check(cookTextures(bsp, none, plan, encoder, dir / "tex", report, error),
              "the box room's texture cooks");
        check(report.encoded == 1 && report.reused == 0 && report.missing.empty(), "one encoded");
        check(fs::exists(dir / "tex" / "testwall_16x16.htx"), "under its sized name");
        check(cookTextures(bsp, none, plan, encoder, dir / "tex", report, error) &&
                  report.encoded == 0 && report.reused == 1,
              "a second cook reuses it");

        // Change one source pixel: the hash differs, so it is made again.
        bsp.textures[0].pixels[0] ^= 1;
        check(cookTextures(bsp, none, plan, encoder, dir / "tex", report, error) &&
                  report.encoded == 1,
              "changed pixels are re-encoded");

        // A texture that is nowhere is reported, not fatal.
        BoxRoom room;
        room.textureName = "absent";
        std::vector<std::uint8_t> bytes = buildBoxRoom(room);
        check(bsp.parse(bytes, error), "parses");
        bsp.textures[0].embedded = false;
        check(cookTextures(bsp, none, planTextures(bsp), encoder, dir / "tex", report, error) &&
                  report.missing.size() == 1 && report.missing[0] == "absent",
              "a texture found nowhere is listed as missing");

        PtexconvEncoder broken(dir / "no-such-program", dir / "tmp");
        check(!broken.encode(image, 64, TextureFormat::Tex4x4, tex, error), "a missing encoder is an error");
        check(findPtexconv(dir / "no-such-program").empty(),
              "an explicit path that does not exist is not replaced by a guess");
        fs::remove_all(dir);
    }
} // namespace

int main()
{
    testIndexedAndResample();
    testDecoder();
    testA3I5Decoder();
    testSoftResample();
    testPackAndNames();
    testBudget();
    testHiddenTextures();
    testEncoder();
    if (g_failures == 0)
        std::printf("test_texture: ok\n");
    return g_failures == 0 ? 0 : 1;
}
