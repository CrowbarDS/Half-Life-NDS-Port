#pragma once

#include "EngineObject.hpp"

// Streaming a map's textures.
//
// A map cooked with --stream-textures has textures that are bigger than
// video memory can hold all at once, marked kTextureStreamed. TextureCache
// draws those as a flat color; this reads the real ones in from the card as
// they come into view and lets the ones that have not been seen for a while go.
//
// A texture drawn in the last few frames is wanted above all else, one drawn
// a little while ago not as much.
// What a map has room for is found when it is loaded (begin):
// When a map is loaded, it gets what is left over after after NEA handles
// the not-streamed textures, sprites and models.
// The textures used by every view (TextureRef::share) are loaded as resident,
// the rest by how widely they are wanted. Maps whose textures all fit have all
// of them resident and never stream at all.
//
// The textures go into a fixed region of video memory taken from NEA when the
// map is loaded (NEA_TextureRegionReserve4x4 and NEA_PaletteRegionReserve):
// the plan's places are the places in it, so what the plan says fits does fit.
// What is read is written by VramWriter, a piece at a time in the vertical
// blanks, and each texture is drawn once it is all there; reading goes on
// meanwhile. A texture written where another was let go is written no sooner
// than two frames after (TextureStreamer::readyAt). The top of the region is
// the weapons' skin slots (ModelCache).

#include <cstdint>

#include <NEAMain.h>

namespace hlrt
{
    /// Per texture, the frame it was last drawn in. Written by every surface
    /// that is drawn (noteTextureDrawn); null when nothing is streamed.
    extern std::uint32_t *g_textureSeenAt;
    extern std::uint32_t g_textureFrame;

    inline void noteTextureDrawn(unsigned index)
    {
        // (0xFFFF: no texture. 512 at most.)
        if (g_textureSeenAt != nullptr && index < 512u)
            g_textureSeenAt[index] = g_textureFrame;
    }

    struct StreamStats
    {
        int streamed = 0;   ///< total streamed textures
        int resident = 0;   ///< current streamed textures in memory
        int pinned = 0;
        int loads = 0;
        int evictions = 0;
        int refused = 0;    ///< textures that could not be loaded (a file not as planned)
        int flatNow = 0;    ///< textures drawn in last few frames as flat color
        int copies = 0;     ///< textures kept in main memory (cycles' pictures, tiled variants)
        int copiesKb = 0;
        int texelKb = 0;    ///< texels currently in use
        int capacityKb = 0; ///< total estimated capacity
        int late = 0;       ///< vertical blanks that ended with writes still waiting (VramWriter)
        int lastLine = 0;   ///< the latest scanline a write ended on
        int dropped = 0;    ///< plans that stopped making way, their list full
        int attached = 0;   ///< streamed textures really in video memory (= resident, or something leaks)
    };

    class TextureStream
    {
    public:
        /// Load after everything else has, as much fits.
        void begin();
        /// Before anything of a map is in video memory: the weapons' two
        /// skin slots, `bytes` each, set by first (a console sent the game
        /// has every texture of the map in video memory, none streamed, and
        /// would have nothing left for them). begin() takes them up.
        void reserveSkinSlots(std::uint32_t bytes);
        /// A console in DSi mode, as a map's stream passes texture `id`: kept in
        /// main memory, where begin() and the plan find it (there is no card
        /// to read it off later).
        void keepFromStream(int id);
        /// Once the map and the game are in: copies in main memory of the
        /// pictures that change often, from what the heap has left past
        /// `keepBack` bytes (what play will take).
        void makeCopies(std::size_t keepBack);
        /// A console in DSi mode: on with asking the host for the textures it
        /// has not (those wanted first, then the widest, as long as the map
        /// lasts); what it asked for came, or never will.
        void fetchTick();
        void fetched(std::uint8_t *data, std::uint32_t size);
        void fetchFailed(bool never);
        bool fetchHeard() const { return fetchCame_; }
        /// Those most views want are here (or will not come).
        bool prerolled() const;
        void end(bool keepRam = false);
        bool active() const { return active_; }

        /// Once a frame, after the vertical blank: what may be written now
        /// is queued to be, and each texture all written is drawn.
        void upload();
        /// The frames counted by upload().
        std::uint32_t blanks() const { return blank_; }
        /// A texture let go: freed two blanks from now, when no picture
        /// still on the screen can be drawing it.
        void retire(NEA_Material *material, NEA_Palette *palette);
        /// At the end of each frame, calculate what is wanted next, and
        /// read `budgetBytes` of the card for it (while what was read before
        /// waits to be written, as far as the staging buffer goes).
        void update(int budgetBytes);

        StreamStats stats() const;
        /// A build that measures (HLDS_PROFILE): what the streaming and the
        /// texture memory are like, and which textures in view are flat and
        /// why, as lines in perf.txt. `why` says what prompted it. (Nothing,
        /// in any other build.)
        void report(const char *why);
    private:
        bool loadNow(int id, std::uint32_t texelOffset, std::uint32_t paletteOffset);
        void demand();
        void freeRetired(bool all);
        /// A spell of flat textures in view that has ended: how long, in
        /// perf.txt (when lines go there: perfLogging).
        void noteFlatSpell();

        bool active_ = false;
        bool fetchCame_ = false;
        std::uint32_t blank_ = 0;
    };

    HL_ENGINE_OBJECT(TextureStream, textureStream);
} // namespace hlrt
