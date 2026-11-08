// (A console that was sent the game only: HLDS_CHILD.)
#ifdef HLDS_CHILD
#include "net/AirDevice.hpp"
#include "Global.hpp"

#include "HlUi.hpp"
#include "HlLevel.hpp"
#include "HlAirBank.hpp"

#include <nds.h>
#include <nds/arm9/device_io.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>

namespace hlrt::air
{
    namespace
    {
        constexpr int kNameBytes = 32;

        constexpr int kOwnedFiles = 64;
        /// In DSi mode what a match asks for comes here too: all of a map's
        /// bank that is not a texture has a place. (From the heap, there being
        /// plenty: a DS keeps its 64 in the program.)
        constexpr int kOwnedFilesMore = 192;

        struct File
        {
            char name[kNameBytes] = {}; ///< (a longer name: as much as fits ...)
            std::uint32_t whole = 0;    ///< (... and the whole of it, hashed)
            const std::uint8_t *data = nullptr;
            std::uint32_t size = 0; ///< as it is read: undone, if it is packed
            std::uint32_t packedSize = 0; ///< what it is kept as, if it is packed (0: it is not)
            bool owned = false;
            bool ofMap = false; ///< the map's (a weapon lying about): let go with it
        };
        File filesHere[kOwnedFiles];
        File *files = filesHere;
        int fileRoom = kOwnedFiles;
        int fileCount = 0;
        bool installed = false;
        hldata::GuestCursor *cursor = nullptr;
        /// The cursor's member is there to be opened (parsed, not yet read).
        bool memberOpen = false;
        /// A demo's map's bank, all of it in memory (mountBank): its members
        /// are files too. (What find() found in it, until the next find.)
        const std::uint8_t *bank = nullptr;
        std::uint32_t bankSize = 0;
        File bankHit;

        /// One file open: where in which, and how far read -- or (`file`
        /// null) the stream's member under way. (Its address is the
        /// descriptor: main RAM, so bits 31:28 and 1:0 are clear, as libnds
        /// wants them.)
        struct Open
        {
            const File *file;
            File inBank; ///< (a member of the bank: `file` is this)
            std::uint32_t at;
            /// A packed file's: where in what it is kept as, and its undoing
            /// (made when it is first read).
            std::uint32_t packedAt;
            hldata::Lz77Stream *lz;
        };

        /// The order the game loads a map's kinds of file in, which is the
        /// stream's.
        bool kindBefore(hldata::GuestKind a, hldata::GuestKind b)
        {
            return static_cast<int>(a) < static_cast<int>(b);
        }

        bool putFile(const char *name, const std::uint8_t *data, std::uint32_t size, std::uint32_t packedSize, bool owned,
                     bool ofMap);

        /// The cursor's member, kept as it came (kGuestKeepPacked): a file of
        /// the map's from now on.
        void keepPacked()
        {
            const hldata::GuestMember &member = cursor->member();
            auto *data = static_cast<std::uint8_t *>(std::malloc(member.packedSize > 0 ? member.packedSize : 1));
            if (data == nullptr || !cursor->readPacked(data) ||
                !putFile(cursor->name(), data, member.rawSize, member.packedSize, true, true))
                std::free(data);
            memberOpen = false;
        }

        /// Whether `name` is the stream's member next, unread: gone past
        /// what is of a kind before its and was never asked for. (One of the
        /// same kind but another name stays, for whoever asks for it.)
        bool streamHas(const char *name)
        {
            if (cursor == nullptr)
                return false;
            const hldata::GuestKind want = hldata::guestKindOf(name);
            // (Nor anything out of the loaders' order -- a weapon kept packed,
            // which comes last and is kept as it passes: asked for early, it
            // would pass over all of what is before it, the map's sounds
            // among them, before they were asked for. It is found once the
            // stream has gone by, 2026-10-30.)
            if (want == hldata::GuestKind::Ui)
                return cursor->member().nameBytes != 0 && memberOpen && cursor->taken() == 0 &&
                       std::strcmp(cursor->name(), name) == 0;
            while (true)
            {
                const bool current = cursor->member().nameBytes != 0 && !cursor->ended() &&
                                     cursor->taken() == 0 && memberOpen;
                if (!current)
                {
                    if (!cursor->next())
                        return false;
                    memberOpen = true;
                }
                if (cursor->member().flags & hldata::kGuestKeepPacked)
                {
                    keepPacked();
                    continue;
                }
                const hldata::GuestKind kind = static_cast<hldata::GuestKind>(cursor->member().kind);
                if (std::strcmp(cursor->name(), name) == 0)
                    return true;
                if (!kindBefore(kind, want))
                    return false;
                memberOpen = false; // (never asked for: passed over)
            }
        }

