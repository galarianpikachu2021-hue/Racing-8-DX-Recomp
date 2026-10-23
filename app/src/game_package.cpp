// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_package.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "ini_file.h"

GamePackage::GamePackage(QString root, QString home)
    : root_dir(std::move(root)), suyu_home(std::move(home)) {}

QString GamePackage::executable() const {
#ifdef Q_OS_WIN
    return QDir(root_dir).filePath(QStringLiteral("mk8d.exe"));
#else
    return QDir(root_dir).filePath(QStringLiteral("mk8d"));
#endif
}

QString GamePackage::baseNsp() const {
    return QDir(root_dir).filePath(QStringLiteral("content/base.nsp"));
}

QString GamePackage::updateNsp() const {
    return QDir(root_dir).filePath(QStringLiteral("content/update-3.0.4.nsp"));
}

QString GamePackage::dlcDir() const {
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
    return QDir(root_dir).filePath(QStringLiteral("user/config/sdl2-config.ini"));
}

QString GamePackage::coverageReport() const {
    return QDir(root_dir).filePath(QStringLiteral("user/log/recomp_coverage.txt"));
}

QString GamePackage::problems() const {
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
