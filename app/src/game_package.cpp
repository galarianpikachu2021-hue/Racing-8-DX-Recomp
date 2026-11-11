// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_package.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "ini_file.h"

GamePackage::GamePackage(QString root, QString home)
    : root_dir(std::move(root)), suyu_home(std::move(home)) {}

GamePackage GamePackage::Installed(const DataLayout& layout, const QString& configRoot, const QString& engine, const QString& engineVersion) {
    GamePackage p(layout.dataRoot, {});
    p.installed = true;
    p.layout = layout;
    p.config_root = configRoot;
    p.engine_path = engine;
    p.engine_version = engineVersion;
    return p;
}

QString GamePackage::overlayFile() const {
    return installed ? QDir(config_root).filePath(QStringLiteral("kartrecomp_overlay.ini"))
                     : QDir(root_dir).filePath(QStringLiteral("user/config/kartrecomp_overlay.ini"));
}

QString GamePackage::modsRoot() const { return root_dir; }

QString GamePackage::miisDir() const {
    return installed ? QDir(root_dir).filePath(QStringLiteral("miis")) : QDir(root_dir).filePath(QStringLiteral("user/miis"));
}
QString GamePackage::savesDir() const {
    return installed ? QDir(root_dir).filePath(QStringLiteral("saves")) : QDir(root_dir).filePath(QStringLiteral("user/saves"));
}
QString GamePackage::nandMiiDatabase() const {
    return QDir(root_dir).filePath(installed ? QStringLiteral("nand/system/save/8000000000000030/MiiDatabase.dat")
                                             : QStringLiteral("user/nand/system/save/8000000000000030/MiiDatabase.dat"));
}

QString GamePackage::pluginsParent() const { return installed ? QFileInfo(engine_path).absolutePath() : root_dir; }

QString GamePackage::workingDirectory() const { return installed ? QFileInfo(engine_path).absolutePath() : root_dir; }

QString GamePackage::executable() const {
    if (installed) return engine_path;
#ifdef Q_OS_WIN
    return QDir(root_dir).filePath(QStringLiteral("mk8d.exe"));
#else
    return QDir(root_dir).filePath(QStringLiteral("mk8d"));
#endif
}

QString GamePackage::baseNsp() const {
    if (installed) return layout.base();
    return QDir(root_dir).filePath(QStringLiteral("content/base.nsp"));
}

QString GamePackage::updateNsp() const {
    if (installed) return layout.update();
    return QDir(root_dir).filePath(QStringLiteral("content/update-3.0.4.nsp"));
}

QString GamePackage::dlcDir() const {
    if (installed) return layout.dlcDir();
    return QDir(root_dir).filePath(QStringLiteral("content/dlc"));
}

QStringList GamePackage::dlcFiles() const {
    QStringList out;
    const QDir dir(dlcDir());
    for (const auto& name : dir.entryList({QStringLiteral("*.nsp")}, QDir::Files, QDir::Name)) {
        out << dir.filePath(name);
    }
    return out;
}

QString GamePackage::configFile() const {
    if (installed) return QDir(config_root).filePath(QStringLiteral("sdl2-config.ini"));
    return QDir(root_dir).filePath(QStringLiteral("user/config/sdl2-config.ini"));
}

QString GamePackage::coverageReport() const {
    if (installed) return QDir(layout.dataRoot).filePath(QStringLiteral("log/recomp_coverage.txt"));
    return QDir(root_dir).filePath(QStringLiteral("user/log/recomp_coverage.txt"));
}

QString GamePackage::problems() const {
    if (installed) {
        if (!QFileInfo(engine_path).isExecutable()) return QStringLiteral("The game engine is missing (%1). Reinstall the launcher.").arg(engine_path);
        if (!IsBuilt(layout, engine_version)) return QStringLiteral("The game has not been built yet: use the Setup page.");
        return {};
    }
    QStringList missing;
    if (root_dir.isEmpty() || !QFileInfo(root_dir).isDir()) {
        return QStringLiteral("No game folder selected.");
    }
    if (!QFileInfo(executable()).isExecutable()) {
        missing << QStringLiteral("Game executable (build it from your own dump first): ") + executable();
    }
    if (!QFileInfo::exists(baseNsp())) {
        missing << QStringLiteral("Base game: ") + baseNsp();
    }
    if (!QFileInfo::exists(updateNsp())) {
        missing << QStringLiteral("Update 3.0.4: ") + updateNsp();
    }
    const QString keys = QDir(suyu_home).filePath(QStringLiteral("suyu/keys/prod.keys"));
    if (suyu_home.isEmpty() || !QFileInfo::exists(keys)) {
        missing << QStringLiteral("Your keys (prod.keys): ") + keys;
    }
    return missing.join(QLatin1Char('\n'));
}

