#pragma once

// The cooked level: the file the converter writes for each map and the DS
// reads, <card>/_nds/hl_nds/data/maps/<map>.hlm.
//
// THE SHAPE. A fixed header, a directory of sections, then the sections: each
// one a flat array of fixed-size records, 4-byte aligned. The DS reads the
// whole file into one heap block with one fread and points into it -- there is
// no parsing, no per-record allocation and nothing to free but the block.
// (yas is used for the manifest and the saves, where the data is a graph. A
// level is tables, and tables are what this is.)
//
// THE CONTRACT. LevelView::open() (HlLevelReader.hpp) checks every index a
// record holds into another section before anything uses it, the same promise
// the converter's BspFile makes about a .bsp. The renderer and the hull trace
// run in ITCM with no bounds checks; they can because of that one pass.
//
// VERSIONING. A level file says two things of itself, and this build knows
// three of its own (the "min and target" of it):
//
//   file   version     the format it was written in (the target of the
//                      converter that cooked it)
//   file   minReader   the oldest build that can read it: a change a build
//                      from before it cannot make sense of raises this to the
//                      new version; one it can (an unused field filled in) does
//                      not
//   build  kLevelVersion      the newest format this build writes and knows
//   build  kLevelMinVersion   the oldest format this build still reads
//   build  kLevelSaveSince    the oldest format whose SAVES this build still
//                      accepts (HlSave.hpp): what a save holds of a map is
//                      indices into its records, so only a change to those
//                      moves it
//
// Bump kLevelVersion on ANY change to a record or to what a field means.
// Then raise kLevelMinVersion (and kLevelMinReader) only if the old layout is
// no longer read, and kLevelSaveSince only if saves of the old one would no
// longer find what they point at. A file outside what a build reads is
// refused with "convert again": the source of truth is the player's copy of
// the game and re-cooking is cheap.
//
// Coordinates keep Half-Life's axes (X forward, Y left, Z up). The swap to the
// geometry engine's axes lives in the view matrix, not in the data.
//
// Formats named in comments (Q12, Q30, ...) are defined in HlFixed.hpp.

#include <array>
#include <cstdint>

#include "HlFixed.hpp"
#include "HlTexture.hpp"

namespace hldata
{
    inline constexpr char kLevelMagic[4] = {'H', 'L', 'D', 'S'};
    inline constexpr std::uint16_t kLevelVersion = 52;
    /// The oldest level format this build reads. (49: a map's textures are
    /// stored in its archive as they are, never compressed, so that one that
    /// streams is read straight in during play. 50: each texture's record
    /// says how many colours its palette has, which is what a streamed one
    /// is given of palette memory; the record is longer. The maps of a
    /// converter before are refused, to be converted again.)
    inline constexpr std::uint16_t kLevelMinVersion = 50;
    /// The oldest build that can read what it writes. (32: what the people
    /// say, after the soldiers' table in Section::Speech, which a build
    /// before would refuse; 33: the round 3 monsters, kinds a build before
    /// has not got; 34: round 4's, the same; 35: round 5's, and the
    /// tanks, mortar fields and wheels; 36: Xen's gravity volumes; 37: the
    /// shotgun shell's ring among the common sounds; 38: no surface across two
    /// leaves, the map's far distance in the header, transition volumes; 39:
    /// the counters, cameras, speakers, gun targets and the rest of round
    /// 39's kinds, Xen's plants, and three items; 40: func_friction's
    /// volumes, and beams that throw sparks. 41 -- a pendulum's damping and
    /// its going back, which a build of 40 reads as none -- and 42 -- the
    /// chargers' own sounds, which it does not play -- do not raise it. 43:
    /// a body falling among the common sounds, and the view models a map
    /// was not cooked with, named in its models to be loaded when needed.)
    /// 44: each node's own surfaces (NodeMarks, NodeSpans), as two sections
    /// a build before does not know of. 45: the long jump module, an item a
    /// build before would refuse. (46, how a box floats, 47, a tank's
    /// sprites, and 48, how a monster_generic is drawn, are in fields 45's
    /// builds leave alone: they read them.) 50: a texture's record is longer
    /// (its palette's colours), which a build before cannot read. (51: a
    /// spark that is switched says so, which a build of 50 reads as one it
    /// may switch anyway. 52: a spark's six sounds, in extra, which a build
    /// before leaves alone.)
    inline constexpr std::uint16_t kLevelMinReader = 50;
    /// The oldest level format whose saves are still read.
    /// 26 -> 27 added multiplayer fields, 27 -> 28 added texture table fields,
    /// 28 -> 29 a func_train's global name (extra[3], 0 before: read as
    /// none), 29 -> 30 the later weapons' items (cooked after every other
    /// actor) and sounds (after the others: kCommonSoundsBefore30), 30 -> 31
    /// the last four weapons and the weaponbox (items cooked after even
    /// those, sounds after theirs: commonsOf), 31 -> 32 what the people say
    /// and a guard's pistol to drop (an item cooked after all of those),
    /// 32 -> 33 the cockroach, leech, alien grunt, controller and snark
    /// (cooked after every other actor; their sounds after the others) and
    /// the nodes in the air (after those on the ground and in the water),
    /// 33 -> 34 the osprey, gargantua, ichthyosaur, assassin, baby crab and
    /// dead scientist in the suit (cooked after every other actor, as round
    /// 3's are; their sounds after the others), none that a save points at;
    /// 34 -> 35 the tanks, mortar fields and wheels (records a map had none
    /// for: a save of such a map is of other records, and is refused) and
    /// round 5's monsters (after round 4's).
    inline constexpr std::uint16_t kLevelSaveSince = 26;
    /// LevelHeader::farUnits: never nearer than this, never farther than that.
    /// The most actors a map may have of its own (the converter refuses one
    /// with more, the reader does not read it): with the visitors, what the
    /// runtime's per-actor caches hold (ModelDraw::kActorSlots).
    inline constexpr int kMaxMapActors = 508;
    inline constexpr int kLevelFarLeast = 1536;
    inline constexpr int kLevelFarMost = 8192;
    /// What the 3D engine is given to draw, kept under the
    /// hardware's polygons and vertices limit.
    inline constexpr int kPassPolygons = 1700;
    inline constexpr int kPassVertices = 5200;
    /// A map whose worst view (the header's worst, as it is seen: the
    /// converter's flood through the portals from every leaf, every way --
    /// Portals.cpp's seenWorst) is more than a pass has room for: it has the
    /// fog on always (WorldDraw::govern), so that what is left out of its
    /// worst views at the far end is never an edge.
    inline constexpr bool levelIsHeavy(unsigned worstPolygons, unsigned worstVertices)
    {
        return worstVertices > kPassVertices || worstPolygons > kPassPolygons;
    }
    /// Whether this build can read a level file that says `version` and `minReader` of itself.
    inline constexpr bool levelVersionOk(std::uint16_t version, std::uint16_t minReader)
    {
        return version >= kLevelMinVersion && minReader <= kLevelVersion;
    }
    /// Whether a save made against maps of level format `version` is still about the maps this build reads.
    inline constexpr bool levelSaveVersionOk(std::uint16_t version)
    {
        return version >= kLevelSaveSince && version <= kLevelVersion;
    }

    enum class Section : std::uint16_t
    {
        Planes,     ///< Plane
        PlaneTypes, ///< u8, parallel to Planes: 0/1/2 = exactly +X/+Y/+Z, 3 = other
        Nodes,      ///< Node: the render tree (hull 0)
        Leaves,     ///< Leaf; leaf 0 is the shared solid leaf
        Marks,      ///< u16: surface indices, grouped per leaf
        Vis,        ///< u8: the BSP's run-length visibility rows, unchanged
        Surfaces,   ///< Surface: world model first, sorted by texture
        GxStream,   ///< u32: packed geometry-engine command words
        Models,     ///< Model; model 0 is the world
        ClipNodes,  ///< ClipNode: hulls 1 (standing) and 3 (crouched), and the world's 2 (large)
        Textures,   ///< TextureRef
        Entities,   ///< EntityRecord
        EntityText, ///< u8: empty (the entity lump was here; it is the converter's, and stays on the PC)
        LightPatch, ///< LightPatch: the vertex colours that lights which change change
        Brushes,    ///< Brush: the map's brush entities (doors, grates, glass...)
        BrushLeaves, ///< u16: the world leaves each brush touches, grouped per brush
        Logic,      ///< LogicRecord: the entities that do something
        LogicAux,   ///< LogicAux: lists hanging off logic records
        PathCorners, ///< PathCorner: the stops of every train's route
        NameHashes, ///< u32, indexed by name id: nameHash() of each name; [0] is 0
        Landmarks,  ///< Landmark: the points two neighbouring maps have in common
        Strings,    ///< u8: NUL-terminated strings records point into (map names)
        ModelRefs,  ///< ModelRef: the models the map's actors are made of
        Actors,     ///< ActorRecord: the people and props that are models
        PathNodes,  ///< PathNode: where something that walks can stand (info_node)
        PathNext,   ///< PathLink: the ways on from each node, grouped per node
        Sounds,     ///< SoundRef: the sounds the map plays (HlSound.hpp); the common ones first
        LogicSounds, ///< LogicSound, parallel to Logic: what each record sounds like
        Sentences,  ///< SentenceRecord: the spoken lines the map has
        SentenceWords, ///< SentenceWord: grouped per sentence
        Rooms,      ///< RoomRecord: what the map's rooms sound like (env_sound)
        Sprites,    ///< SpriteRef: the sprites the map draws (HlSprite.hpp); the common ones first
        /// u16, kSpeechGroups * kSpeechLines of them (or none): for each
        /// thing a soldier may call out, the Sentences that say it; 0xFFFF
        /// where there are fewer.
        Speech,
        Messages,   ///< MessageRecord: what the map writes on the screen (env_message)
        ModelSounds, ///< ModelSound: the sounds the map's models make as they move, by name
        /// u16: surface indices, grouped per render-tree node: the surfaces
        /// that lie on each node's plane (the BSP's node.firstface), which
        /// the game draws whenever the node is gone through.
        NodeMarks,
        NodeSpans, ///< NodeSpan, parallel to Nodes: each node's run of NodeMarks
        Count
    };
    inline constexpr int kSectionCount = static_cast<int>(Section::Count);

    struct SectionDir
    {
        std::uint32_t offset; ///< from the start of the file, a multiple of 4
        std::uint32_t size;   ///< in bytes, before padding
        std::uint32_t count;  ///< records
    };

