// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>

#include <QFile>
#include <QTimer>

#include "main_window.h"
#include "mii_render.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KartRecomp"));
    QApplication::setApplicationName(QStringLiteral("KartRecomp"));
    QApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    // Development: --mii-render <resource folder> <out.png> [key=value ...] draws a default Mii with the given values changed
    // (mii_db.h field keys; "female=1" starts from the default female Mii); --mii-astc-job <resource folder> <out> writes the
    // engine's ASTC decode job.
    if (const QStringList a = QCoreApplication::arguments(); a.size() >= 4 && (a[1] == QLatin1String("--mii-render") || a[1] == QLatin1String("--mii-astc-job"))) {
        Mii::Renderer renderer;
        QString error;
        if (!renderer.Load(a[2], &error)) return qPrintable(error), 2;
        if (a[1] == QLatin1String("--mii-astc-job")) {
            QFile f(a[3]);
            return f.open(QIODevice::WriteOnly) && f.write(renderer.AstcJob()) > 0 ? 0 : 3;
        }
        Mii::StoreData mii = Mii::StoreData::MakeDefault(QStringLiteral("Test"), a.contains(QStringLiteral("female=1")));
        int w = 320, h = 400;
        for (int i = 4; i < a.size(); ++i) {
            const QStringList kv = a[i].split(QLatin1Char('='));
            if (kv.size() != 2) continue;
            if (kv[0] == QLatin1String("db")) { // db=<MiiDatabase.dat>:<index>
                const int colon = kv[1].lastIndexOf(QLatin1Char(':'));
                if (const auto db = Mii::Database::Load(kv[1].left(colon)); db && kv[1].mid(colon + 1).toInt() < db->Count()) mii = db->At(kv[1].mid(colon + 1).toInt());
            } else if (kv[0] == QLatin1String("w")) w = kv[1].toInt();
            else if (kv[0] == QLatin1String("h")) h = kv[1].toInt();
            else if (Mii::FindField(kv[0].toUtf8().constData())) mii.Set(kv[0].toUtf8().constData(), kv[1].toInt());
        }
        return renderer.Render(mii, w, h).save(a[3]) ? 0 : 4;
    }

    MainWindow window;
    // --nolauncher (or --play): start the built game straight away with the saved settings, without showing the launcher, and
    // quit when the game exits. For playing from a TV or Steam (issue #17); the in-game menu (F10, L3+R3) has the settings.
    const QStringList args = QCoreApplication::arguments();
    // -f / --fullscreen: the game starts fullscreen (with or without --nolauncher), like the engine's own -f.
    // One dash is accepted too (-nolauncher, -fullscreen): easy to type that way in Steam's launch options.
    const auto has = [&](const char* name) { return args.contains(QStringLiteral("--") + QLatin1String(name)) || args.contains(QStringLiteral("-") + QLatin1String(name)); };
    if (args.contains(QStringLiteral("-f")) || has("fullscreen")) window.forceFullscreen();
    if (has("nolauncher") || has("play")) {
        if (!window.launchWithoutWindow()) return 1;
        return app.exec();
    }
    window.show();
    // Development: KARTRECOMP_LAUNCHER_SHOT=<tab title part>=<png>[=<seconds>] opens that tab, saves a picture of the window and quits.
    if (const QString shot = qEnvironmentVariable("KARTRECOMP_LAUNCHER_SHOT"); !shot.isEmpty()) {
        const QStringList part = shot.split(QLatin1Char('='));
        if (part.size() >= 2) {
            window.showTab(part[0]);
            QTimer::singleShot(part.size() > 2 ? part[2].toInt() * 1000 : 3000, &window, [&window, part] {
                window.grab().save(part[1]);
                QApplication::quit();
            });
        }
    }
    return app.exec();
}
