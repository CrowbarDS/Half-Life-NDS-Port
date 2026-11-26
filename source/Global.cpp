#include "Global.hpp"
#include "DebugLog.hpp"
#include "render/BottomScreen.hpp"
#include "Settings.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <nds/arm9/dldi.h>
#include <sys/stat.h>

#include "HlArchive.hpp"
#include "HlRtcom.hpp"
#include "HlManifest.hpp"
#include "HlGuest.hpp"
#include "net/AirDevice.hpp"
#include "net/GuestLoad.hpp"
#include "data/VramWriter.hpp"
#include "platform/Slot2.hpp"
#include <cstddef>
extern "C" {
#include <NEAAlloc.h> // (no C++ guard of its own)
}
#include "Overlay.hpp"
#include "render/HudDraw.hpp"
#include "render/GlowLayer.hpp"
#include "render/WaterWobble.hpp"
#include "render/Camera.hpp"
#include "game/Think.hpp"
#include "render/BankD.hpp"
#include "render/GpuStats.hpp"
#include "render/ScreenFade.hpp"

namespace hlrt
{
    void hwMathTune(bool dsi); // HwMathTune.arm.cpp
}

Global global;

namespace
{
    volatile std::uint32_t vblankCount = 0;

    /// NEA's own vblank work, plus the tick the game's clock runs on.
    HL_ITCM void onVblank()
    {
        const hlrt::DebugLog::Perf::Timed timed(hlrt::DebugLog::Section::Vblank);
        vblankCount = vblankCount + 1;
        // First: what is written to video memory has only until line 206.
        hlrt::vramWriter.onVblank();
        hlrt::GpuStats::onVblank();
        // The display's sprites, as the frame before made them.
        hlrt::hudDraw.onVblank();
        hlrt::glowLayer.onVblank();
        // ... and what of them a fade's quad lets through: with its picture.
        hlrt::screenFade.onVblank();
        hlrt::waterWobble.onVblank();
        // (A touch of STOP while a long job holds the console: kept for it.)
        hlrt::BottomScreen::onVblank();
        NEA_VBLFunc();
    }
#ifdef HLDS_PROFILE
    // (Its time, apart: what the ARM9 does besides the game, DebugLog.)
    void profiledHblank()
    {
        const hlrt::DebugLog::Perf::Timed timed(hlrt::DebugLog::Section::Hblank);
        NEA_HBLFunc();
    }
#endif
} // namespace

// (ITCM round three, clean counts: missed often for its size.)
HL_ITCM std::uint32_t Global::vblanks()
{
    return vblankCount;
}

HL_ITCM bool Global::hasRoom()
{
    return global.isDSi || hlrt::slot2::pakBytes() != 0;
}

namespace
{
    bool vblanksCounted = false;

    // A thread that only waits for VBlank, above all others: every VBlank
    // has a thread waiting for it, and so libnds never remembers one that
    // came with none -- after which its next wait for VBlank would not
    // sleep, whoever asked (a frame longer than a VBlank; dswifi's thread
    // waits for one too, now and then: so not always). Every wait for
    // VBlank is again for the next one: the game's, a menu's, Nitro
    // Engine's, which put the textures into video memory then. (Woken, it
    // waits again at once. Its stack is its two calls': 16 bytes, measured
    // as by their frames (perf.txt's "waiter stack"). Interrupts have a
    // stack of their own, and a thread's registers are kept out of its
    // stack, in libnds's record of it; a debug libnds puts 8 bytes of
    // pattern at each end.)
    alignas(8) std::uint32_t vblankWaiterStack[256 / 4];
#ifdef HLDS_PROFILE
    constexpr std::uint32_t kWaiterPaint = 0x7A17E25Bu;
#endif
    HL_ITCM int vblankWaiter(void *)
    {
        for (;;)
            cothread_yield_irq(IRQ_VBLANK);
        return 0;
    }
}

std::uint32_t Global::vblankWaiterPeak()
{
#ifdef HLDS_PROFILE
    std::size_t untouched = 0;
    while (untouched < sizeof(vblankWaiterStack) / 4 && vblankWaiterStack[untouched] == kWaiterPaint)
        ++untouched;
    return static_cast<std::uint32_t>(sizeof(vblankWaiterStack) - untouched * 4);
#else
    return 0;
#endif
}