    struct LevelHeader
    {
        char magic[4];
        std::uint16_t version;
        std::uint16_t sectionCount;
        std::uint32_t fileSize;
        std::uint32_t crc32; ///< of every byte after this header
        std::int16_t mins[3]; ///< world bounds, whole units
        std::int16_t maxs[3];
        std::uint16_t visLeafCount; ///< leaves 1..n have a visibility row
        /// The most the PVS of any one leaf lists, before the runtime's own
        /// frustum and distance culling. What the fog renderer is judged from.
        std::uint16_t worstPolygons;
        std::uint16_t worstVertices;
        /// The map's sky: one more than the index in Textures of the first
        /// of its six sides (in the order right, back, left, front, up,
        /// down); 0 for a map with no sky.
        std::uint16_t skyTexture;
        /// What the player holds: one more than the index in ModelRefs of the
        /// first-person model drawn in front of the view; 0 for none.
        std::uint16_t viewModel;
        /// The oldest build that can read this file.
        std::uint16_t minReader;
        /// The most texels of texture the PVS of any one leaf lists, at the
        /// sizes they are cooked at; what the streaming of textures is sized from.
        std::uint32_t worstTexels;
        /// How far anything of the map can be seen from anywhere in it (the
        /// converter's portal flood, Portals.cpp): the view is never drawn
        /// farther. Whole units, kLevelFarLeast..kLevelFarMost; 0 = not known.
        std::uint32_t farUnits;
        std::uint32_t reserved[4];
    };

    struct Plane
    {
        std::int32_t normal[3]; ///< Q30
        fx dist;                ///< Q12
    };

    inline constexpr std::uint8_t kPlaneAxisX = 0;
    inline constexpr std::uint8_t kPlaneAxisY = 1;
    inline constexpr std::uint8_t kPlaneAxisZ = 2;
    inline constexpr std::uint8_t kPlaneOther = 3;

    struct Node
    {
        std::uint16_t plane;
        /// [0] in front of the plane, [1] behind. Negative: -(leaf + 1).
        std::int16_t children[2];
        std::int16_t mins[3]; ///< whole units
        std::int16_t maxs[3];
        std::uint16_t parent; ///< kNoParent at the root
    };
    inline constexpr std::uint16_t kNoParent = 0xFFFF;

    /// A node's own surfaces: `count` of NodeMarks from `first`.
    struct NodeSpan
    {
        std::uint16_t first;
        std::uint16_t count;
    };
    static_assert(sizeof(NodeSpan) == 4);

    struct Leaf
    {
        std::int32_t visOfs; ///< into Vis; -1 = sees everything
        std::int16_t mins[3];
        std::int16_t maxs[3];
        std::uint16_t firstMark;
        std::uint16_t markCount;
        std::uint16_t parent; ///< the node this leaf hangs from
        std::int8_t contents; ///< kContents*
        std::uint8_t flags;   ///< kLeaf*
    };
    /// Sky can be seen from this leaf: the sky box is drawn only then.
    inline constexpr std::uint8_t kLeafSeesSky = 1;


    inline constexpr std::int8_t kContentsEmpty = -1;
    inline constexpr std::int8_t kContentsSolid = -2;
    inline constexpr std::int8_t kContentsWater = -3;
    inline constexpr std::int8_t kContentsSlime = -4;
    inline constexpr std::int8_t kContentsLava = -5;
    inline constexpr std::int8_t kContentsSky = -6;

    /// One drawable piece of the map: a ready-made run of geometry-engine
    /// commands and what the renderer needs to decide whether to send it.
    ///
    /// A surface is a GROUP of the map's faces -- neighbours on one plane with
    /// one texture -- so that vertices they share are sent once (the
    /// converter's MeshOptimize). One plane means one back-face test decides
    /// for all of it.
    struct Surface
    {
        std::uint32_t gxOfs;    ///< first word in GxStream
        std::uint16_t gxWords;
        /// Plane index in the low 15 bits; bit 15 set when the face looks down
        /// the plane's back, i.e. is visible from BEHIND it.
        std::uint16_t planeRef;
        std::int16_t center[3]; ///< whole units
        std::uint16_t radius;   ///< whole units, rounded up
        std::uint16_t texture;  ///< into Textures, or kNoTexture
        std::uint16_t polygons; ///< polygon RAM this surface uses
        std::uint16_t vertices; ///< vertex RAM this surface uses
        std::uint8_t flags;
        std::uint8_t pad;
    };
    /// A surface drawn in a flat colour (a brush with render mode "colour").
    inline constexpr std::uint16_t kNoTexture = 0xFFFF;
    inline constexpr std::uint16_t kSurfacePlaneBack = 0x8000;
    inline constexpr std::uint16_t kSurfacePlaneMask = 0x7FFF;

    struct Model
    {
        std::int16_t mins[3]; ///< whole units
        std::int16_t maxs[3];
        fx origin[3];
        /// Root of the render tree: a node, or -(leaf + 1) for a model with no
        /// tree of its own.
        std::int32_t headNode;
        /// Roots in ClipNodes; negative when the whole hull is one contents value.
        std::int32_t clipStanding;
        std::int32_t clipCrouched;
        /// The hull of something 64 units across and as high (the game's
        /// third): the world's only. What is that wide may stand only where
        /// this is not solid. A brush's is empty.
        std::int32_t clipLarge;
        std::uint16_t firstSurface;
        std::uint16_t surfaceCount;
    };

    struct ClipNode
    {
        std::uint16_t plane;
        /// [0] in front, [1] behind. Negative: a kContents* value.
        std::int16_t children[2];
        std::uint16_t pad;
    };

    struct TextureRef
    {
        char name[16];        ///< lower-case, NUL padded; the file is tex/<name>.htx
        std::uint16_t width;  ///< as cooked: a power of two
        std::uint16_t height;
        std::uint16_t sourceWidth; ///< as the game had it
        std::uint16_t sourceHeight;
        std::uint32_t flags; ///< kTexture* (HlTexture.hpp)
        /// A texture that is more than one picture (the game's '+'
        /// textures): how many its cycle has, ten a second, this one first
        /// and the rest straight after it in Textures (1: it is still). And
        /// its other face -- what a brush shows when its "frame" is set: a
        /// light off, a button in, a charger empty -- where that starts and
        /// how many pictures it has (kNoTexture: it has none).
        std::uint8_t frames;
        std::uint8_t otherFrames;
        std::uint16_t other;
        /// The average of the texture's own colour, drawn in while the
        /// texture itself is not in video memory. 0x7FFF when not known.
        std::uint16_t average;
        /// The percentage of map leaves whose PVS lists a surface wearing it.
        /// The textures every view wants are kept resident in video memory.
        std::uint8_t share;
        /// How much smaller a console sent the map has it (every texture in
        /// video memory at once, none streamed): halvings of its width in
        /// the low four bits, of its height in the high four. The host's
        /// level only says it; the guest's has it done (the converter's
        /// Pack: its size, and its surfaces' coordinates) and says 0.
        std::uint8_t guestHalvings;
        /// How many colours its palette has, as cooked (two bytes each): a
        /// streamed texture is given that much of palette memory. 0 when it
        /// was not cooked (a map converted without textures).
        std::uint16_t paletteColours;
        std::uint16_t spare;
    };

    /// How a brush entity is drawn. From the entity's "rendermode" and
    /// "renderamt", by the rules the PC engine applies.
    enum class RenderClass : std::uint8_t
    {
        Opaque, ///< like the world
        /// "Solid" mode: '{' textures have real holes. Those textures are
        /// A3I5; the polygons are sent at full alpha.
        Cutout,
        /// "Texture", "glow" and "additive" modes: the whole brush is
        /// see-through by `alpha`. Glass.
        Blend,
        Colour, ///< "colour" mode: no texture, a flat colour at `alpha`
        /// Not drawn at all -- a blended mode with amount 0, or a class that
        /// is never visible (triggers, ladders). It can still be solid.
        Hidden,
    };

    inline constexpr std::uint8_t kBrushSolid = 1;   ///< the player collides with it
    inline constexpr std::uint8_t kBrushHasPivot = 2; ///< built round its origin: it can turn
    inline constexpr std::uint8_t kBrushStartsOff = 4; ///< absent until something switches it on
    /// It turns about something other than the vertical, so the box that
    /// holds it at any angle is a cube round its pivot, not a square column.
    inline constexpr std::uint8_t kBrushTumbles = 8;
    /// It travels far from where it was built (a tram): which leaves it can
    /// be seen from is not known ahead of time, so it is tested against the
    /// view alone.
    inline constexpr std::uint8_t kBrushRoams = 16;
    /// Not seen until an env_render shows it (HlLogic's `look`).
    inline constexpr std::uint8_t kBrushStartsUnseen = 32;

    /// One brush entity: a piece of the map that is its own object -- a door,
    /// a grate, a pane of glass, an invisible wall.
    ///
    /// Its geometry is a Model (surfaces and clip hulls) in the model's own
    /// space; `origin` is where that space sits in the world. For most brushes
    /// that is zero and the model is already in world coordinates; a brush
    /// made to turn is built round its pivot and placed by its origin.
    struct Brush
    {
        std::uint16_t model;       ///< into Models; never 0
        std::uint8_t renderClass;  ///< RenderClass
        std::uint8_t alpha;        ///< 1..31: the polygon alpha it is drawn with
        std::uint8_t flags;        ///< kBrush*
        /// For a body of liquid (func_water): what it is full of, as a
        /// kContents* value stored unsigned; 0 for every other brush.
        std::uint8_t kind;
        std::uint16_t colour;      ///< RGB555, for RenderClass::Colour
        fx origin[3];
        std::int16_t mins[3];      ///< world bounds at rest, whole units
        std::int16_t maxs[3];
        /// The world leaves its bounds touch, in BrushLeaves: it is drawn when
        /// any of them is visible.
        std::uint16_t firstLeaf;
        std::uint16_t leafCount;
        std::uint16_t logic;       ///< its LogicRecord, or kNoLogic if it only stands there
        std::uint16_t pad;
    };
    inline constexpr std::uint16_t kNoLogic = 0xFFFF;

    // The three angles of a brush, in the order the game's maps give them.
    inline constexpr int kPitch = 0; ///< about the brush's Y: positive tips its front down
    inline constexpr int kYaw = 1;   ///< about the vertical: positive turns left
    inline constexpr int kRoll = 2;  ///< about the brush's X