        const char *bare(const char *path)
        {
            if (const char *colon = std::strchr(path, ':'))
                path = colon + 1;
            while (*path == '/')
                ++path;
            return path;
        }

        const File *findOwn(const char *path)
        {
            const char *want = bare(path);
            for (int i = 0; i < fileCount; ++i)
            {
                if (std::strncmp(files[i].name, want, kNameBytes - 1) == 0 &&
                    (std::strlen(want) < kNameBytes || files[i].whole == hldata::nameHash(want)))
                    return &files[i];
            }
            return nullptr;
        }

        /// Its own files, then the bank's members.
        const File *find(const char *path)
        {
            if (const File *file = findOwn(path))
                return file;
            hldata::AirBankEntry entry;
            const char *want = bare(path);
            if (bank == nullptr || !hldata::airBankFind(bank, bankSize, hldata::nameHash(want), entry))
                return nullptr;
            bankHit = File();
            std::strncpy(bankHit.name, want, kNameBytes - 1);
            bankHit.whole = entry.name;
            bankHit.data = bank + entry.offset;
            bankHit.size = entry.raw;
            bankHit.packedSize = entry.packed != entry.raw ? entry.packed : 0;
            return &bankHit;
        }

        Open *of(int fd) { return reinterpret_cast<Open *>(static_cast<std::uintptr_t>(fd)); }

        bool isDrive(const char *name) { return std::strcmp(name, "air") == 0; }

        int open(const char *path, int flags, mode_t)
        {
            if ((flags & O_ACCMODE) != O_RDONLY)
            {
                errno = EROFS;
                return -1;
            }
            const File *file = find(path);
            if (file == nullptr && !streamHas(bare(path)))
            {
                errno = ENOENT;
                return -1;
            }
            auto *open = static_cast<Open *>(std::malloc(sizeof(Open)));
            if (open == nullptr)
            {
                errno = ENOMEM;
                return -1;
            }
            open->file = file;
            if (file == &bankHit)
            {
                open->inBank = bankHit;
                open->file = &open->inBank;
            }
            open->at = 0;
            open->packedAt = 0;
            open->lz = nullptr;
            (void)flags;
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(open));
        }

        int close(int fd)
        {
            Open *open = of(fd);
            if (open->lz != nullptr)
            {
                open->lz->~Lz77Stream();
                std::free(open->lz);
            }
            std::free(open);
            return 0;
        }

        ssize_t read(int fd, void *to, size_t length)
        {
            Open *open = of(fd);
            if (open->file == nullptr)
            {
                // The stream's member: its next bytes, undone into the
                // reader's buffer. (Once read from, it is no other's.)
                const std::size_t got = cursor->read(static_cast<std::uint8_t *>(to), length);
                open->at += static_cast<std::uint32_t>(got);
                if (cursor->taken() == cursor->member().rawSize)
                    memberOpen = false;
                return static_cast<ssize_t>(got);
            }
            if (open->file->packedSize != 0)
            {
                // Kept packed: undone as it is read, from the start only.
                if (open->lz == nullptr)
                {
                    if (open->at != 0)
                    {
                        errno = EIO;
                        return -1;
                    }
                    void *room = std::malloc(sizeof(hldata::Lz77Stream));
                    if (room == nullptr)
                    {
                        errno = ENOMEM;
                        return -1;
                    }
                    open->lz = new (room) hldata::Lz77Stream();
                    open->lz->begin(open->file->size);
                }
                const std::uint8_t *in = open->file->data + open->packedAt;
                std::size_t left = open->file->packedSize - open->packedAt;
                const std::size_t got = open->lz->pump(in, left, static_cast<std::uint8_t *>(to), length);
                open->packedAt = open->file->packedSize - static_cast<std::uint32_t>(left);
                open->at += static_cast<std::uint32_t>(got);
                if (open->lz->failed())
                {
                    errno = EIO;
                    return -1;
                }
                return static_cast<ssize_t>(got);
            }
            const std::uint32_t left = open->file->size - open->at;
            if (length > left)
                length = left;
            std::memcpy(to, open->file->data + open->at, length);
            open->at += static_cast<std::uint32_t>(length);
            return static_cast<ssize_t>(length);
        }

