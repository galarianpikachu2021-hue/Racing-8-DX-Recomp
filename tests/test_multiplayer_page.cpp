// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's Online play tab turns its settings into the engine's room switches (KARTRECOMP_ROOM*, suyu_cmd/kart_multiplayer.h).
#include <QtTest>
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "multiplayer_page.h"

class TestMultiplayerPage : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    enum { Host = 0, Join = 1 };
    static void Set(bool enabled, int role, const QString& nick, const QString& addr, int port, const QString& pass, int members = 8,
                    const QString& name = {}) {
        QSettings s;
        s.clear();
        s.setValue(QStringLiteral("multiplayer/enabled"), enabled);
        s.setValue(QStringLiteral("multiplayer/role"), role);
        s.setValue(QStringLiteral("multiplayer/nickname"), nick);
        s.setValue(QStringLiteral("multiplayer/address"), addr);
        s.setValue(QStringLiteral("multiplayer/port"), port);
        s.setValue(QStringLiteral("multiplayer/password"), pass);
        s.setValue(QStringLiteral("multiplayer/members"), members);
        s.setValue(QStringLiteral("multiplayer/room_name"), name);
    }

private slots:
    void initTestCase() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName(QStringLiteral("kartrecomp-test"));
        QCoreApplication::setApplicationName(QStringLiteral("multiplayer"));
    }
    void off() {
        Set(false, Join, QStringLiteral("Racer One"), QStringLiteral("x"), 24872, {});
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QVERIFY(env.isEmpty());
        QVERIFY(page.problem().isEmpty());
    }
    void join() {
        Set(true, Join, QStringLiteral("Racer One"), QStringLiteral(" rooms.example.org "), 5000, QStringLiteral("pw"));
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM")), QStringLiteral("Racer One:pw@rooms.example.org:5000"));
        QVERIFY(!env.contains(QStringLiteral("KARTRECOMP_ROOM_HOST")));
        if (const QByteArray out = qgetenv("MULTIPLAYER_TAB_PNG"); !out.isEmpty()) {
            page.resize(760, 620);
            page.grab().save(QString::fromLocal8Bit(out));
        }
    }
    void host() {
        Set(true, Host, QStringLiteral("Host_1"), {}, 24872, {}, 4, QStringLiteral("Friday cup"));
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM_HOST")), QStringLiteral("24872"));
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM_NICK")), QStringLiteral("Host_1"));
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM_MAX")), QStringLiteral("4"));
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM_NAME")), QStringLiteral("Friday cup"));
        if (const QByteArray out = qgetenv("ONLINE_TAB_HOST_PNG"); !out.isEmpty()) {
            page.resize(760, 620);
            page.grab().save(QString::fromLocal8Bit(out));
        }
    }
    void oldSettings() {
        // The first launcher with this tab stored only "mode" (0 off, 1 join, 2 host).
        QSettings s;
        s.clear();
        s.setValue(QStringLiteral("multiplayer/mode"), 1);
        s.setValue(QStringLiteral("multiplayer/nickname"), QStringLiteral("Racer One"));
        s.setValue(QStringLiteral("multiplayer/address"), QStringLiteral("10.0.0.2"));
        s.setValue(QStringLiteral("multiplayer/port"), 24872);
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_ROOM")), QStringLiteral("Racer One@10.0.0.2:24872"));
    }
    void profileName() {
        // The profile name is passed whether or not online play is on; the room nickname falls back to it.
        Set(false, Join, {}, QStringLiteral("x"), 24872, {});
        { QSettings st; st.setValue(QStringLiteral("profile/name"), QStringLiteral("Carter")); }
        MultiplayerPage off_page;
        QProcessEnvironment env;
        off_page.apply(env);
        QCOMPARE(env.value(QStringLiteral("KARTRECOMP_PROFILE_NAME")), QStringLiteral("Carter"));
        QVERIFY(!env.contains(QStringLiteral("KARTRECOMP_ROOM")));
        Set(true, Join, {}, QStringLiteral("10.0.0.2"), 24872, {});
        { QSettings st; st.setValue(QStringLiteral("profile/name"), QStringLiteral("Carter")); }
        MultiplayerPage on_page;
        QProcessEnvironment env2;
        on_page.apply(env2);
        QCOMPARE(env2.value(QStringLiteral("KARTRECOMP_ROOM")), QStringLiteral("Carter@10.0.0.2:24872"));
    }
    void invalid() {
        Set(true, Join, QStringLiteral("ab"), QStringLiteral("x"), 24872, {}); // nickname too short
        MultiplayerPage page;
        QProcessEnvironment env;
        page.apply(env);
        QVERIFY(env.isEmpty());
        QVERIFY(!page.problem().isEmpty());
        Set(true, Join, QStringLiteral("Racer One"), {}, 24872, {}); // no address
        MultiplayerPage page2;
        QVERIFY(!page2.problem().isEmpty());
        Set(true, Join, QStringLiteral("Racer One"), QStringLiteral("x"), 24872, QStringLiteral("a:b")); // ':' would split the spec
        MultiplayerPage page3;
        QVERIFY(!page3.problem().isEmpty());
    }
};

QTEST_MAIN(TestMultiplayerPage)
#include "test_multiplayer_page.moc"