    /// What a logic record is. The set is deliberately small: these are the
    /// entities that make the first chapters' doors open and lifts run.
    enum class LogicKind : std::uint8_t
    {
        /// Does nothing when used. It exists so that it can be NAMED: removed
        /// by a killtarget, or switched by something that only needs it to be
        /// there.
        Static,
        Door,
        Button,
        /// Something that breaks: it vanishes and fires its target. extra[0]
        /// is how much damage it takes, extra[1] what it is made of (7 is the
        /// glass that never breaks). It breaks when used by name; when stood
        /// on, if kLogicPressure; when hit, unless kLogicTriggerOnly.
        Breakable,
        WallToggle,     ///< used: appears / disappears
        TriggerOnce,    ///< a volume: fires when the player enters, once
        TriggerMultiple, ///< ... and again after `waitMs`
        TriggerAuto,    ///< fires by itself when the map starts
        TriggerRelay,   ///< used: fires its target with its own use type
        MultiManager,   ///< used: fires a list of targets, each after its own delay
        MultiSource,    ///< an AND gate: fires when every entity targeting it is on
        /// A brush that travels from path corner to path corner. Lifts are
        /// these as often as they are doors.
        Train,
        /// Not something that acts: a volume (mins/maxs) inside which the
        /// player climbs. It is a logic record so that it can be removed by
        /// name like anything else.
        Ladder,
        /// A door that turns about an axis through its origin: extra[0] says
        /// which angle it is (0 pitch, 1 yaw -- the ordinary hinge -- 2 roll).
        DoorRotating,
        /// The way into the next map: a volume (mins/maxs) that works by being
        /// walked into (kLogicTouch) or by being fired by name. extra[0] is
        /// the map's name in Strings, extra[1] the landmark's hash, extra[2]
        /// the hash of a name to fire in the new map and extra[3] how many
        /// milliseconds after arriving.
        ChangeLevel,
        /// Sets a state that outlives the map (extra[0]: its hash). extra[1]
        /// is what using it does: 0 off, 1 on, 2 dead, 3 toggle. With
        /// kLogicStartOpen it sets extra[2] (0 off, 1 on, 2 dead) when the
        /// map starts, if the state is not known yet.
        EnvGlobal,
        /// A brush that spins: a fan. extra[0] is the angle it turns (as for
        /// DoorRotating), `speed` its full speed in turns * 65536 a second
        /// (negative: the other way), extra[1] how much speed it gains or
        /// loses every tenth of a second while spinning up or down.
        /// kLogicStartOpen: spinning from the start. kLogicToggle: it spins UP
        /// gradually too (it always spins down gradually).
        Rotating,
        /// A train that follows a track of path_tracks and turns to face along
        /// it: the tram. Its brush is placed by its origin, `height`
        /// (extra[1], Q12) above the track. `speed` is its speed when
        /// started, extra[3] the speed it starts the map at (Q12), extra[0]
        /// the distance between its "wheels" (Q12): how far ahead it looks to
        /// know which way to face. extra[2] is the hash of its global name,
        /// by which the same train is found in the next map; firstAux is the
        /// track node it starts at. kLogicSelfDriven: track nodes set its
        /// speed, and nobody drives it. mins/maxs: where the player stands to
        /// drive it (its func_traincontrols), in the train's own space as it
        /// was built; all zero, its own box and 72 units above (the game's).
        /// kLogicForwardOnly: it is never driven backwards.
        TrackTrain,
        /// A platform that carries a train from the end of one track down to
        /// the start of another. `move` is its travel, extra[0] how far it
        /// turns on the way (turns * 65536, signed), extra[1] the node the
        /// upper track ends at, extra[2] the node the lower one starts at,
        /// extra[3] the name id of its train.
        TrackChange,
        /// A box the player shoves about. It is a Breakable as well if
        /// kLogicBreaks. extra[2] is the fastest it can be pushed (Q12 units
        /// a second), extra[3] which of the player's two hulls it moves
        /// through the world as (0 standing, 1 crouched). `speed`: how it
        /// floats, the lift a unit of its depth in water gives it (units a
        /// second, a second); 0, it sinks.
        Pushable,
        /// A volume (mins/maxs) that hurts whoever is in it: extra[0] is the
        /// damage a second (signed: a negative one heals). Used, it switches
        /// on and off; kLogicStartOpen is "starts off".
        TriggerHurt,
        /// A volume that pushes whoever is in it along `move` (Q12 units a
        /// second): the player's base velocity while they are in it, which
        /// upwards is an acceleration (pm_shared's). kLogicFireOnce: it adds
        /// `move` to their velocity once, and is gone.
        TriggerPush,
        /// A volume that puts whoever enters it at `move`, facing extra[0]
        /// (turns * 65536).
        TriggerTeleport,
        /// A volume that saves the game the first time the player enters it.
        AutoSave,
        /// A scripted sequence: takes an actor, has it walk to a mark and act
        /// something out, and fires its target when that is done. `move` is
        /// the mark and `speed` the way to face there (turns * 65536).
        /// extra[0] is the actor's name id, or 0 to take the nearest actor of
        /// kind `damage - 1` within `waitMs` units; extra[1] the clip to hold
        /// until triggered and extra[2] the clip to act out (nameHashes, 0 =
        /// none); extra[3] how it gets to the mark (kScriptMove*).
        /// kLogicStartOpen: it starts by itself.
        Script,
        /// A monstermaker: puts one of its actors (the dormant ones from
        /// extra[0], extra[1] of them) where it is. extra[2] is how many it
        /// may make in all (0: no end to them), `waitMs` the time between
        /// two while it is on (kLogicStartOpen: on from the start;
        /// kLogicToggle: one each time it is used instead). Its target is
        /// fired for each one made.
        Maker,
        /// A wall unit that gives health (or, the other, armour) while USE
        /// is held on it. How much it holds is the game's level's
        /// (chargerCapacity); extra[0] is the easy level's, which a build of
        /// before level 42 used. Its LogicSound: `unlocked` as it starts,
        /// `move` its loop while it gives, `locked` as it refuses.
        HealthCharger,
        SuitCharger,
        /// Something heard at a place (`move`): extra[0] is the sound (into
        /// Sounds), or a sentence if extra[3] is 1; extra[1] how far it
        /// carries in units (0: everywhere); extra[2] how loud, 0..127.
        /// kLogicToggle: it loops, and using it starts and stops it
        /// (kLogicStartOpen: it starts the map playing). Otherwise each use
        /// plays it once. Its target, if it has one, is fired when it is
        /// used (a scripted_sentence).
        Ambient,
        /// Starts a track of the game's music: extra[0] is its number. A
        /// volume (mins/maxs) walked into once (kLogicTouch), or used.
        Music,
        // What is only seen (and felt): HlEffects.hpp.
        /// A sprite at a place (`move`): env_sprite, env_glow, cycler_sprite.
        /// extra[0] is the sprite (into Sprites); extra[1] its frames a
        /// second (Q8); extra[2] its colour (RGB555) in the low 16 bits, how
        /// solid it is (1..31) in the next 8 and its scale (Q4) in the top 8;
        /// extra[3] bit 0: it plays through once and goes out; bit 1: it is a
        /// glow, seen through what it sits in; bit 2: a cycler_sprite, always
        /// there, whose frames using it stops and starts. Used, it is
        /// switched on and off (kLogicStartOpen: on from the start).
        Sprite,
        /// A beam from `move` to `mins` (whole units): env_beam, env_laser.
        /// extra[0] is its picture (into Sprites); extra[1] its width in
        /// tenths of a unit (low 16 bits) and how far lightning is thrown
        /// about (the rest); extra[2] colour and solidity as a Sprite's;
        /// extra[3] how long a strike lasts in ms (low 16 bits; 0: it stays
        /// while it is on) and how long until it strikes again (the rest).
        /// `damage` is what it does a second to the first thing in its way
        /// (the player, a monster, something that breaks), where it stops.
        /// kLogicStartOpen: on from the start. kLogicToggle: switched on and
        /// off; otherwise each use is one strike.
        /// Its ends need not be fixed. `target` is the name id of what its
        /// start follows, `netName` of what its end follows (a brush, an
        /// actor: wherever that is now); 0: the place given. A list
        /// (firstAux, auxCount) is places its end is picked from at each
        /// strike (LogicAux: x, y, z in whole units in its three fields).
        /// `maxs[1]` is kBeam* flags: with kBeamRandom it has no end, and
        /// each strike goes to wherever the world stops a line of `maxs[0]`
        /// units in a direction taken at random.
        Beam,
        /// An explosion at `move`: extra[0] is its size (the game's
        /// "magnitude": its damage, and its reach by 2.5), extra[1] its
        /// flags as the game has them (1: no damage, 2: can go off again,
        /// 4: no fireball, 64: no sound).
        Explosion,
        /// Shakes the view: extra[0] how hard (units, Q8), extra[1] for how
        /// long (ms), extra[2] within how far of `move` (units; 0:
        /// everywhere).
        Shake,
        /// Sparks at `move`, at random moments no more than `waitMs` apart,
        /// while it is on (kLogicStartOpen: from the start). kLogicToggle:
        /// each use switches it; without it, using it does nothing.
        /// extra[0..2]: its sounds, two to a word (low half first);
        /// extra[3] how many (0 on a map cooked before: the one in its
        /// LogicSound's `move`).
        Spark,
        /// Throws things out from `move`: extra[0] is how many, extra[1]
        /// the time between two (ms), extra[2] how fast (units a second),
        /// extra[3] how long each lasts (ms). `mins` is the way, a unit
        /// vector in thousandths. kLogicToggle: it can do it again. What it
        /// throws: pieces of a model (`damage` is the model's index in
        /// ModelRefs, plus 1), or a sprite (`maxs[0]` is its index in
        /// Sprites, plus 1), or with neither plain scraps.
        Shooter,
        /// Changes how what it targets is drawn: extra[0] how solid (0..255,
        /// the game's "render amount"), extra[1] the game's render mode.
        Render,
        /// Fades the screen to black and back: extra[0] the fade (ms),
        /// extra[1] how long it holds (ms), extra[2] how dark (0..255),
        /// extra[3] bit 0: from black to clear, not the other way.
        Fade,
        /// Writes on the screen: extra[0] is which of the level's Messages.
        /// kLogicFireOnce: only the first time it is used.
        Message,
        /// A conveyor: a brush whose top carries whatever stands on it,
        /// `move` units a second (Q12), and whose belt -- its "scroll"
        /// texture -- slides `speed` texels a second (Q12, signed). Used,
        /// it runs the other way. extra[0] bit 0: it carries nothing.
        Conveyor,
        /// A platform that travels `move` from where it was built and turns
        /// extra[0] (turns * 65536, signed) about the vertical on the way,
        /// carrying whoever stands on it: the round lift. kLogicToggle: it
        /// goes when used and stays; otherwise it goes when stood on and
        /// comes back by itself. kLogicStartOpen: it starts at the far end.
        /// (Built where it is turned: it is the far end that is unturned.)
        PlatRot,
        /// A light that a switch turns on and off: extra[0] is its style
        /// (HlLevel: LightPatch). kLogicStartOpen: it starts off.
        Light,
        /// A mark that appears on the wall at `move` when it is used:
        /// extra[0] which (a kDecal*), extra[1] how big.
        Mark,
        /// A brush that swings about its origin: `move[0]` is how far
        /// (turns * 65536), `speed` how fast at most (the same a second),
        /// extra[0] which angle, extra[1] its damping (the game's "damp", in
        /// thousandths: 0 swings for ever), extra[2] bit 0: stopped, it goes
        /// back to where it began. kLogicStartOpen: swinging from the start.
        /// Used, it starts and stops.
        Pendulum,
        /// A vending machine's works at `move`: used, a can drops out and
        /// does whoever takes it a point of good, extra[0] times.
        Beverage,
        /// The rush of light the incident's portals open with, at `move`.
        Funnel,
        /// Somebody says a line (scripted_sentence, from level version 32):
        /// extra[0] the sentence, extra[1] for how long (ms, the game's
        /// duration), extra[2] how loud (0..127), extra[3] how long after it
        /// may be said again (refire, ms). The speaker: by name (netName),
        /// or the nearest of a kind (damage: kind + 1) within waitMs units
        /// of `move`. mins[0] how far it carries (kRadius*), mins[1] the
        /// name id of who is spoken to (mins[2] 1: the player), maxs[0] the
        /// map's spawnflags (kSentence*). With no name it starts by itself.
        Sentence,
        // From level version 35 (HlMonsters.hpp's tankTick and the rest).
        /// A gun on a mount (func_tank, func_tanklaser, func_tankrocket,
        /// func_tankmortar): a brush that turns about its origin to aim --
        /// at the player, while it is on (kLogicStartOpen), or where a player
        /// at its controls looks. `move` is its barrel (forward, right, up,
        /// from the origin), `speed` how fast it turns about the vertical
        /// (degrees a second); extra[0] how it rests (yaw, and pitch << 16:
        /// turns, the brush's way, positive tipped down); extra[1] the
        /// degrees it may turn either way and how near is near enough to
        /// fire (yaw range, pitch range << 8, yaw tolerance << 16, pitch
        /// tolerance << 24); extra[2] its rounds a second (sixteenths, low 16
        /// bits), how fast it pitches (degrees a second << 16), its bullet
        /// (kTankBullet* << 24), its spread (<< 26) and what it fires
        /// (kTankGun... << 29); extra[3] how long it goes on firing at what
        /// it has lost sight of (ms). mins[0], mins[1] the nearest and the
        /// furthest it aims at (0: as far as it likes), mins[2] a mortar's
        /// blast; maxs[0] the map's spawnflags (kTank*). `damage` a round's
        /// (0: its bullet's own); netName a laser's env_laser; `target` is
        /// fired at each shot (the map's sound of it). maxs[1] and maxs[2]
        /// (from version 47): its flash's sprite and its smoke's, each the
        /// sprite in the low 12 bits (kTankSpriteNone: none) and its scale in
        /// quarters in the top 4.
        Tank,
        /// Where a player takes a tank's controls (func_tankcontrols): a
        /// volume, mins..maxs; `target` names the tank. USE inside it takes
        /// them, and again lets go.
        TankControls,
        /// Mortar fire on call (func_mortar_field): a volume, mins..maxs,
        /// over which, used, extra[0] shells fall, two and a half seconds on
        /// and a moment apart, extra[1] units about the spot -- chosen at
        /// random (extra[2] 0), on whoever set it off (1), or by two wheels
        /// across it (2: extra[3] the name ids of the one for x, and the one
        /// for y << 16).
        MortarField,
        /// A wheel turned by hand (momentary_rot_button): as a func_rot_button
        /// turns, extra[0] its axis and extra[1..3] how it rests; move[0] is
        /// how far it goes (turns * 65536), `speed` how fast (the same a
        /// second). Used, it turns, until used again or at the end; what it
        /// targets (momentary_door) goes as far along as it is round.
        MomentaryButton,
        /// What a wheel moves (momentary_door): along `move`, at `speed`, to
        /// as far as the wheel that targets it is round.
        MomentaryDoor,
        /// A place on big momma's path (info_bigmomma: HlAliens.hpp's momma),
        /// at `move`: extra[0] the health she has from there (0: she goes on
        /// at once), extra[1] how near is there (units), waitMs how long she
        /// waits there, extra[2] the name id of what reaching it fires,
        /// extra[3] the nameHash of the clip she plays there and damage | pad
        /// << 16 of the one she plays on her way to it; mins[0] the map's
        /// spawnflags (1 run, 2 wait there for good), maxs[0] the way to face
        /// there (turns; 0: as she is); `target` names the next.
        MommaNode,
        /// A place with a name (info_target), where the map has a nihilanth
        /// that goes by them (n_min, n_max, its rechargers): at `move`. Taken
        /// away by a killtarget, it is not there.
        Target,
        /// A soldier who comes down a rope when told (monster_grunt_repel):
        /// from `move`, into the actor place extra[0] (kept for him, not
        /// there until then).
        Repel,
        /// A volume (mins/maxs) that sets how hard the world pulls the player
        /// who touches it, for as long as nothing sets it again (Xen's
        /// trigger_gravity): extra[0] in percent of sv_gravity.
        TriggerGravity,
        /// Where the player must stand for a ChangeLevel to go
        /// (trigger_transition): a volume, mins..maxs; extra[0] the hash of
        /// its name, which is that of the landmark. A way out whose landmark
        /// has volumes does nothing while the player is in none of them --
        /// the door that shuts behind them as they arrive fires the way
        /// back, and it is the player walking on that keeps them there.
        Transition,
        // From level version 39.
        /// Counts the times it is used (trigger_counter): the extra[0]-th
        /// fires its targets, and it is spent.
        Counter,
        /// Gives another record a target of its own (trigger_changetarget):
        /// used, every record named `target` fires extra[0] (a name id) from
        /// then on, in place of what the map gave it (LogicState::target).
        ChangeTarget,
        /// Takes every weapon and all ammunition from the player, not the
        /// suit (player_weaponstrip).
        WeaponStrip,
        /// The game is lost (player_loadsaved): used, the screen fades to
        /// extra[2] of darkness (0..255) over extra[0] ms and holds it, shows
        /// message extra[3] (into Messages; 0xFFFFFFFF none), and extra[1] ms
        /// after being used the last save is loaded (LogicWorld::reloadMs).
        LoadSaved,
        /// The end of a part of the game (trigger_endsection): touched (unless
        /// kLogicUseOnly) or used, the game goes back to its title
        /// (LogicWorld::endSection).
        EndSection,
        /// Throws what fights into the air as it comes into a volume
        /// (trigger_monsterjump): `move` is the velocity (Q12 units a
        /// second); kLogicStartOpen: off until used, and used again, off.
        MonsterJump,
        /// A view from somewhere else (trigger_camera): from `move`, looking
        /// at what `netName` names (0: straight on, at extra[1] yaw turns)
        /// for waitMs, then fires `target`. extra[0] its flags: 1 starts at
        /// the player's eye, 2 looks at the player, 4 the player cannot move
        /// meanwhile. (LogicWorld::camera.)
        Camera,
        /// Announcements (speaker): now and then one of the sentences of its
        /// aux list (each LogicAux::target a sentence index), at `move`,
        /// extra[0] its volume (0..127). kLogicStartOpen: speaking from the
        /// start; used, it starts or stops.
        Speaker,
        /// A target that rides a path (func_guntarget): a Train (firstAux its
        /// first corner, `move` its middle, speed) that, while it rides,
        /// takes extra[0] of damage and then stops and fires `netName`.
        /// Used, it starts (whole again) or stops.
        GunTarget,
        /// A brush that a shot turns on and off (button_target): each hit
        /// toggles its texture's other frame and fires `target` on or off;
        /// kLogicUseOnly: using it does too; kLogicStartOpen: on to begin.
        ButtonTarget,
        /// Blood at a place (env_blood): used, a burst of extra[0] (0 red,
        /// 1 yellow) of size extra[1] at `move` -- or at the player's eye
        /// (extra[2] bit 2) -- sprayed along extra[3] (a yaw | pitch << 16,
        /// or anywhere: extra[2] bit 0), and a mark where it lands (bit 3).
        Blood,
        // From level version 40.
        /// A volume (mins/maxs) where the ground holds the player less
        /// (func_friction): extra[0] is its hold, in percent of the world's
        /// friction. Its brush, if it is seen, is drawn as any other.
        Friction,
        Count
    };
    // A tank's spawnflags (LogicKind::Tank, maxs[0]): the game's.
    inline constexpr int kTankActive = 0x0001;
    inline constexpr int kTankLineOfSight = 0x0010;
    inline constexpr int kTankCanControl = 0x0020;
    // ... its bullet, and what it fires.
    inline constexpr int kTankBulletNone = 0, kTankBullet9mm = 1, kTankBulletMp5 = 2, kTankBullet12mm = 3;
    inline constexpr int kTankGun = 0, kTankLaser = 1, kTankRocket = 2, kTankMortar = 3;
    // What a scripted_sentence's spawnflags say (LogicKind::Sentence).
    inline constexpr std::int16_t kSentenceOnce = 1;       ///< said once, then gone
    inline constexpr std::int16_t kSentenceFollowers = 2;  ///< only by one following the player
    inline constexpr std::int16_t kSentenceInterrupt = 4;  ///< by one busy with something else, too
    inline constexpr std::int16_t kSentenceConcurrent = 8; ///< (the game's: the others are NOT silenced)

