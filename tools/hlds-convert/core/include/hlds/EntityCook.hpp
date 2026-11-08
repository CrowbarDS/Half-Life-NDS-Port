#pragma once

// The map's entities that DO something, turned into logic records.
//
// On the PC an entity is a bag of text keys that the game parses when the map
// loads and keeps consulting as it runs. On the DS none of that happens: this
// reads the keys once, here, and writes down what they amount to -- a door's
// travel as a vector, a wait as milliseconds, a name as a number -- so that
// the console only ever sees fixed records (LogicRecord, shared/HlLevel.hpp)
// and the code that runs them (shared/HlLogic.hpp) never touches a string.
//
// Classes this does not know are COUNTED, not refused. A map with monsters and
// sound entities still cooks; the count is how far the port has to go.

#include <map>
#include <string>
#include <vector>

#include "HlLevel.hpp"
#include "hlds/SoundCook.hpp"
#include "hlds/TitlesFile.hpp"
#include "hlds/BrushInfo.hpp"
#include "hlds/BspFile.hpp"
#include "hlds/EntityParser.hpp"

namespace hlds
{
    struct LogicCook
    {
        std::vector<hldata::LogicRecord> records;
        std::vector<hldata::LogicAux> aux;
        std::vector<hldata::PathCorner> corners;
        std::vector<std::uint32_t> nameHashes; ///< by name id
        std::vector<hldata::Landmark> landmarks;
        std::vector<char> strings;
        std::vector<hldata::ModelRef> modelRefs;
        std::vector<hldata::ActorRecord> actors;
        /// The sounds the map plays, as the game's own paths under sound/
        /// ("doors/doormove2.wav", lower-case); empty where the map has no
        /// use for a common sound. The index is the index in the level's
        /// Sounds. `soundLoops` says which go round.
        std::vector<std::string> sounds;
        std::vector<bool> soundLoops;
        /// ... and which are a weapon's the map has not (`lazyModels`): read
        /// when first played, out of the common archive, not the map's.
        std::vector<bool> soundLazy;
        std::vector<hldata::LogicSound> logicSounds; ///< parallel to `records`
        /// The sprites the map draws, as the game's own paths
        /// ("sprites/flare1.spr", lower-case), the common ones first; and
        /// whether each is drawn as light, whatever its file says.
        std::vector<std::string> sprites;
        std::vector<bool> spriteLights;
        std::vector<hldata::SentenceRecord> sentences;
        std::vector<hldata::SentenceWord> sentenceWords;
        std::vector<hldata::RoomRecord> rooms;
        /// What soldiers call out, if the map has any: kSpeechGroups *
        /// kSpeechLines indices into `sentences` (0xFFFF: none).
        std::vector<std::uint16_t> speech;
        /// What the map writes on the screen (their text is in `strings`).
        std::vector<hldata::MessageRecord> messages;
        /// By entity index: its logic record, or -1.
        std::vector<int> recordOfEntity;
        int names = 0; ///< distinct target names
        /// Classes with no logic record and no other handling, with how many
        /// entities of each the map has.
        std::map<std::string, int> unsupported;
    };

    /// The name, in a multiplayer map's models, of the one the other
    /// players are seen as.
    inline constexpr const char *kPlayerModelName = "player";

    /// Whether the map is one for a game between consoles: it has places
    /// for players to come in (info_player_deathmatch).
    bool isMultiplayerMap(const std::vector<Entity> &entities);

    /// In a multiplayer map, what lies about of weapons the port has not got
    /// yet (the rocket launcher, the gauss gun, the crossbow, satchels...)
    /// and of their ammunition is made into something it has, so that the
    /// map has its full count of things to pick up: the entity's class is
    /// changed, here, before anything is cooked from it. Returns how many
    /// were. Other maps are left as they are.
    int standInEntities(std::vector<Entity> &entities);
    /// A map that begins in the dark (worldspawn's "startdark") is given
    /// what makes it so: a fade from black, held first, set off once as the
    /// map begins.
    void addStartDark(std::vector<Entity> &entities);
    /// The track a map begins with (worldspawn's "sounds"): a music record
    /// fired once as the map starts.
    void addStartMusic(std::vector<Entity> &entities);

