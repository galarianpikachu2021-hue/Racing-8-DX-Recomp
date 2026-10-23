// SPDX-License-Identifier: GPL-3.0-or-later
#include "mod_addon.h"
#include "text_merge.h"

#include <QDir>
#include <QDirIterator>

#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include "ini_file.h"
#include "zip_archive.h"

namespace ModAddon {
namespace {

const QString kContentsPrefix = QStringLiteral("atmosphere/contents/");

QString PackPrefix() { return kContentsPrefix + QLatin1String(kMk8dTitleId) + QLatin1Char('/'); }

const QString kCtgpName = QStringLiteral("CTGP DX Cups");
QString EnabledDir(const QString& root) { return QDir(root).filePath(QStringLiteral("mods/%1/%2").arg(QLatin1String(kMk8dTitleId), kCtgpName)); }

// The emulator disables a mod by NAME, through the "DisabledAddOns" list in its config (renaming the folder does nothing: every folder
// under mods/<title id>/ is loaded). Entry format (a QSettings array): N\title_id=<decimal title id>, N\disabled\size=K,
// N\disabled\j\d=<name>.
const QString kDisabledSection = QStringLiteral("DisabledAddOns");
const QString kTitleDecimal = QStringLiteral("72080821221203968"); // 0x0100152000022000

QString ConfigPath(const QString& root) { return QDir(root).filePath(QStringLiteral("user/config/sdl2-config.ini")); }

int FindEntry(const IniFile& ini) {
    const int size = ini.intValue(kDisabledSection, QStringLiteral("size"), 0);
    for (int i = 1; i <= size; ++i) {
        if (ini.value(kDisabledSection, QStringLiteral("%1\\title_id").arg(i)) == kTitleDecimal) return i;
    }
    return 0;
}

QStringList ReadDisabled(const QString& root) {
    IniFile ini;
    QStringList out;
    if (!ini.load(ConfigPath(root))) return out;
    const int entry = FindEntry(ini);
    if (!entry) return out;
    const int n = ini.intValue(kDisabledSection, QStringLiteral("%1\\disabled\\size").arg(entry), 0);
    for (int j = 1; j <= n; ++j) out << ini.value(kDisabledSection, QStringLiteral("%1\\disabled\\%2\\d").arg(entry).arg(j));
    return out;
}

bool WriteDisabled(const QString& root, const QStringList& names, QString* error) {
    IniFile ini;
    if (!ini.load(ConfigPath(root))) {
        if (error) *error = QStringLiteral("Start the game once so it creates its config, then try again.");
        return false;
    }
    int entry = FindEntry(ini);
    if (!entry) {
        entry = ini.intValue(kDisabledSection, QStringLiteral("size"), 0) + 1;
        ini.setRawValue(kDisabledSection, QStringLiteral("size"), QString::number(entry));
        ini.setRawValue(kDisabledSection, QStringLiteral("%1\\title_id\\default").arg(entry), QStringLiteral("false"));
        ini.setRawValue(kDisabledSection, QStringLiteral("%1\\title_id").arg(entry), kTitleDecimal);
    }
    ini.removeKeysWithPrefix(kDisabledSection, QStringLiteral("%1\\disabled\\").arg(entry));
    ini.setRawValue(kDisabledSection, QStringLiteral("%1\\disabled\\size").arg(entry), QString::number(names.size()));
    for (int j = 0; j < names.size(); ++j) {
        ini.setRawValue(kDisabledSection, QStringLiteral("%1\\disabled\\%2\\d\\default").arg(entry).arg(j + 1), QStringLiteral("false"));
        ini.setRawValue(kDisabledSection, QStringLiteral("%1\\disabled\\%2\\d").arg(entry).arg(j + 1), names[j]);
    }
    if (!ini.save()) {
        if (error) *error = QStringLiteral("Could not write the game's config.");
        return false;
    }
    return true;
}

bool SetDisabled(const QString& root, const QString& name, bool disabled, QString* error) {
    QStringList list = ReadDisabled(root);
    list.removeAll(name);
    if (disabled) list << name;
    return WriteDisabled(root, list, error);
}

} // namespace

QString AddonDir(const QString& root) { return QDir(root).filePath(QStringLiteral("addons/ctgpdx")); }

QString ModDir(const QString& root) { return EnabledDir(root); }

PackInfo AnalyzeCtgpPack(const QString& zip_path) {
    PackInfo info;
    info.title_id = QLatin1String(kMk8dTitleId);
    info.zip_bytes = static_cast<quint64>(QFileInfo(zip_path).size());
    ZipArchive zip;
    if (!zip.open(zip_path, &info.error)) return info;
    const QString prefix = PackPrefix();
    const ZipEntry* npdm = nullptr;
    for (const auto& e : zip.entries()) {
        if (e.is_directory() || !e.name.startsWith(prefix)) continue;
        const QString rel = e.name.mid(prefix.size());
        if (rel.startsWith(QLatin1String("romfs/"))) {
            ++info.romfs_files;
            info.romfs_bytes += e.size;
            if (rel.startsWith(QLatin1String("romfs/skyline/plugins/")) && rel.endsWith(QLatin1String(".nro"))) {
                ++info.plugin_files;
                info.plugin_bytes += e.size;
            }
        } else if (rel.startsWith(QLatin1String("exefs/"))) {
            ++info.exefs_files;
            if (rel == QLatin1String("exefs/main.npdm")) npdm = &e;
        }
    }
    if (info.romfs_files == 0 && info.exefs_files == 0) {
        info.error = QStringLiteral("This zip has no content for Mario Kart 8 Deluxe (expected atmosphere/contents/%1/...).").arg(info.title_id);
        return info;
    }
    if (npdm && npdm->size >= 16 && npdm->size < 65536) {
        const QString tmp = QDir::temp().filePath(QStringLiteral("kartrecomp-ctgp-main.npdm"));
        if (zip.extract(*npdm, tmp)) {
            QFile f(tmp);
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray head = f.read(16);
                if (head.size() == 16 && head.startsWith("META")) {
                    // Process flags: bit 0 = 64-bit instructions (3.0.3 is 0x04 = 32-bit, 3.0.4 is 0x07 = 64-bit).
                    const quint8 flags = static_cast<quint8>(head[12]);
                    info.code_is_32bit = (flags & 0x01) == 0;
                    info.code_known = true;
                }
            }
            QFile::remove(tmp);
        }
    }
    info.valid = true;
    return info;
}

