#pragma once

// What the monsters hear and smell (dlls/soundent.cpp, CSoundEnt): a pool of
// sixty-four "sounds", each somewhere, of a kind, as loud as how far off it
// is heard, until its time is up -- a shot, a door, a grenade about to go, a
// body, a gib. Nothing here is played: it is what a monster's Listen()
// finds. The first one is the player's own, never let go: how loud their
// steps and their gun are just now (dlls/player.cpp, UpdatePlayerSound).
//
// Not saved: a map that is loaded hears from then on.
//
// std-only and inline: the host tests run it.

#include <cstdint>

#include "HlFixed.hpp"

namespace hldata
{
    // What kind of sound (bits_SOUND_*).
    inline constexpr std::uint8_t kSoundCombat = 1 << 0;  ///< shots, blasts
    inline constexpr std::uint8_t kSoundWorld = 1 << 1;   ///< a door, glass breaking
    inline constexpr std::uint8_t kSoundPlayer = 1 << 2;  ///< anything the player does
    inline constexpr std::uint8_t kSoundCarcass = 1 << 3; ///< a body
    inline constexpr std::uint8_t kSoundMeat = 1 << 4;    ///< a gib
    inline constexpr std::uint8_t kSoundDanger = 1 << 5;  ///< a grenade about to go off
    inline constexpr std::uint8_t kSoundGarbage = 1 << 6;
    /// Heard, as against smelt (CSound::FIsSound / FIsScent).
    inline constexpr std::uint8_t kSoundHeard = kSoundCombat | kSoundWorld | kSoundPlayer | kSoundDanger;
    inline constexpr std::uint8_t kSoundScent = kSoundCarcass | kSoundMeat | kSoundGarbage;

    // How loud the weapons are (dlls/weapons.h and the weapons' own files).
    inline constexpr int kLoudGunVolume = 1000;
    inline constexpr int kNormalGunVolume = 600;
    inline constexpr int kQuietGunVolume = 200;
    inline constexpr int kBigExplosionVolume = 2048;
    inline constexpr int kNormalExplosionVolume = 1024;
    inline constexpr int kSmallExplosionVolume = 512;
    inline constexpr int kCrowbarBodyHitVolume = 128;
    inline constexpr int kCrowbarWallHitVolume = 512;
    inline constexpr int kGaussFireVolume = 450;
    inline constexpr int kGaussChargeVolume = 256;
    inline constexpr int kEgonVolume = 450;

    inline constexpr int kWorldSounds = 64; ///< MAX_WORLD_SOUNDS
    inline constexpr std::int8_t kSoundListEmpty = -1;

    struct AiSound
    {
        fx origin[3] = {0, 0, 0};
        std::uint8_t type = 0;
        std::int16_t volume = 0;
        std::int32_t expireMs = 0; ///< when it goes; -1: never
        std::int8_t next = kSoundListEmpty;
    };

    /// The pool: the free list and the active one, newest first, as the game
    /// keeps them; swept three times a second.
    struct SoundEnt
    {
        AiSound pool[kWorldSounds];
        std::int8_t active = kSoundListEmpty;
        std::int8_t free = 0;
        std::int32_t sweepMs = 0; ///< until the next sweep
        /// The player's: how loud their gun has been made (m_iWeaponVolume),
        /// what that is coming down to, and what is heard of them now.
        std::int32_t weaponVolume = 0;
        std::int32_t targetVolume = 0;
        /// What more their sound is for a moment (m_iExtraSoundTypes, until
        /// m_flStopExtraSoundTime): an MP5's grenade makes it danger.
        std::uint8_t extraTypes = 0;
        std::int32_t extraMs = 0;
    };

    namespace detail
    {
        inline std::int8_t soundAlloc(SoundEnt &s)
        {
            const std::int8_t i = s.free;
            if (i == kSoundListEmpty)
                return kSoundListEmpty;
            s.free = s.pool[i].next;
            s.pool[i].next = s.active;
            s.active = i;
            return i;
        }
    } // namespace detail

