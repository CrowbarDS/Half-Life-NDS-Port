// hlds-convert-gui: the installer. See Installer.hpp.

#include <QApplication>

#include "Installer.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Where QSettings keeps the folder last used, and the cache goes.
    QApplication::setOrganizationName(QStringLiteral("hlds"));
    QApplication::setApplicationName(QStringLiteral("hlds-convert"));

    Installer window;
    window.show();
    return app.exec();
}
