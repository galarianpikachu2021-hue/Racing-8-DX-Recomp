// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <functional>

// A small read-only ZIP reader (stored and deflated entries, ZIP64 for archives over 4 GB), enough to unpack a mod pack without
// any extra tools. Entry names that would leave the destination folder (absolute paths, "..") are refused.
struct ZipEntry {
    QString name;
    quint64 compressed_size = 0;
    quint64 size = 0;
    quint64 local_offset = 0;
    quint32 crc = 0;
    quint16 method = 0;
    bool is_directory() const { return name.endsWith(QLatin1Char('/')); }
};

class ZipArchive {
public:
    bool open(const QString& path, QString* error = nullptr);
    const QList<ZipEntry>& entries() const { return list; }

    // Writes one entry to `destination` (parent folders are created) and checks its CRC. `progress` gets the bytes written so far
    // for this entry; return false from it to cancel.
    bool extract(const ZipEntry& entry, const QString& destination, QString* error = nullptr,
                 const std::function<bool(quint64)>& progress = {}) const;

    // The folder-safe relative form of an entry name, or empty when it is unsafe.
    static QString safeRelativePath(const QString& name);

private:
    QString archive_path;
    QList<ZipEntry> list;
};