        std::uint32_t sizeOf(const Open *open)
        {
            return open->file != nullptr ? open->file->size : cursor->member().rawSize;
        }

        off_t lseek(int fd, off_t offset, int whence)
        {
            Open *open = of(fd);
            const off_t from = whence == SEEK_SET ? 0
                               : whence == SEEK_CUR ? static_cast<off_t>(open->at)
                               : whence == SEEK_END ? static_cast<off_t>(sizeOf(open))
                                                    : -1;
            const off_t to = from + offset;
            // (A member of the stream goes one way: only to where it is, as
            // a size is asked -- to its end and straight back.)
            const bool oneWay = open->file == nullptr || open->file->packedSize != 0;
            if (from < 0 || to < 0 || to > static_cast<off_t>(sizeOf(open)) ||
                (oneWay && to != static_cast<off_t>(open->at) && to != static_cast<off_t>(sizeOf(open))))
            {
                errno = EINVAL;
                return -1;
            }
            if (oneWay)
                return to; // (where it is stays where it is: the size was asked)
            open->at = static_cast<std::uint32_t>(to);
            return to;
        }

        void fill(struct stat *st, std::uint32_t size)
        {
            std::memset(st, 0, sizeof(*st));
            st->st_size = static_cast<off_t>(size);
            st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;
            st->st_nlink = 1;
        }

        int fstat(int fd, struct stat *st)
        {
            fill(st, sizeOf(of(fd)));
            return 0;
        }

        int stat(const char *path, struct stat *st)
        {
            const File *file = find(path);
            if (file == nullptr && !streamHas(bare(path)))
            {
                errno = ENOENT;
                return -1;
            }
            fill(st, file != nullptr ? file->size : cursor->member().rawSize);
            return 0;
        }

