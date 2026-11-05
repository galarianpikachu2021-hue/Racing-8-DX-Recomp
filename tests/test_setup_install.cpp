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

class TestSetupInstall : public QObject {
    Q_OBJECT
private slots:
    void layoutFollowsTheXdgFolders() {
        const auto l = DataLayout::FromRoots("/data/kartrecomp", "/cache/kartrecomp");
        QCOMPARE(l.base(), QString("/data/kartrecomp/content/base.nsp"));
        QCOMPARE(l.update(), QString("/data/kartrecomp/content/update-3.0.4.nsp"));
        QCOMPARE(l.keysFile(), QString("/data/kartrecomp/keys/prod.keys"));
        QCOMPARE(l.firmwareDir(), QString("/data/kartrecomp/nand/system/Contents/registered"));
        QCOMPARE(l.moduleDir(), QString("/data/kartrecomp/module"));
        QCOMPARE(l.exportDir(), QString("/cache/kartrecomp/export"));
        QCOMPARE(l.workDir(), QString("/cache/kartrecomp/build-work"));
    }
    void installedInputsAreWhatIsActuallyOnDisk() {
        QTemporaryDir root;
        const auto l = DataLayout::FromRoots(root.filePath("data/kartrecomp"), root.filePath("cache/kartrecomp"));
        QVERIFY(l.installedKeys().isEmpty());
        QVERIFY(l.installedFirmware().isEmpty());
        QVERIFY(l.installedDlc().isEmpty());
        Write(l.keysFile(), "header_key = 00\n");
        Write(l.firmwareDir() + "/0001.nca", "FW");
        Write(l.dlcDir() + "/b.nsp", "B");
        Write(l.dlcDir() + "/a.nsp", "A");
        Write(l.dlcDir() + "/notes.txt", "x");
        QCOMPARE(l.installedKeys(), l.keysFile());
        QCOMPARE(l.installedFirmware(), l.firmwareDir());
        QCOMPARE(l.installedDlc(), (QStringList{l.dlcDir() + "/a.nsp", l.dlcDir() + "/b.nsp"}));
    }
    void installingFromTheInstalledFirmwareFolderKeepsTheFirmware() {
        QTemporaryDir root;
        const auto l = DataLayout::FromRoots(root.filePath("data/kartrecomp"), root.filePath("cache/kartrecomp"));
        Write(l.base(), "BASE");
        Write(l.update(), "UPD");
        Write(l.keysFile(), "header_key = 00\n");
        Write(l.firmwareDir() + "/0001.nca", "FW1");
        Write(l.firmwareDir() + "/0002.nca", "FW2");
        SetupInputs in;
        in.base = l.base();
        in.update = l.update();
        in.keys = l.keysFile();
        in.firmware = l.firmwareDir();
        QCOMPARE(InstallInputs(in, l), QString());
        QVERIFY(QFileInfo::exists(l.firmwareDir() + "/0001.nca"));
        QVERIFY(QFileInfo::exists(l.firmwareDir() + "/0002.nca"));
        QVERIFY(QFileInfo::exists(l.keysFile()));
        QVERIFY(QFileInfo::exists(l.base()));
    }
    void installPlacesEveryInputWhereTheEngineLooksForIt() {
        QTemporaryDir src, dst;
        // a folder name with spaces and non-ASCII letters, like a user's Downloads folder
        const QString dl = src.filePath(QString::fromUtf8("Mein Spiel äö"));
        Write(dl + "/base.nsp", "BASE");
        Write(dl + "/upd.nsp", "UPD");
        Write(dl + "/dlc1.nsp", "D1");
        Write(dl + "/prod.keys", "header_key = 00\n");
        Write(dl + "/title.keys", "00 = 00\n");
        Write(dl + "/fw/0001.nca", "FW1");
        Write(dl + "/fw/0002.cnmt.nca", "FW2");
        SetupInputs in{dl + "/base.nsp", dl + "/upd.nsp", dl + "/prod.keys", dl + "/fw", {dl + "/dlc1.nsp"}};
        const auto l = DataLayout::FromRoots(dst.filePath("data"), dst.filePath("cache"));
        QCOMPARE(InstallInputs(in, l), QString());
        QCOMPARE(QFile(l.base()).size(), qint64(4));
        QCOMPARE(QFile(l.update()).size(), qint64(3));
        QVERIFY(QFile::exists(l.dlcDir() + "/dlc1.nsp"));
        QVERIFY(QFile::exists(l.keysFile()));
        QVERIFY(QFile::exists(QFileInfo(l.keysFile()).absolutePath() + "/title.keys"));
        QVERIFY(QFile::exists(l.firmwareDir() + "/0001.nca") && QFile::exists(l.firmwareDir() + "/0002.cnmt.nca"));
    }
    void installingAgainReplacesWhatWasThere() {
        QTemporaryDir src, dst;
        Write(src.filePath("base.nsp"), "ONE");
        Write(src.filePath("upd.nsp"), "U");
        Write(src.filePath("prod.keys"), "header_key = 00\n");
        Write(src.filePath("fw/a.nca"), "F");
        SetupInputs in{src.filePath("base.nsp"), src.filePath("upd.nsp"), src.filePath("prod.keys"), src.filePath("fw"), {}};
        const auto l = DataLayout::FromRoots(dst.filePath("data"), dst.filePath("cache"));
        QCOMPARE(InstallInputs(in, l), QString());
        Write(src.filePath("base.nsp"), "TWO22");
        QCOMPARE(InstallInputs(in, l), QString());
        QCOMPARE(QFile(l.base()).size(), qint64(5));
    }
    void nspsAreMovedIntoTheDataFolderNotLinked() {
        QTemporaryDir src, dst;
        Write(src.filePath("base.nsp"), "BASE");
        Write(src.filePath("upd.nsp"), "UPD");
        Write(src.filePath("d.nsp"), "D");
        Write(src.filePath("prod.keys"), "header_key = 00\n");
        Write(src.filePath("fw/a.nca"), "F");
        SetupInputs in{src.filePath("base.nsp"), src.filePath("upd.nsp"), src.filePath("prod.keys"), src.filePath("fw"), {src.filePath("d.nsp")}};
        const auto l = DataLayout::FromRoots(dst.filePath("data"), dst.filePath("cache"));
        QCOMPARE(InstallInputs(in, l), QString());
        QVERIFY(!QFileInfo(l.base()).isSymLink());
        QVERIFY(!QFile::exists(src.filePath("base.nsp")) && !QFile::exists(src.filePath("upd.nsp")) && !QFile::exists(src.filePath("d.nsp")));
        QVERIFY(QFile::exists(src.filePath("prod.keys"))); // keys are copied, the user keeps theirs
        // a second build with the same inputs pointing at the files already in place must not delete them
        SetupInputs again{l.base(), l.update(), src.filePath("prod.keys"), src.filePath("fw"), {l.dlcDir() + "/d.nsp"}};
        QCOMPARE(InstallInputs(again, l), QString());
        QCOMPARE(QFile(l.base()).size(), qint64(4));
        QVERIFY(QFile::exists(l.dlcDir() + "/d.nsp"));
    }
    void aMissingSourceFileIsReportedNotIgnored() {
        QTemporaryDir src, dst;
        SetupInputs in{src.filePath("none.nsp"), src.filePath("none2.nsp"), src.filePath("none.keys"), src.filePath("fw"), {}};
        const auto l = DataLayout::FromRoots(dst.filePath("data"), dst.filePath("cache"));
        QVERIFY(!InstallInputs(in, l).isEmpty());
    }
    void detectionPrefersTheEnvironmentOverrides() {
        qputenv("KARTRECOMP_EXPORTER", "/x/exporter");
        qputenv("KARTRECOMP_ENGINE", "/x/engine");
        qputenv("KARTRECOMP_TOOLCHAIN", "/x/toolchain");
        const auto p = BuilderPaths::Detect();
        QCOMPARE(p.exporter, QString("/x/exporter"));
        QCOMPARE(p.engine, QString("/x/engine"));
        QCOMPARE(p.toolchain, QString("/x/toolchain"));
        QString missing;
        QVERIFY(!p.complete(&missing));
        QVERIFY(missing.contains("exporter"));
        qunsetenv("KARTRECOMP_EXPORTER");
        qunsetenv("KARTRECOMP_ENGINE");
        qunsetenv("KARTRECOMP_TOOLCHAIN");
    }
    void builtStateNeedsAModuleJsonForThisEngine() {
        QTemporaryDir d;
        const auto l = DataLayout::FromRoots(d.filePath("data"), d.filePath("cache"));
        QVERIFY(!IsBuilt(l, "eng-1"));
        Write(l.moduleDir() + "/" + BuildRunner::ModuleFileName("image"), "x");
        QVERIFY(!IsBuilt(l, "eng-1")); // modules but no stamp: not verified
        Write(l.moduleDir() + "/module.json", R"({ "engineVersion": "eng-0" })");
        QVERIFY(!IsBuilt(l, "eng-1")); // built for another engine
        Write(l.moduleDir() + "/module.json", R"({ "engineVersion": "eng-1" })");
        QVERIFY(IsBuilt(l, "eng-1"));
    }
};
QTEST_APPLESS_MAIN(TestSetupInstall)
#include "test_setup_install.moc"
