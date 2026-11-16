#include "hlds/LevelWriter.hpp"

#include <cstring>

#include "HlLevelReader.hpp"

namespace hlds
{
    using namespace hldata;

    namespace
    {
        struct Blob
        {
            const void *data;
            std::size_t size;
            std::size_t count;
        };

        template <class T>
        Blob blobOf(const std::vector<T> &items)
        {
            return Blob{items.data(), items.size() * sizeof(T), items.size()};
        }
    } // namespace

    bool writeLevel(const CookedLevel &level, std::vector<std::uint8_t> &out, std::string &error)
    {
        // Indexed by Section: the order here IS the enum's order.
        const Blob blobs[kSectionCount] = {
            blobOf(level.planes),
            blobOf(level.planeTypes),
            blobOf(level.nodes),
            blobOf(level.leaves),
            blobOf(level.marks),
            blobOf(level.vis),
            blobOf(level.surfaces),
            blobOf(level.gxStream),
            blobOf(level.models),
            blobOf(level.clipNodes),
            blobOf(level.textures),
            blobOf(level.entities),
            // (The map's entity text is the converter's own, and stays in
            // the stage beside the level, <map>.ent: the console has no
            // use for it, and used to carry it in memory all the same.)
            Blob{nullptr, 0, 0},
            blobOf(level.lightPatches),
            blobOf(level.brushes),
            blobOf(level.brushLeaves),
            blobOf(level.logic),
            blobOf(level.logicAux),
            blobOf(level.corners),
            blobOf(level.nameHashes),
            blobOf(level.landmarks),
            blobOf(level.strings),
            blobOf(level.modelRefs),
            blobOf(level.actors),
            blobOf(level.pathNodes),
            blobOf(level.pathLinks),
            blobOf(level.sounds),
            blobOf(level.logicSounds),
            blobOf(level.sentences),
            blobOf(level.sentenceWords),
            blobOf(level.rooms),
            blobOf(level.sprites),
            blobOf(level.speech),
            blobOf(level.messages),
            blobOf(level.modelSounds),
            blobOf(level.nodeMarks),
            blobOf(level.nodeSpans),
        };

        SectionDir dir[kSectionCount];
        std::size_t offset = kLevelDataStart;
        for (int i = 0; i < kSectionCount; ++i)
        {
            dir[i].offset = static_cast<std::uint32_t>(offset);
            dir[i].size = static_cast<std::uint32_t>(blobs[i].size);
            dir[i].count = static_cast<std::uint32_t>(blobs[i].count);
            offset += (blobs[i].size + 3) & ~std::size_t(3);
        }
        if (offset > 0x7FFFFFFFu)
        {
            error = "the cooked level is impossibly large";
            return false;
        }

        out.assign(offset, 0);
        std::memcpy(out.data() + sizeof(LevelHeader), dir, sizeof(dir));
        for (int i = 0; i < kSectionCount; ++i)
        {
            if (blobs[i].size != 0)
                std::memcpy(out.data() + dir[i].offset, blobs[i].data, blobs[i].size);
        }

        LevelHeader header{};
        std::memcpy(header.magic, kLevelMagic, sizeof(header.magic));
        header.version = kLevelVersion;
        header.minReader = kLevelMinReader;
        header.sectionCount = kSectionCount;
        header.fileSize = static_cast<std::uint32_t>(out.size());
        for (int i = 0; i < 3; ++i)
        {
            header.mins[i] = level.mins[i];
            header.maxs[i] = level.maxs[i];
        }
        header.visLeafCount = level.visLeafCount;
        header.worstPolygons = level.worstPolygons;
        header.worstVertices = level.worstVertices;
        header.farUnits = level.farUnits;
        header.worstTexels = level.worstTexels;
        header.skyTexture = level.skyTexture;
        header.viewModel = level.viewModel;
        header.families = static_cast<std::uint8_t>(familiesOfActors(level.actors) | level.visitorFamilies);
        header.crc32 = crc32(out.data() + sizeof(LevelHeader),
                             static_cast<std::uint32_t>(out.size() - sizeof(LevelHeader)));
        std::memcpy(out.data(), &header, sizeof(header));

        LevelView view;
        const LevelError result = view.open(out.data(), static_cast<std::uint32_t>(out.size()));
        if (result != LevelError::Ok)
        {
            error = std::string("the cooked level does not pass the DS reader: ") +
                    levelErrorText(result);
            out.clear();
            return false;
        }
        return true;
    }
} // namespace hlds
