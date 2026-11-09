#pragma once

// The radio: the DS's local wireless, as one console the host and up to
// three its clients (dswifi's local multiplayer mode, of the fork that also
// does Download Play). After Skyjo-DS's NetLink, which found out the hard
// way most of what is written in the .cpp.
//
// Only frames go through here: what is in them is shared/HlNet.hpp's. Every
// frame the host sends one that all the clients hear (send), and each
// client's answer goes back by itself; what has come is read with take.
// Frames arrive in an interrupt and are copied into a ring there; nothing
// else happens outside the caller's own time.
//
// All of dswifi is behind this header.

#include <cstddef>
#include <cstdint>

namespace hlrt::netlink
{
    /// The channel a host is on: fixed, so that a console that knows which
    /// host it wants can go straight to it.
    inline constexpr int kChannel = 6;
    /// What a beacon of this game says it is.
    inline constexpr std::uint32_t kGameId = 0x484C4453; // "HLDS"

    /// Online: the relay over the Internet in place of the radio
    /// (NetRelay.hpp) for everything a lobby and a match use -- host, scan,
    /// join, the frames. Whichever was on is powered down first. (Download
    /// Play and joinDirect are the radio's alone.)
    void useRelay(bool on);
    bool usingRelay();

    /// Leave whatever was going on and be silent. Safe at any time.
    void shutdown();
    /// ... and give the radio and the library back altogether (on leaving
    /// multiplayer for the title).
    void powerDown();
    /// Why the radio could not be started, as the library numbers it.
    int failStage();

    // ------------------------------------------------------------- the host
    /// Be a host, seen by consoles that look, under `name`, for `most`
    /// players at most (itself among them). False if the radio could not be
    /// started.
    bool host(const char *name, int most);
    /// Which clients are there: bit n for client n (1..7).
    unsigned clients();
    /// What the beacon says of the match (HlNet.hpp's NetRoomInfo, as
    /// netRoomWrite has it): seen by consoles looking from the next beacon.
    void hostRoom(const std::uint8_t *bytes, int size);
    /// ... and, on a console looking, what host `index` says of its match
    /// (`size` 0 for a host that says nothing), and how strong its signal is.
    bool hostRoomAt(int index, std::uint8_t *bytes, int &size, int &rssi);
    /// On a client: how strong the host's signal is (0..255).
    int strength();
    /// Let more consoles in, or not (a match under way lets nobody in).
    void admit(bool allow);
    /// Send client `c` away (one that stopped answering).
    void kick(int c);

    // ----------------------------------------------------------- a client
    /// Look for hosts.
    bool scan();
    /// How many things the scan has found, and whether number `index` is a
    /// host of this game that lets people in: its name, and how many are in
    /// it of how many.
    int found();
    bool hostAt(int index, char *name, int room, int *players, int *most);
    /// Join the host that is number `index` of what was found.
    bool join(int index);
    /// Join the host with this address, without having looked for it (a
    /// console started by Download Play knows who started it). `again`: on
    /// a radio already set to do so, ask once more.
    bool joinDirect(const std::uint8_t address[6], bool again);
    bool joined();     ///< it is this host's client now
    bool refused();    ///< ... or the host would not have it

    // --------------------------------------------------------------- frames
    /// Send a frame: the host's to all its clients, a client's to the host.
    void send(const void *bytes, int size);
    /// The oldest frame not yet read: its bytes into `into` (which holds a
    /// frame of any length), and who it is from (a client's number; 0 for
    /// the host's). Returns its length; 0 if there is none.
    int take(std::uint8_t *into, int &from);
    /// How many frames were dropped because nobody read them in time.
    unsigned dropped();
    /// A client: what may take a frame of the host's as it arrives, in the
    /// interrupt, before it is put in the ring -- a stream's pieces, put
    /// straight where they go (GuestLoad), so that a loader busy for a while
    /// loses none. True if it took it. (In an interrupt: it may not
    /// allocate, print, or wait.)
    using Sink = bool (*)(const std::uint8_t *bytes, int size);
    void setSink(Sink sink);

    /// This console's own address, and the name its owner gave it.
    void address(std::uint8_t out[6]);
    void ownerName(char *out, int room);

    // ------------------------------------------------------- Download Play
    /// Offer `rom` (a whole program, which must stay where it is until
    /// dlStop) to consoles looking at their Download Play menu. `param` is
    /// 32 bytes handed to the program when it starts. The consoles that
    /// come are held (gathered) until dlGather(false): then all of them are
    /// sent it at once -- a console will not join a host already sending.
    bool dlStart(const void *rom, std::size_t size, const char *description, const void *param);
    void dlGather(bool hold);
    void dlUpdate();      ///< once a frame while offering
    void dlStop();
    bool dlActive();
    unsigned dlGuests();  ///< bit n: guest n is there
    /// How far guest `n` has got with the program: 0..100; -1 if it is not
    /// there.
    int dlProgress(int n);
    /// Whether guest `n` is checking the program it was sent (it is the
    /// console's own Download Play that does), or has failed.
    bool dlChecking(int n);
    bool dlFailed(int n);
    /// The name guest `n`'s owner gave it ("" until it has said).
    const char *dlName(int n);
    /// Whether every guest there has the whole program and is waiting (one
    /// that failed aside), and there is one.
    bool dlAllReady();
    void dlLock(bool locked); ///< no more guests
    /// How fast the program goes out: the library's own pace (Nintendo's:
    /// six blocks ahead of what the consoles have said they have), or far
    /// ahead of it -- they keep any block they are sent.
    void dlFast(bool fast);
    /// The radio's own counts while it sends (they wrap; what matters is how
    /// much they move): exchanges begun, done, done again for a client that
    /// did not answer, frames the sending could not queue; and the most
    /// blocks of the program any guest has.
    struct DlCounts
    {
        unsigned armed = 0, done = 0, retried = 0, refused = 0, blocks = 0;
    };
    DlCounts dlCounts();
    /// Frames the library could not queue, for want of room (all along).
    unsigned txRefused();
    bool dlBootAll();         ///< start the program on all of them
} // namespace hlrt::netlink
