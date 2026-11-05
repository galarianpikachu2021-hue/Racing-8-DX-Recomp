// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "build_runner.h"
#include "test_support.h"

#include "game_package.h"

namespace {
void Write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
}
} // namespace

class TestGamePackage : public QObject {
    Q_OBJECT
private slots:
    void installedPathsFollowTheXdgLayout() {
        NEEDS_SH();
        const auto l = DataLayout::FromRoots("/d/kartrecomp", "/c/kartrecomp");
        const auto p = GamePackage::Installed(l, "/cfg/kartrecomp", "/app/engine/mk8d-engine", "v1");
        QVERIFY(p.isInstalled());
        QCOMPARE(p.executable(), QString("/app/engine/mk8d-engine"));
        QCOMPARE(p.configFile(), QString("/cfg/kartrecomp/sdl2-config.ini"));
        QCOMPARE(p.overlayFile(), QString("/cfg/kartrecomp/kartrecomp_overlay.ini"));
        QCOMPARE(p.modsRoot(), QString("/d/kartrecomp"));
        QCOMPARE(p.pluginsParent(), QString("/app/engine"));
        QCOMPARE(p.workingDirectory(), QString("/app/engine"));
        QCOMPARE(p.coverageReport(), QString("/d/kartrecomp/log/recomp_coverage.txt"));
    }
    void installedLaunchArgumentsListModuleBaseUpdateAndEachDlc() {
        QTemporaryDir d;
        const auto l = DataLayout::FromRoots(d.filePath("d/kartrecomp"), d.filePath("c/kartrecomp"));
        Write(l.dlcDir() + "/b.nsp", "x");
        Write(l.dlcDir() + "/a.nsp", "x");
        const auto p = GamePackage::Installed(l, d.filePath("cfg"), "/e/mk8d-engine", "v1");
        const QStringList a = p.launchArguments(false);
        QVERIFY(a.mid(0, 2) == QStringList({"--module-dir", l.moduleDir()}));
        QVERIFY(a.contains(l.base()) && a.contains(l.update()));
        QCOMPARE(a.indexOf(l.dlcDir() + "/a.nsp") + 2, a.indexOf(l.dlcDir() + "/b.nsp")); // each DLC after its own --content-dlc, sorted
        QVERIFY(!a.contains("-f"));
        QVERIFY(p.launchArguments(true).contains("-f"));
    }
    void installedEnvironmentNamesTheAppAndReadsLaunchEnv() {
        QTemporaryDir d;
        const QString cfg = d.filePath("cfg/kartrecomp");
        Write(cfg + "/launch.env", "# comment\nKARTRECOMP_PIN=2,3\n\nKARTRECOMP_NATIVE_HEIGHT=1440\nbroken line\n");
        const auto l = DataLayout::FromRoots(d.filePath("d/kartrecomp"), d.filePath("c/kartrecomp"));
        const auto p = GamePackage::Installed(l, cfg, "/e/mk8d-engine", "v1");
        const auto env = p.launchEnvironment();
        QCOMPARE(env.value("KARTRECOMP_APP_NAME"), QString("kartrecomp"));
        QCOMPARE(env.value("KARTRECOMP_PIN"), QString("2,3"));
        QCOMPARE(env.value("KARTRECOMP_NATIVE_HEIGHT"), QString("1440"));
        QCOMPARE(env.value("KARTRECOMP_NATIVE_GPU"), QString("native"));
#ifndef Q_OS_WIN
        QVERIFY(env.value("XDG_DATA_HOME") != d.filePath("d")); // the engine finds its own XDG folders from the app name
#else
        QCOMPARE(env.value("XDG_DATA_HOME"), d.filePath("d")); // portable: the engine's data folder is the launcher's
#endif
    }
    void installedProblemsAskForTheBuildFirst() {
        QTemporaryDir d;
        const auto l = DataLayout::FromRoots(d.filePath("d/kartrecomp"), d.filePath("c/kartrecomp"));
#ifdef Q_OS_WIN
        const QString engine = d.filePath("mk8d-engine.exe");
#else
        const QString engine = d.filePath("mk8d-engine");
#endif
        const auto p = GamePackage::Installed(l, d.filePath("cfg"), engine, "v1");
        QVERIFY(p.problems().contains("engine is missing"));
        Write(engine, "#!/bin/sh\n");
        QFile(engine).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(p.problems().contains("Setup")); // engine present, game not built
        Write(l.moduleDir() + "/" + BuildRunner::ModuleFileName("image"), "x");
        Write(l.moduleDir() + "/module.json", R"({ "engineVersion": "v1" })");
        QCOMPARE(p.problems(), QString());
    }
    void theDeveloperLayoutIsUnchanged() {
        NEEDS_SH();
        const GamePackage p("/g/game", "/g/home");
        QVERIFY(!p.isInstalled());
        QCOMPARE(p.executable(), QString("/g/game/mk8d"));
        QCOMPARE(p.configFile(), QString("/g/game/user/config/sdl2-config.ini"));
        QCOMPARE(p.overlayFile(), QString("/g/game/user/config/kartrecomp_overlay.ini"));
        QCOMPARE(p.modsRoot(), QString("/g/game"));
        QCOMPARE(p.pluginsParent(), QString("/g/game"));
        QCOMPARE(p.workingDirectory(), QString("/g/game"));
        QCOMPARE(p.launchArguments(false).mid(0, 2), QStringList({"--content-base", "content/base.nsp"}));
    }
};
QTEST_APPLESS_MAIN(TestGamePackage)
#include "test_game_package.moc"
