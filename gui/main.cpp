#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ezp2019-gui"));
    app.setApplicationDisplayName(QStringLiteral("EZP2019 USB Programmer"));
    app.setOrganizationName(QStringLiteral("ezp2019"));

    MainWindow window;
    window.show();

    return app.exec();
}
