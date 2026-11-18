// SPDX-License-Identifier: GPL-3.0-or-later
// ReadCtgpTables against the user's own CTGP DX pack. The pack is not in the repository: the test runs when
// KARTRECOMP_TEST_CTGP_NRO names an extracted CTGPDX.nro (romfs/skyline/plugins/), and is skipped otherwise.
// Expected values: the plugin's tables as dumped by the emulator (tools/ctgp/emu_plugin.py export), see docs/CTGP_PORT.md.
#include <QFile>
#include <QtTest>

#include "ctgp_tables.h"

class TestCtgpTables : public QObject {
    Q_OBJECT
private slots:
    void rejectsGarbage() {
        const auto r = ReadCtgpTables(QByteArray(4096, '\0'));
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }
    void readsThePack() {
        const QString path = qEnvironmentVariable("KARTRECOMP_TEST_CTGP_NRO");
        if (path.isEmpty()) QSKIP("KARTRECOMP_TEST_CTGP_NRO not set (the user's CTGP DX pack is needed)");
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto r = ReadCtgpTables(f.readAll());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.courses, 188);
        QCOMPARE(r.cups, 40);
        const QString t = QString::fromUtf8(r.text);
        QVERIFY(t.contains(QStringLiteral("course 124 CT_00 CT_00_00 index=123 base=82 name=500100 platform=500002 slot=0 author=600002 bgm=82 cup=24")));
        QVERIFY(t.contains(QStringLiteral("course 126 CT_02 CT_02_00 index=125 base=20 name=500141 platform=1462 slot=2 author=600004 bgm=92 cup=24")));
        QVERIFY(t.contains(QStringLiteral("cup 0 label=1301 icon=Mushroom courses=28,29,20,18")));
        QVERIFY(t.contains(QStringLiteral("cup 39 label=700015 icon=Coin courses=184,185,186,187")));
        QVERIFY(t.contains(QStringLiteral("rows_a_top 0,1,2,3,8,10,24,26,28,30,32,34,36,38")));
        QVERIFY(t.contains(QStringLiteral("rows_b_bottom 18,19,20,21,22,23")));
    }
};

QTEST_GUILESS_MAIN(TestCtgpTables)
#include "test_ctgp_tables.moc"
