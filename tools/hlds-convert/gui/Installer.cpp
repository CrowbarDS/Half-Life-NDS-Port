#include "Installer.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizardPage>

#include "Detect.hpp"
#include "InstallWorker.hpp"
#include "hlds/Converter.hpp"
#include "hlds/TextureEncoder.hpp"

using namespace hlds::install;

namespace
{
    constexpr const char *kOrange = "#d97a1e";
    constexpr const char *kGood = "#2e8b57";
    constexpr const char *kBad = "#c0392b";

    QString fromPath(const std::filesystem::path &path)
    {
        return QDir::cleanPath(QString::fromStdWString(path.wstring()));
    }

    QString sizeText(std::uint64_t bytes)
    {
        const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        return gb >= 1.0 ? QStringLiteral("%1 GB").arg(gb, 0, 'f', 1)
                         : QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
    }

    QLabel *wrapped(const QString &text = {})
    {
        auto *label = new QLabel(text);
        label->setWordWrap(true);
        return label;
    }

    void say(QLabel *label, const QString &text, const char *colour = nullptr)
    {
        label->setText(text);
        label->setStyleSheet(colour != nullptr ? QStringLiteral("color: %1; font-weight: 600;").arg(QLatin1String(colour)) : QString());
    }

    QString partsOf(const GameFolder &game)
    {
        QStringList parts;
        if (game.campaign)
            parts << QStringLiteral("the full game");
        if (game.hazardCourse)
            parts << QStringLiteral("Hazard Course");
        if (game.uplink)
            parts << QStringLiteral("Uplink");
        return QStringLiteral("%1 (%2 maps)").arg(parts.join(QStringLiteral(", "))).arg(game.maps);
    }

    QStringList allMaps(const QString &game)
    {
        hlds::ConvertOptions options;
        options.gameDir = std::filesystem::path(game.toStdWString());
        const hlds::Converter converter(options);
        QStringList maps;
        for (const std::string &map : converter.listMaps())
            maps << QString::fromStdString(map);
        return maps;
    }

    // ------------------------------------------------------------------ pages

    class WelcomePage : public QWizardPage
    {
    public:
        WelcomePage()
        {
            setTitle(QStringLiteral("Half-Life on your DS"));
            auto *layout = new QVBoxLayout(this);
            layout->addWidget(wrapped(QStringLiteral(
                "This puts Half-Life on your console's SD card, using your own copy of the game.")));
            layout->addSpacing(10);
            layout->addWidget(wrapped(QStringLiteral("<b>You will need:</b>")));
            layout->addWidget(wrapped(QStringLiteral(u"\u2022 Half-Life installed on this computer")));
            layout->addWidget(wrapped(QStringLiteral(u"\u2022 Your console's SD card, plugged in")));
            layout->addWidget(wrapped(QStringLiteral(u"\u2022 About 1 GB free on this computer, for a while")));
            layout->addSpacing(10);
            layout->addWidget(wrapped(QStringLiteral(
                "It finds both by itself, then takes around half an hour the first time. Saved games already on the card are kept.")));
            layout->addStretch();
        }
    };
}

class GamePage : public QWizardPage
{
public:
    explicit GamePage(Installer *owner) : owner_(owner)
    {
        setTitle(QStringLiteral("Your copy of Half-Life"));
        auto *layout = new QVBoxLayout(this);
        status_ = wrapped();
        layout->addWidget(status_);
        list_ = new QListWidget;
        layout->addWidget(list_, 1);
        auto *row = new QHBoxLayout;
        hint_ = wrapped(QStringLiteral("Is it somewhere else? Choose its folder."));
        row->addWidget(hint_, 1);
        auto *browse = new QPushButton(QStringLiteral("Choose folder..."));
        row->addWidget(browse);
        layout->addLayout(row);
        connect(browse, &QPushButton::clicked, this, &GamePage::browse);
        connect(list_, &QListWidget::currentRowChanged, this, [this](int) { choose(); });
    }