State CtgpState(const QString& root) {
    if (!QFileInfo::exists(EnabledDir(root))) return State::NotImported;
    return ReadDisabled(root).contains(kCtgpName) ? State::Disabled : State::Enabled;
}

bool SetCtgpEnabled(const QString& root, bool enabled, QString* error) {
    if (!QFileInfo::exists(EnabledDir(root))) return true;
    return SetDisabled(root, kCtgpName, !enabled, error);
}

bool RemoveCtgp(const QString& root, QString* error) {
    const bool ok = QDir(EnabledDir(root)).removeRecursively();
    SetDisabled(root, kCtgpName, false, nullptr);
    if (!ok && error) *error = QStringLiteral("Could not remove every file");
    return ok;
}

QString CodeRecompBlocker(const PackInfo& info) {
    Q_UNUSED(info)
    return QStringLiteral("CTGP DX's code is a 32-bit ARM plugin made for game version 3.0.3, and this build is the 64-bit 3.0.4 game, so the code cannot "
                          "run here. The pack's tracks are data, so they are converted instead: each custom track takes the place of one of the game's "
                          "race courses, with its thumbnail, music and name, and the cups are renamed. Not available: the extra cups' own menus, the "
                          "speedometer, online play and the pack's settings screen.");
}

// ------------------------------------------------------------------------------------------------------------ ordinary mods