    /// A model a map needs, and the sequences of it the map plays.
    struct ModelNeed
    {
        std::string name; ///< lower-case, no directory or extension: "scientist"
        std::string file; ///< as the game names it: "models/scientist.mdl"
        std::vector<std::string> clips; ///< sequence labels, lower-case
        /// What a player holds (HlWeaponBundle.hpp): baked to one piece,
        /// relative to the hand.
        bool held = false;
        bool firstSequence = false;     ///< ... and whichever its first sequence is
        /// ... and every sequence the game's code may choose by what it is for
        /// (its activity): a monster that thinks as the game's do. On a DSi
        /// only (a .dsi.hmd) when `dsActivities` says what the DS keeps.
        bool activities = false;
        /// What a DS keeps of those: for each of these activities the
        /// heaviest clip, unless `clips` has one for it already. (What is left
        /// out the game's code goes without: a plain death for one by where
        /// the blow landed, the turn on the spot for a turn clip.)
        std::vector<std::uint8_t> dsActivities;
    };

    class MdlFile;

    /// The clips of `mdl` that `need` asks for: what a DS's file has (`ds`),
    /// and a DSi's (`dsi`) -- the DS's first, then the rest of those for an
    /// activity. Equal when there is nothing more for a DSi.
    void modelClips(const MdlFile &mdl, const ModelNeed &need, std::vector<std::string> &ds,
                    std::vector<std::string> &dsi);

    /// What the map's entities need by way of models: every model some actor
    /// is made of, with the clips those actors stand in and are scripted to
    /// play. Nothing is read from the models themselves.
    std::vector<ModelNeed> collectModelNeeds(const std::vector<Entity> &entities);

    /// Add to `needs` what the actors who may ARRIVE from a neighbouring map
    /// need: those with a name who stand near the landmark called `landmark`
    /// there come along when the player crosses (the guard who opens the
    /// tram's door at the station is waiting in the map before it), and this
    /// map must have their models, with the clips its scripts have them play.
    void addVisitorNeeds(const std::vector<Entity> &entities, const std::vector<Entity> &neighbour,
                         const std::string &landmark, std::vector<ModelNeed> &needs);

    /// Add to `needs` what a multiplayer map needs besides: the model the
    /// other players are seen as (kPlayerModelName), with what a body does
    /// whole. Nothing, for any other map.
    void addMultiplayerNeeds(const std::vector<Entity> &entities, std::vector<ModelNeed> &needs);

    /// Add to `needs` the spent casings out of the guns (the game's
    /// EjectBrass: models/shell.mdl and shotgunshell.mdl), where something
    /// fires them: the player, if `playerArmed` (the pistol, the MP5 or the
    /// shotgun may be in hand), or a soldier or an assassin among `needs`
    /// (the map's own, or one come from next door).
    void addCasingNeeds(bool playerArmed, std::vector<ModelNeed> &needs);

    /// ... and the grenades in flight, as the game's models of them: a hand
    /// grenade (models/w_grenade.mdl, with the "toss" it spins through in
    /// the air and the "roll1" it rolls with) where the player may have one
    /// (`playerGrenades`), and the launcher's (models/grenade.mdl) where the
    /// MP5 may be in hand (`playerLauncher`); both where a soldier is.
    void addGrenadeNeeds(bool playerGrenades, bool playerLauncher, std::vector<ModelNeed> &needs);

    /// `models` are the names of the models that were cooked for this map
    /// (ModelNeed::name), in the order they will be in the level: an actor
    /// whose model is not among them is left out.
    /// `brushRecordOfEntity` gives, by entity index, the Brush record the
    /// cooker made for that entity, or -1.
    /// `book` is the game's sentences.txt, if it was there to be read: the
    /// lines the map speaks are looked up in it. `titles` is its titles.txt,
    /// likewise: what the map's messages say and how they are shown.
    /// `chapter` is the name, in titles.txt, of the title of the chapter the
    /// map is part of (Converter works it out: chapterOf); empty if unknown.
    void cookLogic(const BspFile &bsp, const std::vector<Entity> &entities,
                   const std::vector<int> &brushRecordOfEntity,
                   const std::vector<std::string> &models, LogicCook &out,
                   const SentenceBook *book = nullptr, const TitlesBook *titles = nullptr,
                   const std::string &chapter = std::string(),
                   const std::vector<std::string> &lazyModels = {});

    /// How far a door or button travels: the engine's formula, from the
    /// direction it moves in, the size of its brush and its "lip".
    void moverTravel(const double *direction, const double *size, double lip, double *out);

    /// An entity's angles (pitch, yaw, roll; degrees) as the engine reads
    /// them: "angle" N is (0, N, 0), and of it and "angles" the one written
    /// last is what counts.
    void entityAngles(const Entity &entity, double *out);

    /// The direction an entity's "angles" / "angle" key means for a mover: -1
    /// is up and -2 is down, anything else is a compass heading.
    void moverDirection(const Entity &entity, double *out);
} // namespace hlds