    void initializePage() override
    {
        const QSettings settings;
        found_ = detect::games(settings.value(QStringLiteral("game")).toString());
        // (Given beforehand: that one.)
        if (!owner_->game_.isEmpty())
            add(checkGame(owner_->game_.toStdWString()));
        fill();
    }
    bool isComplete() const override { return !owner_->game_.isEmpty(); }

private:
    void add(const GameFolder &game)
    {
        if (!game.ok)
            return;
        for (std::size_t n = 0; n < found_.size(); ++n)
        {
            if (fromPath(found_[n].valve) == fromPath(game.valve))
            {
                std::swap(found_[0], found_[n]);
                return;
            }
        }
        found_.insert(found_.begin(), game);
    }
    void fill()
    {
        const QSignalBlocker quiet(list_);
        list_->clear();
        for (const GameFolder &game : found_)
            list_->addItem(QStringLiteral("%1\n%2").arg(partsOf(game), fromPath(game.valve)));
        list_->setVisible(!found_.empty());
        if (found_.empty())
        {
            owner_->game_.clear();
            say(status_, QStringLiteral("Half-Life was not found on this computer."), kBad);
            emit completeChanged();
            return;
        }
        list_->setCurrentRow(0);
        choose();
    }
    void choose()
    {
        const int row = list_->currentRow();
        if (row < 0 || row >= static_cast<int>(found_.size()))
            return;
        const GameFolder &game = found_[static_cast<std::size_t>(row)];
        owner_->game_ = fromPath(game.valve);
        say(status_, game.campaign ? QStringLiteral("Found Half-Life.")
                                   : QStringLiteral("Found part of Half-Life: only what is there can be installed."),
            game.campaign ? kGood : kOrange);
        emit completeChanged();
    }
    void browse()
    {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Where is Half-Life?"), QDir::homePath());
        if (folder.isEmpty())
            return;
        const GameFolder game = checkGame(folder.toStdWString());
        if (!game.ok)
        {
            say(hint_, QString::fromStdString(game.why), kBad);
            return;
        }
        say(hint_, QStringLiteral("Is it somewhere else? Choose its folder."));
        add(game);
        fill();
    }

    Installer *owner_;
    std::vector<GameFolder> found_;
    QLabel *status_ = nullptr, *hint_ = nullptr;
    QListWidget *list_ = nullptr;
};

class CardPage : public QWizardPage
{
public:
    explicit CardPage(Installer *owner) : owner_(owner)
    {
        setTitle(QStringLiteral("Your SD card"));
        auto *layout = new QVBoxLayout(this);
        status_ = wrapped();
        layout->addWidget(status_);
        list_ = new QListWidget;
        layout->addWidget(list_, 1);
        warning_ = wrapped();
        layout->addWidget(warning_);
        auto *row = new QHBoxLayout;
        row->addWidget(wrapped(QStringLiteral("Not listed? Choose it yourself.")), 1);
        auto *browse = new QPushButton(QStringLiteral("Choose folder..."));
        row->addWidget(browse);
        layout->addLayout(row);
        connect(browse, &QPushButton::clicked, this, &CardPage::browse);
        connect(list_, &QListWidget::currentRowChanged, this, [this](int) { choose(); });
        // A card plugged in while this page is up appears by itself.
        timer_ = new QTimer(this);
        timer_->setInterval(2000);
        connect(timer_, &QTimer::timeout, this, &CardPage::refresh);
    }

