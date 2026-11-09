#pragma once

// Write a file so that it is either complete under its final name or not there
// at all.
//
// The converter writes to an SD card, and a cook interrupted half way -- a
// pulled card, a closed window -- must not leave a truncated level that the DS
// then tries to load. The bytes go to "<name>.tmp<process>-<n>" first, onto
// the disk itself (not only the system's cache), and are renamed into place
// only once they are all there; the file under the final name is then read
// back and compared with what was meant.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hlds
{
    /// Creates missing parent directories. On failure the final name is left as
    /// it was (absent, or the previous complete file).
    bool writeFileAtomic(const std::filesystem::path &path,
                         const std::vector<std::uint8_t> &bytes, std::string &error);

    bool readFile(const std::filesystem::path &path, std::vector<std::uint8_t> &bytes,
                  std::string &error);

    /// Whether a file is one of writeFileAtomic's temporaries (a run cut
    /// short leaves them): never cooked output, never packed.
    bool isTempName(const std::filesystem::path &path);

    /// This process's own number as text, for a temporary folder that no
    /// other converter running at the same time also uses.
    std::string processTag();
} // namespace hlds
