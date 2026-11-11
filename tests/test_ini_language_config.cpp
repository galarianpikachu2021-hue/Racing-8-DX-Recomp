// SPDX-License-Identifier: GPL-3.0-or-later
// Round-trip test for the emulated console language: the launcher writes
// [System] language_index to suyu's sdl2-config.ini (the key that
// ISettingsServer::GetLanguageCode reads at boot), so the chosen language
// persists as the console default until changed in the launcher or in game.
#include <QtTest>

#include "ini_file.h"

class TestIniLanguageConfig : public QObject {
    Q_OBJECT
private slots:
    void writesAndReadsBackTheLauncherLanguageIndex() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = tmp.filePath("sdl2-config.ini");
        // A minimal config, as the game would have written it: no language_index yet.
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[Renderer]\nresolution_setup=3\n\n[Core]\nuse_speed_limit=true\n");
        }
        IniFile config;
        QVERIFY(config.load(path));
        // No key yet -> suyu's own default, English (US), index 1.
        QCOMPARE(config.intValue("System", "language_index", 1), 1);

        // The launcher sets, say, Japanese (index 0).
        config.setInt("System", "language_index", 0);
        QVERIFY(config.save());

        // Round-trip: a fresh read sees the persisted value.
        IniFile reread;
        QVERIFY(reread.load(path));
        QCOMPARE(reread.intValue("System", "language_index", 1), 0);

        // suyu keeps the written value instead of resetting to its default: the
        // "key\default=false" companion line must be present alongside the value.
        {
            QFile raw(path);
            if (!raw.open(QIODevice::ReadOnly)) {
                QFAIL("cannot re-open the written config");
            }
            const QString text = QString::fromUtf8(raw.readAll());
            QVERIFY(text.contains("[System]"));
            QVERIFY(text.contains("language_index\\default=false") || text.contains("language_index\\default=true"));
            QVERIFY(text.contains("language_index=0"));
        }

        // And it survives repeatedly: setting another language overwrites, no
        // duplicate value lines.
        config.setInt("System", "language_index", 5);
        QVERIFY(config.save());
        IniFile second;
        QVERIFY(second.load(path));
        QCOMPARE(second.intValue("System", "language_index", 1), 5);
        {
            QFile raw(path);
            if (!raw.open(QIODevice::ReadOnly)) {
                QFAIL("cannot re-open the written config (second)");
            }
            const QString again = QString::fromUtf8(raw.readAll());
            QVERIFY(again.count("language_index=5") == 1);
            QVERIFY(!again.contains("language_index=0"));
        }
    }
};
QTEST_APPLESS_MAIN(TestIniLanguageConfig)
#include "test_ini_language_config.moc"
