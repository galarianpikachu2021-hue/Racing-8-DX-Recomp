// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QUuid>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#ifdef Q_OS_LINUX
#include <sys/statvfs.h>
#include <unistd.h>
#endif

#include "ini_file.h"

namespace fs = std::filesystem;

namespace {
bool g_ignore = false;

QString SettingFile() { return DataLayout::ConfigRoot() + "/storage.txt"; }
QString Text(const char* s) { return QCoreApplication::translate("Messages", s); }
fs::path Path(const QString& s) { return QFileInfo(s).filesystemFilePath(); }

/// The closest folder that exists.
QString Existing(const QString& dir) {
    QString probe = QDir::cleanPath(QDir(dir).absolutePath());
    while (!QFileInfo::exists(probe)) {
        const QString parent = QFileInfo(probe).absolutePath();
        if (parent == probe) break;
        probe = parent;
    }
    return probe;
}

bool Inside(const QString& child, const QString& parent) {
    const QString c = QDir::cleanPath(QDir(child).absolutePath()), p = QDir::cleanPath(QDir(parent).absolutePath());
    return c == p || c.startsWith(p + '/');
}

struct Entry {
    QString rel;
    bool dir = false, link = false;
    qint64 size = 0;
};

bool List(const QString& root, std::vector<Entry>& out, qint64& total, const std::atomic<bool>* cancel) {
    if (!QFileInfo::exists(root)) return true;
    const QDir base(root);
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories); // links are not followed
    while (it.hasNext()) {
        if (cancel && cancel->load()) return false;
        it.next();
        const QFileInfo info = it.fileInfo();
        Entry e{base.relativeFilePath(info.filePath()), info.isDir() && !info.isSymLink(), info.isSymLink(), 0};
        if (!e.dir && !e.link && !info.isFile()) continue; // sockets and the like
        if (!e.dir && !e.link) e.size = info.size();
        total += e.size;
        out.push_back(std::move(e));
    }
    return true;
}

/// Copies one file, keeping its time and permissions. "" or the reason.
QString CopyFile(const QString& from, const QString& to, const std::function<void(qint64)>& wrote, const std::atomic<bool>* cancel) {
    QFile in(from), out(to);
    if (!in.open(QIODevice::ReadOnly)) return Text("Cannot read %1: %2").arg(from, in.errorString());
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return Text("Cannot write %1: %2").arg(to, out.errorString());
    std::vector<char> buffer(4 << 20);
    qint64 copied = 0;
    for (;;) {
        if (cancel && cancel->load()) return Text("Cancelled.");
        const qint64 n = in.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (n < 0) return Text("Cannot read %1: %2").arg(from, in.errorString());
        if (n == 0) break;
        if (out.write(buffer.data(), n) != n) return Text("Cannot write %1: %2").arg(to, out.errorString());
        copied += n;
        wrote(n);
    }
    if (!out.flush()) return Text("Cannot write %1: %2").arg(to, out.errorString());
#ifdef Q_OS_LINUX
    if (::fsync(out.handle()) != 0) return Text("Cannot write %1: %2").arg(to, QString::fromLocal8Bit(strerror(errno))); // really on the card, not only in memory
#endif
    out.setFileTime(in.fileTime(QFileDevice::FileModificationTime), QFileDevice::FileModificationTime);
    out.close();
    QFile::setPermissions(to, in.permissions()); // some file systems have none
    if (QFileInfo(to).size() != copied) return Text("%1 was not written completely.").arg(to);
    return {};
}

QString Remove(const QString& dir) {
    std::error_code ec;
    fs::remove_all(Path(dir), ec); // never follows links
    return ec ? dir : QString();
}
} // namespace