    // What a script's spawnflags say (LogicKind::Script, in its `pad`, from
    // level version 32; 0 before): as the game numbers them, and above.
    inline constexpr std::uint16_t kScriptLeaveCorpse = 8;
    inline constexpr std::uint16_t kScriptNoInterrupt = 32;
    inline constexpr std::uint16_t kScriptOverrideState = 64;
    inline constexpr std::uint16_t kScriptNoScriptMovement = 128;
    inline constexpr std::uint16_t kScriptAi = 0x100;     ///< an aiscripted_sequence
    inline constexpr std::uint16_t kScriptAmbush = 0x200; ///< ... that ends in an ambush (m_iFinishSchedule 1)

    // What a "use" asks for.
    inline constexpr std::uint8_t kUseOff = 0;
    inline constexpr std::uint8_t kUseOn = 1;
    inline constexpr std::uint8_t kUseSet = 2;
    inline constexpr std::uint8_t kUseToggle = 3;

    // LogicRecord::flags. The map's own spawnflags mean different things per
    // class; the converter turns them into these.
    inline constexpr std::uint16_t kLogicStartOpen = 1; ///< a door that begins open
    /// A door that stays where it is sent until used again; a button that
    /// stays in until pressed again.
    inline constexpr std::uint16_t kLogicToggle = 2;
    inline constexpr std::uint16_t kLogicUseOnly = 4;   ///< a door the player opens with USE
    inline constexpr std::uint16_t kLogicTouch = 8;     ///< works by being walked into
    inline constexpr std::uint16_t kLogicFireOnce = 16; ///< a relay that removes itself
    inline constexpr std::uint16_t kLogicThread = 32;   ///< a multi_manager that can overlap itself
    /// A rotating door that always opens the same way. Without it, it opens
    /// away from whoever opens it.
    inline constexpr std::uint16_t kLogicOneWay = 64;
    /// A track train nobody drives: the track's nodes set its speed.
    inline constexpr std::uint16_t kLogicSelfDriven = 128;
    // A Breakable's:
    inline constexpr std::uint16_t kLogicTriggerOnly = 256; ///< nothing but its name breaks it
    inline constexpr std::uint16_t kLogicPressure = 512;    ///< standing on it breaks it
    inline constexpr std::uint16_t kLogicInstant = 1024;    ///< one blow of the crowbar does
    /// A Pushable that is breakable too.
    inline constexpr std::uint16_t kLogicBreaks = 2048;
    /// A trigger volume that a pushed box sets off as the player does...
    inline constexpr std::uint16_t kLogicPushables = 4096;
    /// ... and one that the player does not.
    inline constexpr std::uint16_t kLogicNoPlayer = 8192;
    /// A button that TURNS when it is pressed (a valve's wheel) instead of
    /// going in: its travel is an angle, as a door on hinges' is -- which
    /// way round in extra[0], how far in move[0], both as that door has them.
    inline constexpr std::uint16_t kLogicTurns = 16384;
    /// A track train that is driven forwards only.
    inline constexpr std::uint16_t kLogicForwardOnly = 32768;

