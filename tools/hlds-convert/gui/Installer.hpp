#pragma once

// The installer: a few pages, each with one thing to confirm. It finds
// Half-Life and the card by itself, and the player only has to look.

#include <QWizard>

#include <vector>

#include "Install.hpp"

class QCheckBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class InstallWorker;

class Installer : public QWizard
{
    Q_OBJECT
public:
    enum PageId
    {
        Welcome,
        Game,
        Card,
        Ready,
        Installing,
        Finished,
    };
    explicit Installer(QWidget *parent = nullptr);

    // What is chosen, as the pages say.
    QString gameFolder() const { return game_; }
    QString cardFolder() const { return card_; }
    void setGameFolder(const QString &folder) { game_ = folder; }
    void setCardFolder(const QString &folder) { card_ = folder; }
    /// The maps to install, as Advanced has them.
    QStringList chosenMaps() const;
    /// (hlds-install-check) these maps alone; and its cache elsewhere.
    void limitMaps(const QStringList &maps) { limit_ = maps; }
    void setCacheFolder(const QString &folder) { cache_ = folder; }
    QString cacheFolder() const;
    void start();
    bool succeeded() const { return succeeded_; }

signals:
    void installDone(bool ok);

protected:
    void reject() override;

private:
    friend class GamePage;
    friend class CardPage;
    friend class ReadyPage;
    friend class InstallingPage;
    friend class FinishedPage;
    void showStep(int id);

    QString game_, card_, ptexconv_, why_;
    QString language_; ///< a translation beside the game (french); empty: English
    QStringList limit_;
    QString cache_;
    bool succeeded_ = false;
    bool textures_ = true;
    bool stopping_ = false;
    int failed_ = 0;
    InstallWorker *worker_ = nullptr;
    std::vector<QLabel *> steps_;
};
