#pragma once

// The installation itself, off the window's thread: the game converted into
// this computer's cache, then what that made copied to the card. Maps a past
// run already converted, from the same game by the same program, are not done
// again.

#include <QString>
#include <QStringList>
#include <QThread>

#include <atomic>

class InstallWorker : public QThread
{
    Q_OBJECT
public:
    struct Job
    {
        QString game;     ///< the valve folder
        QString card;     ///< the card's root
        QString cache;    ///< this computer's folder for the conversion
        QString rom;      ///< empty: none to copy
        QString ptexconv; ///< empty: look for it
        QStringList maps; ///< those to install
        bool textures = true;
        QString language; ///< a translation beside the game (french); empty: the game's own
    };

    explicit InstallWorker(Job job, QObject *parent = nullptr) : QThread(parent), job_(std::move(job)) {}

    void stop() { stop_.store(true); }
    /// How many maps could not be converted, as the run ended.
    int failedMaps() const { return failed_; }

signals:
    void step(int step);                  ///< 0 checking, 1 converting, 2 copying, 3 done
    void progress(int permille, const QString &now);
    void logLine(const QString &line);
    void finishedRun(bool ok, const QString &why);

protected:
    void run() override;

private:
    bool convert(QString &why);
    bool copy(QString &why);

    Job job_;
    std::atomic<bool> stop_{false};
    int failed_ = 0;
};