    /// One entity that does something, with everything it needs already worked
    /// out by the converter: names are numbers, times are milliseconds, a
    /// door's travel is a vector. Nothing here is parsed at run time.
    struct LogicRecord
    {
        std::uint8_t kind;    ///< LogicKind
        std::uint8_t useType; ///< kUse*: what a relay or trigger_auto sends
        std::uint16_t flags;  ///< kLogic*
        // Names, as ids; 0 = none. Entities with the same name share an id.
        std::uint16_t name;       ///< what it is called: how others target it
        std::uint16_t target;     ///< whom it fires
        std::uint16_t killTarget; ///< whom it removes when it fires
        std::uint16_t master;     ///< the multisource that must be on for it to work
        std::uint16_t netName;    ///< a door's second target, fired when it shuts
        std::uint16_t brush;      ///< into Brushes, or kNoBrush
        /// Into LogicAux -- except for a train, where it is the path corner
        /// the train starts at (into PathCorners) or kNoCorner.
        std::uint16_t firstAux;
        std::uint16_t auxCount;
        std::int32_t waitMs;      ///< before returning / re-arming; -1 = never
        std::uint32_t delayMs;    ///< between being triggered and firing its targets
        fx speed;                 ///< units a second
        /// A door or button's whole travel. For a train: the middle of its
        /// brush, which is the point that visits each corner. For a rotating
        /// door: move[0] is how far it turns, in turns * 65536 (signed), and
        /// `speed` is in the same unit per second.
        fx move[3];
        std::int16_t mins[3];     ///< a trigger's volume, world, whole units
        std::int16_t maxs[3];
        std::uint16_t damage;     ///< what a blocked door does to what blocks it
        /// For an Ambient: how fast its sound is played, in percent of how
        /// it was recorded (0: as it was). For a trigger volume (TriggerHurt,
        /// TriggerPush, TriggerTeleport, TriggerGravity, Friction) whose
        /// brush is not a plain box: its model, into Models, whose hull the
        /// player's origin has to be in as well (0: the box is all of it).
        std::uint16_t pad;
        /// For a trigger_auto or a multisource: the hash of a global state
        /// that has to be ON for it to work; 0 = none.
        std::uint32_t globalState;
        std::uint32_t extra[4];   ///< what its kind says (LogicKind)
    };
    inline constexpr std::uint16_t kNoBrush = 0xFFFF;
    inline constexpr std::uint16_t kBeamRandom = 1; ///< LogicKind::Beam, in maxs[1]
    inline constexpr std::uint16_t kBeamRing = 2;   ///< ... a circle between its two ends
    inline constexpr std::uint16_t kBeamSparkStart = 4; ///< ... sparks fly at its start
    inline constexpr std::uint16_t kBeamSparkEnd = 8;   ///< ... and where it ends

    /// An entry of a list a logic record owns. For a multi_manager: one target
    /// and how long after the manager is used it fires. For a multisource: one
    /// of its inputs, as a record index in `target`.
    struct LogicAux
    {
        std::uint16_t target;
        std::uint16_t pad;
        std::uint32_t delayMs;
    };

    /// One stop on a train's route.
    struct PathCorner
    {
        fx pos[3];
        fx speed;             ///< the speed to LEAVE this corner at; 0 = as before
        std::int32_t waitMs;  ///< how long the train stops here
        std::uint16_t next;   ///< into PathCorners, or kNoCorner where the route ends
        std::uint16_t fireTarget; ///< a name fired each time a train arrives; 0 = none
        std::uint16_t flags;  ///< kCorner*
        /// On a track: a name fired when a train comes to rest here because
        /// the track goes no further; 0 = none.
        std::uint16_t netName;
    };
    inline constexpr std::uint16_t kNoCorner = 0xFFFF;
    /// The train stops here until it is used again.
    inline constexpr std::uint16_t kCornerWaitForUse = 1;
    /// The train does not travel to the next corner: it is simply there.
    inline constexpr std::uint16_t kCornerTeleport = 2;
    // On a track (path_track):
    inline constexpr std::uint16_t kCornerDisabled = 4;     ///< the track ends before it
    inline constexpr std::uint16_t kCornerFireOnce = 8;     ///< its fireTarget fires once only
    inline constexpr std::uint16_t kCornerDisableTrain = 16; ///< the train stops being drivable

    /// A point that is the same place in two maps: info_landmark. The player
    /// leaves one map so far from it and arrives in the next as far from the
    /// landmark of the same name there.
    struct Landmark
    {
        std::uint32_t hash; ///< nameHash() of its name
        fx origin[3];
    };

    /// How a name is known ACROSS maps, where name ids (which are per map)
    /// mean nothing: 32-bit FNV-1a of its lower-cased text; never 0.
    constexpr std::uint32_t nameHash(const char *text)
    {
        std::uint32_t hash = 2166136261u;
        for (; *text != '\0'; ++text)
        {
            char c = *text;
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
        }
        return hash == 0 ? 1 : hash;
    }

    // How a scripted actor gets to its mark (the map's "m_fMoveTo").
    inline constexpr std::uint32_t kScriptMoveNone = 0;    ///< acts where it stands
    inline constexpr std::uint32_t kScriptMoveWalk = 1;
    inline constexpr std::uint32_t kScriptMoveRun = 2;
    inline constexpr std::uint32_t kScriptMoveInstant = 4; ///< put on the mark at once
    inline constexpr std::uint32_t kScriptMoveTurn = 5;    ///< turns to face, no more

    /// A model a map uses: mdl/<map>/<name>.hmd, with the clips THIS map
    /// plays, and its skin mdl/<name>.htx.
    struct ModelRef
    {
        char name[24]; ///< lower-case, NUL padded; the last byte is kModelRefLazy or 0
    };
    /// A weapon's view model the map was not cooked with, named so that one
    /// given by a cheat is found: not in the map's archive but in the common
    /// one (wpn/<name>.hmd, its skin beside it), loaded when it is taken up.
    inline constexpr char kModelRefLazy = 1;
    inline bool modelRefLazy(const ModelRef &ref)
    {
        return ref.name[sizeof(ref.name) - 1] == kModelRefLazy;
    }

    /// What an actor is: how it behaves when nothing is directing it.
    enum class ActorKind : std::uint8_t
    {
        Scientist,
        Guard,
        Gman,
        Generic,   ///< monster_generic: any model, stands and idles
        Furniture, ///< monster_furniture: stands still until a script moves it
        Cycler,    ///< plays its first sequence over and over
        Pose,      ///< dead: one frame, held
        // What fights (HlMonsters.hpp).
        Headcrab,
        Zombie,
        Barnacle,
        /// Something to pick up, lying where it is: ActorRecord::item says
        /// what (ItemKind).
        Item,
        Slave,     ///< the alien slave: zaps from a distance, claws up close
        Bullsquid, ///< spits from a distance, bites and whips up close
        Houndeye,  ///< runs up and lets off a blast
        /// The scientist at a desk: sits where the map has him, going from
        /// one of his five ways of sitting to another.
        Sitter,
        // The guns that mind a place by themselves (HlMonsters.hpp).
        Miniturret, ///< small, on a floor or a ceiling; folds away
        Sentry,     ///< on three legs; woken by a touch, a shot or a tripwire
        Turret,     ///< the big one: has to spin up before it fires
        Grunt,      ///< a soldier (HlGrunt.hpp)
        Tripmine,   ///< a mine on a wall, with its beam
        /// Another player, in a game between consoles (HlNetGame.hpp): not
        /// in any map; put where their own console says they are.
        Player,
        // Round 3 of the monsters (HlAliens.hpp). (After all the others: a
        // kind's number is in the levels.)
        Cockroach,  ///< runs from light and feet, eats what lies about
        Leech,      ///< swims its water and bites what comes into it
        AlienGrunt, ///< the hornet gun, and big fists
        Controller, ///< flies; throws balls of energy
        Snark,      ///< a monstermaker's: hunts, bites, bursts
        // Round 4 (HlMilitary.hpp, HlAliens.hpp).
        Osprey,     ///< flies its corners; lets soldiers down on ropes
        Gargantua,  ///< flames, a stomp, and only what blows up hurts it
        BabyCrab,   ///< a monstermaker's small headcrab
        Assassin,   ///< the black-clad soldier: leaps, a silenced pistol
        Ichthyosaur, ///< swims its water, circles, charges and bites
        // Round 5 (HlAliens.hpp, HlMilitary.hpp).
        Tentacle,   ///< rises from its pit to strike at what it hears
        Apache,     ///< the helicopter: a gun and rockets
        BigMomma,   ///< the gonarch: walks her path, spits, lays crabs
        Nihilanth,  ///< the last of them
        Boid,       ///< one of a flock of flyers (monster_flyer_flock)
        /// A plant of Xen (HlXen.hpp): what kind (XenPlant) in the low 8 bits
        /// of ActorRecord::item, and for a light, its glow's colour (RGB555)
        /// in the top 16.
        Xen,
        Count
    };

