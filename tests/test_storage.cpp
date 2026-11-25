// SPDX-License-Identifier: GPL-3.0-or-later
// The Storage tab's choice and move: nothing is lost when a move fails, and every page follows the choice.
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "setup_paths.h"
#include "storage.h"

class TestStorage : public QObject {
    Q_OBJECT
    static void Write(const QString& path, const QByteArray& data) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    static QByteArray Read(const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("(missing)");
    }
    /// Another file system than the temporary folder's, so a move really copies ("" = none here).
    static QString OtherDrive() { return qEnvironmentVariable("KARTRECOMP_TEST_OTHER_DRIVE"); }
    static void Fill(const DataLayout& l) {
        Write(l.base(), "base");
        Write(l.dataRoot + "/saves/.hidden", "save");
        Write(l.moduleDir() + "/recompiled_image.so", QByteArray(9 << 20, 'm'));
        QDir().mkpath(l.dataRoot + "/empty");
        Write(l.cacheRoot + "/build-work/a.o", "obj");
    }
    static void Check(const DataLayout& l) {
        QCOMPARE(Read(l.base()), QByteArray("base"));
        QCOMPARE(Read(l.dataRoot + "/saves/.hidden"), QByteArray("save"));
        QCOMPARE(Read(l.moduleDir() + "/recompiled_image.so"), QByteArray(9 << 20, 'm'));
        QVERIFY(QDir(l.dataRoot + "/empty").exists());
        QCOMPARE(Read(l.cacheRoot + "/build-work/a.o"), QByteArray("obj"));
    }
    QTemporaryDir home;

private slots:
    void initTestCase() {
        qputenv("XDG_CONFIG_HOME", home.filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", home.filePath("data").toUtf8());
        qputenv("XDG_CACHE_HOME", home.filePath("cache").toUtf8());
    }
    void choice() {
        QCOMPARE(Storage::Chosen(), QString());
        QCOMPARE(DataLayout::Default().dataRoot, home.filePath("data/kartrecomp"));
        const QString card = home.filePath("card/KartRecomp");
        QCOMPARE(Storage::SetChosen(card), QString());
        QVERIFY(Storage::Missing()); // the card is out
        QDir().mkpath(card);
        QVERIFY(!Storage::Missing());
        QCOMPARE(DataLayout::Default().dataRoot, card + "/data/kartrecomp");
        QCOMPARE(DataLayout::Default().cacheRoot, card + "/cache/kartrecomp");
        QCOMPARE(DataLayout::Standard().dataRoot, home.filePath("data/kartrecomp"));
        QCOMPARE(Storage::SetChosen({}), QString());
        QCOMPARE(DataLayout::Default().dataRoot, home.filePath("data/kartrecomp"));
    }
    void check() {
        QCOMPARE(Storage::CheckFolder(home.filePath("new/KartRecomp")), QString());
        QVERIFY(QDir(home.filePath("new/KartRecomp")).isEmpty()); // no probe file left
#ifdef Q_OS_LINUX
        QVERIFY(!Storage::CheckFolder("/proc/KartRecomp").isEmpty());
#endif
    }
    void move_data() {
        QTest::addColumn<QString>("target");
        QTest::newRow("same drive") << home.filePath("moved");
        if (!OtherDrive().isEmpty()) QTest::newRow("other drive") << OtherDrive() + "/kartrecomp-test";
    }
    void move() {
        QFETCH(QString, target);
        QTemporaryDir d;
        const auto from = DataLayout::FromRoots(d.filePath("d/kartrecomp"), d.filePath("c/kartrecomp"));
        const auto to = Storage::LayoutFor(target);
        Fill(from);
        Write(d.filePath("outside"), "outside");
#ifndef Q_OS_WIN
        QVERIFY(QFile::link(d.filePath("outside"), from.dataRoot + "/out"));        // stays a link to the same file
        QVERIFY(QFile::link(from.dataRoot + "/content", from.dataRoot + "/inside")); // follows the move when copied
#endif
        // Refused while saving the choice fails, and cancelled: everything is as before.
        QString left;
        QCOMPARE(Storage::Move(from, to, [] { return QString("no"); }, {}, nullptr, &left), QString("no"));
        Check(from);
        QVERIFY(!QFileInfo::exists(to.dataRoot));
        QVERIFY(!QFileInfo::exists(to.cacheRoot));
        std::atomic<bool> cancel{true};
        Storage::Move(from, to, [] { return QString("cancelled too late"); }, {}, &cancel, &left);
        Check(from);
        QVERIFY(!QFileInfo::exists(to.dataRoot));

        QCOMPARE(Storage::Move(from, to, [] { return QString(); }, {}, nullptr, &left), QString());
        QCOMPARE(left, QString());
        Check(to);
        QVERIFY(!QFileInfo::exists(from.dataRoot));
        QVERIFY(!QFileInfo::exists(from.cacheRoot));
        QCOMPARE(Read(d.filePath("outside")), QByteArray("outside")); // never followed
#ifndef Q_OS_WIN
        QCOMPARE(QFileInfo(to.dataRoot + "/out").symLinkTarget(), d.filePath("outside"));
        if (target == OtherDrive() + "/kartrecomp-test") QCOMPARE(QFileInfo(to.dataRoot + "/inside").symLinkTarget(), to.content());
#endif
        // A place that holds files is never written over.
        Fill(from);
        QVERIFY(!Storage::Move(from, to, [] { return QString(); }, {}, nullptr, &left).isEmpty());
        Check(from);
        Check(to);
        QDir(target).removeRecursively();
    }
    void overlap() {
        QTemporaryDir d;
        const auto from = DataLayout::FromRoots(d.filePath("d/kartrecomp"), d.filePath("c/kartrecomp"));
        Fill(from);
        QString left;
        QVERIFY(!Storage::Move(from, Storage::LayoutFor(from.dataRoot + "/x"), [] { return QString(); }, {}, nullptr, &left).isEmpty());
        Check(from);
    }
    void repoint() {
        QTemporaryDir d;
        const auto from = DataLayout::FromRoots("/a/kartrecomp", "/c/kartrecomp"), to = DataLayout::FromRoots("/b/data/kartrecomp", "/b/cache/kartrecomp");
        const QString ini = d.filePath("sdl2-config.ini");
        Write(ini, "[Data%20Storage]\nnand_directory\\default=false\nnand_directory=/a/kartrecomp/nand\nload_directory=/a/kartrecomp2/mods\nsave_directory=\n");
        QVERIFY(Storage::Repoint(ini, from, to));
        const QByteArray text = Read(ini);
        QVERIFY(text.contains("nand_directory=/b/data/kartrecomp/nand\n"));
        QVERIFY(text.contains("load_directory=/a/kartrecomp2/mods\n")); // another folder whose name only starts the same
        QVERIFY(Storage::Repoint(d.filePath("none.ini"), from, to));
    }
};
QTEST_GUILESS_MAIN(TestStorage)
#include "test_storage.moc"
