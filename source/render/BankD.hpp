#pragma once

// Video memory Bank D: in play, texture slot 3, where every texture that is
// not 4x4 compressed goes (the marks, the sprites, see-through and light
// textures, the flat stand-ins, players' skins and sprays). Banks A, B and C
// are then the 4x4 textures' alone: a texture that is not 4x4 there took
// room from them, its index's share too.
//
// Bank D is also the only place a display capture can write that is not in
// use: FrameFreeze keeps the last picture there while a map loads, and the
// map's pictures (HlPreview.hpp) are taken through it. While it is lent so,
// what is drawn from it is missing from the picture (tools/hwtest/slot3: a
// texture whose memory is not a texture slot reads as nothing), a texture
// loaded meanwhile is given its place there but not written, and when it
// comes back every texture in it is written again: read again off the card,
// from where each was read (or made again, for the few made by the game).
// So nothing in Bank D needs to be anywhere else as well.
//
// In the menus, Bank D is not a texture slot, and textures that are not 4x4
// go to banks A to C as before: the menus have room, and a picture held
// over a load (the title's) must not have been drawn from D.
//
// (A console sent the game keeps Bank D out of the texture memory, as
// before: its maps were measured without it, and its loads have no card to
// read a texture again from.)

#include <cstdint>

#ifndef HLDS_CHILD
#include <NEAMain.h>
#endif

#include "EngineObject.hpp"

namespace hlrt
{
    class BankD
    {
    public:
        /// The texture memory is new (Global::InitVideo): the menus'.
        void reset();
        /// A map is loading: textures that are not 4x4 go to Bank D from
        /// now on (each map, after the old one is let go and before its
        /// first texture).
        void enterPlay();
        /// The menus: such textures in banks A to C again, and Bank D not
        /// a texture slot.
        void enterMenus();
        /// Bank D for a display capture, in LCD mode: no texture is drawn
        /// from it until giveBack().
        void lend();
        /// Back from lend(): in play, a texture slot again, every texture in
        /// it written again (behind the curtain: the screen shows none of
        /// it while it is written); in the menus, left in LCD mode.
        void giveBack();
        bool lent() const { return lent_; }
        bool playing() const { return playing_; }

#ifndef HLDS_CHILD
        /// A texture just loaded from `member`, its texels `bytes` long from
        /// `offset` in it: if it is in Bank D, where to read them again.
        void noteFile(NEA_Material *material, const NEA_NpacMember &member, std::uint32_t offset,
                      std::uint32_t bytes);
        /// ... or made by the game: `make` makes its `bytes` of texels
        /// again, from `key`, into `to`.
        using Make = bool (*)(std::uint32_t key, std::uint8_t *to, std::uint32_t bytes);
        void noteMade(NEA_Material *material, Make make, std::uint32_t key, std::uint32_t bytes);
#endif

        struct Stats
        {
            int noted = 0;    ///< textures it would write again
            int written = 0;  ///< written again, all told
            int failed = 0;   ///< that could not be (the card, or no room to read into)
            int overflow = 0; ///< in Bank D with no room to note them
        };
        const Stats &stats() const { return stats_; }

    private:
        bool playing_ = false;
        bool lent_ = false;
        Stats stats_;
#ifndef HLDS_CHILD
        struct Entry
        {
            NEA_NpacMember member; ///< where it is read from (if `make` is null)
            Make make;
            std::uint32_t key;
            std::uint32_t offset; ///< of its texels in the member
            std::uint32_t bytes;
            std::uint32_t at; ///< its address in Bank D
        };
        static constexpr int kMostEntries = 96;
        Entry entries_[kMostEntries];
        int count_ = 0;
        /// A place in Bank D for `material`'s texels, `bytes` of them: room
        /// in the table, any it overlaps let go first. Null if it is not in
        /// Bank D (or there is no room).
        Entry *place(NEA_Material *material, std::uint32_t bytes);
        /// Forgets those no texture starts at any longer.
        void prune();
        void restore();
#endif
    };

    HL_ENGINE_OBJECT(BankD, bankD);
} // namespace hlrt