    void initializePage() override
    {
        needed_ = spaceFor(static_cast<int>(owner_->chosenMaps().size()));
        if (!owner_->card_.isEmpty())
            chosen_ = detect::volumeOf(owner_->card_);
        refresh();
        timer_->start();
    }
    void cleanupPage() override { timer_->stop(); }
    bool validatePage() override
    {
        timer_->stop();
        if (haveChosen_ && chosen_.system)
        {
            const auto answer = QMessageBox::warning(this, QStringLiteral("This is not a removable card"),
                                                     QStringLiteral("This is the disk your computer runs from. Install here anyway?"),
                                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
            {
                timer_->start();
                return false;
            }
        }
        return true;
    }
    bool isComplete() const override
    {
        return !owner_->card_.isEmpty() && formatOf(formatName()) != Format::Refused && !chosen_.readOnly;
    }

private:
    std::string formatName() const { return chosen_.format; }

    void refresh()
    {
        const std::vector<Volume> ranked = rankVolumes(detect::volumes(), needed_);
        QString was;
        if (list_->currentRow() >= 0 && list_->currentRow() < static_cast<int>(ranked_.size()))
            was = fromPath(ranked_[static_cast<std::size_t>(list_->currentRow())].root);
        if (!owner_->card_.isEmpty() && was.isEmpty())
            was = owner_->card_;
        // (Nothing has changed: the player's place in the list is left alone.)
        bool same = ranked.size() == ranked_.size();
        for (std::size_t n = 0; same && n < ranked.size(); ++n)
            same = ranked[n].root == ranked_[n].root;
        if (same && !first_)
            return;
        first_ = false;
        ranked_ = ranked;
        const QSignalBlocker quiet(list_);
        list_->clear();
        int select = ranked_.empty() ? -1 : 0;
        for (std::size_t n = 0; n < ranked_.size(); ++n)
        {
            const Volume &volume = ranked_[n];
            list_->addItem(QStringLiteral("%1   %2 free of %3%4\n%5")
                               .arg(QString::fromStdString(volume.name), sizeText(volume.free), sizeText(volume.total),
                                    volume.hasNds ? QStringLiteral("   -   used in a console") : QString(), fromPath(volume.root)));
            if (fromPath(volume.root) == was)
                select = static_cast<int>(n);
        }
        list_->setVisible(!ranked_.empty());
        if (select >= 0)
        {
            list_->setCurrentRow(select);
            choose();
            return;
        }
        if (haveChosen_)
        {
            describe();
            return;
        }
        owner_->card_.clear();
        say(status_, QStringLiteral("No SD card found. Plug it in, and it will appear here."), kOrange);
        say(warning_, {});
        emit completeChanged();
    }
    void choose()
    {
        const int row = list_->currentRow();
        if (row < 0 || row >= static_cast<int>(ranked_.size()))
            return;
        chosen_ = ranked_[static_cast<std::size_t>(row)];
        haveChosen_ = true;
        owner_->card_ = fromPath(chosen_.root);
        describe();
    }
    void describe()
    {
        say(status_, chosen_.hasNds ? QStringLiteral("Found your console's card.") : QStringLiteral("Found a card."), kGood);
        const Format format = formatOf(chosen_.format);
        if (format == Format::Refused)
            say(warning_, QStringLiteral("This one is formatted as %1, which the console cannot read. It has to be FAT32.")
                              .arg(QString::fromStdString(chosen_.format)), kBad);
        else if (chosen_.readOnly)
            say(warning_, QStringLiteral("This card cannot be written to. Is its lock switch on?"), kBad);
        else if (chosen_.free < needed_ && !chosen_.hasNds)
            say(warning_, QStringLiteral("It has %1 free, and about %2 is needed.").arg(sizeText(chosen_.free), sizeText(needed_)), kOrange);
        else if (format == Format::Warn)
            say(warning_, QStringLiteral("This one is formatted as exFAT. Some consoles' loaders cannot read that: FAT32 is safer."), kOrange);
        else
            say(warning_, {});
        emit completeChanged();
    }
    void browse()
    {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Where is your SD card?"), QDir::rootPath());
        if (folder.isEmpty())
            return;
        chosen_ = detect::volumeOf(folder);
        chosen_.root = folder.toStdWString();
        haveChosen_ = true;
        owner_->card_ = QDir::cleanPath(folder);
        list_->clearSelection();
        describe();
    }

    Installer *owner_;
    std::vector<Volume> ranked_;
    Volume chosen_;
    bool haveChosen_ = false, first_ = true;
    std::uint64_t needed_ = 0;
    QLabel *status_ = nullptr, *warning_ = nullptr;
    QListWidget *list_ = nullptr;
    QTimer *timer_ = nullptr;
};

class ReadyPage : public QWizardPage
{
public:
    explicit ReadyPage(Installer *owner) : owner_(owner)
    {
        setTitle(QStringLiteral("Ready to install"));
        setCommitPage(true);
        setButtonText(QWizard::CommitButton, QStringLiteral("Install"));
        auto *layout = new QVBoxLayout(this);
        summary_ = wrapped();
        layout->addWidget(summary_);
        problem_ = wrapped();
        layout->addWidget(problem_);
        layout->addStretch();
        auto *more = new QToolButton;
        more->setText(QStringLiteral("Advanced"));
        more->setCheckable(true);
        more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        more->setArrowType(Qt::RightArrow);
        more->setAutoRaise(true);
        layout->addWidget(more);
        advanced_ = new QWidget;
        auto *inner = new QVBoxLayout(advanced_);
        inner->setContentsMargins(16, 0, 0, 0);
        for (int part = 0; part < 4; ++part)
        {
            parts_[part] = new QCheckBox;
            parts_[part]->setChecked(true);
            inner->addWidget(parts_[part]);
            connect(parts_[part], &QCheckBox::toggled, this, &ReadyPage::update);
        }
        textures_ = new QCheckBox(QStringLiteral("Convert the textures (without them the world is plain colours)"));
        textures_->setChecked(true);
        inner->addWidget(textures_);
        connect(textures_, &QCheckBox::toggled, this, &ReadyPage::update);
        auto *row = new QHBoxLayout;
        encoder_ = wrapped();
        row->addWidget(encoder_, 1);
        encoderBrowse_ = new QPushButton(QStringLiteral("Find ptexconv..."));
        row->addWidget(encoderBrowse_);
        inner->addLayout(row);
        connect(encoderBrowse_, &QPushButton::clicked, this, [this]()
                {
                    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Where is ptexconv?"));
                    if (!file.isEmpty())
                    {
                        owner_->ptexconv_ = file;
                        update();
                    }
                });
        advanced_->setVisible(false);
        layout->addWidget(advanced_);
        connect(more, &QToolButton::toggled, this, [this, more](bool on)
                {
                    more->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                    advanced_->setVisible(on);
                });
    }

