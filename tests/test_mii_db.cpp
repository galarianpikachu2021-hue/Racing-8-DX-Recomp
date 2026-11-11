// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "mii_db.h"

class TestMiiDb : public QObject {
    Q_OBJECT
private slots:
    // The engine's own empty database: magic, version 1, count 0, CRC 0x60b7 stored as bytes 60 b7.
    void emptyDatabaseMatchesTheEngine() {
        Mii::Database db;
        const QByteArray bytes = db.ToBytes();
        QCOMPARE(bytes.size(), Mii::kDatabaseSize);
        QCOMPARE(bytes.left(4), QByteArray("NFDB"));
        QCOMPARE(bytes.right(4), QByteArray::fromHex("010060b7"));
        QVERIFY(Mii::Database::FromBytes(bytes).has_value());
    }
    void defaultMiiIsValid() {
        for (bool female : {false, true}) {
            const auto d = Mii::StoreData::MakeDefault(QStringLiteral("Racer"), female);
            QString why;
            QVERIFY2(d.IsValid(&why), qPrintable(why));
            QCOMPARE(d.Name(), QStringLiteral("Racer"));
            QCOMPARE(d.Get("gender"), female ? 1 : 0);
            QCOMPARE(d.Get("hair_type"), female ? 12 : 33);
            QCOMPARE(d.Get("eyebrow_y"), 7);
            QCOMPARE(d.Get("type"), 0);
        }
    }
    void fieldsRoundTripAndClamp() {
        auto d = Mii::StoreData::MakeDefault(QStringLiteral("A"));
        for (const auto& f : Mii::Fields()) {
            d.Set(f, f.max);
            QCOMPARE(d.Get(f), f.max);
        }
        // Setting each field to its max must not disturb any other field.
        auto e = Mii::StoreData::MakeDefault(QStringLiteral("A"));
        for (const auto& f : Mii::Fields()) {
            auto g = e;
            g.Set(f, f.max);
            for (const auto& o : Mii::Fields())
                if (&o != &f) QCOMPARE(g.Get(o), e.Get(o));
        }
        d.Set("hair_type", 500);
        QCOMPARE(d.Get("hair_type"), 131);
        d.Set("height", -4);
        QCOMPARE(d.Get("height"), 0);
    }
    void editedMiiNeedsNewChecksums() {
        auto d = Mii::StoreData::MakeDefault(QStringLiteral("B"));
        d.Set("hair_color", 5);
        QVERIFY(!d.IsValid());
        d.UpdateChecksums();
        QVERIFY(d.IsValid());
    }
    void nameRules() {
        auto d = Mii::StoreData::MakeDefault(QStringLiteral("ABCDEFGHIJKLMN"));
        QCOMPARE(d.Name(), QStringLiteral("ABCDEFGHIJ"));
        d.SetName(QStringLiteral("  "));
        QCOMPARE(d.Name(), QStringLiteral("Mii"));
    }
    void databaseRoundTrip() {
        Mii::Database db;
        QVERIFY(db.Add(Mii::StoreData::MakeDefault(QStringLiteral("One"))));
        QVERIFY(db.Add(Mii::StoreData::MakeDefault(QStringLiteral("Two"), true)));
        const auto back = Mii::Database::FromBytes(db.ToBytes());
        QVERIFY(back.has_value());
        QCOMPARE(back->Count(), 2);
        QCOMPARE(back->At(1).Name(), QStringLiteral("Two"));
        QVERIFY(back->At(0).IsValid());
        QVERIFY(back->At(0).CreateId() != back->At(1).CreateId());
        QByteArray broken = db.ToBytes();
        broken[100] = static_cast<char>(broken[100] ^ 1);
        QString err;
        QVERIFY(!Mii::Database::FromBytes(broken, &err).has_value());
        QVERIFY(err.contains(QStringLiteral("checksum")));
    }
    void fullDatabase() {
        Mii::Database db;
        int added = 0;
        while (db.Add(Mii::StoreData::MakeDefault(QStringLiteral("M")))) added++;
        QCOMPARE(added, 99);
        QVERIFY(Mii::Database::FromBytes(db.ToBytes()).has_value());
    }
};

QTEST_GUILESS_MAIN(TestMiiDb)
#include "test_mii_db.moc"
