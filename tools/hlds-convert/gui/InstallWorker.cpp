#include "InstallWorker.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <set>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif

#include "Install.hpp"
#include "hlds/Converter.hpp"

namespace
{
    constexpr int kConvertShare = 880; ///< of the bar's 1000, the rest the copy's

    std::filesystem::path toPath(const QString &text) { return std::filesystem::path(text.toStdWString()); }

    /// What says a map's conversion is still good: this program, that file,
    /// and the language it was made in.
    QString stampOf(const QString &bsp, bool textures, const QString &language)
    {
        const QFileInfo program(QCoreApplication::applicationFilePath());
        const QFileInfo map(bsp);
        return QStringLiteral("%1 %2 %3 %4 %5 %6")
            .arg(program.size())
            .arg(program.lastModified().toSecsSinceEpoch())
            .arg(map.size())
            .arg(map.lastModified().toSecsSinceEpoch())
            .arg(textures ? 1 : 0)
            .arg(language.isEmpty() ? QStringLiteral("english") : language);
    }

    /// The file of `map` in the game's maps folder, whatever its case.
    QString bspOf(const QString &game, const QString &map)
    {
        QDir dir(game);
        for (const QString &sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        {
            if (sub.compare(QStringLiteral("maps"), Qt::CaseInsensitive) != 0)
                continue;
            QDir maps(dir.filePath(sub));
            for (const QString &file : maps.entryList(QDir::Files))
            {
                if (file.compare(map + QStringLiteral(".bsp"), Qt::CaseInsensitive) == 0)
                    return maps.filePath(file);
            }
        }
        return {};
    }
}

void InstallWorker::run()
{
    QString why;
    emit step(1);
    if (!convert(why))
    {
        emit finishedRun(false, why);
        return;
    }
    emit step(2);
    if (!copy(why))
    {
        emit finishedRun(false, why);
        return;
    }
    emit step(3);
    emit finishedRun(true, {});
}

bool InstallWorker::convert(QString &why)
{
    QDir().mkpath(job_.cache);
    const QString cardCache = job_.cache + QStringLiteral("/card");
    const QString stage = job_.cache + QStringLiteral("/stage");
    const QString stamps = job_.cache + QStringLiteral("/stamps");
    QDir().mkpath(cardCache);
    QDir().mkpath(stamps);

    hlds::ConvertOptions options;
    options.gameDir = toPath(job_.game);
    options.cardRoot = toPath(cardCache);
    options.stageRoot = toPath(stage);
    options.textures = job_.textures;
    if (!job_.language.isEmpty())
    {
        options.languageDir = hlds::languageFolder(options.gameDir, job_.language.toStdString());
        if (options.languageDir.empty())
        {
            why = QStringLiteral("The %1 translation is no longer beside the game.").arg(job_.language);
            return false;
        }
    }
    if (!job_.ptexconv.isEmpty())
        options.ptexconv = toPath(job_.ptexconv);
    options.status = [this](const std::string &what)
    { emit progress(-1, QString::fromStdString(what)); };
    hlds::Converter converter(options);
    const std::string problem = converter.preflight();
    if (!problem.empty())
    {
        why = QString::fromStdString(problem);
        return false;
    }

    // What was wanted before and is not now goes, or it would be listed.
    const auto forget = [&](const QString &map)
    {
        const QString data = cardCache + QStringLiteral("/_nds/hl_nds/data/");
        QFile::remove(data + QStringLiteral("maps/") + map + QStringLiteral(".npac"));
        QDir air(data + QStringLiteral("air"));
        for (const QString &file : air.entryList({map + QStringLiteral(".*")}, QDir::Files))
            air.remove(file);
        QFile::remove(stage + QStringLiteral("/_nds/hl_nds/data/maps/") + map + QStringLiteral(".hlm"));
        QFile::remove(stamps + QLatin1Char('/') + map);
    };
    const std::set<QString> wanted(job_.maps.begin(), job_.maps.end());
    for (const QString &stamp : QDir(stamps).entryList(QDir::Files))
    {
        if (!wanted.count(stamp))
            forget(stamp);
    }

    // Those still good from a past run, and those to do.
    std::vector<std::string> names;
    QStringList todo;
    for (const QString &map : job_.maps)
    {
        QFile stamp(stamps + QLatin1Char('/') + map);
        const QString now = stampOf(bspOf(job_.game, map), job_.textures, job_.language);
        if (stamp.open(QIODevice::ReadOnly) && QString::fromUtf8(stamp.readAll()) == now)
            continue;
        names.push_back(map.toStdString());
        todo << map;
    }
    const int total = static_cast<int>(names.size());
    const bool someKept = total < job_.maps.size();
    emit logLine(QStringLiteral("%1 maps to convert, %2 still good from last time").arg(total).arg(job_.maps.size() - total));

    std::string error;
    if (total > 0)
    {
        std::atomic<int> done{0};
        failed_ = converter.cookMaps(
            names, [this](std::size_t, const std::string &name)
            { emit logLine(QStringLiteral("Converting %1").arg(QString::fromStdString(name))); },
            [&](const hlds::MapResult &r)
            {
                const QString name = QString::fromStdString(r.name);
                // (What stopping it left unfinished is no fault of the map's.)
                const bool stopped = !r.ok && stop_.load();
                for (const std::string &warning : r.warnings)
                {
                    if (!stopped)
                        emit logLine(name + QStringLiteral(": ") + QString::fromStdString(warning));
                }
                if (!r.ok && !stopped)
                    emit logLine(name + QStringLiteral(": FAILED: ") + QString::fromStdString(r.error));
                else if (r.ok)
                {
                    QFile stamp(stamps + QLatin1Char('/') + name);
                    if (stamp.open(QIODevice::WriteOnly))
                        stamp.write(stampOf(bspOf(job_.game, name), job_.textures, job_.language).toUtf8());
                }
                const int now = ++done;
                emit progress(kConvertShare * now / total, QStringLiteral("Converted %1 of %2 maps").arg(now).arg(total));
            },
            &stop_, error);
        for (const std::string &warning : converter.common().warnings)
            emit logLine(QString::fromStdString(warning));
    }
    if (stop_.load())
    {
        why = QStringLiteral("Stopped. What was converted is kept, and the next run carries on from there.");
        return false;
    }
    // (Maps kept say nothing of what fits a guest until they are packed again.)
    if (someKept)
    {
        emit progress(kConvertShare, QStringLiteral("Packing the maps"));
        std::string packError;
        converter.cookCommon();
        converter.packAll({}, packError);
        if (!packError.empty())
            error = packError;
    }
    if (!error.empty())
    {
        why = QString::fromStdString(error);
        return false;
    }
    return true;
}

bool InstallWorker::copy(QString &why)
{
    emit progress(kConvertShare, QStringLiteral("Comparing with what is on the card"));
    const auto data = toPath(job_.cache + QStringLiteral("/card")) / "_nds" / "hl_nds" / "data";
    const hlds::install::CopyPlan plan =
        hlds::install::planCopy(data, job_.rom.isEmpty() ? std::filesystem::path() : toPath(job_.rom), toPath(job_.card));
    emit logLine(QStringLiteral("%1 files to copy, %2 already on the card").arg(plan.copy.size()).arg(plan.same));
    std::string error;
    const bool ok = hlds::install::runCopy(
        plan,
        [this](std::uint64_t done, std::uint64_t all, const std::string &file)
        {
            const int share = all == 0 ? 0 : static_cast<int>(done * (1000 - kConvertShare) / all);
            emit progress(kConvertShare + share, QStringLiteral("Copying %1").arg(QString::fromStdString(file)));
        },
        &stop_, error);
    if (!ok)
    {
        why = QString::fromStdString(error);
        return false;
    }
#ifndef Q_OS_WIN
    // All of it on the card itself, not waiting in the system's memory.
    ::sync();
#endif
    return true;
}
