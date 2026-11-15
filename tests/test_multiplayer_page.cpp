// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's Multiplayer tab turns its settings into the engine's room switches (KARTRECOMP_ROOM*, suyu_cmd/kart_multiplayer.h).
#include <QtTest>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "multiplayer_page.h"

class TestMultiplayerPage : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    static void Set(int mode, const QString& nick, const QString& addr, int port, const QString& pass, int members = 8) {
        QSettings s;
        s.setValue(QStringLiteral("multiplayer/mode"), mode);
        s.setValue(QStringLiteral("multiplayer/nickname"), nick);
        s.setValue(QStringLiteral("multiplayer/address"), addr);
        s.setValue(QStringLiteral("multiplayer/port"), port);
        s.setValue(QStringLiteral("multiplayer/password"), pass);
        s.setValue(QStringLiteral("multiplayer/members"), members);
    }

private slots:
    void initTestCase() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName(QStringLiteral("kartrecomp-test"));
        QCoreApplication::setApplicationName(QStringLiteral("multiplayer"));
    }
    void off() {
        Set(0, QStringLiteral("Racer One"), {}, 24872, {});
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QVERIFY(env.isEmpty());
        QVERIFY(page.problem().isEmpty());
    }
    void join() {
        Set(1, QStringLiteral("Racer One"), QStringLiteral(" rooms.example.org "), 5000, QStringLiteral("pw"));
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM")), QStringLiteral("Racer One:pw@rooms.example.org:5000"));
        QVERIFY(!env.contains(QStringLiteral("KARTRECOMP_ROOM_HOST")));
        if (const QByteArray out = qgetenv("MULTIPLAYER_TAB_PNG"); !out.isEmpty()) {
            page.resize(760, 520);
            page.grab().save(QString::fromLocal8Bit(out));
        }
    }
    void host() {
        Set(2, QStringLiteral("Host_1"), {}, 24872, {}, 4);
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM_HOST")), QStringLiteral("24872"));
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM_NICK")), QStringLiteral("Host_1"));
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM_MAX")), QStringLiteral("4"));
    }
    void invalid() {
        Set(1, QStringLiteral("ab"), QStringLiteral("x"), 24872, {}); // nickname too short
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QVERIFY(env.isEmpty());
        QVERIFY(!page.problem().isEmpty());
        Set(1, QStringLiteral("Racer One"), {}, 24872, {}); // no address
        MultiplayerPage page2;
        QVERIFY(!page2.problem().isEmpty());
    }
};

QTEST_MAIN(TestMultiplayerPage)
#include "test_multiplayer_page.moc"