    void initializePage() override
    {
        maps_ = owner_->limit_.isEmpty() ? allMaps(owner_->game_) : owner_->limit_;
        int counts[4] = {};
        for (const QString &map : maps_)
            ++counts[static_cast<int>(partOfMap(map.toStdString()))];
        static const char *const kNames[4] = {"The full game", "Hazard Course", "Uplink", "Multiplayer maps"};
        for (int part = 0; part < 4; ++part)
        {
            parts_[part]->setText(QStringLiteral("%1 (about %2)").arg(QLatin1String(kNames[part]),
                                                                      sizeText(spaceFor(counts[part]) - spaceFor(0))));
            parts_[part]->setVisible(counts[part] > 0);
            parts_[part]->setChecked(counts[part] > 0);
        }
        update();
    }
    bool isComplete() const override { return problem_->text().isEmpty() && !chosen().isEmpty(); }

    QStringList chosen() const
    {
        QStringList out;
        for (const QString &map : maps_)
        {
            if (parts_[static_cast<int>(partOfMap(map.toStdString()))]->isChecked())
                out << map;
        }
        return out;
    }

private:
    void update()
    {
        owner_->textures_ = textures_->isChecked();
        const std::uint64_t needed = spaceFor(static_cast<int>(chosen().size()));
        const Volume card = detect::volumeOf(owner_->card_);
        // What is on the card of ours is replaced, not added to.
        std::uint64_t ours = 0;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(std::filesystem::path(owner_->card_.toStdWString()) / "_nds" / "hl_nds" / "data", ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (it->is_regular_file(ec))
                ours += it->file_size(ec);
        }
        const QString rom = detect::findRom();
        summary_->setText(QStringLiteral(
            "<table cellspacing=6>"
            "<tr><td><b>From</b></td><td>%1</td></tr>"
            "<tr><td><b>To</b></td><td>%2</td></tr>"
            "<tr><td><b>On the card</b></td><td>about %3 (%4 free)</td></tr>"
            "<tr><td><b>Time</b></td><td>around half an hour; less if some was done before</td></tr>"
            "</table><p>Your saved games and settings on the card are kept.</p>%5")
            .arg(owner_->game_.toHtmlEscaped(), owner_->card_.toHtmlEscaped(), sizeText(needed), sizeText(card.free),
                 rom.isEmpty() ? QStringLiteral("<p>The console's program, half-life-nds-port.nds, was not found beside this "
                                                "application: you will have to copy it to the card yourself.</p>")
                               : QString()));

        const QString cache = owner_->cacheFolder();
        QDir().mkpath(cache);
        const auto cacheFree = static_cast<std::uint64_t>(QStorageInfo(cache).bytesAvailable());
        hlds::ConvertOptions options;
        options.gameDir = owner_->game_.toStdWString();
        options.cardRoot = owner_->card_.toStdWString();
        options.textures = textures_->isChecked();
        if (!owner_->ptexconv_.isEmpty())
            options.ptexconv = owner_->ptexconv_.toStdWString();
        const bool encoderFound = !hlds::Converter(options).preflight().starts_with("ptexconv");
        encoder_->setText(owner_->ptexconv_.isEmpty() ? (encoderFound ? QStringLiteral("Texture converter found.") : QStringLiteral("Texture converter not found."))
                                                      : owner_->ptexconv_);
        QString problem;
        if (chosen().isEmpty())
            problem = QStringLiteral("Nothing is chosen to install.");
        else if (card.free + ours < needed)
            problem = QStringLiteral("The card is too small: about %1 is needed, and it has %2 free.").arg(sizeText(needed), sizeText(card.free + ours));
        else if (cacheFree < needed * 2)
            problem = QStringLiteral("This computer needs about %1 free while converting, and has %2.").arg(sizeText(needed * 2), sizeText(cacheFree));
        else if (!encoderFound && textures_->isChecked())
            problem = QStringLiteral("The texture converter, ptexconv, was not found. Under Advanced, show where it is, or go without textures.");
        say(problem_, problem, kBad);
        emit completeChanged();
    }

