// A console sent the game by Download Play has no card at all (measured on
// a real one: no FAT to mount), and reads only what the host sends it, by its
// own drive (net/AirDevice.cpp). libnds's file calls still name the SD card's
// and NitroFS's -- every open, read and stat asks them first whether a path is
// theirs -- and bring in with them FatFs, the SD and NAND drivers and the
// card's reads: over 10 KB of a guest's program for drives it never has.
//
// Here, in the child alone, each answers as a drive that is not there: no
// path is theirs, and a call that reaches one anyway fails with ENODEV, as
// it would with nothing mounted. (A test build that reads the emulator's
// card, HLDS_CHILD_FROMCARD, keeps the real ones.) The declarations are
// libnds's own (source/arm9/libc/fat_device.h, nitrofs_device.h).

#if defined(HLDS_CHILD) && !defined(HLDS_CHILD_FROMCARD)

#include <cerrno>
#include <cstddef>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <utime.h>

namespace
{
    int none()
    {
        errno = ENODEV;
        return -1;
    }
} // namespace

extern "C"
{
    bool fat_isdrive(const char *) { return false; }
    bool nitrofs_isdrive(const char *) { return false; }

    int fat_open(const char *, int, mode_t) { return none(); }
    ssize_t fat_read(int, void *, size_t) { return none(); }
    ssize_t fat_write(int, const void *, size_t) { return none(); }
    int fat_fsync(int) { return none(); }
    int fat_close(int) { return none(); }
    off_t fat_lseek(int, off_t, int) { return none(); }
    int fat_unlink(const char *) { return none(); }
    int fat_rmdir(const char *) { return none(); }
    int fat_stat(const char *, struct stat *) { return none(); }
    int fat_fstat(int, struct stat *) { return none(); }
    int fat_isatty(int) { return 0; }
    int fat_rename(const char *, const char *) { return none(); }
    int fat_ftruncate(int, off_t) { return none(); }
    int fat_truncate(const char *, off_t) { return none(); }
    int fat_mkdir(const char *, mode_t) { return none(); }
    int fat_access(const char *, int) { return none(); }
    int fat_statvfs(const char *, struct statvfs *) { return none(); }
    int fat_fstatvfs(int, struct statvfs *) { return none(); }
    void *fat_opendir(const char *, DIR *)
    {
        errno = ENODEV;
        return nullptr;
    }
    int fat_closedir(DIR *) { return none(); }
    struct dirent *fat_readdir(DIR *)
    {
        errno = ENODEV;
        return nullptr;
    }
    void fat_rewinddir(DIR *) {}
    void fat_seekdir(DIR *, long) {}
    long fat_telldir(DIR *) { return none(); }
    int fat_utimes(const char *, const struct timeval *) { return none(); }
    int fat_utime(const char *, const struct utimbuf *) { return none(); }

    int nitrofs_open(const char *, int, mode_t) { return none(); }
    ssize_t nitrofs_read(int, void *, size_t) { return none(); }
    off_t nitrofs_lseek(int, off_t, int) { return none(); }
    int nitrofs_close(int) { return none(); }
    int nitrofs_stat(const char *, struct stat *) { return none(); }
    int nitrofs_fstat(int, struct stat *) { return none(); }
    int nitrofs_isatty(int) { return 0; }
    int nitrofs_access(const char *, int) { return none(); }
    void *nitrofs_opendir(const char *, DIR *)
    {
        errno = ENODEV;
        return nullptr;
    }
    int nitrofs_closedir(DIR *) { return none(); }
    struct dirent *nitrofs_readdir(DIR *)
    {
        errno = ENODEV;
        return nullptr;
    }
    void nitrofs_rewinddir(DIR *) {}
    void nitrofs_seekdir(DIR *, long) {}
    long nitrofs_telldir(DIR *) { return none(); }
}

#endif