    /// Which plant of Xen an ActorKind::Xen is (the low 8 bits of its
    /// ActorRecord::item).
    enum class XenPlant : std::uint8_t
    {
        Hair,        ///< sways; not in the way
        SporeSmall,  ///< the three fungi: stand, sway, in the way
        SporeMedium,
        SporeLarge,
        Tree,        ///< strikes at what comes in front of it
        Light,       ///< a glow on a stalk, that hides as the player comes near
        Count
    };

    inline bool isTurret(std::uint8_t kind)
    {
        return kind == static_cast<std::uint8_t>(ActorKind::Miniturret) ||
               kind == static_cast<std::uint8_t>(ActorKind::Sentry) ||
               kind == static_cast<std::uint8_t>(ActorKind::Turret);
    }

    /// The kinds that think as the game's monsters do (HlAi.hpp).
    inline bool actorThinks(std::uint8_t kind)
    {
        switch (static_cast<ActorKind>(kind))
        {
        case ActorKind::Headcrab:
        case ActorKind::Zombie:
        case ActorKind::Barnacle:
        case ActorKind::Slave:
        case ActorKind::Bullsquid:
        case ActorKind::Houndeye:
        case ActorKind::Grunt:
        case ActorKind::Scientist:
        case ActorKind::Guard:
        case ActorKind::Sitter:
        case ActorKind::Gman:
        case ActorKind::Generic:
        case ActorKind::Cockroach:
        case ActorKind::Leech:
        case ActorKind::AlienGrunt:
        case ActorKind::Controller:
        case ActorKind::Snark:
        case ActorKind::Osprey:
        case ActorKind::Gargantua:
        case ActorKind::BabyCrab:
        case ActorKind::Assassin:
        case ActorKind::Ichthyosaur:
        case ActorKind::Tentacle:
        case ActorKind::Apache:
        case ActorKind::BigMomma:
        case ActorKind::Nihilanth:
        case ActorKind::Boid:
            return true;
        default:
            return false;
        }
    }

    inline constexpr std::uint8_t kActorSolid = 1; ///< the player cannot walk through it
    /// Not there when the map starts: a place kept for what a monstermaker
    /// makes.
    inline constexpr std::uint8_t kActorDormant = 2;
    /// A turret: hung from a ceiling, upside down; and one that comes out
    /// by itself when it sees someone (the others wait to be switched on).
    /// A monster the map keeps as a prisoner (the game's flag): it goes
    /// for nobody. The things glimpsed during the cascade are these.
    inline constexpr std::uint8_t kActorPrisoner = 16;
    inline constexpr std::uint8_t kActorCeiling = 4;
    inline constexpr std::uint8_t kActorAuto = 8;
    /// A tripmine: armed a second after the map starts, not two and a half.
    inline constexpr std::uint8_t kActorQuick = kActorAuto;
    /// A soldier: one who does nothing until a script has had him; his
    /// squad's leader; one who does nothing until the player has seen him;
    /// one who has nothing to say while all is quiet.
    inline constexpr std::uint8_t kActorWaitScript = kActorAuto;
    inline constexpr std::uint8_t kActorLeader = 32;
    inline constexpr std::uint8_t kActorWaitSeen = 64;
    /// A monster_generic whose origin is the middle of its body, not its
    /// feet: the game gives holo.mdl and player.mdl the player's box
    /// (genericmonster.cpp) -- the hazard course's hologram. (Its bit is a
    /// soldier's and a tripmine's for other things.)
    inline constexpr std::uint8_t kActorCentred = kActorAuto;
    inline constexpr std::uint8_t kActorGag = 128;

    // What a soldier carries (ActorRecord::item, the low 8 bits): the
    // game's own "weapons" bits.
    inline constexpr std::uint32_t kGruntMp5 = 1;
    inline constexpr std::uint32_t kGruntGrenades = 2;
    inline constexpr std::uint32_t kGruntLauncher = 4;
    inline constexpr std::uint32_t kGruntShotgun = 8;
    /// ... and in the four bits above them, when he sets off what his
    /// record's `target` names (the game's "TriggerCondition": 1 and 7 to
    /// 11 at the sight or sound of the player, 2 hurt, 3 down to half his
    /// health, 4 killed). Once.
    inline constexpr int kGruntTriggerShift = 4;
    /// One of the people (a scientist, a guard), in the low bits of the same
    /// word: met before the disaster (the game's SF_MONSTER_PREDISASTER):
    /// he will not follow.
    inline constexpr std::uint32_t kPersonPreDisaster = 1;
    /// A monster_generic, in bits 1..3 of the same word (from level version
    /// 48): how it is drawn (the map's rendermode and renderamt). 0 solid;
    /// 1..6 see-through, that many sevenths of it there; kActorLookHologram
    /// the game's hologram (renderfx 16), which fades and flickers as the
    /// game's does (hologramBlend).
    inline constexpr int kActorLookShift = 1;
    inline constexpr std::uint32_t kActorLookMask = 7;
    inline constexpr std::uint32_t kActorLookHologram = 7;

    // What a soldier calls out (Section::Speech): the game's HG_ groups.
    inline constexpr int kSpeakGrenade = 0; ///< a grenade lands by him
    inline constexpr int kSpeakAlert = 1;   ///< the leader, at the sight of the player
    inline constexpr int kSpeakCover = 2;   ///< hurt, and running for cover
    inline constexpr int kSpeakThrow = 3;   ///< about to throw or launch a grenade
    inline constexpr int kSpeakTaunt = 4;   ///< staying put
    inline constexpr int kSpeakCharge = 5;  ///< going in after someone out of sight
    inline constexpr int kSpeakIdle = 6;    ///< nothing doing
    inline constexpr int kSpeakQuestion = 7; ///< ... a question, and the answer to one
    inline constexpr int kSpeakAnswer = 8;
    inline constexpr int kSpeakCheck = 9;   ///< "check in", and "all clear"
    inline constexpr int kSpeakClear = 10;
    inline constexpr int kSpeechGroups = 11;
    inline constexpr int kSpeechLines = 6;

    // What the people of the place say (the game's talkmonster groups):
    // after the soldiers' table in Section::Speech (version 32 on), for each
    // voice and slot the first of its lines and how many, then the lines
    // (Sentences) themselves. A slot is the game's TLK_* (in its order), then
    // what each voice has besides.
    inline constexpr int kVoiceScientist = 0;
    inline constexpr int kVoiceBarney = 1;
    inline constexpr int kVoices = 2;
    enum : int
    {
        kTalkAnswer,
        kTalkQuestion,
        kTalkIdle,
        kTalkStare,
        kTalkUse,
        kTalkUnuse,
        kTalkStop,
        kTalkNoShoot,
        kTalkHello,
        kTalkPHello,
        kTalkPIdle,
        kTalkPQuestion,
        kTalkPlHurt1,
        kTalkPlHurt2,
        kTalkPlHurt3,
        kTalkSmell,
        kTalkWound,
        kTalkMortal,
        kTalkDecline, ///< SC_POK, BA_POK: "not now" (DeclineFollowing)
        kTalkExtra1,  ///< SC_SCREAM; BA_ATTACK
        kTalkExtra2,  ///< SC_HEAL;   BA_MAD
        kTalkExtra3,  ///< SC_FEAR;   BA_SHOT
        kTalkExtra4,  ///< SC_PLFEAR; BA_KILL
        kTalkSlots
    };
    inline constexpr int kTalkScream = kTalkExtra1, kTalkHeal = kTalkExtra2, kTalkFear = kTalkExtra3,
                         kTalkPlFear = kTalkExtra4;
    inline constexpr int kTalkAttack = kTalkExtra1, kTalkMad = kTalkExtra2, kTalkShot = kTalkExtra3,
                         kTalkKill = kTalkExtra4;
    inline constexpr int kTalkGroups = kVoices * kTalkSlots;
    /// Where the talk table starts in Section::Speech, and how long its
    /// heading (first, count for each group) is.
    inline constexpr int kTalkTableAt = kSpeechGroups * kSpeechLines;
    inline constexpr int kTalkHeading = kTalkGroups * 2;
    /// The most lines of a group a map is given (all of the game's would be
    /// several hundred, and each a sound the map must name).
    inline constexpr int kTalkLines = 8;

    /// The game's name of each group ("!" before one line said as it is);
    /// null for one that voice has none of.
    inline const char *talkGroupName(int voice, int slot)
    {
        static const char *const kScientist[kTalkSlots] = {
            "SC_ANSWER", "SC_QUESTION", "SC_IDLE",   "SC_STARE",  "SC_OK",     "SC_WAIT",   "SC_STOP",   "SC_SCARED",
            "SC_HELLO",  "SC_PHELLO",   "SC_PIDLE",  "SC_PQUEST", "!SC_CUREA", "!SC_CUREB", "!SC_CUREC", "SC_SMELL",
            "SC_WOUND",  "SC_MORTAL",   "SC_POK",    "SC_SCREAM", "SC_HEAL",   "SC_FEAR",   "SC_PLFEAR"};
        static const char *const kBarney[kTalkSlots] = {
            "BA_ANSWER", "BA_QUESTION", "BA_IDLE", "BA_STARE",  "BA_OK",     "BA_WAIT",   "BA_STOP",   "BA_SCARED",
            "BA_HELLO",  nullptr,       nullptr,   "BA_PQUEST", "!BA_CUREA", "!BA_CUREB", "!BA_CUREC", "BA_SMELL",
            "BA_WOUND",  "BA_MORTAL",   "BA_POK",  "BA_ATTACK", "BA_MAD",    "BA_SHOT",   "BA_KILL"};
        if (slot < 0 || slot >= kTalkSlots)
            return nullptr;
        return voice == kVoiceScientist ? kScientist[slot] : (voice == kVoiceBarney ? kBarney[slot] : nullptr);
    }

