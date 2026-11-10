#include "Detect.hpp"
#include "BundledRom.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace hlds::install;

namespace
{
    std::filesystem::path toPath(const QString &text)
    {
        return std::filesystem::path(text.toStdWString());
    }

#if defined(Q_OS_WIN)
    constexpr System kSystem = System::Windows;
#elif defined(Q_OS_MACOS)
    constexpr System kSystem = System::Mac;
#else
    constexpr System kSystem = System::Linux;
#endif

    bool isRemovable(const QStorageInfo &info)
    {
        const QString root = info.rootPath();
#if defined(Q_OS_WIN)
        return GetDriveTypeW(reinterpret_cast<const wchar_t *>(root.utf16())) == DRIVE_REMOVABLE;
#elif defined(Q_OS_MACOS)
        return root.startsWith(QStringLiteral("/Volumes/"));
#else
        // Where the desktop mounts what is plugged in; else the kernel's word.
        if (root.startsWith(QStringLiteral("/media/")) || root.startsWith(QStringLiteral("/run/media/")))
            return true;
        QString device = QFileInfo(QString::fromUtf8(info.device())).fileName();
        if (device.startsWith(QStringLiteral("mmcblk")))
            return true;
        while (!device.isEmpty() && device.back().isDigit())
            device.chop(1);
        QFile flag(QStringLiteral("/sys/block/%1/removable").arg(device));
        return flag.open(QIODevice::ReadOnly) && flag.read(1) == "1";
#endif
    }

    bool holdsSystem(const QString &root)
    {
        const auto under = [&](const QString &path)
        { return QDir::cleanPath(path).startsWith(QDir::cleanPath(root), Qt::CaseInsensitive); };
#ifdef Q_OS_WIN
        return under(QDir::homePath()) || under(qEnvironmentVariable("SystemRoot"));
#else
        return root == QStringLiteral("/") || QDir::cleanPath(QDir::homePath()).startsWith(QDir::cleanPath(root) + '/');
#endif
    }

    Volume fromInfo(const QStorageInfo &info)
    {
        Volume volume;
        volume.root = toPath(info.rootPath());
        volume.name = (info.displayName().isEmpty() ? info.rootPath() : info.displayName()).toStdString();
        volume.format = QString::fromUtf8(info.fileSystemType()).toStdString();
        volume.total = static_cast<std::uint64_t>(info.bytesTotal());
        volume.free = static_cast<std::uint64_t>(info.bytesAvailable());
        volume.removable = isRemovable(info);
        volume.system = holdsSystem(info.rootPath());
        volume.readOnly = info.isReadOnly();
        lookAtVolume(volume);
        return volume;
    }
}

namespace detect
{
    std::vector<GameFolder> games(const QString &remembered)
    {
        const QString home = QDir::homePath();
        std::vector<std::filesystem::path> roots = steamRoots(toPath(home), kSystem);
#ifdef Q_OS_WIN
        const QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
        const QString path = steam.value(QStringLiteral("SteamPath")).toString();
        if (!path.isEmpty())
            roots.insert(roots.begin(), toPath(path));
#endif
        std::vector<std::filesystem::path> others;
        if (!remembered.isEmpty())
            others.push_back(toPath(remembered));
        others.push_back(toPath(QCoreApplication::applicationDirPath()));
        for (const char *guess : {"Half-Life", "Games/Half-Life", "games/Half-Life", ".wine/drive_c/Sierra/Half-Life",
                                  ".wine/drive_c/Program Files (x86)/Valve/Half-Life"})
            others.push_back(toPath(home) / guess);
#ifdef Q_OS_WIN
        for (const char *guess : {"C:/Sierra/Half-Life", "C:/Program Files (x86)/Sierra/Half-Life",
                                  "C:/Program Files (x86)/Valve/Half-Life", "C:/GOG Games/Half-Life"})
            others.emplace_back(guess);
#endif
        return findGames(roots, others);
    }

    std::vector<Volume> volumes()
    {
        std::vector<Volume> out;
        for (const QStorageInfo &info : QStorageInfo::mountedVolumes())
        {
            if (info.isValid() && info.isReady())
                out.push_back(fromInfo(info));
        }
        return out;
    }

    Volume volumeOf(const QString &path)
    {
        return fromInfo(QStorageInfo(path));
    }

    QString findRom(Device device)
    {
        // The one made with this converter: its data is what that program
        // reads. (Made for the console, and written to the cache folder, as
        // everything is, so that the card's is compared with it and left
        // alone if it is the same.)
        std::vector<std::uint8_t> source;
        const std::span<const std::uint8_t> bundled = bundledRom();
        if (!bundled.empty())
            source.assign(bundled.begin(), bundled.end());
        else
        {
            QDir dir(QCoreApplication::applicationDirPath());
            for (int up = 0; up < 5 && source.empty(); ++up)
            {
                QFile file(dir.filePath(QString::fromLatin1(kRomName)));
                if (file.open(QIODevice::ReadOnly))
                {
                    const QByteArray bytes = file.readAll();
                    source.assign(bytes.begin(), bytes.end());
                }
                if (!dir.cdUp())
                    break;
            }
        }
        std::vector<std::uint8_t> made;
        std::string why;
        if (source.empty() || !romFor(source, device, made, why))
            return {};
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        const QString file = folder + QStringLiteral("/") + QString::fromLatin1(kRomName);
        if (putRom(made, toPath(file), why) == RomPut::Failed)
            return {};
        return file;
    }

    bool eject(const QString &root)
    {
#if defined(Q_OS_MACOS)
        return QProcess::execute(QStringLiteral("diskutil"), {QStringLiteral("eject"), root}) == 0;
#elif defined(Q_OS_LINUX)
        return QProcess::execute(QStringLiteral("gio"), {QStringLiteral("mount"), QStringLiteral("-e"), root}) == 0 ||
               QProcess::execute(QStringLiteral("gio"), {QStringLiteral("mount"), QStringLiteral("-u"), root}) == 0;
#else
        Q_UNUSED(root);
        return false;
#endif
    }
}
