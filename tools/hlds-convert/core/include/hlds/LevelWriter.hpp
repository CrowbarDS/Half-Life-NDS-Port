#pragma once

// The cooked level as the converter holds it while building it, and the one
// function that turns it into the bytes of a .hlm file (shared/HlLevel.hpp).
//
// Nothing else in the converter knows the file layout: the cooker fills these
// vectors, and what the DS reads back is checked by running the DS's own reader
// (HlLevelReader.hpp) over the result before it is ever written to the card.

#include <cstdint>
#include <string>
#include <vector>

#include "HlLevel.hpp"

namespace hlds
{
    struct CookedLevel
    {
        std::int16_t mins[3] = {0, 0, 0};
        std::int16_t maxs[3] = {0, 0, 0};
        std::uint16_t visLeafCount = 0;
        std::uint16_t worstPolygons = 0;
        std::uint16_t worstVertices = 0;
        std::uint32_t farUnits = 0; ///< LevelHeader::farUnits
        std::uint32_t worstTexels = 0;
        std::uint16_t skyTexture = 0; ///< LevelHeader::skyTexture
        std::uint16_t viewModel = 0;  ///< LevelHeader::viewModel

        std::vector<hldata::Plane> planes;
        std::vector<std::uint8_t> planeTypes;
        std::vector<hldata::Node> nodes;
        std::vector<hldata::Leaf> leaves;
        std::vector<std::uint16_t> marks;
        std::vector<std::uint16_t> nodeMarks;      ///< Section::NodeMarks
        std::vector<hldata::NodeSpan> nodeSpans;   ///< Section::NodeSpans
        std::vector<std::uint8_t> vis;
        std::vector<hldata::Surface> surfaces;
        std::vector<std::uint32_t> gxStream;
        std::vector<hldata::Model> models;
        std::vector<hldata::ClipNode> clipNodes;
        std::vector<hldata::TextureRef> textures;
        std::vector<hldata::EntityRecord> entities;
        std::string entityText;
        std::vector<hldata::Brush> brushes;
        std::vector<std::uint16_t> brushLeaves;
        std::vector<hldata::LogicRecord> logic;
        std::vector<hldata::LogicAux> logicAux;
        std::vector<hldata::PathCorner> corners;
        std::vector<std::uint32_t> nameHashes = {0};
        std::vector<hldata::Landmark> landmarks;
        std::vector<char> strings = {'\0'};
        std::vector<hldata::ModelRef> modelRefs;
        std::vector<hldata::ActorRecord> actors;
        /// The game's families (familyBit) of those who may come in from a
        /// neighbouring map, besides the actors' own (LevelHeader::families).
        std::uint8_t visitorFamilies = 0;
        std::vector<hldata::PathNode> pathNodes;
        std::vector<hldata::PathLink> pathLinks;
        std::vector<hldata::SoundRef> sounds;
        std::vector<hldata::LogicSound> logicSounds;
        std::vector<hldata::SentenceRecord> sentences;
        std::vector<hldata::SentenceWord> sentenceWords;
        std::vector<hldata::RoomRecord> rooms;
        std::vector<std::uint16_t> speech;
        std::vector<hldata::MessageRecord> messages;
        std::vector<hldata::ModelSound> modelSounds;
        std::vector<hldata::LightPatch> lightPatches;
        std::vector<hldata::SpriteRef> sprites;
        /// Not written: the game's path of each of `sprites`, and whether it
        /// is drawn as light (Converter cooks them and fills `sprites` in).
        std::vector<std::string> spritePaths;
        std::vector<bool> spriteLights;
        /// Not written: what `sounds` are made from (the game's own paths,
        /// and which loop), for whoever cooks the sound files and settles
        /// what each is called on the card (Converter).
        std::vector<std::string> soundPaths;
        std::vector<bool> soundLoops;
    };

    /// Serialize, then open the result with the DS's reader. Fails -- with the
    /// reader's own reason -- rather than return a file the console would
    /// refuse.
    bool writeLevel(const CookedLevel &level, std::vector<std::uint8_t> &out,
                    std::string &error);
} // namespace hlds