void Global::waitVBlank()
{
    if (!vblanksCounted)
    {
        swiWaitForVBlank();
        return;
    }
    // (Until a VBlank of its own comes: should libnds ever have remembered
    // one that came before -- the VBlank waiter above sees to it that it
    // does not -- that one is not the one waited for.)
    const std::uint32_t was = vblankCount;
    do
        cothread_yield_irq(IRQ_VBLANK);
    while (vblankCount == was);
}

namespace
{
    std::uint32_t drawClockAt = 0;
}

HL_ITCM std::uint32_t Global::drawClock()
{
    return drawClockAt;
}

HL_ITCM void Global::latchDrawClock()
{
    drawClockAt = vblankCount;
}

void Global::process(void (*draw)())
{
    // A 3DS's word about its top screen (arm7/source/Rtcom.cpp), once, and
    // the picture made to match if the screen setting follows it.
    static bool heard = false;
    if (!heard && fifoCheckValue32(FIFO_USER_02))
    {
        heard = true;
        const std::uint32_t word = fifoGetValue32(FIFO_USER_02);
        rtcomAnswered = (word & hldata::kRtcomAnswered) != 0;
        wideTop = (word & hldata::kRtcomWide) != 0;
        if (wideTop)
            hlrt::camera.applyScreenShape();
    }
    NEA_Process(draw);
}

namespace
{
#ifndef HLDS_CHILD
    // A 3DS's wide TwlBg, left in Luma's place for it: TWiLight Menu++ puts
    // its Widescreen.cxi there for one launch and takes it back on its own
    // next start -- a game started otherwise leaves it, and every DS(i)
    // program after is shown wide. Put back where TWiLight keeps it (not
    // over one already there: then it is left as it is).
    void putBackWideTwlBg(const std::string &root)
    {
        struct stat st;
        const std::string from = root + "luma/sysmodules/TwlBg.cxi";
        if (stat(from.c_str(), &st) != 0)
            return;
        const std::string dir = root + "_nds/TWiLightMenu/TwlBg";
        const std::string to = dir + "/Widescreen.cxi";
        if (stat(to.c_str(), &st) == 0)
            return;
        mkdir((root + "_nds").c_str(), 0777);
        mkdir((root + "_nds/TWiLightMenu").c_str(), 0777);
        mkdir(dir.c_str(), 0777);
        std::rename(from.c_str(), to.c_str());
    }
#endif
}

#ifdef HLDS_CHILD
// What a child build was given to start with (tools/child_testpack.py), for
// a test with no host: the files it holds, put on the "air:" drive as if
// they had been received. Empty in a build made to be sent.
// (bin2c gives its size as a macro, in a header of the child's build: the
// container says where each file is, and it was made by the same build.)
extern "C" const std::uint8_t airpack_bin[];

namespace
{
    void putTestPack()
    {
        struct Entry
        {
            char name[32];
            std::uint32_t at;
            std::uint32_t size;
        };
        constexpr std::uint32_t kMagic = 0x42414C48; // "HLAB"
        std::uint32_t head[2];
        std::memcpy(head, airpack_bin, sizeof(head));
        if (head[0] != kMagic || head[1] > 8)
            return;
        // (The streams as they would come over the air, a packet's worth at
        // a time: the common one, the group's base, the map's.)
        for (std::uint32_t i = 0; i < head[1]; ++i)
        {
            Entry entry;
            std::memcpy(&entry, airpack_bin + 8 + i * sizeof(Entry), sizeof(entry));
            entry.name[sizeof(entry.name) - 1] = '\0';
            const std::string name = entry.name;
            hlrt::guest::MemorySupply supply;
            supply.data = airpack_bin + entry.at;
            supply.size = entry.size;
            if (name == "common.hgs")
                hlrt::guest::receiveCommon(supply);
            else if (name == "base.hgs")
                hlrt::guest::receiveBase(supply);
            else if (name.rfind("maps/", 0) == 0 && name.size() > 9)
                hlrt::guest::offerMap(name.substr(5, name.size() - 9), supply.data, supply.size);
            else
                hlrt::air::put(entry.name, airpack_bin + entry.at, entry.size);
        }
    }
} // namespace
#endif

