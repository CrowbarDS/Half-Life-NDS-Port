// hlds-convert-gui: the installer. See Installer.hpp.

#include <QApplication>

#include "Installer.hpp"
#include "hlds/Converter.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Where QSettings keeps the folder last used, and the cache goes.
    QApplication::setOrganizationName(QStringLiteral("hlds"));
    QApplication::setApplicationName(QStringLiteral("hlds-convert"));
    QApplication::setApplicationVersion(QString::fromLatin1(hlds::kConverterVersion));

    Installer window;
    window.show();
    return app.exec();
}
