#include "CookWorker.hpp"

CookWorker::CookWorker(hlds::ConvertOptions options, QStringList maps, QObject *parent)
    : QThread(parent), options_(std::move(options)), maps_(std::move(maps))
{
}

void CookWorker::run()
{
    hlds::Converter converter(options_);
    std::vector<std::string> names;
    for (const QString &map : maps_)
        names.push_back(map.toStdString());

    const int total = static_cast<int>(names.size());
    std::string error;
    const int failed = converter.cookMaps(
        names,
        [&](std::size_t index, const std::string &name)
        { emit mapStarted(static_cast<int>(index), total, QString::fromStdString(name)); },
        [&](const hlds::MapResult &r)
        {
            for (const std::string &warning : r.warnings)
                emit logLine(QString::fromStdString(r.name) + QStringLiteral(": warning: ") + QString::fromStdString(warning));
            if (!r.ok)
            {
                emit logLine(QString::fromStdString(r.name) + QStringLiteral(": FAILED: ") + QString::fromStdString(r.error));
                return;
            }
            emit logLine(QStringLiteral("  %1 KB, %2 textures (%3 new)")
                             .arg(r.levelBytes / 1024)
                             .arg(r.texturesUsed)
                             .arg(r.texturesEncoded));
        },
        &cancel_, error);
    if (!error.empty())
        emit logLine(QStringLiteral("error: ") + QString::fromStdString(error));
    emit finishedRun(failed, cancel_.load());
}