void Global::Init(const char *launchedFrom)
{
    // First, before anything is timed against the CPU: every later measurement
    // and every budget chosen from isDSi assumes the clock is already what it
    // is going to be. setCpuClock is a no-op outside DSi mode.
    isDSi = isDSiMode();
    #ifdef HLDS_CHILD
    twlChild = isDSi && *reinterpret_cast<const volatile std::uint32_t *>(0x02FFDFF8) == 0x434C5754;
    #endif
    if (isDSi)
    {
        setCpuClock(true);
        fastClock = true;
    }
    // The root's way on this console (HwMath.s): its wait is counted in the
    // CPU's instructions, so after the clock is set.
    hlrt::hwMathTune(isDSi);

    // The GBA slot: an Expansion Pak or a Rumble Pak (none in DSi mode).
    hlrt::slot2::init();

    // (The HBlank interrupt is set, not turned on: it counts lines for Nitro
    // Engine's CPU meter, all it does here -- the game sets none of its line
    // effects -- and costs some 3% of the ARM9, every line of every frame.
    // The readout, which shows the meter, turns it on while it is shown:
    // DebugOverlay.)
    irqSet(IRQ_VBLANK, onVblank);
    vblanksCounted = true;
#ifdef HLDS_PROFILE
    for (std::uint32_t &w : vblankWaiterStack)
        w = kWaiterPaint;
#endif
    // (Made at its priority: one set afterwards takes effect only once the
    // thread has run.)
    cothread_create_priority_manual(vblankWaiter, nullptr, vblankWaiterStack, sizeof(vblankWaiterStack), 127,
                                    0);
#ifdef HLDS_PROFILE
    irqSet(IRQ_HBLANK, profiledHblank);
#else
    irqSet(IRQ_HBLANK, NEA_HBLFunc);
#endif

#ifdef HLDS_CHILD
    // Sent the game: no card is looked for (on a console that has one, it is
    // not this game's), and what was sent is on the "air:" drive. For a
    // test, what a build was given to start with (HLDS_CHILD_TESTPACK:
    // tools/child_testpack.py) is put there now, as though received.
    (void)launchedFrom;
    hlrt::air::install();
    putTestPack();
#ifdef HLDS_CHILD_FROMCARD
    // (A test that is given its data as the air would give it, with none of
    // it in memory: the streams on the emulator's card, a packet's worth at a
    // time. Nothing else of the card is read.)
    if (fatInitDefault() && fatGetDefaultDrive() != nullptr)
    {
        const std::string air = std::string(fatGetDefaultDrive()) + "_nds/hl_nds/data/air/";
        const char *map = HLDS_CHILD_FROMCARD;
        const std::string group = std::strncmp(map, "t0a0", 4) == 0      ? "hazard"
                                  : std::strncmp(map, "hldemo", 6) == 0 ? "uplink"
                                                                        : "match";
        hlrt::guest::FileSupply supply;
        for (const std::string &stream : {air + "common.hgs", air + group + ".base.hgs"})
        {
            supply.file = std::fopen(stream.c_str(), "rb");
            if (supply.file == nullptr)
                continue;
            if (stream.find("common") != std::string::npos)
                hlrt::guest::receiveCommon(supply);
            else
                hlrt::guest::receiveBase(supply);
            std::fclose(supply.file);
        }
        hlrt::guest::offerMapFile(map, air + map + ".hgs");
    }
#endif
    hasData = hlrt::guest::offeredCount() > 0;
    return;
#endif
    // SD / flashcard FAT access -- every byte of game content comes from here.
    const bool fatOk = fatInitDefault();
    fatDevice = fatGetDefaultDrive();
    hasFat = fatOk && (fatDevice != nullptr);
    hasData = hasFat;
    if (hasFat)
    {
        fatDeviceCPP = fatDevice;
#ifndef HLDS_CHILD
        putBackWideTwlBg(fatDeviceCPP);
#endif
    }
    else
    {
        // In DSi mode the card is the console's own SD, reached only when
        // the loader has left it switched on (SCFG_EXT bit 18; the register
        // reads 0 if the loader locked SCFG). In DS mode it is whatever the
        // loader's DLDI driver reaches -- none, if it put none in.
        char text[160];
        const DLDI_INTERFACE *dldi = io_dldi_data;
        std::snprintf(text, sizeof(text), "%s mode, SCFG %08lX, DLDI \"%.24s\", from %.60s", isDSi ? "DSi" : "DS",
                      isDSi ? static_cast<unsigned long>(REG_SCFG_EXT) : 0ul,
                      dldi != nullptr ? dldi->friendlyName : "none",
                      launchedFrom != nullptr && launchedFrom[0] != '\0' ? launchedFrom : "(no path)");
        cardWhy = text;
    }

}

