// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "unit_split.h"

namespace {
// A miniature generated unit: header with the guard array and one initialised global, ten block functions, the lookup table.
QByteArray FixtureUnit(int blocks = 10) {
    QByteArray t = "/* auto-generated */\n#include \"recomp_runtime.h\"\nextern int g_recomp_guard_host_v2;\n"
                   "static uint32_t recomp_gg_seen[8];\nint g_unit_flag=2;\n";
    for (int i = 0; i < blocks; ++i) {
        const QByteArray name = QByteArray("blk_main_") + QByteArray::number(i * 4, 16).rightJustified(16, '0');
        t += "void " + name + "(GuestContext* c){\n    RECOMP_GG_GUARD(_expected," + QByteArray::number(i) + "U);\n}\n";
    }
    t += "const struct _recomp_ent _seg_main_0[] = {\n  {0x0ULL, 0x0ULL, blk_main_0000000000000000},\n};\n";
    return t;
}
QString Write(const QTemporaryDir& d, const QString& name, const QByteArray& bytes) {
    const QString p = d.filePath(name);
    QFile f(p);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    return p;
}
QByteArray Read(const QString& p) { QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
QByteArray BlockName(int i) { return QByteArray("blk_main_") + QByteArray::number(i * 4, 16).rightJustified(16, '0'); }
} // namespace

class TestUnitSplit : public QObject {
    Q_OBJECT
private slots:
    void splitsColdBlocksEvenlyAndKeepsHotSeparate() {
        QTemporaryDir dir;
        const QString unit = Write(dir, "recompiled_main_0.c", FixtureUnit());
        const QSet<QByteArray> hot{BlockName(3), BlockName(7)};
        QString error;
        const auto pieces = UnitSplit::Split(unit, dir.filePath("out"), hot, 4, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(pieces.size(), 5);
        QCOMPARE(pieces[0].name, QString("recompiled_main_0_p0.c"));
        QCOMPARE(pieces[4].name, QString("recompiled_main_0_hot.c"));
        QVERIFY(pieces[4].hot);
        QVERIFY(!pieces[0].hot);
        const QByteArray p0 = Read(dir.filePath("out/recompiled_main_0_p0.c"));
        const QByteArray p1 = Read(dir.filePath("out/recompiled_main_0_p1.c"));
        const QByteArray hotText = Read(dir.filePath("out/recompiled_main_0_hot.c"));
        // 8 cold blocks over 4 pieces: two each, in order; the hot blocks only in the hot piece
        QVERIFY(p0.contains("void " + BlockName(0) + "(") && p0.contains("void " + BlockName(1) + "("));
        QVERIFY(p1.contains("void " + BlockName(2) + "(") && !p1.contains("void " + BlockName(3) + "("));
        QVERIFY(hotText.contains("void " + BlockName(3) + "(") && hotText.contains("void " + BlockName(7) + "("));
        // guard array: defined once in piece 0, extern elsewhere, same index; other initialised globals defined once
        QVERIFY(p0.contains("uint32_t recomp_gg_seen_recompiled_main_0[8];") && !p0.contains("extern uint32_t recomp_gg_seen"));
        QVERIFY(p1.contains("extern uint32_t recomp_gg_seen_recompiled_main_0[8];"));
        QVERIFY(p0.contains("int g_unit_flag=2;") && p1.contains("extern int g_unit_flag;"));
        // the last piece declares every block and carries the lookup table; earlier ones do not
        QVERIFY(hotText.contains("void " + BlockName(9) + "(GuestContext*);"));
        QVERIFY(hotText.contains("const struct _recomp_ent _seg_main_0[]"));
        QVERIFY(!p0.contains("_seg_main_0"));
    }
    void withoutHotListEverythingGoesToTheColdPieces() {
        QTemporaryDir dir;
        const QString unit = Write(dir, "recompiled_main_0.c", FixtureUnit());
        QString error;
        const auto pieces = UnitSplit::Split(unit, dir.filePath("out"), {}, 4, &error);
        QCOMPARE(pieces.size(), 4);
        for (const auto& p : pieces) QVERIFY(!p.hot);
    }
    void rejectsAUnitWithoutTheGuardArray() {
        QTemporaryDir dir;
        QByteArray bad = FixtureUnit();
        bad.replace("static uint32_t recomp_gg_seen[8];", "");
        const QString unit = Write(dir, "recompiled_main_0.c", bad);
        QString error;
        const auto pieces = UnitSplit::Split(unit, dir.filePath("out"), {}, 4, &error);
        QVERIFY(pieces.isEmpty());
        QVERIFY(error.contains("recomp_gg_seen"));
    }
    void matchesThePythonSplitterOnARealUnit() {
        // KARTRECOMP_REAL_UNIT=<recompiled_<mod>_<n>.c> KARTRECOMP_HOT_LIST=<hot_blocks.txt>, repo root as the working directory
        const QString unit = qEnvironmentVariable("KARTRECOMP_REAL_UNIT");
        if (unit.isEmpty()) QSKIP("no real unit given");
        const QString hotFile = qEnvironmentVariable("KARTRECOMP_HOT_LIST");
        QFile hf(hotFile);
        QVERIFY(hf.open(QIODevice::ReadOnly));
        const auto hot = UnitSplit::ParseHotList(hf.readAll());
        QTemporaryDir cpp, py;
        QString error;
        const auto pieces = UnitSplit::Split(unit, cpp.path(), hot, 4, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QProcess p;
        p.start("python3", {"tools/recomp/split_units.py", unit, py.path(), "4", hotFile});
        QVERIFY(p.waitForFinished(-1) && p.exitCode() == 0);
        for (const auto& piece : pieces) QVERIFY2(Read(cpp.filePath(piece.name)) == Read(py.filePath(piece.name)), qPrintable(piece.name));
        QVERIFY(!pieces.isEmpty());
    }
    void parsesAHotListOfNamesSeparatedByWhitespace() {
        const auto hot = UnitSplit::ParseHotList("blk_main_0000000000000004\nblk_sdk_0000000000000008  blk_main_000000000000000c\n");
        QCOMPARE(hot.size(), 3);
        QVERIFY(hot.contains("blk_sdk_0000000000000008"));
    }
};
QTEST_APPLESS_MAIN(TestUnitSplit)
#include "test_unit_split.moc"
