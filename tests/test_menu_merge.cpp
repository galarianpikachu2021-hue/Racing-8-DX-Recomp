// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "menu_merge.h"

namespace {
QByteArray Pattern(int n, int seed) {
    QByteArray d(n, '\0');
    for (int i = 0; i < n; ++i) d[i] = static_cast<char>((i / 7 + seed * (i % 13 == 0)) & 0xFF);
    return d;
}
MenuMerge::Archive Make(const QList<QPair<QString, QByteArray>>& files) {
    MenuMerge::Archive a;
    for (const auto& [name, data] : files) {
        MenuMerge::Archive::Member m;
        m.named = true;
        m.name = name;
        m.hash = a.nameHash(name);
        m.data = data;
        m.align = name.endsWith(QStringLiteral(".bntx")) ? 0x1000 : 4;
        a.members << m;
    }
    std::sort(a.members.begin(), a.members.end(), [](const auto& x, const auto& y) { return x.hash < y.hash; });
    return a;
}
} // namespace

class TestMenuMerge : public QObject {
    Q_OBJECT
private slots:
    void yaz0RoundTrip() {
        for (const QByteArray& d : {QByteArray(), QByteArray("a"), Pattern(100000, 3), QByteArray(5000, 'x')}) {
            const QByteArray z = MenuMerge::Yaz0Encode(d);
            QCOMPARE(MenuMerge::Yaz0Decode(z), d);
        }
        QVERIFY(MenuMerge::Yaz0Encode(QByteArray(100000, 'x')).size() < 2000); // it really compresses
    }
    void archiveRoundTripKeepsAlignment() {
        const auto a = Make({{QStringLiteral("a.bflyt"), Pattern(300, 1)}, {QStringLiteral("b.bntx"), Pattern(9000, 2)}});
        const QByteArray w = a.write();
        MenuMerge::Archive b;
        QVERIFY(b.read(w));
        QCOMPARE(b.members.size(), 2);
        const int i = b.find(QStringLiteral("b.bntx"));
        QVERIFY(i >= 0);
        QCOMPARE(b.members[i].data, Pattern(9000, 2));
        QVERIFY(b.members[i].align >= 0x1000);
        MenuMerge::Archive c; // writing what was read keeps every member (alignment can only grow)
        QVERIFY(c.read(b.write()));
        QCOMPARE(c.members.size(), 2);
        for (const auto& m : b.members) QCOMPARE(c.members[c.find(m.name)].data, m.data);
    }
    void mergeTakesOnlyTheModsChanges() {
        // 3.0.4: x, y, icons. The update changed y and added z. The mod (made for 3.0.4) changed x only.
        const QByteArray x0 = Pattern(500, 1), y0 = Pattern(600, 2), y1 = Pattern(600, 9), x1 = Pattern(500, 7);
        const auto v304 = Make({{QStringLiteral("x.bflyt"), x0}, {QStringLiteral("y.bflyt"), y0}});
        auto upd = Make({{QStringLiteral("x.bflyt"), x0}, {QStringLiteral("y.bflyt"), y1}, {QStringLiteral("z.bflyt"), Pattern(50, 4)}});
        const auto mod = Make({{QStringLiteral("x.bflyt"), x1}, {QStringLiteral("y.bflyt"), y0}});
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath(QStringLiteral("vanilla")));
        QDir().mkpath(dir.filePath(QStringLiteral("mod/romfs/UI/cmn")));
        QFile f(dir.filePath(QStringLiteral("vanilla/menu.sarc")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(upd.write());
        f.close();
        QFile g(dir.filePath(QStringLiteral("mod/romfs/UI/cmn/menu.sarc")));
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.write(mod.write());
        g.close();
        MenuMerge::Baseline base;
        for (const auto& m : v304.members)
            base.sha1.insert(QStringLiteral("menu.sarc/") + m.name, QCryptographicHash::hash(m.data, QCryptographicHash::Sha1).toHex());
        QString error;
        QCOMPARE(MenuMerge::Merge({dir.filePath(QStringLiteral("mod"))}, dir.filePath(QStringLiteral("vanilla")), base,
                                  dir.filePath(QStringLiteral("out")), &error), 1);
        QFile r(dir.filePath(QStringLiteral("out/romfs/UI/cmn/menu.sarc")));
        QVERIFY(r.open(QIODevice::ReadOnly));
        MenuMerge::Archive out;
        QVERIFY(out.read(r.readAll()));
        QCOMPARE(out.members.size(), 3);                                       // the update's new member is kept
        QCOMPARE(out.members[out.find(QStringLiteral("x.bflyt"))].data, x1);  // the mod's change
        QCOMPARE(out.members[out.find(QStringLiteral("y.bflyt"))].data, y1);  // the update's change, not the mod's stale copy
    }
};

QTEST_GUILESS_MAIN(TestMenuMerge)
#include "test_menu_merge.moc"