namespace {

QString TitleModsDir(const QString& root) { return QDir(root).filePath(QStringLiteral("mods/%1").arg(QLatin1String(kMk8dTitleId))); }

// The normalised relative path ("romfs/..." or "exefs/...") of a file inside a mod, whatever folders wrap it (Atmosphere's
// atmosphere/contents/<id>/, yuzu's load/<id>/<mod>/, a "Romfs" with another case), or empty for files that are not mod data.
QString NormalisePath(const QString& path) {
    const QString p = ZipArchive::safeRelativePath(path);
    if (p.isEmpty()) return {};
    const auto parts = p.split(QLatin1Char('/'));
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString lower = parts[i].toLower();
        if (lower == QLatin1String("romfs") || lower == QLatin1String("exefs")) {
            const QStringList rest = parts.mid(i + 1);
            return lower + QLatin1Char('/') + rest.join(QLatin1Char('/'));
        }
    }
    return {};
}

QString UniqueName(const QString& base, const QString& dir) {
    const QString name = base.isEmpty() ? QStringLiteral("mod") : base;
    QString candidate = name;
    for (int n = 2; QFileInfo::exists(QDir(dir).filePath(candidate)); ++n) {
        candidate = QStringLiteral("%1 (%2)").arg(name).arg(n);
    }
    return candidate;
}

void CountTree(const QString& dir, int& files, quint64& bytes) {
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++files;
        bytes += static_cast<quint64>(it.fileInfo().size());
    }
}

} // namespace

QList<ModEntry> ListMods(const QString& root) {
    QList<ModEntry> mods;
    const QDir dir(TitleModsDir(root));
    const QStringList disabled = ReadDisabled(root);
    for (const auto& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        ModEntry m;
        m.name = name;
        m.enabled = !disabled.contains(name);
        if (m.name == kCtgpName || m.name == QLatin1String(TextMerge::kMergedModName)) continue; // CTGP is shown as its own add-on; the merged text is automatic
        m.path = dir.filePath(name);
        m.has_romfs = QFileInfo(QDir(m.path).filePath(QStringLiteral("romfs"))).isDir();
        m.has_exefs = QFileInfo(QDir(m.path).filePath(QStringLiteral("exefs"))).isDir();
        CountTree(m.path, m.files, m.bytes);
        mods.append(m);
    }
    return mods;
}

bool ImportModFolder(const QString& source, const QString& root, QString* error, const std::function<bool(int, int, const QString&)>& progress) {
    struct Job { QString from, rel; };
    QList<Job> jobs;
    QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
    const QDir src(source);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString rel = NormalisePath(src.relativeFilePath(file));
        if (!rel.isEmpty()) jobs.append({file, rel});
    }
    if (jobs.isEmpty()) {
        if (error) *error = QStringLiteral("No romfs or exefs folder found in %1.").arg(source);
        return false;
    }
    const bool has_data = std::any_of(jobs.begin(), jobs.end(), [](const Job& j) { return j.rel.startsWith(QLatin1String("romfs/")); });
    if (!has_data) {
        if (error) *error = QStringLiteral("Skipped %1: it only holds code patches, which this recompiled build cannot apply.").arg(QFileInfo(source).fileName());
        return false;
    }
    QDir().mkpath(TitleModsDir(root));
    const QString name = UniqueName(QFileInfo(source).fileName(), TitleModsDir(root));
    const QString target = QDir(TitleModsDir(root)).filePath(name);
    int done = 0;
    for (const auto& job : jobs) {
        if (progress && !progress(done, static_cast<int>(jobs.size()), job.rel)) {
            QDir(target).removeRecursively();
            if (error) *error = QStringLiteral("Cancelled");
            return false;
        }
        const QString to = QDir(target).filePath(job.rel);
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QFile::copy(job.from, to)) {
            QDir(target).removeRecursively();
            if (error) *error = QStringLiteral("Could not copy %1").arg(job.from);
            return false;
        }
        ++done;
    }
    return true;
}

