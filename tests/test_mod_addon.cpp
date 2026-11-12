// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>

#include "menu_merge.h"
#include "mod_addon.h"

namespace {
void Write(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
} // namespace

class TestModAddon : public QObject {
    Q_OBJECT
private slots:
    // The installed layout (AppImage, Windows zip) keeps the config outside the game folder: the disabled-mods list must come from it.
    // Then: the clean-up call made after every change must not delete a menu merge that is still wanted (4.0.1 froze at the flag
    // screen because the merged menus were removed right after they were made).
    void configElsewhereAndMergedMenusSurviveCleanup() {
        QTemporaryDir root, config;
        const QString mods = root.filePath(QStringLiteral("mods/0100152000022000"));
        Write(mods + QStringLiteral("/Vector/romfs/UI/cmn/menu.sarc"), "SARC");
        Write(mods + QStringLiteral("/Off/romfs/UI/cmn/menu.sarc"), "SARC");
        Write(root.filePath(QStringLiteral("module/module.json")), R"({ "engineVersion": "x", "update": "4.0.1" })");
        const QString ini = config.filePath(QStringLiteral("sdl2-config.ini"));
        Write(ini, "[DisabledAddOns]\nsize=1\n1\\title_id=72080821221203968\n1\\disabled\\size=1\n1\\disabled\\1\\d=Off\n");
        ModAddon::SetConfigFile(ini);

        const QStringList enabled = ModAddon::EnabledModDirs(root.path());
        QCOMPARE(enabled.size(), 1);
        QVERIFY(enabled.first().endsWith(QStringLiteral("/Vector")));

        const QString merged = mods + QLatin1Char('/') + QLatin1String(MenuMerge::kMergedModName);
        Write(merged + QStringLiteral("/romfs/UI/cmn/menu.sarc"), "SARC");
        QString error;
        QVERIFY(ModAddon::UpdateMenuMerge(root.path(), {}, {}, &error, nullptr)); // clean-up only
        QVERIFY(QFileInfo::exists(merged + QStringLiteral("/romfs/UI/cmn/menu.sarc")));

        // No mod replaces menus any more: the merged copy is stale and goes.
        Write(ini, "[DisabledAddOns]\nsize=1\n1\\title_id=72080821221203968\n1\\disabled\\size=2\n1\\disabled\\1\\d=Off\n1\\disabled\\2\\d=Vector\n");
        QVERIFY(ModAddon::UpdateMenuMerge(root.path(), {}, {}, &error, nullptr));
        QVERIFY(!QFileInfo::exists(merged));
        ModAddon::SetConfigFile({});
    }
};

QTEST_GUILESS_MAIN(TestModAddon)
#include "test_mod_addon.moc"