namespace
{
    // (548 at most, measured on the maps with the most textures and
    // palettes -- rocket_frenzy on a DSi; c2a5e, the heaviest of the
    // campaign, 358. Past this they come from the heap, counted in perf.txt.)
    [[maybe_unused]] constexpr int kNeaChunks = 1024;

#ifndef HLDS_CHILD
    // The map's archive index, kept here rather than on the heap: one map is
    // mounted after another into the same memory (HlArchive.hpp). One whose
    // index is bigger -- the converter says so -- is mounted on the heap.
    // It is the map's overlay (build.py's OVERLAYS): the memory the front
    // end's code has while no map is mounted.
    alignas(4) std::uint8_t mapIndex[hldata::kMapIndexMost] HL_OVL_BSS(map);

    HL_OVL_GATED int mountMapArchive(const std::string &path)
    {
        // (The front end's code gives the memory up, and with it what it had
        // open: hlrt::loadGame's leave checks.)
#if HL_OVL_COUNT > 0
        hlrt::ovl::load(hlrt::ovl::Id::Map);
#endif
        HL_OVL_GATE(Map);
        if (NEA_NpacMountInto("map", path.c_str(), mapIndex, sizeof(mapIndex)) == 0)
            return 0;
        return errno == ENOSPC ? NEA_NpacMount("map", path.c_str()) : -1;
    }

    /// How `drive`'s archive, `file`, seeks now that it is mounted: into
    /// `seek`, and a `mount:` line in perf.txt (at `log`).
    void noteSeek(const char *drive, const std::string &file, NEA_NpacSeek &seek, const std::string &log)
    {
        seek = NEA_NpacSeek{};
        if (NEA_NpacSeekInfo(drive, &seek) != 0 || !perfLogging())
            return;
        std::FILE *out = std::fopen(log.c_str(), "a");
        if (out == nullptr)
            return;
        const std::size_t slash = file.rfind('/');
        const char *name = file.c_str() + (slash == std::string::npos ? 0 : slash + 1);
        static const char *const kWhy[] = {"", "not on a FAT card", "no memory for the table",
                                           "too many for the table", "opened again after a failed read"};
        const unsigned why = static_cast<unsigned>(seek.state);
        if (seek.fast)
            std::fprintf(out, "mount: %s %s fast seek, %ld piece(s), table %lu bytes\n", drive, name,
                         static_cast<long>(seek.fragments), static_cast<unsigned long>(seek.table_bytes));
        else
            std::fprintf(out, "mount: %s %s NO fast seek, %ld piece(s) (%s)\n", drive, name,
                         static_cast<long>(seek.fragments), why < 5 ? kWhy[why] : "?");
        std::fclose(out);
    }
#endif
}

