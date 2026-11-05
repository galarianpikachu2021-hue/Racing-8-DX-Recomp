// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "build_runner.h"
#include "setup_paths.h"

namespace {
void Write(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
}
} // namespace

class TestPlatformPaths : public QObject {
    Q_OBJECT
private slots:
    void availableMemoryIsReported() { QVERIFY(BuildRunner::AvailableMemoryBytes() > (64LL << 20)); }
    void defaultLayoutHonoursTheEnvironmentOverrides() {
        qputenv("XDG_DATA_HOME", "/xdg/data");
        qputenv("XDG_CACHE_HOME", "/xdg/cache");
        const auto l = DataLayout::Default();
        QCOMPARE(l.dataRoot, QString("/xdg/data/kartrecomp"));
        QCOMPARE(l.cacheRoot, QString("/xdg/cache/kartrecomp"));
        qunsetenv("XDG_DATA_HOME");
        qunsetenv("XDG_CACHE_HOME");
    }
    void withoutOverridesWindowsKeepsEverythingBesideTheLauncher() {
#ifdef Q_OS_WIN
        qunsetenv("XDG_DATA_HOME");
        qunsetenv("XDG_CACHE_HOME");
        qunsetenv("XDG_CONFIG_HOME");
        const QString base = QCoreApplication::applicationDirPath() + "/userdata";
        QCOMPARE(DataLayout::Default().dataRoot, QDir::cleanPath(base + "/data/kartrecomp"));
        QCOMPARE(DataLayout::Default().cacheRoot, QDir::cleanPath(base + "/cache/kartrecomp"));
        QCOMPARE(DataLayout::ConfigRoot(), QDir::cleanPath(base + "/config/kartrecomp"));
#else
        QSKIP("Windows layout only");
#endif
    }
    void gameFilesWithSpacesAndAccentsAreMovedIn() {
        QTemporaryDir src, dst;
        const QString dl = src.filePath(QString::fromUtf8("Mein Spiel äö ü"));
        Write(dl + "/base.nsp", "BASE");
        Write(dl + "/upd.nsp", "UPD");
        Write(dl + "/prod.keys", "header_key = 00\n");
        Write(dl + "/fw/0001.nca", "FW");
        SetupInputs in{dl + "/base.nsp", dl + "/upd.nsp", dl + "/prod.keys", dl + "/fw", {}};
        const auto l = DataLayout::FromRoots(dst.filePath("Daten ä/kartrecomp"), dst.filePath("cache/kartrecomp"));
        QCOMPARE(InstallInputs(in, l), QString());
        QCOMPARE(QFile(l.base()).size(), qint64(4));
        QVERIFY(!QFile::exists(dl + "/base.nsp"));
    }
    void aChildDiesWhenItsParentsJobEnds() {
        // Starts a long sleeper through DieWithParent and kills it through the same path the launcher uses on cancel.
        QProcess p;
        BuildRunner::DieWithParent(p);
#ifdef Q_OS_WIN
        p.start("ping", {"-n", "30", "127.0.0.1"});
#else
        p.start("sleep", {"30"});
#endif
        QVERIFY(p.waitForStarted());
        p.kill();
        QVERIFY(p.waitForFinished(5000));
    }
};
QTEST_MAIN(TestPlatformPaths)
#include "test_platform_paths.moc"
