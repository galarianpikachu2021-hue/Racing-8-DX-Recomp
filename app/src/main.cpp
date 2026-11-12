// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>

#include "main_window.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KartRecomp"));
    QApplication::setApplicationName(QStringLiteral("KartRecomp"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    MainWindow window;
    // --nolauncher (or --play): start the built game straight away with the saved settings, without showing the launcher, and
    // quit when the game exits. For playing from a TV or Steam (issue #17); the in-game menu (F10, L3+R3) has the settings.
    const QStringList args = QCoreApplication::arguments();
    // -f / --fullscreen: the game starts fullscreen (with or without --nolauncher), like the engine's own -f.
    if (args.contains(QStringLiteral("-f")) || args.contains(QStringLiteral("--fullscreen"))) window.forceFullscreen();
    if (args.contains(QStringLiteral("--nolauncher")) || args.contains(QStringLiteral("--play"))) {
        if (!window.launchWithoutWindow()) return 1;
        return app.exec();
    }
    window.show();
    return app.exec();
}