    enum class ItemKind : std::uint8_t
    {
        None,
        Crowbar,
        Glock,
        Shotgun,
        GlockClip,
        Buckshot,
        HealthKit,
        Battery,
        Suit, ///< the HEV suit: armour is for someone wearing it
        Grenades, ///< hand grenades: five of them
        Mp5,        ///< the MP5: comes with 25 rounds
        Python,     ///< the .357: comes loaded
        Mp5Clip,    ///< 50 of 9 mm
        AmmoBox,    ///< 200 of 9 mm
        ArGrenades, ///< two for the MP5's launcher
        Ammo357,    ///< six
        // The weapons that came after the first six (and their ammunition):
        Crossbow,   ///< comes with five bolts in it
        Bolts,      ///< five for the crossbow
        Satchel,    ///< one charge
        Tripmine,   ///< one mine
        Snark,      ///< a nest of five
        // ... and the last four, and the box of ammunition (from level
        // version 31, cooked after even those above: kItemFirstLast):
        Rpg,        ///< comes with a rocket in it and one besides
        RpgClip,    ///< two rockets
        Gauss,      ///< 20 uranium
        GaussClip,  ///< 20 uranium (the egon's too)
        Egon,       ///< 20 uranium
        Hornetgun,  ///< full of hornets
        /// A weaponbox: ammunition of several kinds at once. ActorRecord::item
        /// is this in its low 8 bits, and above them where in the level's
        /// Strings what is in it is written: "uranium=60 rockets=8".
        Weaponbox,
        // From level version 39.
        Antidote, ///< one more antidote (Arsenal::antidotes)
        Security, ///< a security card (Arsenal::securityCards)
        /// Air for the player under water (item_airtank): it stays, and
        /// gives again 30 seconds on; shot, it bursts.
        Airtank,
        // From level version 45, cooked last of all.
        LongJump, ///< the long jump module (kArsenalLongJump): for someone wearing the suit
        Count
    };
    /// The first of those: an actor of one of these is cooked after every
    /// other actor (so that a save made before they were points at the same
    /// actors it did).
    inline constexpr ItemKind kItemFirstMore = ItemKind::Crossbow;
    /// ... and the first of the last ones, cooked after those (so that a
    /// save of a map of version 30 points at the same actors it did).
    inline constexpr ItemKind kItemFirstLast = ItemKind::Rpg;
    inline constexpr int kItemStringShift = 8; ///< a weaponbox's Strings offset, in ActorRecord::item
    /// Whether an item actor's ActorRecord::item makes sense: a kind there is,
    /// and for a weaponbox where its contents are in the level's Strings
    /// (`stringsSize` bytes); for anything else nothing above the kind.
    inline bool itemWordOk(std::uint32_t item, std::uint32_t stringsSize)
    {
        const std::uint32_t kind = item & 0xFFu;
        if (kind >= static_cast<std::uint32_t>(ItemKind::Count))
            return false;
        if (kind == static_cast<std::uint32_t>(ItemKind::Weaponbox))
            return (item >> kItemStringShift) < stringsSize;
        return (item >> kItemStringShift) == 0;
    }

    /// A sound the map plays: the file it is in under snd/ on the card,
    /// without the extension ("ambience_labdrone2.11k"). Empty: the map has
    /// no use for the sound of this index.
    ///
    /// `twin`, if not 0: a DSi has a better copy of it beside that one, at
    /// that many thousand samples a second (the same name with that number
    /// in it: "weapons_hks1.22k"). A DS has not the memory for those.
    struct SoundRef
    {
        char name[47];
        std::uint8_t twin;
    };
    static_assert(sizeof(SoundRef) == 48);
    /// In `twin`: a sound of a weapon the map was not cooked with
    /// (kModelRefLazy) -- not loaded with the map nor in its archive, but
    /// read out of the common one when it is first played.
    inline constexpr std::uint8_t kSoundLazy = 0x80;
    inline bool soundLazy(const SoundRef &ref)
    {
        return (ref.twin & kSoundLazy) != 0;
    }

    /// The name of a sound's better copy (SoundRef::twin): `name` with its
    /// rate changed. False if `name` is not of the usual shape.
    inline bool soundTwinName(const SoundRef &ref, char *out, std::size_t size)
    {
        const char *dot = nullptr;
        for (const char *c = ref.name; *c != '\0'; ++c)
            dot = *c == '.' ? c : dot;
        const int rate = ref.twin & ~kSoundLazy;
        if (rate == 0 || dot == nullptr)
            return false;
        const std::size_t stem = static_cast<std::size_t>(dot - ref.name) + 1;
        std::size_t end = stem;
        while (ref.name[end] >= '0' && ref.name[end] <= '9')
            ++end;
        // (stem, two digits, the letter that says whether it loops, NUL.)
        if (stem + 4 > size || ref.name[end] == '\0' || ref.name[end + 1] != '\0')
            return false;
        for (std::size_t i = 0; i < stem; ++i)
            out[i] = ref.name[i];
        out[stem] = static_cast<char>('0' + (rate / 10) % 10);
        out[stem + 1] = static_cast<char>('0' + rate % 10);
        out[stem + 2] = ref.name[end];
        out[stem + 3] = '\0';
        return true;
    }
    /// A sprite the map draws: its atlas (spr/<name>.<w>x<h>.htx on the
    /// card) and how that is cut into frames -- `columns` to a row, each
    /// `cellWidth` by `cellHeight` texels -- and how big it is in the world
    /// at scale 1 (units). No frames: the map has no use for the sprite of
    /// this index, or the game has no such file.
    struct SpriteRef
    {
        char name[24];
        std::uint16_t width;  ///< of the atlas, texels
        std::uint16_t height;
        std::uint8_t cellWidth;
        std::uint8_t cellHeight;
        std::uint8_t columns;
        std::uint8_t frames;
        std::uint16_t worldWidth;
        std::uint16_t worldHeight;
        std::uint8_t kind; ///< kSprite*: which way it faces
        /// How many frames the game's own sprite has: what its animation is
        /// timed by (the atlas may hold fewer, evenly taken).
        std::uint8_t sourceFrames;
        std::uint8_t pad[2];
    };
    static_assert(sizeof(SpriteRef) == 40);
    inline constexpr std::uint16_t kNoSprite = 0xFFFF;
    /// A tank's sprite field with no sprite in it (LogicKind::Tank).
    inline constexpr std::uint16_t kTankSpriteNone = 0x0FFF;

    /// What a logic record sounds like: indices into Sounds, 0xFFFF for
    /// none. A door's or a button's moving and stopping (a moving sound
    /// that goes round is heard for as long as the thing moves); what
    /// something that breaks sounds like breaking (in `move`); what
    /// something that turns hums like (in `move`). And what a door or a
    /// button that will not work for the player answers with: a sound, and
    /// a spoken line (into Sentences).
    struct LogicSound
    {
        std::uint16_t move;
        std::uint16_t stop;
        std::uint16_t locked;
        std::uint16_t lockedLine;
        /// ... and with what, when it works for them after having refused.
        std::uint16_t unlocked;
        std::uint16_t unlockedLine;
    };
    static_assert(sizeof(LogicSound) == 12);
    /// A spoken line: `wordCount` words from SentenceWords, one after
    /// another, at the line's `volume`.
    struct SentenceRecord
    {
        std::uint16_t firstWord;
        std::uint8_t wordCount;
        std::uint8_t volume; ///< 0..127
        std::uint8_t pitch;  ///< (not used: each word has its own)
        std::uint8_t pad[3];
    };
    static_assert(sizeof(SentenceRecord) == 8);
    /// One word of a line, as the game's sentences.txt has it said: which
    /// sound, how fast (percent; 0: as recorded), how loud (percent of the
    /// line's), and where it is cut off (percent of its length).
    struct SentenceWord
    {
        std::uint16_t sound; ///< into Sounds
        std::uint8_t pitch;
        std::uint8_t volume;
        std::uint8_t end;
        std::uint8_t pad[3];
    };
    static_assert(sizeof(SentenceWord) == 8);

    /// A sound one of the map's models makes in one of its clips (a
    /// footstep in a walk, a line in a scene): the hash of its name, which
    /// is what the model's event knows it by (HlModel.hpp), and where it is
    /// in Sounds. Sorted by hash.
    struct ModelSound
    {
        std::uint32_t hash;
        std::uint16_t sound;
        std::uint16_t pad;
    };

    /// A place with a sound of its own: within `radius` units of `origin`,
    /// what is heard echoes as a room of the game's type `type` does
    /// (HlRoomPresets.hpp).
    struct RoomRecord
    {
        fx origin[3];
        std::uint16_t radius;
        std::uint8_t type;
        std::uint8_t pad;
    };

    /// Where something that walks can stand, as the map's author marked it:
    /// its feet, in the world -- and the ways on from there (`linkCount` of
    /// PathLinks from `firstLink`).
    struct PathNode
    {
        fx pos[3];
        std::uint16_t firstLink;
        std::uint8_t linkCount;
        std::uint8_t flags; ///< kPathNode*
    };
    static_assert(sizeof(PathNode) == 16);
    inline constexpr std::uint8_t kPathNodeWater = 1; ///< under water
    /// In the air: for what flies only (a map with an alien controller).
    inline constexpr std::uint8_t kPathNodeAir = 2;
    inline constexpr std::uint8_t kNoPathNode = 255;
    inline constexpr int kMaxPathNodes = 254;
    /// A way from one node to another: who can take it (by size), how long
    /// it is, and the brush that stands in it -- a door, which may be shut
    /// or locked just now, or something that has to be broken first.
    ///
    /// Where the game came with its own graph of a map
    /// (maps/graphs/<map>.nod) the links are that graph's, each tried once
    /// with the walk the console really makes; otherwise they are worked
    /// out. There is no table of ways from each node to each: the console
    /// finds its way when it needs one, over the links as they are at that
    /// moment (HlWalk.hpp, pathStep).
    struct PathLink
    {
        std::uint8_t to;
        std::uint8_t flags;   ///< kPathLink*
        std::uint16_t length; ///< whole units
        std::uint16_t brush;  ///< into Brushes, or kNoBrush
        std::uint16_t pad;
    };
    static_assert(sizeof(PathLink) == 8);
    inline constexpr std::uint8_t kPathLinkSmall = 1; ///< for what is 24 units across (a headcrab)
    inline constexpr std::uint8_t kPathLinkHuman = 2; ///< for a person
    inline constexpr std::uint8_t kPathLinkLarge = 4; ///< for what is 64 across (a bullsquid)
    /// One way only: off a ledge, down to the node below.
    inline constexpr std::uint8_t kPathLinkDrop = 8;
    /// For what flies (on a map that has something that does).
    inline constexpr std::uint8_t kPathLinkFly = 16;

