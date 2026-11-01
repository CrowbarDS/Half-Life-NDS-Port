// The game's films (hlds/AviFile.hpp): run-length and Cinepak pictures made
// up here, decoded as they say; and, with HLDS_VALVE_DIR set, the game's own
// valve.avi and logo.avi read whole. With HLDS_DUMP set to a folder, some of
// their pictures are written there (PPM) to be looked at.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "TestBspBuilder.hpp"
#include "hlds/AviFile.hpp"

using namespace hlds;
using namespace hldstest;

namespace
{
    void testRle()
    {
        // A palette of three: black, red, green (BGRA).
        std::uint8_t palette[256 * 4] = {};
        palette[1 * 4 + 2] = 255; // red
        palette[2 * 4 + 1] = 255; // green
        // 4x2, bottom row first: a run of 3 red and an absolute pair; end of
        // row; then a move on by 2 and 2 green; end of picture.
        const std::vector<std::uint8_t> data = {3, 1, 0, 3, 2, 1, 2, 0, /*pad*/ 0, 0,
                                                0, 2, 2, 0, 2, 2, 0, 1};
        RgbaImage frame;
        std::string error;
        // (The absolute run is 3 bytes: 2 1 2, padded to a word with one 0.)
        const std::vector<std::uint8_t> fixed = {3, 1, 0, 3, 2, 1, 2, 0, 0, 0, 0, 2, 2, 0, 2, 2, 0, 1};
        (void)data;
        check(decodeRle8(fixed.data(), fixed.size(), 4, 2, palette, frame, error), "a run-length picture decodes");
        // Bottom row (y 1): red red red, then green (the first of 2 1 2 is
        // past the row's end at x 3... no: x 3 is green, then 1, 2 off it).
        check(frame.at(0, 1)[0] == 255 && frame.at(2, 1)[0] == 255 && frame.at(3, 1)[1] == 255,
              "... runs and pixels as they are, the bottom row first");
        // Top row (y 0): moved on 2 across (and 0 down): 2 green at x 2, 3.
        check(frame.at(0, 0)[0] == 0 && frame.at(1, 0)[1] == 0 && frame.at(2, 0)[1] == 255 &&
                  frame.at(3, 0)[1] == 255,
              "... and a move on leaves what it passes");
    }

    void testCinepak()
    {
        // One strip, 8x4: a V1 codebook of two entries (grey, by the full
        // 6-byte form), then a key picture of two V1 blocks.
        std::vector<std::uint8_t> v;
        auto be16 = [&](int x) { v.push_back(static_cast<std::uint8_t>(x >> 8)); v.push_back(static_cast<std::uint8_t>(x)); };
        auto be24 = [&](int x) { v.push_back(static_cast<std::uint8_t>(x >> 16)); be16(x & 0xFFFF); };
        const std::vector<std::uint8_t> book = {0x22, 0, 0, 4 + 12, 10, 20, 30, 40, 0, 0, 200, 200, 200, 200, 0, 0};
        const std::vector<std::uint8_t> blocks = {0x32, 0, 0, 4 + 2, 0, 1};
        const int strip = 12 + static_cast<int>(book.size() + blocks.size());
        v.push_back(0); // flags
        be24(10 + strip);
        be16(8);
        be16(4);
        be16(1);
        v.push_back(0x10); // a key strip
        be24(strip);
        be16(0);
        be16(0);
        be16(4);
        be16(8);
        v.insert(v.end(), book.begin(), book.end());
        v.insert(v.end(), blocks.begin(), blocks.end());
        CinepakState state;
        RgbaImage frame;
        std::string error;
        check(decodeCinepak(v.data(), v.size(), 8, 4, state, frame, error), "a Cinepak picture decodes");
        check(frame.at(0, 0)[0] == 10 && frame.at(1, 1)[0] == 10 && frame.at(2, 0)[0] == 20 &&
                  frame.at(0, 2)[0] == 30 && frame.at(3, 3)[0] == 40,
              "... a V1 block: each of its four brightnesses over two by two");
        check(frame.at(4, 0)[0] == 200 && frame.at(7, 3)[1] == 200, "... and the next block, its own");
    }

    void dump(const RgbaImage &frame, const std::string &path)
    {
        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << frame.width << " " << frame.height << "\n255\n";
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x)
                out.write(reinterpret_cast<const char *>(frame.at(x, y)), 3);
    }

    void testReal(const char *dir)
    {
        const char *dumpTo = std::getenv("HLDS_DUMP");
        for (const char *name : {"valve.avi", "logo.avi"})
        {
            std::ifstream in(std::string(dir) + "/media/" + name, std::ios::binary);
            if (!in)
            {
                std::printf("test_avi: no %s\n", name);
                continue;
            }
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            AviFile avi;
            std::string error;
            const bool ok = avi.load(bytes, error);
            std::printf("test_avi: %s: %dx%d, %zu pictures at %u us, %zu samples at %d Hz%s%s\n", name, avi.width,
                        avi.height, avi.frames.size(), avi.frameMicros, avi.samples.size(), avi.sampleRate,
                        ok ? "" : ": ", error.c_str());
            check(ok, (std::string("the game's ") + name + " is read").c_str());
            if (!ok)
                continue;
            // Not all black: something is drawn in the middle of it.
            const RgbaImage &middle = avi.frames[avi.frames.size() / 2];
            long lit = 0;
            for (std::size_t i = 0; i < middle.rgba.size(); i += 4)
                lit += middle.rgba[i] + middle.rgba[i + 1] + middle.rgba[i + 2] > 60 ? 1 : 0;
            check(lit > 200, (std::string("... and has a picture in it: ") + name).c_str());
            if (std::string(name) == "valve.avi")
                check(avi.width == 640 && avi.height == 480 && avi.frames.size() == 150 && avi.sampleRate == 22050 &&
                          avi.samples.size() == 220500,
                      "valve.avi: 640x480, 150 pictures, ten seconds of sound");
            else
                check(avi.width == 640 && avi.height == 100 && avi.frames.size() == 110, "logo.avi: 640x100, 110 pictures");
            if (dumpTo != nullptr)
                for (std::size_t f = 0; f < avi.frames.size(); f += avi.frames.size() / 4)
                    dump(avi.frames[f], std::string(dumpTo) + "/" + name + "_" + std::to_string(f) + ".ppm");
        }
    }
} // namespace

int main()
{
    testRle();
    testCinepak();
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir != nullptr && dir[0] != '\0')
        testReal(dir);
    else
        std::printf("test_avi: HLDS_VALVE_DIR not set, the game's films were not read\n");
    if (g_failures == 0)
        std::printf("test_avi: ok\n");
    return g_failures == 0 ? 0 : 1;
}
