// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_paths.h"
#include "build_runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QSet>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "zip_archive.h"

DataLayout DataLayout::FromRoots(const QString& data, const QString& cache) { return {data, cache}; }

QString DataLayout::PortableRoot() { return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/userdata"); }

QString DataLayout::ConfigRoot() {
#ifdef Q_OS_WIN
    if (const QString v = qEnvironmentVariable("XDG_CONFIG_HOME"); !v.isEmpty()) return v + "/kartrecomp";
    return PortableRoot() + "/config/kartrecomp";
#else
    const QString v = qEnvironmentVariable("XDG_CONFIG_HOME");
    return (v.isEmpty() ? QDir::homePath() + "/.config" : v) + "/kartrecomp";
#endif
}

DataLayout DataLayout::Default() {
#ifdef Q_OS_WIN
    // Windows is portable: everything the launcher and the game write (the built game, saves, mods, logs) sits in a "userdata" folder
    // next to kartrecomp.exe, so the folder can be moved, copied or zipped as it is. XDG_* variables still override (tests).
    const auto root = [](const char* env, const QString& fallback) {
        const QString v = qEnvironmentVariable(env);
        return (v.isEmpty() ? fallback : v) + "/kartrecomp";
    };
    return {root("XDG_DATA_HOME", PortableRoot() + "/data"), root("XDG_CACHE_HOME", PortableRoot() + "/cache")};
#else
    const auto root = [](const char* env, const QString& fallback) {
        const QString v = qEnvironmentVariable(env);
        return (v.isEmpty() ? fallback : v) + "/kartrecomp";
    };
    const QString home = QDir::homePath();
    return {root("XDG_DATA_HOME", home + "/.local/share"), root("XDG_CACHE_HOME", home + "/.cache")};
#endif
}

BuilderPaths BuilderPaths::Detect() {
    BuilderPaths p;
    const QString app = QCoreApplication::applicationDirPath();
    // Installed: <root>/{bin/kartrecomp, engine/, tools/, toolchain/, data/}; AppImage: the same under usr/. A source checkout has them
    // under local/ and data/ beside build/app/kartrecomp.
    const QStringList roots = {app, QDir::cleanPath(app + "/.."), QDir::cleanPath(app + "/../lib/kartrecomp"), QDir::cleanPath(app + "/../..")};
    const auto pick = [&](const char* env, QStringList rel) {
        if (const QString v = qEnvironmentVariable(env); !v.isEmpty()) return v;
#ifdef Q_OS_WIN
        for (QString& x : rel)
            if (!x.endsWith(".txt") && !x.contains("toolchain")) x += ".exe";
#endif
        for (const QString& r : roots)
            for (const QString& x : rel)
                if (QFileInfo::exists(r + "/" + x)) return QDir::cleanPath(r + "/" + x);
        return QString();
    };
    p.exporter = pick("KARTRECOMP_EXPORTER", {"tools/kartrecomp-export", "bin/kartrecomp-export"});
    p.engine = pick("KARTRECOMP_ENGINE", {"bin/mk8d-engine", "engine/mk8d-engine", "local/game/mk8d-engine"});
    p.toolchain = pick("KARTRECOMP_TOOLCHAIN", {"toolchain", "local/toolchain"});
    p.hotList = pick("KARTRECOMP_HOT_LIST", {"data/mk8d-3.0.4/hot_blocks.txt"});
    if (!p.engine.isEmpty()) {
        QFile v(QFileInfo(p.engine).absolutePath() + "/ENGINE_VERSION");
        if (v.open(QIODevice::ReadOnly)) p.engineVersion = QString::fromUtf8(v.readAll()).trimmed();
    }
    return p;
}

bool BuilderPaths::complete(QString* missing) const {
    const std::pair<const char*, QString> parts[] = {{"exporter", exporter}, {"engine", engine}, {"toolchain", toolchain}, {"hot block list", hotList}};
    for (const auto& [name, path] : parts) {
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            if (missing) *missing = QStringLiteral("The %1 is missing (%2). Reinstall the launcher.").arg(name, path.isEmpty() ? QStringLiteral("not found") : path);
            return false;
        }
    }
    return true;
}

