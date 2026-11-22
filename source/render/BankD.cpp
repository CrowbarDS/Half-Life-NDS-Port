#include "global_header.hpp"

#include "render/BankD.hpp"

#include <cstdlib>

#include "data/CardRead.hpp"
#include "data/TextureStream.hpp"

namespace hlrt
{
    BankD bankD;

#ifdef HLDS_CHILD
    // (Sent the game: Bank D is no texture memory, only a capture's.)
    void BankD::reset()
    {
        playing_ = lent_ = false;
        vramSetBankD(VRAM_D_LCD);
    }
    void BankD::enterPlay() { playing_ = true; }
    void BankD::enterMenus() { playing_ = false; }
    void BankD::lend()
    {
        lent_ = true;
        vramSetBankD(VRAM_D_LCD);
    }
    BankD::Elsewhere::Elsewhere() {}
    BankD::Elsewhere::~Elsewhere() {}
    void BankD::giveBack()
    {
        lent_ = false;
        vramSetBankD(VRAM_D_LCD);
    }
#else
    namespace
    {
        constexpr std::uintptr_t kBankD = 0x06860000, kBankDEnd = 0x06880000;
        constexpr std::uint32_t kBanksAtoC = NEA_VRAM_A | NEA_VRAM_B | NEA_VRAM_C;

        /// Where `material`'s texels are, from its parameters (0: none).
        std::uintptr_t texelsOf(NEA_Material *material)
        {
            const std::uint32_t param = NEA_MaterialGetTexParam(material);
            if (param == 0)
                return 0;
            return 0x06800000u + ((param & 0xFFFFu) << 3);
        }
    } // namespace

    void BankD::reset()
    {
        playing_ = lent_ = false;
        count_ = 0;
        stats_ = Stats();
        NEA_TextureSetPlainBanks(kBanksAtoC);
        NEA_TextureSetPlainDeferred(false);
        vramSetBankD(VRAM_D_LCD);
    }

    void BankD::enterPlay()
    {
        playing_ = true;
        NEA_TextureSetPlainBanks(NEA_VRAM_D);
        // (While it is lent -- the last picture held over the load -- what
        // goes there is written when it comes back.)
        NEA_TextureSetPlainDeferred(lent_);
        if (!lent_)
            vramSetBankD(VRAM_D_TEXTURE_SLOT3);
        prune();
    }

    void BankD::enterMenus()
    {
        playing_ = false;
        NEA_TextureSetPlainBanks(kBanksAtoC);
        NEA_TextureSetPlainDeferred(false);
        if (!lent_)
            vramSetBankD(VRAM_D_LCD);
    }

    void BankD::lend()
    {
        lent_ = true;
        if (playing_)
            NEA_TextureSetPlainDeferred(true);
        vramSetBankD(VRAM_D_LCD);
    }

    void BankD::giveBack()
    {
        lent_ = false;
        NEA_TextureSetPlainDeferred(false);
        if (!playing_)
        {
            vramSetBankD(VRAM_D_LCD);
            return;
        }
        vramSetBankD(VRAM_D_TEXTURE_SLOT3);
        restore();
    }

    BankD::Elsewhere::Elsewhere()
        : banks_(NEA_TextureGetPlainBanks()), deferred_(bankD.lent() && bankD.playing())
    {
        NEA_TextureSetPlainBanks(kBanksAtoC);
        NEA_TextureSetPlainDeferred(false);
    }

    BankD::Elsewhere::~Elsewhere()
    {
        NEA_TextureSetPlainBanks(banks_);
        NEA_TextureSetPlainDeferred(deferred_);
    }

    BankD::Entry *BankD::place(NEA_Material *material, std::uint32_t bytes)
    {
        if (material == nullptr || bytes == 0)
            return nullptr;
        const std::uintptr_t at = texelsOf(material);
        if (at < kBankD || at >= kBankDEnd)
            return nullptr;
        // (What it is put over was let go: whatever was noted there is not
        // to be written again.)
        int kept = 0;
        for (int i = 0; i < count_; ++i)
        {
            const Entry &e = entries_[i];
            if (e.at < at + bytes && at < e.at + e.bytes)
                continue;
            entries_[kept++] = e;
        }
        count_ = kept;
        if (count_ >= kMostEntries)
        {
            ++stats_.overflow;
            return nullptr;
        }
        Entry &e = entries_[count_++];
        e = Entry();
        e.at = static_cast<std::uint32_t>(at);
        e.bytes = bytes;
        stats_.noted = count_;
        return &e;
    }

    void BankD::noteFile(NEA_Material *material, const NEA_NpacMember &member, std::uint32_t offset,
                         std::uint32_t bytes)
    {
        if (Entry *e = place(material, bytes))
        {
            e->member = member;
            e->make = nullptr;
            e->offset = offset;
        }
    }

    void BankD::noteMade(NEA_Material *material, Make make, std::uint32_t key, std::uint32_t bytes)
    {
        if (Entry *e = place(material, bytes))
        {
            e->make = make;
            e->key = key;
        }
    }

    void BankD::prune()
    {
        int kept = 0;
        for (int i = 0; i < count_; ++i)
        {
            const Entry &e = entries_[i];
            if (NEA_TextureBytesAt(reinterpret_cast<const void *>(e.at)) < static_cast<int>(e.bytes))
                continue;
            entries_[kept++] = e;
        }
        count_ = kept;
        stats_.noted = count_;
    }

    void BankD::restore()
    {
        prune();
        if (count_ == 0)
            return;
        // Read into the textures' staging buffer if nothing waits in it (as
        // after a load), else into a little of the heap, a piece at a time.
        std::uint32_t room = 0;
        std::uint8_t *buffer = textureStream.borrowStage(room);
        std::uint8_t *own = nullptr;
        if (buffer == nullptr)
        {
            room = kCardPiece;
            own = static_cast<std::uint8_t *>(std::malloc(room));
            if (own == nullptr)
            {
                room = 1024;
                own = static_cast<std::uint8_t *>(std::malloc(room));
            }
            buffer = own;
        }
        for (int i = 0; i < count_; ++i)
        {
            const Entry &e = entries_[i];
            auto *to = reinterpret_cast<std::uint8_t *>(e.at);
            bool ok = buffer != nullptr;
            if (ok && e.make != nullptr)
            {
                ok = e.bytes <= room && e.make(e.key, buffer, e.bytes);
                if (ok)
                    NEA_VramWrite(to, buffer, e.bytes);
            }
            else if (ok && e.member.method == NEA_NPAC_STORED)
            {
                for (std::uint32_t done = 0; ok && done < e.bytes;)
                {
                    const std::uint32_t want = e.bytes - done < room ? e.bytes - done : room;
                    ok = cardReadMember(e.member, e.offset + done, buffer, want);
                    if (ok)
                        NEA_VramWrite(to + done, buffer, want);
                    done += want;
                }
            }
            else if (ok)
            {
                // (Packed, on a card converted before these were stored: read
                // whole, as it is undone.)
                auto *whole = static_cast<std::uint8_t *>(std::malloc(e.member.size));
                ok = whole != nullptr && e.offset + e.bytes <= e.member.size &&
                     cardReadMember(e.member, 0, whole, e.member.size);
                if (ok)
                    NEA_VramWrite(to, whole + e.offset, e.bytes);
                std::free(whole);
            }
            if (ok)
                ++stats_.written;
            else
                ++stats_.failed;
        }
        std::free(own);
    }
#endif
} // namespace hlrt