namespace Storage {

QString Chosen() {
    if (g_ignore) return {};
    QFile f(SettingFile());
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QString folder = QString::fromUtf8(f.readLine()).trimmed();
    return QDir::isAbsolutePath(folder) ? QDir::cleanPath(folder) : QString();
}

QString SetChosen(const QString& folder) {
    if (folder.isEmpty()) return !QFile::exists(SettingFile()) || QFile::remove(SettingFile()) ? QString() : Text("Cannot write %1.").arg(SettingFile());
    QDir().mkpath(DataLayout::ConfigRoot());
    QSaveFile f(SettingFile());
    if (!f.open(QIODevice::WriteOnly)) return Text("Cannot write %1.").arg(SettingFile());
    f.write(QDir::cleanPath(folder).toUtf8() + '\n');
    return f.commit() ? QString() : Text("Cannot write %1.").arg(SettingFile());
}

bool Missing() {
    const QString folder = Chosen();
    return !folder.isEmpty() && !QFileInfo(folder).isDir();
}

void IgnoreChoice() { g_ignore = true; }

DataLayout LayoutFor(const QString& folder) {
    if (folder.isEmpty()) return DataLayout::Standard();
    return DataLayout::FromRoots(folder + "/data/kartrecomp", folder + "/cache/kartrecomp");
}

QList<Volume> OtherVolumes() {
    QList<Volume> out;
    QSet<QByteArray> seen{QStorageInfo(Existing(DataLayout::Standard().dataRoot)).device()};
    for (const QStorageInfo& v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady() || v.isReadOnly() || v.bytesTotal() < (qint64(8) << 30)) continue;
#ifdef Q_OS_LINUX
        if (!v.device().startsWith("/dev/") || v.device().startsWith("/dev/loop")) continue;
        // Only places for the user's files: removable drives and extra disks, not the system's own partitions.
        const QString root = v.rootPath();
        if (!root.startsWith("/run/media/") && !root.startsWith("/media/") && !root.startsWith("/mnt/") && !QFileInfo(root).isWritable()) continue;
        if (root == "/boot" || root.startsWith("/boot/") || root == "/efi") continue;
#endif
        if (seen.contains(v.device())) continue; // the same drive mounted twice
        seen.insert(v.device());
        out << Volume{v.name().isEmpty() ? v.displayName() : v.name(), v.rootPath(), v.bytesAvailable(), v.bytesTotal()};
    }
    return out;
}

QString CheckFolder(const QString& folder) {
    if (!QDir().mkpath(folder)) return Text("Cannot create %1 (is the drive write-protected, or does it belong to another user?).").arg(folder);
    const QString probe = folder + "/.kartrecomp-" + QUuid::createUuid().toString(QUuid::Id128);
    QFile f(probe);
    const bool ok = f.open(QIODevice::WriteOnly) && f.write("x") == 1 && f.flush();
    f.close();
    f.remove();
    if (!ok) return Text("Cannot write to %1.").arg(folder);
    const QByteArray type = QStorageInfo(folder).fileSystemType().toLower();
    if (type == "vfat" || type == "fat32" || type == "fat" || type == "msdos")
        return Text("%1 is formatted as FAT32, which cannot hold files over 4 GB (the game is bigger). Format it as ext4 (Steam Deck: Settings > System > Format SD card), exFAT or NTFS.").arg(folder);
#ifdef Q_OS_LINUX
    struct statvfs s{};
    if (::statvfs(QFile::encodeName(folder).constData(), &s) == 0 && (s.f_flag & ST_NOEXEC))
        return Text("%1 is mounted so that programs cannot run from it, and the built game is a program. Format it as ext4 (Steam Deck: Settings > System > Format SD card).").arg(folder);
#endif
    return {};
}

qint64 SizeOf(const QString& dir, const std::atomic<bool>* cancel) {
    std::vector<Entry> entries;
    qint64 total = 0;
    List(dir, entries, total, cancel);
    return total;
}

qint64 FreeBytes(const QString& dir) { return QStorageInfo(Existing(dir)).bytesAvailable(); }

bool HoldsData(const DataLayout& l) {
    return !QDir(l.dataRoot).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty();
}

bool Repoint(const QString& configFile, const DataLayout& from, const DataLayout& to) {
    IniFile ini;
    if (!ini.load(configFile)) return true;
    const QString section = QStringLiteral("Data%20Storage");
    bool changed = false;
    for (const char* key : {"nand_directory", "sdmc_directory", "load_directory", "dump_directory", "tas_directory", "save_directory"}) {
        const QString old = ini.value(section, QLatin1String(key));
        if (!QDir::isAbsolutePath(old) || !Inside(old, from.dataRoot)) continue;
        ini.setValue(section, QLatin1String(key), to.dataRoot + QDir::cleanPath(old).mid(QDir::cleanPath(from.dataRoot).size()));
        changed = true;
    }
    return !changed || ini.save();
}

