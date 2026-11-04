// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "zip_archive.h"

DataLayout DataLayout::FromRoots(const QString& data, const QString& cache) { return {data, cache}; }

DataLayout DataLayout::Default() {
    const auto root = [](const char* env, const QString& fallback) {
        const QString v = qEnvironmentVariable(env);
        return (v.isEmpty() ? fallback : v) + "/kartrecomp";
    };
    const QString home = QDir::homePath();
    return {root("XDG_DATA_HOME", home + "/.local/share"), root("XDG_CACHE_HOME", home + "/.cache")};
}

BuilderPaths BuilderPaths::Detect() {
    BuilderPaths p;
    const QString app = QCoreApplication::applicationDirPath();
    // Installed: <root>/{bin/kartrecomp, engine/, tools/, toolchain/, data/}; AppImage: the same under usr/. A source checkout has them
    // under local/ and data/ beside build/app/kartrecomp.
    const QStringList roots = {QDir::cleanPath(app + "/.."), QDir::cleanPath(app + "/../lib/kartrecomp"), QDir::cleanPath(app + "/../..")};
    const auto pick = [&](const char* env, const QStringList& rel) {
        if (const QString v = qEnvironmentVariable(env); !v.isEmpty()) return v;
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
bool PlaceFile(const QString& from, const QString& to, bool link, QString* error) {
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::remove(to);
    if (!QFileInfo(from).isFile()) {
        *error = QStringLiteral("%1 was not found.").arg(from);
        return false;
    }
    if (link && QFile::link(QFileInfo(from).absoluteFilePath(), to)) return true;
    if (QFile::copy(from, to)) return true;
    *error = QStringLiteral("Could not copy %1 to %2 (is there enough free space?).").arg(from, to);
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
    QDir(l.dlcDir()).removeRecursively();
    for (const QString& dlc : in.dlc)
        if (!PlaceFile(dlc, l.dlcDir() + "/" + QFileInfo(dlc).fileName(), true, &error)) return error;
    // firmware: every .nca of a folder, or of a zip, flat into the registered folder
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
    if (!QFileInfo::exists(l.moduleDir() + "/recompiled_image.so")) return false;
    QFile f(l.moduleDir() + "/module.json");
    if (!f.open(QIODevice::ReadOnly)) return false;
    return f.readAll().contains("\"engineVersion\": \"" + engineVersion.toUtf8() + "\"");
}
