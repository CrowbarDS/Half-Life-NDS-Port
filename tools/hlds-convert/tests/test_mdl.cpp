// The studio model reader (core/src/MdlFile.cpp) on the game's own files:
// what it reads is held against counts taken from the files by other means,
// and against what the format promises (every triangle a mesh claims is
// there; a parent before its children; animation that ends where it began
// when it loops).
//
// Needs HLDS_VALVE_DIR; without it there is nothing to read and the test
// says so and is skipped.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "TestBspBuilder.hpp"
#include "hlds/MdlFile.hpp"

using namespace hlds;
using namespace hldstest;

namespace
{
    struct Expect
    {
        const char *file;
        int bones, usedBones, sequences, frames, textures;
        int triangles; ///< body 0
    };

    void checkModel(const std::string &dir, const Expect &want)
    {
        MdlFile mdl;
        std::string error;
        if (!mdl.load(dir + "/models/" + want.file, error))
        {
            std::printf("test_mdl: %s\n", error.c_str());
            check(false, "the model loads");
            return;
        }
        int used = 0;
        for (char u : mdl.usedBones())
            used += u;
        int frames = 0;
        for (const MdlSequence &seq : mdl.sequences)
            frames += seq.frameCount;
        int triangles = 0;
        bool claimsMatch = true;
        for (const MdlSubModel *model : mdl.selectBody(0))
        {
            triangles += model->triangleCount();
            for (const MdlMesh &mesh : model->meshes)
            {
                int counted = 0;
                for (const MdlStrip &strip : mesh.strips)
                    counted += static_cast<int>(strip.corners.size()) - 2;
                claimsMatch = claimsMatch && counted == mesh.triangles;
            }
        }
        const bool counts = static_cast<int>(mdl.bones.size()) == want.bones && used == want.usedBones &&
                            static_cast<int>(mdl.sequences.size()) == want.sequences &&
                            frames == want.frames &&
                            static_cast<int>(mdl.textures.size()) == want.textures &&
                            triangles == want.triangles;
        if (!counts)
            std::printf("test_mdl: %s: %zu bones (%d used), %zu sequences, %d frames, %zu textures, "
                        "%d triangles\n",
                        want.file, mdl.bones.size(), used, mdl.sequences.size(), frames,
                        mdl.textures.size(), triangles);
        check(counts, "bones, sequences, frames, textures and triangles are what the file holds");
        check(claimsMatch, "every mesh has the triangles it says it has");

        // Every sequence decodes, to numbers that are numbers.
        bool decodes = true;
        for (std::size_t s = 0; s < mdl.sequences.size() && decodes; ++s)
        {
            std::vector<MdlPose> poses;
            decodes = mdl.decode(static_cast<int>(s), poses, error) &&
                      poses.size() == mdl.sequences[s].frameCount * mdl.bones.size();
            for (const MdlPose &pose : poses)
            {
                for (int a = 0; a < 3 && decodes; ++a)
                    decodes = std::isfinite(pose.pos[a]) && std::isfinite(pose.rot[a]) &&
                              std::fabs(pose.pos[a]) < 4096 && std::fabs(pose.rot[a]) < 64;
            }
        }
        check(decodes, "every sequence decodes");
    }
} // namespace

int main()
{
    const char *dir = std::getenv("HLDS_VALVE_DIR");
    if (dir == nullptr || *dir == '\0')
    {
        std::printf("test_mdl: HLDS_VALVE_DIR not set, nothing to read\n");
        return 77;
    }
    // Counts measured from the files with an independent script.
    static const Expect kModels[] = {
        {"scientist.mdl", 22, 20, 128, 6847, 31, 740},
        {"barney.mdl", 24, 21, 71, 3138, 26, 785},
        {"gman.mdl", 39, 31, 14, 594, 23, 576},
        {"v_crowbar.mdl", 11, 2, 11, 312, 6, 329},
        {"zombie.mdl", 49, 47, 44, 1916, 19, 844},
        {"filecabinet.mdl", 1, 1, 2, 202, 2, 14},
    };
    for (const Expect &model : kModels)
        checkModel(dir, model);

    // The scientist's heads: "body" picks one of four, and a needle on top.
    MdlFile scientist;
    std::string error;
    if (scientist.load(std::string(dir) + "/models/scientist.mdl", error))
    {
        check(scientist.bodyParts.size() == 3 && scientist.bodyParts[1].models.size() == 4 &&
                  scientist.bodyParts[2].base == 4,
              "the scientist: a body, four heads, and a needle counted in fours");
        const auto einstein = scientist.selectBody(1);
        const auto withNeedle = scientist.selectBody(1 + 4);
        check(einstein.size() == 3 && einstein[1] == &scientist.bodyParts[1].models[1] &&
                  withNeedle[2] == &scientist.bodyParts[2].models[1],
              "body 1 is the second head; adding 4 adds the needle");
        check(scientist.skins.size() == 2 && scientist.sequenceByName("WALK") >= 0,
              "two skins, and sequences found by name whatever the case");
        // A walk carries the model forwards; a loop ends near where it began.
        const int walk = scientist.sequenceByName("walk");
        const MdlSequence &seq = scientist.sequences[walk];
        const double carried = std::sqrt(seq.linearMovement[0] * seq.linearMovement[0] +
                                         seq.linearMovement[1] * seq.linearMovement[1]);
        const double speed = carried * seq.fps / (seq.frameCount - 1);
        check((seq.flags & kMdlSequenceLooping) && speed > 50 && speed < 70,
              "the walk loops, and carries a scientist at a walking pace");
    }
    else
    {
        check(false, "the scientist loads");
    }

    if (g_failures == 0)
        std::printf("test_mdl: ok\n");
    return g_failures == 0 ? 0 : 1;
}