namespace {
// move=true: the NSPs are big (13 GB), so they are moved into the data folder, not copied or linked; a link would break the moment the
// user moves or deletes their original. The launcher warns before it does this. Small files (keys) are copied.
bool PlaceFile(const QString& from, const QString& to, bool move, QString* error) {
    const QFileInfo src(from);
    if (move && !src.isFile() && QFileInfo(to).isFile()) return true; // moved by an earlier build; the installed copy is the game file
    if (!src.isFile()) {
        *error = QStringLiteral("%1 was not found.").arg(from);
        return false;
    }
    if (QFileInfo(to).exists() && QFileInfo(to).canonicalFilePath() == src.canonicalFilePath()) return true; // already in place
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::remove(to);
    if (move && QFile::rename(from, to)) return true;
    if (QFile::copy(from, to)) {
        if (move) QFile::remove(from);
        return true;
    }
    QFile::remove(to); // never leave half a file behind
    *error = QStringLiteral("Could not %1 %2 to %3 (is there enough free space?).").arg(move ? QStringLiteral("move") : QStringLiteral("copy"), from, to);
    return false;
}
} // namespace

QString InstallInputs(const SetupInputs& in, const DataLayout& l) {
    QString error;
    if (!PlaceFile(in.base, l.base(), true, &error) || !PlaceFile(in.update, l.update(), true, &error) ||
        !PlaceFile(in.keys, l.keysFile(), false, &error))
        return error;
    // title.keys beside prod.keys, when the user has one
    const QString title = QFileInfo(in.keys).absolutePath() + "/title.keys";
    if (QFileInfo(title).isFile() && !PlaceFile(title, QFileInfo(l.keysFile()).absolutePath() + "/title.keys", false, &error)) return error;
    // DLC files already in the folder (moved there by an earlier build) stay; anything the user dropped from the list goes
    QSet<QString> wanted;
    for (const QString& dlc : in.dlc) wanted.insert(QFileInfo(dlc).fileName());
    for (const QFileInfo& old : QDir(l.dlcDir()).entryInfoList(QDir::Files))
        if (!wanted.contains(old.fileName())) QFile::remove(old.absoluteFilePath());
    for (const QString& dlc : in.dlc)
        if (!PlaceFile(dlc, l.dlcDir() + "/" + QFileInfo(dlc).fileName(), true, &error)) return error;
    // firmware: every .nca of a folder, or of a zip, flat into the registered folder
    // Already installed (the page shows the installed folder when its own settings are gone): clearing it first would delete the input.
    if (QFileInfo(in.firmware).isDir() && QDir(in.firmware).canonicalPath() == QDir(l.firmwareDir()).canonicalPath()) return {};
    QDir(l.firmwareDir()).removeRecursively();
    QDir().mkpath(l.firmwareDir());
    if (QFileInfo(in.firmware).isDir()) {
        QDirIterator it(in.firmware, {"*.nca"}, QDir::Files, QDirIterator::Subdirectories);
        int n = 0;
        while (it.hasNext()) {
            it.next();
            if (!QFile::copy(it.filePath(), l.firmwareDir() + "/" + it.fileName())) return QStringLiteral("Could not copy the firmware file %1.").arg(it.fileName());
            ++n;
        }
        if (n == 0) return QStringLiteral("The firmware folder holds no .nca files.");
    } else {
        ZipArchive zip;
        if (!zip.open(in.firmware, &error)) return QStringLiteral("Cannot open the firmware zip: %1").arg(error);
        int n = 0;
        for (const ZipEntry& e : zip.entries()) {
            if (e.is_directory() || !e.name.endsWith(".nca", Qt::CaseInsensitive)) continue;
            if (!zip.extract(e, l.firmwareDir() + "/" + QFileInfo(e.name).fileName(), &error)) return QStringLiteral("Cannot unpack the firmware: %1").arg(error);
            ++n;
        }
        if (n == 0) return QStringLiteral("The firmware zip holds no .nca files.");
    }
    return {};
}

bool IsBuilt(const DataLayout& l, const QString& engineVersion) {
    if (!QFileInfo::exists(l.moduleDir() + "/" + BuildRunner::ModuleFileName("image"))) return false;
    QFile f(l.moduleDir() + "/module.json");
    if (!f.open(QIODevice::ReadOnly)) return false;
    return f.readAll().contains("\"engineVersion\": \"" + engineVersion.toUtf8() + "\"");
}

QString DataLayout::installedKeys() const { return QFileInfo(keysFile()).isFile() ? keysFile() : QString(); }

QString DataLayout::installedFirmware() const {
    return QDir(firmwareDir()).entryList({"*.nca"}, QDir::Files).isEmpty() ? QString() : firmwareDir();
}

QStringList DataLayout::installedDlc() const {
    QStringList out;
    for (const QString& f : QDir(dlcDir()).entryList({"*.nsp"}, QDir::Files, QDir::Name)) out << dlcDir() + "/" + f;
    return out;
}
