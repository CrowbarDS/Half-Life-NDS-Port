#pragma once

// What only the running system can say: where Steam is, and which volumes are
// mounted. The decisions are hlds::install's.

#include <QString>

#include <vector>

#include "Install.hpp"

namespace detect
{
    /// Every Half-Life that can be found, `remembered` (the last one used) too.
    std::vector<hlds::install::GameFolder> games(const QString &remembered);
    /// All the volumes, looked at.
    std::vector<hlds::install::Volume> volumes();
    /// The volume `path` is on, looked at.
    hlds::install::Volume volumeOf(const QString &path);
    /// The console's program: the one built into this converter (put in this
    /// computer's cache folder, to be copied from); or, with none built in,
    /// one beside this program, or in the folders above it (a build tree).
    /// Empty: not there.
    QString findRom();
    /// A volume, ejected if the system has a way; false if not.
    bool eject(const QString &root);
}
