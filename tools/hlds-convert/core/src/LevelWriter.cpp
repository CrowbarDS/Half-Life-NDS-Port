#include "hlds/LevelWriter.hpp"

#include <cstring>

#include "HlDsiGx.hpp"
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
        // The sounds: the common ones by their rate alone (their names are
        // made again from their paths), the map's own whole. A common one
        // not called what its path and rate make is the converter's mistake.
        const std::size_t commonCount = static_cast<std::size_t>(CommonSound::Count);
        std::vector<CommonSoundRef> commonSounds(commonCount, CommonSoundRef{0, 0});
        std::vector<SoundRef> ownSounds;
        for (std::size_t i = 0; i < level.sounds.size(); ++i)
        {
            const SoundRef &ref = level.sounds[i];
            if (i >= commonCount)
            {
                ownSounds.push_back(ref);
                continue;
            }
            if (ref.name[0] == '\0')
                continue;
            const char *dot = std::strrchr(ref.name, '.');
            int rate = 0;
            const char *at = dot != nullptr ? dot + 1 : ref.name;
            while (*at >= '0' && *at <= '9')
                rate = rate * 10 + (*at++ - '0');
            const bool loops = *at == 'l';
            char made[sizeof(SoundRef::name)];
            if (dot != nullptr && rate > 0 && rate <= 99)
                soundCardName(commonSoundPath(static_cast<CommonSound>(i)), rate, loops, made);
            if (dot == nullptr || rate <= 0 || rate > 99 || std::strcmp(made, ref.name) != 0)
            {
                error = std::string("common sound ") + std::to_string(i) + " is cooked as \"" + ref.name +
                        "\", not as its path names it";
                return false;
            }
            commonSounds[i] = CommonSoundRef{static_cast<std::uint8_t>(rate | (loops ? kCommonSoundLoops : 0)), ref.twin};
        }

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
            blobOf(ownSounds),
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
            blobOf(commonSounds),
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

    std::vector<std::uint8_t> dsiGxBytes(const CookedLevel &level, std::uint32_t levelCrc)
    {
        if (level.dsiFine.empty())
            return {};
        hldata::DsiGxHeader header{};
        std::memcpy(header.magic, hldata::kDsiGxMagic, sizeof(header.magic));
        header.version = hldata::kDsiGxVersion;
        header.count = static_cast<std::uint16_t>(level.dsiFine.size());
        header.levelCrc = levelCrc;
        std::vector<hldata::DsiGxSurface> table;
        std::vector<std::uint32_t> words;
        for (const CookedLevel::FineSurface &entry : level.dsiFine)
        {
            hldata::DsiGxSurface s{};
            s.surface = entry.surface;
            s.words = static_cast<std::uint16_t>(entry.words.size());
            s.polygons = static_cast<std::uint16_t>(entry.polygons);
            s.vertices = static_cast<std::uint16_t>(entry.vertices);
            s.ofs = static_cast<std::uint32_t>(words.size());
            table.push_back(s);
            words.insert(words.end(), entry.words.begin(), entry.words.end());
        }
        header.words = static_cast<std::uint32_t>(words.size());
        std::vector<std::uint8_t> bytes(sizeof(header) + table.size() * sizeof(hldata::DsiGxSurface) + words.size() * 4);
        std::memcpy(bytes.data(), &header, sizeof(header));
        std::memcpy(bytes.data() + sizeof(header), table.data(), table.size() * sizeof(hldata::DsiGxSurface));
        std::memcpy(bytes.data() + sizeof(header) + table.size() * sizeof(hldata::DsiGxSurface), words.data(),
                    words.size() * 4);
        return bytes;
    }
} // namespace hlds