bool ImportModZip(const QString& zip_path, const QString& root, QString* error, const std::function<bool(int, int, const QString&)>& progress) {
    ZipArchive zip;
    if (!zip.open(zip_path, error)) return false;
    struct Job { const ZipEntry* entry; QString rel; };
    QList<Job> jobs;
    for (const auto& e : zip.entries()) {
        if (e.is_directory()) continue;
        const QString rel = NormalisePath(e.name);
        if (!rel.isEmpty()) jobs.append({&e, rel});
    }
    if (jobs.isEmpty()) {
        if (error) *error = QStringLiteral("The zip has no romfs or exefs folder.");
        return false;
    }
    if (std::none_of(jobs.begin(), jobs.end(), [](const Job& j) { return j.rel.startsWith(QLatin1String("romfs/")); })) {
        if (error) *error = QStringLiteral("Skipped: the zip only holds code patches, which this recompiled build cannot apply.");
        return false;
    }
    QDir().mkpath(TitleModsDir(root));
    const QString name = UniqueName(QFileInfo(zip_path).completeBaseName(), TitleModsDir(root));
    const QString target = QDir(TitleModsDir(root)).filePath(name);
    int done = 0;
    for (const auto& job : jobs) {
        if (progress && !progress(done, static_cast<int>(jobs.size()), job.rel)) {
            QDir(target).removeRecursively();
            if (error) *error = QStringLiteral("Cancelled");
            return false;
        }
        if (!zip.extract(*job.entry, QDir(target).filePath(job.rel), error)) {
            QDir(target).removeRecursively();
            return false;
        }
        ++done;
    }
    return true;
}

QStringList EnabledModDirs(const QString& root) {
    const QDir dir(TitleModsDir(root));
    const QStringList disabled = ReadDisabled(root);
    QStringList out;
    for (const auto& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (name == QLatin1String(TextMerge::kMergedModName) || disabled.contains(name)) continue;
        out << dir.filePath(name);
    }
    return out;
}

namespace {

// Which mods and message files the merged copy was built from; a different signature means it is stale.
QString MergeSignature(const QString& root) {
    QString sig;
    for (const auto& mod : EnabledModDirs(root)) {
        const QDir ui(QDir(mod).filePath(QStringLiteral("romfs/UI")));
        for (const auto& lang : ui.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QFileInfo f(ui.filePath(lang + QStringLiteral("/message.sarc")));
            if (f.exists()) sig += QStringLiteral("%1/%2:%3:%4\n").arg(QFileInfo(mod).fileName(), lang).arg(f.size()).arg(f.lastModified().toSecsSinceEpoch());
        }
    }
    return sig;
}

QString MergedDir(const QString& root) { return QDir(TitleModsDir(root)).filePath(QLatin1String(TextMerge::kMergedModName)); }

} // namespace

bool HasTextConflict(const QString& root) { return TextMerge::HasConflict(EnabledModDirs(root)); }

bool NeedsTextMerge(const QString& root) {
    if (!TextMerge::HasConflict(EnabledModDirs(root))) return false;
    QFile f(QDir(MergedDir(root)).filePath(QStringLiteral(".signature")));
    return !(f.open(QIODevice::ReadOnly) && f.readAll() == MergeSignature(root).toUtf8());
}

QString VanillaUiDir(const QString& root) { return QDir(root).filePath(QStringLiteral("addons/vanilla-ui")); }

bool UpdateTextMerge(const QString& root, const QString& vanilla_ui_dir, QString* error) {
    const QString out = MergedDir(root);
    const QStringList dirs = EnabledModDirs(root);
    QDir(out).removeRecursively();
    if (!TextMerge::HasConflict(dirs)) return true;
    if (TextMerge::Merge(dirs, vanilla_ui_dir, out, error, nullptr) < 0) return false;
    QFile sig(QDir(out).filePath(QStringLiteral(".signature")));
    if (sig.open(QIODevice::WriteOnly | QIODevice::Truncate)) sig.write(MergeSignature(root).toUtf8());
    return true;
}

bool SetModEnabled(const QString& root, const QString& name, bool enabled, QString* error) {
    return SetDisabled(root, name, !enabled, error);
}

bool RemoveMod(const QString& root, const QString& name, QString* error) {
    const bool ok = QDir(QDir(TitleModsDir(root)).filePath(name)).removeRecursively();
    SetDisabled(root, name, false, nullptr);
    if (!ok && error) *error = QStringLiteral("Could not remove every file");
    return ok;
}

} // namespace ModAddon
