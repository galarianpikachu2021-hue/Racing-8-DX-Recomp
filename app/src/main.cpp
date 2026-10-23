// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>

#include "main_window.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KartRecomp"));
    QApplication::setApplicationName(QStringLiteral("KartRecomp"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    MainWindow window;
    window.show();
    return app.exec();
}