void Global::InitVideo()
{
    videoReady_ = true;
    NEA_Init3D();
    // NEA puts the 3D screen on top by default; said explicitly because the
    // whole control scheme depends on the touch screen NOT being the 3D one.
    NEA_MainScreenSetOnTop();
    // The 3D picture and the sprites at priority 1, so that a fade's colour
    // can be put over both, at 0 (render/ScreenFade.cpp). (The sprites are
    // still over the picture: of the same priority, they are drawn first.)
    REG_BG0CNT = static_cast<std::uint16_t>((REG_BG0CNT & ~3) | BG_PRIORITY(1));
    NEA_SetDepthBufferMode(NEA_WBUFFER);
    // A DSi has a second set of DMA channels, which NEA uses for display
    // lists when it is told it may: they can feed the geometry engine in the
    // modes where the old ones are busy with the screen. (The world's own
    // list is sent the same way: WorldDraw::emitList.)
    NEA_DisplayListEnableNDMA(isDSiMode());
    Global::waitVBlank();
    Global::waitVBlank();

    // Room for a map's worth of textures: NEA's defaults (128 materials, 64
    // palettes) are below what one retail map uses, and every 4x4 compressed
    // texture has a palette of its own. All four banks: compressed textures
    // live in Banks A and C with their indexes in Bank B, and in play the
    // others in Bank D (render/BankD.hpp, which says where they go when).
    // (Sent the game, Bank D is held back, as its maps were measured.)
    // (Every split of NEA's VRAM lists takes a node and every merge gives one
    // back: from a reserve set aside once, so loading and freeing textures in
    // play never goes to the heap. Sized from what the heaviest maps use.)
    // (Not when sent the game: what a guest's memory holds decides which maps
    // it can be sent, and that was measured without it.)
#ifndef HLDS_CHILD
    static bool chunksReserved = false;
    if (!chunksReserved)
        chunksReserved = NEA_AllocReserveChunks(kNeaChunks) == 0;
#endif
#ifdef HLDS_CHILD
    NEA_TextureSystemReset(512, 1024, NEA_VRAM_ABC);
#else
    NEA_TextureSystemReset(512, 1024, NEA_VRAM_ABCD);
#endif
    hlrt::bankD.reset();

    // The projection the world is drawn through. NEA rebuilds it every frame
    // from these, so they are set once.
    NEA_SetFov(hlrt::Camera::kFovDegrees);
    // The worker that thinks for the actors while a frame waits.
    hlrt::think.restart();
    NEA_ClippingPlanesSetI(hlrt::Camera::kNearClip, hlrt::Camera::kFarClip);
    NEA_ClearColorSet(RGB15(0, 0, 0), 31, 63);

    // (No text console: what there is to read is drawn on the touch screen,
    // in the game's own letters -- ui/TouchCanvas.)

    // Bringing the engine up again closed its archives.
    mountCommon();
#ifndef HLDS_CHILD
    if (!mapMounted_.empty() && !NEA_NpacIsMounted("map") && mountMapArchive(mapArchive(mapMounted_)) == 0)
        noteSeek("map", mapArchive(mapMounted_), mapSeek, dataPath("perf.txt"));
#endif
}

bool Global::mountMap(const std::string &name)
{
    unmountMap();
#ifdef HLDS_CHILD
    // (Sent the game: the map's stream, given to the "air:" drive, whose
    // files are read from it as the map is loaded.)
    if (!hlrt::guest::beginMap(name))
        return false;
#else
    if (!hasData || mountMapArchive(mapArchive(name)) != 0)
        return false;
    noteSeek("map", mapArchive(name), mapSeek, dataPath("perf.txt"));
#endif
    mapMounted_ = name;
    return true;
}

void Global::unmountMap()
{
#ifdef HLDS_CHILD
    hlrt::guest::endMap();
#else
    if (NEA_NpacIsMounted("map"))
        NEA_NpacUnmount("map");
#endif
    mapMounted_.clear();
}

void Global::mountCommon()
{
#ifndef HLDS_CHILD
    // (A card with no common archive is a game without its own letters and
    // pictures, which is said where each is missed, not here.)
    if (hasData && !NEA_NpacIsMounted("common") && NEA_NpacMount("common", (gameDir() + "common.npac").c_str()) == 0)
        noteSeek("common", "common.npac", commonSeek, dataPath("perf.txt"));
#endif
}

#ifndef HLDS_CHILD
HL_TWL_CODE void Global::remountCommon()
{
    if (NEA_NpacIsMounted("common"))
        NEA_NpacUnmount("common");
    mountCommon();
}
#endif

#ifndef HLDS_CHILD
#endif

#ifndef HLDS_CHILD
HL_TWL_CODE std::string Global::otherSavesDir() const
{
    return otherGameDir() + "saves/";
}

HL_TWL_CODE std::string Global::otherGameDir() const
{
    return dataDir() + hldata::activeGameInfo().cardDir;
}

HL_TWL_CODE std::uint8_t Global::scanGames() const
{
    std::uint8_t present = 0;
    if (!hasFat)
        return present;
    for (int g = 0; g < hldata::kGameCount; ++g)
    {
        const std::string path = dataDir() + hldata::gameInfo(static_cast<hldata::GameId>(g)).cardDir + "manifest.bin";
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
            continue;
        char magic[4] = {};
        if (std::fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
            std::memcmp(magic, hldata::kManifestMagic, sizeof(magic)) == 0)
            present |= static_cast<std::uint8_t>(1u << g);
        std::fclose(file);
    }
    return present;
}
#endif

