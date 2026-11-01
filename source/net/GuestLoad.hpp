#pragma once

// What a console that was sent the game (HLDS_CHILD) is sent, taken as it
// comes (HlGuest.hpp's streams):
//
//  - the common stream, at the start: its files put on the "air:" drive whole
//    for the game to read in its own order as it starts up (the letters, the
//    display's pictures, the menu's sounds), and let go of once it has
//    (dropCommon);
//  - a group's base: each model's block and sound's clip kept as it came,
//    by its name, from one map to the next; the map's loaders borrow them
//    (borrow) rather than read them;
//  - a map's own stream: given to the "air:" drive (beginMap), whose files
//    the map's loaders read as they come, each straight into its own block.
//
// Where the bytes come from is a GuestSupply: the air -- the host's frames,
// HlNetAir.hpp, taken as they come while the stream is read -- or, for a
// test with no host, what the build was given (MemorySupply, in pieces the
// size of what the air brings).

#include <cstdint>
#include <cstdio>
#include <string>

#include "HlGuest.hpp"
#include "HlNetAir.hpp"

namespace hldata
{
    struct LevelView;
}

namespace hlrt::guest
{
    /// A stream already in memory, given out a piece at a time.
    struct MemorySupply : hldata::GuestSupply
    {
        const std::uint8_t *data = nullptr;
        std::uint32_t size = 0;
        std::uint32_t at = 0;
        std::uint32_t piece = 600;
        bool more(const std::uint8_t *&out, std::size_t &length) override;
    };

    bool receiveCommon(hldata::GuestSupply &supply);
    void dropCommon();
    bool receiveBase(hldata::GuestSupply &supply);
    /// A model's block or a sound's clip (`name` as the stream has it:
    /// "mdl/v_357.hmd", "snd/weapons_357_shot1.11k.hsn") the base holds.
    bool borrow(const char *name, const std::uint8_t *&data, std::uint32_t &size);

    /// A stream on a card, read a packet's worth at a time: for a test that
    /// gives the child its data as the air would, with none of it in memory
    /// (HLDS_CHILD_FROMCARD).
    struct FileSupply : hldata::GuestSupply
    {
        std::FILE *file = nullptr;
        std::uint8_t piece[600];
        bool more(const std::uint8_t *&out, std::size_t &length) override;
    };

    /// A map's stream, for a test: where it is (MemorySupply), or the file
    /// on the card it is in (FileSupply).
    void offerMap(const std::string &map, const std::uint8_t *data, std::uint32_t size);
    void offerMapFile(const std::string &map, const std::string &path);
    bool offered(const std::string &map);
    /// The maps offered, in order.
    int offeredCount();
    std::string offeredName(int index);
    /// Its stream to the "air:" drive, as the map is loaded; false if there
    /// is none, or it is not a stream of this version.
    bool beginMap(const std::string &map);
    /// The map is loaded: what is left of its stream passed over, and the
    /// drive's stream let go.
    void endMap();

    // ---------------------------------------------------------------- the air

    /// Ask the host for `stream` (the common one, on coming), and take it
    /// whole when it comes: receiveAir.
    void airWant(hldata::AirStream stream);
    /// A frame of the host's heard in the lobby that is a piece of a stream:
    /// what it means here. `reply` (kNetReplyBytes): an answer to give in
    /// place of the game's, `replySize` 0 if none. True if it begins a
    /// stream this console is to take now: `head` says which (receiveAir; a
    /// map, offerAir and load it).
    bool airFrame(const std::uint8_t *bytes, int size, std::uint8_t *reply, int &replySize, hldata::AirHead &head);
    /// The stream begun or asked for, taken whole (the common one, or a
    /// group's base, which replaces the one held). `progress`, if any, is
    /// called now and then with how far it has got (0..100). False if it
    /// could not be (the host gone quiet).
    bool receiveAir(void (*progress)(int percent));
    /// The map being sent, to be loaded from the air: beginMap reads it as
    /// it comes.
    void offerAir(const std::string &map);
    /// What is told how far a map's stream has got as it loads (0..100), as
    /// receiveAir's `progress` is for the others.
    void onMapProgress(void (*progress)(int percent));
    /// Between the steps of a map's load: the map's stream heard and
    /// answered, or the host, hearing nothing, leaves this console out.
    void hear();
    /// What the program's sender left it (HlNetAir.hpp), wherever this memory
    /// map has it: read once, at the very start, before the heap can reach
    /// it. False: not sent by Download Play.
    bool bootParam(hldata::NetDlParam &param);
    /// Out of a game: no map is said to be held (the next host's numbers
    /// begin again, and would look like this one's).
    void forgetTaken();
    /// The stream being taken: how long it is, and how fast it has come
    /// (KB a second, since it began).
    std::uint32_t airLength();
    int airRate();
    /// The group whose base is held ("": none).
    const char *baseGroup();
    /// How the stream being taken stands, in a line (for when it was not).
    void airStatus(char *line, int room);
    /// The map last taken whole from the air, and how hard its match is.
    const char *airMap();
    int airSkill();

    // -------------------------------------------------------------- a demo

    /// A console in DSi mode playing a demo: its memory for the demo's streams
    /// (longest: the longest), its first map asked for and offered. All its
    /// streams at full quality, till demoEnd. False: not enough memory.
    bool demoBegin(std::uint32_t longest, const char *map);
    void demoEnd();
    /// All of the map being played has come.
    bool demoMapHere();
    /// A map being loaded: its textures wait, for the air it comes on.
    bool demoLoading();
    /// The map is loaded: where its ways out lead, to get them ready.
    void demoLoaded(const hldata::LevelView &view);
    /// Once in a while as a frame is sent: what to say of the map coming, in
    /// place of the game's frame. 0: nothing.
    int demoAnswer(std::uint8_t *reply);

    /// What the guest holds of what it was sent, by kind (the readout's,
    /// and the converter's estimate's check): the base's bytes, and what the
    /// last map's stream gave its loaders.
    struct Held
    {
        std::uint32_t baseBytes = 0;
        std::uint32_t mapBytes[static_cast<int>(hldata::GuestKind::Count)] = {};
    };
    const Held &held();
    /// `bytes` of a map's `kind` taken by its loader (for held()).
    void count(hldata::GuestKind kind, std::uint32_t bytes);
} // namespace hlrt::guest