    Installer *owner_;
    QStringList maps_;
    QLabel *summary_ = nullptr, *problem_ = nullptr, *encoder_ = nullptr;
    QWidget *advanced_ = nullptr;
    QCheckBox *parts_[4] = {};
    QCheckBox *textures_ = nullptr;
    QPushButton *encoderBrowse_ = nullptr;
};

class InstallingPage : public QWizardPage
{
public:
    explicit InstallingPage(Installer *owner) : owner_(owner)
    {
        setTitle(QStringLiteral("Installing"));
        auto *layout = new QVBoxLayout(this);
        for (int n = 0; n < 2; ++n)
        {
            lines_[n] = new QLabel;
            layout->addWidget(lines_[n]);
        }
        showLines(0);
        layout->addSpacing(8);
        bar_ = new QProgressBar;
        // (Moving by itself until there is a measure of how far it has got.)
        bar_->setRange(0, 0);
        bar_->setTextVisible(false);
        layout->addWidget(bar_);
        now_ = wrapped();
        layout->addWidget(now_);
        left_ = wrapped();
        layout->addWidget(left_);
        layout->addStretch();
        auto *more = new QToolButton;
        more->setText(QStringLiteral("Show details"));
        more->setCheckable(true);
        more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        more->setArrowType(Qt::RightArrow);
        more->setAutoRaise(true);
        layout->addWidget(more);
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(5000);
        log_->setVisible(false);
        layout->addWidget(log_, 1);
        connect(more, &QToolButton::toggled, this, [this, more](bool on)
                {
                    more->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                    log_->setVisible(on);
                });
    }
    void initializePage() override
    {
        finished_ = false;
        QTimer::singleShot(0, owner_, &Installer::start);
    }
    bool isComplete() const override { return finished_; }

