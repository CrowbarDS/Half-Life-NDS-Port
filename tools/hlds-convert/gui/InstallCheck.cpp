// hlds-install-check: the installer's own pages, driven with no display, for a
// test: each page's picture taken, and a real installation of the maps named.
//
//   QT_QPA_PLATFORM=offscreen hlds-install-check <shots> <card> <cache> [map,map,...]

#include <QApplication>
#include <QDir>
#include <QPixmap>
#include <QTimer>
#include <QTextStream>

#include "Installer.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("hlds-check"));
    QApplication::setApplicationName(QStringLiteral("hlds-check"));
    if (argc < 4)
        return 2;
    const QString shots = QString::fromLocal8Bit(argv[1]);
    const QString card = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(shots);
    QDir().mkpath(card);
    QTextStream out(stdout);

    Installer wizard;
    wizard.setCacheFolder(QString::fromLocal8Bit(argv[3]));
    if (argc > 4)
        wizard.limitMaps(QString::fromLocal8Bit(argv[4]).split(QLatin1Char(',')));
    wizard.show();
    const auto shot = [&](const char *name)
    {
        QApplication::processEvents();
        wizard.grab().save(QStringLiteral("%1/%2.png").arg(shots, QLatin1String(name)));
    };
    shot("1-welcome");
    wizard.next();
    shot("2-game");
    out << "game: " << wizard.gameFolder() << Qt::endl;
    wizard.next();
    shot("3-card-as-found");
    out << "card found: " << wizard.cardFolder() << Qt::endl;
    wizard.setCardFolder(card);
    wizard.next();
    shot("4-console");
    wizard.setDevice(hlds::install::Device::Emulator);
    wizard.next();
    shot("5-ready");
    QObject::connect(&wizard, &Installer::installDone, &app, [&](bool ok)
                     {
                         out << "done: " << ok << Qt::endl;
                         QTimer::singleShot(200, &app, [&]()
                                            {
                                                shot("7-finished");
                                                QApplication::exit(wizard.succeeded() ? 0 : 1);
                                            });
                     });
    if (argc <= 4)
        return 0; // (the pages alone)
    wizard.next();
    QTimer::singleShot(4000, &app, [&]() { shot("6-installing"); });
    return app.exec();
}