    /// A person or a prop that is a model: where it starts and as what.
    struct ActorRecord
    {
        std::uint16_t model;  ///< into ModelRefs
        std::uint16_t name;   ///< name id; 0 = none
        fx origin[3];         ///< its feet
        std::uint16_t yaw;    ///< turns
        std::uint8_t body;
        std::uint8_t kind;    ///< ActorKind
        std::uint32_t clip;   ///< nameHash of the clip it starts in; 0 = the model's first
        std::uint8_t flags;   ///< kActor*
        std::uint8_t light;   ///< how bright it stands, 0..31
        /// Name id of what an item fires when it is taken, or a turret when
        /// it comes out, folds away or is destroyed; 0 = nothing.
        std::uint16_t target;
        /// For an ActorKind::Item: its ItemKind. For an osprey or an apache: the corner
        /// it flies to first, plus one (0: none), in the low 16 bits (for big
        /// momma, her first MommaNode record, the same) -- it
        /// has one skin, and sets nothing off. For anything else: its
        /// skin in bits 8..15 (kActorSkinShift), and for a soldier which
        /// weapons he has in the low 8 (kGrunt*) and the name id of his
        /// squad in the top 16. A tripmine's low 8 are how far up it
        /// points, in 256ths of a turn.
        std::uint32_t item;
    };
    inline constexpr int kActorSkinShift = 8;
    inline constexpr int kActorSquadShift = 16;
    /// ActorRecord::light, its top bit: the map's SF_MONSTER_FADECORPSE --
    /// dead, it fades away (the game's FadeMonster). The rest is the light.
    inline constexpr std::uint8_t kActorLightFadeCorpse = 0x80;

    enum class EntityClass : std::uint16_t
    {
        Worldspawn,
        InfoPlayerStart,
        /// Where a player comes into a game between consoles (the game's
        /// info_player_deathmatch): a map that has any is a multiplayer map.
        InfoPlayerDeathmatch,
        /// Where the game shows the map between matches (its
        /// info_intermission), facing as it does: what the map's preview is
        /// taken from. Pitch as an entity's angles have it: down is more.
        InfoIntermission,
    };

    struct EntityRecord
    {
        std::uint16_t classId; ///< EntityClass
        std::uint16_t flags;
        fx origin[3];
        std::uint16_t angles[3]; ///< pitch, yaw, roll in turns
        std::uint16_t model;     ///< into Models; 0 = none
        std::uint32_t reserved[2];
    };

    static_assert(sizeof(SectionDir) == 12);
    static_assert(sizeof(LevelHeader) == 64);
    static_assert(sizeof(Plane) == 16);
    static_assert(sizeof(Node) == 20);
    static_assert(sizeof(Leaf) == 24);
    static_assert(sizeof(Surface) == 24);
    static_assert(sizeof(Model) == 44);
    static_assert(sizeof(ClipNode) == 8);
    static_assert(sizeof(TextureRef) == 40);
    static_assert(sizeof(EntityRecord) == 32);
    static_assert(sizeof(Brush) == 40);
    static_assert(sizeof(LogicRecord) == 80);
    static_assert(sizeof(Landmark) == 16);
    static_assert(sizeof(ModelRef) == 24);
    static_assert(sizeof(ActorRecord) == 32);
    static_assert(sizeof(LogicAux) == 8);
    static_assert(sizeof(PathCorner) == 28);

    /// Light that changes. The world's light is in its vertices' colours,
    /// in the geometry stream; where some of it comes from a light a switch
    /// turns on and off, or one that flickers (the game's light "styles":
    /// 1 to 12 are its patterns, 32 and up are switched), the colour word of
    /// each vertex it reaches has records here: first one with `style`
    /// kPatchBase and the colour with none of that light, then one for each
    /// style reaching it with what that style adds at its full. The console
    /// works the word out again whenever a style's strength changes
    /// (HlLightStyle.hpp).
    struct LightPatch
    {
        std::uint32_t word;   ///< into GxStream: a colour command's parameter
        std::uint16_t colour; ///< five bits each of red, green, blue
        std::uint8_t style;   ///< kPatchBase, or the style (under kLightStyles)
        std::uint8_t pad;
    };
    static_assert(sizeof(LightPatch) == 8);
    inline constexpr std::uint8_t kPatchBase = 0xFF;
    inline constexpr int kLightStyles = 64;

    /// Something the map writes over the picture: a chapter's name, a name
    /// of the introduction, a lesson of the hazard course (the game's
    /// titles.txt says what and how; an env_message says when).
    struct MessageRecord
    {
        std::uint32_t text;     ///< into Strings: its lines, '\n' between them
        /// Where: 0..256 of the room left over across and down the screen;
        /// -1 in the middle.
        std::int16_t x, y;
        std::uint8_t effect;    ///< kMessage*: how it comes and goes
        std::uint8_t flags;     ///< kMessageLogo, kMessageAtStart
        std::uint16_t colour;   ///< five bits each of red, green, blue
        std::uint16_t colour2;  ///< kMessageScan: what a letter is while it is new
        std::uint16_t fadeInMs; ///< (kMessageScan: for each letter)
        std::uint16_t fadeOutMs;
        std::uint16_t holdMs;
        std::uint16_t fxMs;     ///< kMessageScan: how long a letter stays new
        std::uint16_t pad;
    };
    static_assert(sizeof(MessageRecord) == 24);
    inline constexpr std::uint8_t kMessageFade = 0;
    inline constexpr std::uint8_t kMessageFlicker = 1; ///< (shown as a fade)
    inline constexpr std::uint8_t kMessageScan = 2;    ///< written out letter by letter
    /// Not text: the game's own title, as a picture (ui/title.hic).
    inline constexpr std::uint8_t kMessageLogo = 1;
    /// Shown by itself when the map is come into (the chapter's name).
    inline constexpr std::uint8_t kMessageAtStart = 2;
    /// The title of the chapter this map is part of (every map has one such
    /// message, shown or not): what a save made in the map is listed under.
    inline constexpr std::uint8_t kMessageChapter = 4;

    /// The size of one record of a section, which is how both the writer and
    /// the reader know that `size == count * recordSize`.
    constexpr std::uint32_t sectionRecordSize(Section section)
    {
        switch (section)
        {
        case Section::Planes:     return sizeof(Plane);
        case Section::PlaneTypes: return 1;
        case Section::Nodes:      return sizeof(Node);
        case Section::Leaves:     return sizeof(Leaf);
        case Section::Marks:      return 2;
        case Section::Vis:        return 1;
        case Section::Surfaces:   return sizeof(Surface);
        case Section::GxStream:   return 4;
        case Section::Models:     return sizeof(Model);
        case Section::ClipNodes:  return sizeof(ClipNode);
        case Section::Textures:   return sizeof(TextureRef);
        case Section::Entities:   return sizeof(EntityRecord);
        case Section::EntityText: return 1;
        case Section::LightPatch: return sizeof(LightPatch);
        case Section::Brushes:    return sizeof(Brush);
        case Section::BrushLeaves: return 2;
        case Section::Logic:      return sizeof(LogicRecord);
        case Section::LogicAux:   return sizeof(LogicAux);
        case Section::PathCorners: return sizeof(PathCorner);
        case Section::NameHashes: return 4;
        case Section::Landmarks:  return sizeof(Landmark);
        case Section::Strings:    return 1;
        case Section::ModelRefs:  return sizeof(ModelRef);
        case Section::Actors:     return sizeof(ActorRecord);
        case Section::PathNodes:  return sizeof(PathNode);
        case Section::PathNext:   return sizeof(PathLink);
        case Section::Sounds:     return sizeof(SoundRef);
        case Section::LogicSounds: return sizeof(LogicSound);
        case Section::Sentences:  return sizeof(SentenceRecord);
        case Section::SentenceWords: return sizeof(SentenceWord);
        case Section::Rooms:      return sizeof(RoomRecord);
        case Section::Sprites:    return sizeof(SpriteRef);
        case Section::Speech:     return 2;
        case Section::Messages:   return sizeof(MessageRecord);
        case Section::ModelSounds: return sizeof(ModelSound);
        case Section::NodeMarks:  return 2;
        case Section::NodeSpans:  return sizeof(NodeSpan);
        default:                  return 0;
        }
    }

    inline constexpr std::uint32_t kLevelDataStart =
        sizeof(LevelHeader) + kSectionCount * sizeof(SectionDir);

    // ---- On the card -------------------------------------------------------
    //
    // A map is one archive, maps/<map>.npac (Nitro Engine Advanced's NPAC,
    // mounted as a drive), holding everything the map loads in the order it
    // loads it: the level, then tex/, spr/, mdl/ and snd/. The level itself is
    // in pieces, "level.0", "level.1"...: the first is the header and the
    // section directory, which is all that has to be read to know what a map
    // is; the rest follow in pieces of kLevelPieceBytes, each compressed on
    // its own. A compressed member is inflated whole as it is opened, and a
    // DS has not the memory to do that to a whole level beside the level.
    inline constexpr std::uint32_t kLevelPieceBytes = 256 * 1024;

    /// How many pieces a level of `fileSize` bytes is in, the first included.
    constexpr std::uint32_t levelPieces(std::uint32_t fileSize)
    {
        return fileSize <= kLevelDataStart
                   ? 1
                   : 1 + (fileSize - kLevelDataStart + kLevelPieceBytes - 1) / kLevelPieceBytes;
    }
    /// Where piece `piece` begins in the level, and how long it is.
    constexpr std::uint32_t levelPieceStart(std::uint32_t piece)
    {
        return piece == 0 ? 0 : kLevelDataStart + (piece - 1) * kLevelPieceBytes;
    }
    constexpr std::uint32_t levelPieceBytes(std::uint32_t fileSize, std::uint32_t piece)
    {
        const std::uint32_t start = levelPieceStart(piece);
        const std::uint32_t end = piece == 0 ? kLevelDataStart : start + kLevelPieceBytes;
        return start >= fileSize ? 0 : (end < fileSize ? end : fileSize) - start;
    }

    /// CRC-32 (the zlib polynomial), a byte at a time through a table. The
    /// table is a kilobyte of the DS's memory; going bit by bit instead made
    /// the check of a level a large part of loading it.
    inline constexpr std::array<std::uint32_t, 256> kCrcTable = []
    {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            table[i] = crc;
        }
        return table;
    }();

    /// A CRC taken a piece at a time: start with kCrcStart, add each piece
    /// with crc32Update, and the CRC is crc32Finish of what that leaves --
    /// the same as crc32 of all of it at once.
    inline constexpr std::uint32_t kCrcStart = 0xFFFFFFFFu;
    constexpr std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t *data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            crc = (crc >> 8) ^ kCrcTable[(crc ^ data[i]) & 0xFF];
        return crc;
    }
    constexpr std::uint32_t crc32Finish(std::uint32_t crc) { return ~crc; }

    constexpr std::uint32_t crc32(const std::uint8_t *data, std::uint32_t size)
    {
        return crc32Finish(crc32Update(kCrcStart, data, size));
    }
} // namespace hldata