QStringList GamePackage::launchArguments(bool fullscreen) const {
    if (installed) {
        QStringList args{QStringLiteral("--module-dir"), layout.moduleDir(), QStringLiteral("--content-base"), layout.base(),
                         QStringLiteral("--content-update"), layout.update()};
        for (const auto& dlc : dlcFiles()) args << QStringLiteral("--content-dlc") << dlc;
        if (fullscreen) args << QStringLiteral("-f");
        return args;
    }
    // Relative paths: the process runs with the package as its working directory.
    QStringList args{QStringLiteral("--content-base"), QStringLiteral("content/base.nsp"),
                     QStringLiteral("--content-update"), QStringLiteral("content/update-3.0.4.nsp")};
    const QDir root(root_dir);
    for (const auto& dlc : dlcFiles()) {
        args << QStringLiteral("--content-dlc") << root.relativeFilePath(dlc);
    }
    if (fullscreen) {
        args << QStringLiteral("-f");
    }
    return args;
}

QProcessEnvironment GamePackage::launchEnvironment() const {
    auto env = QProcessEnvironment::systemEnvironment();
    if (installed) {
        // The engine finds its XDG folders from the app name. launch.env (VAR=value lines, '#' comments) holds the launcher's runtime settings.
        env.insert(QStringLiteral("KARTRECOMP_APP_NAME"), QStringLiteral("kartrecomp"));
#ifdef Q_OS_WIN
        // Portable: the engine's <XDG>/kartrecomp folders are the ones beside the launcher, not %APPDATA%.
        env.insert(QStringLiteral("XDG_DATA_HOME"), QFileInfo(layout.dataRoot).absolutePath());
        env.insert(QStringLiteral("XDG_CACHE_HOME"), QFileInfo(layout.cacheRoot).absolutePath());
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), QFileInfo(config_root).absolutePath());
#endif
        QFile f(QDir(config_root).filePath(QStringLiteral("launch.env")));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!f.atEnd()) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                const int eq = line.indexOf(QLatin1Char('='));
                if (line.startsWith(QLatin1Char('#')) || eq <= 0) continue;
                env.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
            }
        }
        if (!env.contains(QStringLiteral("KARTRECOMP_NATIVE_GPU"))) env.insert(QStringLiteral("KARTRECOMP_NATIVE_GPU"), QStringLiteral("native"));
        return env;
    }
#ifndef Q_OS_WIN
    // The executable reads keys and firmware from "the installed suyu", which it finds
    // through the XDG base directories.
    env.insert(QStringLiteral("XDG_DATA_HOME"), suyu_home);
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), QDir(suyu_home).filePath(QStringLiteral("config")));
    env.insert(QStringLiteral("XDG_CACHE_HOME"), QDir(suyu_home).filePath(QStringLiteral("cache")));
#endif
    // TODO(windows): the executable uses %APPDATA%\suyu or a recorded portable suyu; pass
    // suyu_home through the user/ record instead of the environment.
    return env;
}

bool GamePackage::fixDataPaths() const {
    if (installed) return true; // the engine derives every data path from the app name
    IniFile ini;
    if (!ini.load(configFile())) {
        return false; // first launch: the executable writes a fresh config itself
    }
    const QDir user(QDir(root_dir).filePath(QStringLiteral("user")));
    const QString section = QStringLiteral("Data%20Storage");
    const QList<QPair<QString, QString>> wanted{
        {QStringLiteral("nand_directory"), user.filePath(QStringLiteral("nand"))},
        {QStringLiteral("sdmc_directory"), user.filePath(QStringLiteral("sdmc"))},
        {QStringLiteral("load_directory"), QDir(root_dir).filePath(QStringLiteral("mods"))},
        {QStringLiteral("dump_directory"), user.filePath(QStringLiteral("dump"))},
        {QStringLiteral("tas_directory"), user.filePath(QStringLiteral("tas"))},
    };
    bool changed = false;
    for (const auto& [key, path] : wanted) {
        const QString current = ini.value(section, key);
        if (!current.isEmpty() && QDir::cleanPath(current) != QDir::cleanPath(path)) {
            ini.setValue(section, key, path);
            changed = true;
        }
    }
    return !changed || ini.save();
}

QString GamePackage::findDefaultRoot() {
    const QStringList candidates{
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../local/game")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("game")),
        QDir::current().filePath(QStringLiteral("local/game")),
    };
    for (const auto& c : candidates) {
        if (QFileInfo(QDir(c).filePath(QStringLiteral("content"))).isDir()) {
            return QDir::cleanPath(QFileInfo(c).absoluteFilePath());
        }
    }
    return {};
}

QString GamePackage::findDefaultSuyuHome(const QString& root) {
    if (root.isEmpty()) {
        return {};
    }
    const QString sibling = QDir(root).filePath(QStringLiteral("../suyu-home"));
    return QFileInfo(sibling).isDir() ? QDir::cleanPath(QFileInfo(sibling).absoluteFilePath())
                                      : QString{};
}