    void showLines(int at)
    {
        static const char *const kWords[2] = {"Converting the game", "Copying to the card, and checking it"};
        for (int n = 0; n < 2; ++n)
        {
            const bool done = n < at, here = n == at;
            lines_[n]->setText(QStringLiteral("%1  %2").arg(done ? QStringLiteral("\u2713") : here ? QStringLiteral("\u25B6") : QStringLiteral("\u2022"),
                                                            QLatin1String(kWords[n])));
            lines_[n]->setStyleSheet(here ? QStringLiteral("font-weight: 600; color: %1;").arg(QLatin1String(kOrange))
                                          : done ? QStringLiteral("color: %1;").arg(QLatin1String(kGood)) : QStringLiteral("color: gray;"));
        }
    }
    void setProgress(int permille, const QString &now)
    {
        if (permille >= 0)
        {
            bar_->setRange(0, 1000);
            bar_->setValue(permille);
            // How long is left, once there is enough to go by.
            if (!clock_.isValid())
                clock_.start();
            const qint64 spent = clock_.elapsed();
            if (permille > 30 && permille > startedAt_ && spent > 20000)
            {
                const qint64 left = spent * (1000 - permille) / (permille - startedAt_);
                const qint64 minutes = left / 60000 + 1;
                left_->setText(minutes <= 1 ? QStringLiteral("About a minute left") : QStringLiteral("About %1 minutes left").arg(minutes));
            }
        }
        now_->setText(now);
    }
    void addLog(const QString &line) { log_->appendPlainText(line); }
    void setFinished()
    {
        finished_ = true;
        emit completeChanged();
    }

private:
    Installer *owner_;
    QLabel *lines_[2] = {};
    QProgressBar *bar_ = nullptr;
    QLabel *now_ = nullptr, *left_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QElapsedTimer clock_;
    int startedAt_ = 0;
    bool finished_ = false;
};

class FinishedPage : public QWizardPage
{
public:
    explicit FinishedPage(Installer *owner) : owner_(owner)
    {
        auto *layout = new QVBoxLayout(this);
        words_ = wrapped();
        words_->setTextFormat(Qt::RichText);
        layout->addWidget(words_);
        layout->addStretch();
        eject_ = new QPushButton(QStringLiteral("Eject the card"));
        layout->addWidget(eject_, 0, Qt::AlignLeft);
        ejected_ = wrapped();
        layout->addWidget(ejected_);
        free_ = new QPushButton(QStringLiteral("Free up this computer's disk space"));
        layout->addWidget(free_, 0, Qt::AlignLeft);
        connect(eject_, &QPushButton::clicked, this, [this]()
                {
                    say(ejected_, detect::eject(owner_->card_) ? QStringLiteral("You can take the card out now.")
                                                               : QStringLiteral("Eject it from your system's file manager, then take it out."));
                });
        connect(free_, &QPushButton::clicked, this, [this]()
                {
                    QDir(owner_->cacheFolder()).removeRecursively();
                    free_->setEnabled(false);
                    free_->setText(QStringLiteral("Disk space freed"));
                });
    }
    void initializePage() override
    {
        setFinalPage(true);
        const QString rom = detect::findRom();
        if (owner_->succeeded_)
        {
            setTitle(QStringLiteral("Half-Life is on your card"));
            QString text;
            if (owner_->failed_ > 0)
                text += QStringLiteral("<p style='color:%1'><b>%2 maps could not be converted</b>, and are left out. Show details on the last page says which.</p>")
                            .arg(QLatin1String(kOrange)).arg(owner_->failed_);
            text += QStringLiteral("<p><b>What to do now:</b></p><ol><li>Eject the card, and put it in your console.</li>");
            text += rom.isEmpty() ? QStringLiteral("<li>Copy <b>half-life-nds-port.nds</b> to the root of the card; it was not found here.</li><li>Start it from your console's menu.</li>")
                                  : QStringLiteral("<li>Start <b>half-life-nds-port.nds</b> from your console's menu.</li>");
            text += QStringLiteral("</ol>");
            words_->setText(text);
        }
        else
        {
            setTitle(QStringLiteral("The installation did not finish"));
            words_->setText(QStringLiteral("<p>%1</p><p>Run this again to carry on: nothing done so far is lost, and your card still works as it did.</p>")
                                .arg(owner_->why_.toHtmlEscaped()));
        }
        eject_->setVisible(owner_->succeeded_);
        free_->setVisible(owner_->succeeded_);
    }

private:
    Installer *owner_;
    QLabel *words_ = nullptr, *ejected_ = nullptr;
    QPushButton *eject_ = nullptr, *free_ = nullptr;
};

Installer::Installer(QWidget *parent) : QWizard(parent)
{
    setWindowTitle(QStringLiteral("Half-Life DS Installer"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnLastPage);
    setOption(QWizard::NoCancelButtonOnLastPage);
    resize(720, 500);
    setPage(Welcome, new WelcomePage);
    setPage(Game, new GamePage(this));
    setPage(Card, new CardPage(this));
    setPage(Ready, new ReadyPage(this));
    setPage(Installing, new InstallingPage(this));
    setPage(Finished, new FinishedPage(this));
    setButtonText(QWizard::FinishButton, QStringLiteral("Close"));

    // Where it has got to, down the side.
    auto *side = new QWidget;
    side->setStyleSheet(QStringLiteral("background: #1b1b1f;"));
    side->setMinimumWidth(170);
    auto *column = new QVBoxLayout(side);
    column->setContentsMargins(18, 24, 12, 18);
    column->setSpacing(14);
    auto *title = new QLabel(QStringLiteral("Half-Life DS"));
    title->setStyleSheet(QStringLiteral("color: %1; font-size: 16pt; font-weight: 700;").arg(QLatin1String(kOrange)));
    column->addWidget(title);
    column->addSpacing(10);
    for (const char *word : {"Welcome", "Half-Life", "SD card", "Ready", "Installing", "Finished"})
    {
        auto *label = new QLabel(QLatin1String(word));
        steps_.push_back(label);
        column->addWidget(label);
    }
    column->addStretch();
    setSideWidget(side);
    connect(this, &QWizard::currentIdChanged, this, &Installer::showStep);
    showStep(Welcome);

    const QSettings settings;
    Q_UNUSED(settings);
}

void Installer::showStep(int id)
{
    for (std::size_t n = 0; n < steps_.size(); ++n)
    {
        const int at = static_cast<int>(n);
        steps_[n]->setStyleSheet(at == id ? QStringLiteral("color: white; font-weight: 700;")
                                 : at < id ? QStringLiteral("color: #9a9aa2;")
                                           : QStringLiteral("color: #5c5c64;"));
    }
}

QString Installer::cacheFolder() const
{
    return cache_.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/install") : cache_;
}

QStringList Installer::chosenMaps() const
{
    if (const auto *ready = static_cast<const ReadyPage *>(page(Ready)))
    {
        const QStringList maps = ready->chosen();
        if (!maps.isEmpty())
            return maps;
    }
    return limit_.isEmpty() ? allMaps(game_) : limit_;
}

void Installer::start()
{
    if (worker_ != nullptr)
        return;
    QSettings().setValue(QStringLiteral("game"), game_);
    auto *page = static_cast<InstallingPage *>(this->page(Installing));
    InstallWorker::Job job;
    job.game = game_;
    job.card = card_;
    job.cache = cacheFolder();
    job.rom = detect::findRom();
    job.ptexconv = ptexconv_;
    job.maps = chosenMaps();
    job.textures = textures_;
    worker_ = new InstallWorker(std::move(job), this);
    connect(worker_, &InstallWorker::step, this, [page](int step) { page->showLines(step - 1); });
    connect(worker_, &InstallWorker::progress, this, [page](int permille, const QString &now) { page->setProgress(permille, now); });
    connect(worker_, &InstallWorker::logLine, this, [page](const QString &line) { page->addLog(line); });
    connect(worker_, &InstallWorker::finishedRun, this, [this, page](bool ok, const QString &why)
            {
                succeeded_ = ok;
                why_ = why;
                failed_ = worker_->failedMaps();
                worker_->wait();
                page->setFinished();
                if (stopping_)
                {
                    QWizard::reject();
                    return;
                }
                emit installDone(ok);
                next();
            });
    worker_->start();
}

void Installer::reject()
{
    // A run going on: asked first, and it is stopped, not abandoned.
    if (worker_ != nullptr && worker_->isRunning())
    {
        if (stopping_)
            return;
        const auto answer = QMessageBox::question(this, QStringLiteral("Stop installing?"),
                                                  QStringLiteral("What is done so far is kept, and running this again carries on from there."));
        if (answer != QMessageBox::Yes)
            return;
        stopping_ = true;
        worker_->stop();
        static_cast<InstallingPage *>(page(Installing))->setProgress(-1, QStringLiteral("Stopping, once the maps being converted are done..."));
        button(QWizard::CancelButton)->setEnabled(false);
        return;
    }
    QWizard::reject();
}