        device_io_t device;
    } // namespace

    bool install()
    {
        if (installed)
            return true;
        std::memset(&device, 0, sizeof(device));
        device.isdrive = isDrive;
        device.open = open;
        device.close = close;
        device.read = read;
        device.lseek = lseek;
        device.fstat = fstat;
        device.stat = stat;
        installed = deviceIoAdd(&device) >= 0;
        return installed;
    }

    namespace
    {
        bool putFile(const char *name, const std::uint8_t *data, std::uint32_t size, std::uint32_t packedSize, bool owned,
                     bool ofMap)
        {
            File *slot = const_cast<File *>(findOwn(name));
            if (slot == nullptr)
            {
                if (fileCount == fileRoom && global.twlChild && files == filesHere)
                {
                    auto *more = static_cast<File *>(std::calloc(kOwnedFilesMore, sizeof(File)));
                    if (more != nullptr)
                    {
                        std::memcpy(more, filesHere, sizeof(filesHere));
                        files = more;
                        fileRoom = kOwnedFilesMore;
                    }
                }
                if (fileCount == fileRoom)
                    return false;
                slot = &files[fileCount++];
                std::strncpy(slot->name, name, kNameBytes - 1);
                slot->name[kNameBytes - 1] = '\0';
                slot->whole = hldata::nameHash(name);
            }
            else if (slot->owned)
            {
                std::free(const_cast<std::uint8_t *>(slot->data));
            }
            slot->data = data;
            slot->size = size;
            slot->packedSize = packedSize;
            slot->owned = owned;
            slot->ofMap = ofMap;
            return true;
        }

        /// The common stream's files kept after it has been read through:
        /// what is read again later -- the title's background each time it
        /// is shown, the logos for the profile and the spray. (41 KB:
        /// HlGuest.hpp's reserve. Not the loading picture: a load goes dark
        /// here -- FrameFreeze -- and it would be garbled.)
        bool keptCommon(const char *name)
        {
            return std::strcmp(name, hldata::kMenuFile) == 0 || std::strcmp(name, hldata::kLogosFile) == 0 ||
                   std::strcmp(name, hldata::kProfileModelFile) == 0 || std::strcmp(name, hldata::kProfileSkinFile) == 0 ||
                   (global.twlChild &&
                    (std::strcmp(name, hldata::kProfileFineModelFile) == 0 || std::strcmp(name, hldata::kProfileFineSkinFile) == 0));
        }

        void drop(bool ofMap)
        {
            int kept = 0;
            for (int i = 0; i < fileCount; ++i)
            {
                if (files[i].owned && files[i].ofMap == ofMap && (ofMap || !keptCommon(files[i].name)))
                    std::free(const_cast<std::uint8_t *>(files[i].data));
                else
                    files[kept++] = files[i];
            }
            for (int i = kept; i < fileCount; ++i)
                files[i] = File();
            fileCount = kept;
        }
    } // namespace

    bool put(const char *name, const std::uint8_t *data, std::uint32_t size)
    {
        return putFile(name, data, size, 0, false, false);
    }

    bool putMapFile(const char *name, std::uint8_t *data, std::uint32_t size)
    {
        return putFile(name, data, size, 0, true, true);
    }

    void dropCharacters()
    {
        int kept = 0;
        for (int i = 0; i < fileCount; ++i)
        {
            if (files[i].owned && std::strncmp(files[i].name, "chr/", 4) == 0)
                std::free(const_cast<std::uint8_t *>(files[i].data));
            else
                files[kept++] = files[i];
        }
        for (int i = kept; i < fileCount; ++i)
            files[i] = File();
        fileCount = kept;
    }

    bool putOwned(const char *name, std::uint8_t *data, std::uint32_t size)
    {
        return putFile(name, data, size, 0, true, false);
    }

    void dropMapFiles() { drop(true); }

    void drain()
    {
        // (What is left of the map's stream: the weapons lying about, kept.)
        if (cursor == nullptr)
            return;
        while (true)
        {
            const bool current = memberOpen && cursor->taken() == 0 && !cursor->ended();
            if (!current && !cursor->next())
                break;
            memberOpen = true;
            if (cursor->member().flags & hldata::kGuestKeepPacked)
                keepPacked();
            else
                memberOpen = false;
        }
    }

    void dropOwned()
    {
        // (Those it was only shown -- a test's debug_start.txt -- stay; and
        // the map's.)
        drop(false);
    }

    void stream(hldata::GuestCursor *to)
    {
        cursor = to;
        memberOpen = false;
    }

    bool has(const char *name) { return find(name) != nullptr; }

    void mountBank(const std::uint8_t *data, std::uint32_t size)
    {
        bank = data;
        bankSize = data != nullptr ? size : 0;
    }
    bool bankMounted() { return bank != nullptr; }
    bool bankMember(const char *name, const std::uint8_t *&data, std::uint32_t &size)
    {
        hldata::AirBankEntry entry;
        if (bank == nullptr || !hldata::airBankFind(bank, bankSize, hldata::nameHash(bare(name)), entry) ||
            entry.packed != entry.raw || (reinterpret_cast<std::uintptr_t>(bank + entry.offset) & 3) != 0)
            return false;
        data = bank + entry.offset;
        size = entry.raw;
        return true;
    }

    bool memoryOf(const char *name, const std::uint8_t *&data, std::uint32_t &size)
    {
        const File *file = findOwn(name);
        if (file == nullptr || file->packedSize != 0)
            return false;
        data = file->data;
        size = file->size;
        return true;
    }

    void forget(const char *name)
    {
        File *slot = const_cast<File *>(findOwn(name));
        if (slot == nullptr)
            return;
        if (slot->owned)
            std::free(const_cast<std::uint8_t *>(slot->data));
        *slot = files[fileCount - 1];
        files[--fileCount] = File();
    }
    int count() { return fileCount; }
    const char *name(int index) { return index >= 0 && index < fileCount ? files[index].name : ""; }
} // namespace hlrt::air
#endif