QString Move(const DataLayout& from, const DataLayout& to, const std::function<QString()>& commit,
             const std::function<void(qint64, qint64, const QString&)>& progress, const std::atomic<bool>* cancel, QString* leftover) {
    const std::pair<QString, QString> pairs[] = {{from.dataRoot, to.dataRoot}, {from.cacheRoot, to.cacheRoot}};
    for (const auto& [src, dst] : pairs) {
        if (Inside(dst, src) || Inside(src, dst)) return Text("%1 and %2 overlap.").arg(src, dst);
        if (QFileInfo(src).isSymLink()) return Text("%1 is a link to another place: move it by hand.").arg(src);
        if (QFileInfo::exists(dst) && !QDir(dst).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
            return Text("%1 already holds files.").arg(dst);
    }
    std::vector<Entry> entries[2];
    qint64 total = 0;
    bool rename[2], renamed[2] = {false, false}, copied[2] = {false, false};
    for (int i = 0; i < 2; ++i) {
        rename[i] = QStorageInfo(Existing(pairs[i].first)).device() == QStorageInfo(Existing(pairs[i].second)).device();
        if (rename[i]) continue;
        if (!List(pairs[i].first, entries[i], total, cancel)) return Text("Cancelled.");
    }
    if (FreeBytes(to.dataRoot) < total + (qint64(64) << 20))
        return Text("Not enough free space: %1 GB are needed there, %2 GB are free.")
            .arg(double(total) / 1e9, 0, 'f', 1).arg(double(FreeBytes(to.dataRoot)) / 1e9, 0, 'f', 1);

    qint64 done = 0;
    const auto copy_all = [&]() -> QString {
        for (int i = 0; i < 2; ++i) {
            const auto& [src, dst] = pairs[i];
            if (!QFileInfo::exists(src)) continue;
            if (rename[i]) {
                if (!QDir().mkpath(QFileInfo(dst).absolutePath())) return Text("Cannot create %1 (is the drive write-protected, or does it belong to another user?).").arg(dst);
                std::error_code ec;
                fs::rename(Path(src), Path(dst), ec);
                if (ec) return Text("Could not move %1 to %2 (is there enough free space?).").arg(src, dst);
                renamed[i] = true;
                continue;
            }
            copied[i] = true;
            if (!QDir().mkpath(dst)) return Text("Cannot create %1 (is the drive write-protected, or does it belong to another user?).").arg(dst);
            for (const Entry& e : entries[i]) {
                if (cancel && cancel->load()) return Text("Cancelled.");
                const QString a = src + '/' + e.rel, b = dst + '/' + e.rel;
                if (progress) progress(done, total, e.rel);
                if (!QDir().mkpath(e.dir ? b : QFileInfo(b).absolutePath())) return Text("Cannot create %1 (is the drive write-protected, or does it belong to another user?).").arg(b);
                if (e.dir) continue;
                if (e.link) {
                    std::error_code ec;
                    QString target = QFileInfo(a).readSymLink();
                    if (QDir::isAbsolutePath(target) && Inside(target, src)) // a link into the folder that moves points into the new one
                        target = dst + QDir::cleanPath(target).mid(QDir::cleanPath(src).size());
                    fs::create_symlink(Path(target), Path(b), ec);
                    if (ec) return Text("%1 is a link, which cannot be made on that drive.").arg(a);
                    continue;
                }
                if (const QString why = CopyFile(a, b, [&](qint64 n) {
                        done += n;
                        if (progress) progress(done, total, e.rel);
                    }, cancel); !why.isEmpty())
                    return why;
            }
        }
        return {};
    };
    QString why = copy_all();
    if (why.isEmpty()) why = commit();
    if (!why.isEmpty()) { // only what this move wrote: both targets were empty before
        for (int i = 0; i < 2; ++i) {
            std::error_code ec;
            if (renamed[i]) fs::rename(Path(pairs[i].second), Path(pairs[i].first), ec);
            else if (copied[i]) Remove(pairs[i].second);
        }
        return why;
    }
    for (int i = 0; i < 2; ++i) {
        if (!copied[i]) continue;
        const QString& src = pairs[i].first;
        const QString left = QFileInfo::exists(src) ? Remove(src) : QString();
        if (!left.isEmpty() && leftover) *leftover = left;
    }
    return {};
}

} // namespace Storage