    /// Empty, with the player's own sound kept aside (CSoundEnt::Initialize,
    /// for the one client there is).
    inline void soundEntInit(SoundEnt &s)
    {
        for (int i = 0; i < kWorldSounds; ++i)
        {
            s.pool[i] = AiSound();
            s.pool[i].next = static_cast<std::int8_t>(i + 1 < kWorldSounds ? i + 1 : kSoundListEmpty);
        }
        s.active = kSoundListEmpty;
        s.free = 0;
        s.sweepMs = 0;
        s.weaponVolume = s.targetVolume = 0;
        const std::int8_t mine = detail::soundAlloc(s);
        s.pool[mine].expireMs = -1;
    }

    /// The player's sound (the one kept for them).
    inline AiSound &playerSound(SoundEnt &s)
    {
        return s.pool[0];
    }

    /// A sound of `type` at `origin`, heard `volume` units off, for
    /// `durationMs` (CSoundEnt::InsertSound). `nowMs`: the time it is.
    inline void soundInsert(SoundEnt &s, std::int32_t nowMs, std::uint8_t type, const fx *origin, int volume,
                            int durationMs)
    {
        const std::int8_t i = detail::soundAlloc(s);
        if (i == kSoundListEmpty)
            return;
        AiSound &sound = s.pool[i];
        for (int k = 0; k < 3; ++k)
            sound.origin[k] = origin[k];
        sound.type = type;
        sound.volume = static_cast<std::int16_t>(volume > 32767 ? 32767 : volume);
        sound.expireMs = nowMs + durationMs;
    }

    /// What has had its time goes (CSoundEnt::Think, every 0.3 s).
    inline void soundEntTick(SoundEnt &s, std::int32_t nowMs, int ms)
    {
        s.sweepMs -= ms;
        if (s.sweepMs > 0)
            return;
        s.sweepMs += 300;
        if (s.sweepMs <= 0)
            s.sweepMs = 300;
        std::int8_t previous = kSoundListEmpty;
        std::int8_t i = s.active;
        while (i != kSoundListEmpty)
        {
            AiSound &sound = s.pool[i];
            const std::int8_t next = sound.next;
            if (sound.expireMs != -1 && sound.expireMs <= nowMs)
            {
                if (previous != kSoundListEmpty)
                    s.pool[previous].next = next;
                else
                    s.active = next;
                sound.next = s.free;
                s.free = i;
            }
            else
            {
                previous = i;
            }
            i = next;
        }
    }

    /// The player's own sound, each tick (CBasePlayer::UpdatePlayerSound):
    /// their steps by how fast they go on the ground (512 at most, and 100
    /// more for a jump), or their gun if that is louder -- which makes it a
    /// shot, heard as one; it fades at 250 a second. `speed`: their speed
    /// along the ground, or -1 in the air. `unheard`: nothing of them.
    inline void playerSoundTick(SoundEnt &s, const fx *origin, int speed, bool jumping, bool unheard, int ms)
    {
        AiSound &sound = playerSound(s);
        sound.type = 0;
        int body = speed < 0 ? 0 : (speed > 512 ? 512 : speed);
        if (jumping)
            body += 100;
        if (s.weaponVolume > body)
        {
            s.targetVolume = s.weaponVolume;
            sound.type |= kSoundCombat;
        }
        else
        {
            s.targetVolume = body;
        }
        s.weaponVolume -= 250 * ms / 1000;
        if (s.weaponVolume < 0)
            s.weaponVolume = 0;
        int volume = sound.volume;
        if (s.targetVolume > volume)
        {
            volume = s.targetVolume;
        }
        else if (volume > s.targetVolume)
        {
            volume -= 250 * ms / 1000;
            if (volume < s.targetVolume)
                volume = 0;
        }
        if (unheard)
            volume = 0;
        for (int k = 0; k < 3; ++k)
            sound.origin[k] = origin[k];
        sound.type |= kSoundPlayer;
        if (s.extraMs > 0)
        {
            sound.type |= s.extraTypes;
            s.extraMs -= ms;
        }
        else
        {
            s.extraTypes = 0;
        }
        sound.volume = static_cast<std::int16_t>(volume);
    }

    /// The player's gun has made this much noise (m_iWeaponVolume = ...).
    inline void playerWeaponVolume(SoundEnt &s, int volume)
    {
        s.weaponVolume = volume;
    }
} // namespace hldata