std::size_t Global::heapHeadroom() const
{
    // (What is free at the heap's top is given back to it first: libnds's
    // allocator keeps up to 2 MB of it below the heap's end otherwise, and
    // after a big map a smaller one would find no room for its sounds and
    // some of its models.)
    malloc_trim(0);
    return static_cast<std::size_t>(getHeapLimit() - getHeapEnd());
}

Global::DataStatus Global::ReadManifest(std::string &startMap, std::vector<hldata::NetMapInfo> *multiplayer,
                                        std::string *why, std::string *madeBy) const
{
#ifdef HLDS_CHILD
    // Sent the game, there is no manifest: the maps are the ones it was
    // sent, and the first is where it starts.
    (void)why;
    (void)madeBy;
    for (int i = 0; i < hlrt::guest::offeredCount(); ++i)
    {
        const std::string map = hlrt::guest::offeredName(i);
        if (startMap.empty())
            startMap = map;
        // (Sent it: it fits. Its places are its own business here.)
        if (multiplayer != nullptr)
            multiplayer->push_back(hldata::NetMapInfo{map, 0, true});
    }
    // (A console sent the game by a host has none until the host sends it
    // one: that is not a card without the game.)
    return DataStatus::Ok;
#endif
    if (!hasFat)
        return DataStatus::NoCard;

    std::FILE *file = std::fopen(gamePath("manifest.bin").c_str(), "rb");
    if (file == nullptr)
        return DataStatus::NoData;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    // A manifest is a few kilobytes. The size is the card's word, so it is
    // bounded before it is used.
    if (size <= 0 || size > 256 * 1024)
    {
        std::fclose(file);
        return DataStatus::Damaged;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    const bool got = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    if (!got)
        return DataStatus::Damaged;

    // readManifest checks magic, versions, length and checksum BEFORE yas
    // touches the payload: yas reports a bad archive by throwing, and this
    // build has no exceptions.
    hldata::Manifest manifest;
    switch (hldata::readManifest(bytes.data(), bytes.size(), manifest))
    {
    case hldata::ManifestError::Ok:
        break;
    case hldata::ManifestError::OtherVersion:
        if (why != nullptr)
            *why = hldata::manifestRefusal(bytes.data(), bytes.size());
        return DataStatus::OtherVersion;
    default:
        return DataStatus::Damaged;
    }
    // (Another game's maps in this game's folder: not to be played as this one.)
    if (manifest.startMap.empty() || manifest.find(manifest.startMap) == nullptr || manifest.game != hldata::activeGame)
        return DataStatus::Damaged;
    startMap = manifest.startMap;
    if (madeBy != nullptr)
        *madeBy = manifest.converterVersion;
    for (const hldata::ManifestMap &map : manifest.maps)
    {
        // (A DS: not a map it has not the memory for.)
        if (multiplayer != nullptr && map.spawns != 0 && (isDSi || map.dsFits))
        {
            // (How far up the memory of a console sent it, and to play.)
            const std::uint32_t need =
                map.guestPeakBytes != 0 ? (map.guestPeakBytes + hldata::kGuestPlayBytes + 1023) / 1024 : 0;
            multiplayer->push_back(
                hldata::NetMapInfo{map.name, map.spawns, map.guestFits, map.airGroup, need, map.dsSpareKb});
        }
    }
    return DataStatus::Ok;
}

const char *Global::DataStatusTitle(DataStatus s)
{
    switch (s)
    {
    case DataStatus::NoCard:       return "No SD card found.";
    case DataStatus::NoData:       return "No Half-Life data on the card.";
    case DataStatus::OtherVersion: return "The data is from another version.";
    case DataStatus::Damaged:      return "The data on the card is damaged.";
    default:                       return "";
    }
}

const char *Global::DataStatusHint(DataStatus s)
{
    switch (s)
    {
    case DataStatus::NoCard:       return "Insert a card the console can read.";
    case DataStatus::NoData:       return "Run the converter first.";
    case DataStatus::OtherVersion: return "Run this version's converter again.";
    case DataStatus::Damaged:      return "Run the converter again.";
    default:                       return "";
    }
}
