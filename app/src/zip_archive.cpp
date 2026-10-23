// SPDX-License-Identifier: GPL-3.0-or-later
#include "zip_archive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <zlib.h>

namespace {

quint16 U16(const uchar* p) { return static_cast<quint16>(p[0] | (p[1] << 8)); }
quint32 U32(const uchar* p) { return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) | (static_cast<quint32>(p[2]) << 16) | (static_cast<quint32>(p[3]) << 24); }
quint64 U64(const uchar* p) { return static_cast<quint64>(U32(p)) | (static_cast<quint64>(U32(p + 4)) << 32); }

bool Fail(QString* error, const QString& text) {
    if (error) *error = text;
    return false;
}

bool ReadAt(QFile& file, qint64 offset, void* out, qint64 size) {
    return file.seek(offset) && file.read(static_cast<char*>(out), size) == size;
}

} // namespace

QString ZipArchive::safeRelativePath(const QString& name) {
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char(':'))) {
        return {};
    }
    const auto parts = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const auto& part : parts) {
        if (part == QLatin1String("..")) return {};
    }
    return parts.join(QLatin1Char('/'));
}

bool ZipArchive::open(const QString& path, QString* error) {
    archive_path = path;
    list.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Fail(error, QStringLiteral("Cannot open %1").arg(path));
    const qint64 total = file.size();
    if (total < 22) return Fail(error, QStringLiteral("Not a zip file"));

    // End of central directory: scan the last 64 KB + 22 bytes for its signature.
    const qint64 scan = qMin<qint64>(total, 65557);
    QByteArray tail(static_cast<int>(scan), 0);
    if (!ReadAt(file, total - scan, tail.data(), scan)) return Fail(error, QStringLiteral("Read error"));
    int eocd = -1;
    for (int i = static_cast<int>(scan) - 22; i >= 0; --i) {
        if (U32(reinterpret_cast<const uchar*>(tail.constData()) + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) return Fail(error, QStringLiteral("Not a zip file (no end record)"));
    const auto* e = reinterpret_cast<const uchar*>(tail.constData()) + eocd;
    quint64 count = U16(e + 10);
    quint64 cd_size = U32(e + 12);
    quint64 cd_offset = U32(e + 16);
    if (count == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu) {
        // ZIP64: the locator sits right before the end record.
        const qint64 locator_at = total - scan + eocd - 20;
        uchar loc[20];
        if (locator_at < 0 || !ReadAt(file, locator_at, loc, 20) || U32(loc) != 0x07064b50u) {
            return Fail(error, QStringLiteral("Corrupt zip64 end record"));
        }
        uchar z64[56];
        if (!ReadAt(file, static_cast<qint64>(U64(loc + 8)), z64, 56) || U32(z64) != 0x06064b50u) {
            return Fail(error, QStringLiteral("Corrupt zip64 end record"));
        }
        count = U64(z64 + 32);
        cd_size = U64(z64 + 40);
        cd_offset = U64(z64 + 48);
    }
    if (cd_offset + cd_size > static_cast<quint64>(total) || cd_size > (1ull << 30)) return Fail(error, QStringLiteral("Corrupt central directory"));
    QByteArray cd(static_cast<int>(cd_size), 0);
    if (!ReadAt(file, static_cast<qint64>(cd_offset), cd.data(), static_cast<qint64>(cd_size))) return Fail(error, QStringLiteral("Read error"));

    const auto* p = reinterpret_cast<const uchar*>(cd.constData());
    const auto* end = p + cd.size();
    for (quint64 i = 0; i < count; ++i) {
        if (end - p < 46 || U32(p) != 0x02014b50u) return Fail(error, QStringLiteral("Corrupt central directory entry"));
        ZipEntry entry;
        entry.method = U16(p + 10);
        entry.crc = U32(p + 16);
        entry.compressed_size = U32(p + 20);
        entry.size = U32(p + 24);
        const int name_len = U16(p + 28), extra_len = U16(p + 30), comment_len = U16(p + 32);
        entry.local_offset = U32(p + 42);
        if (end - p < 46 + name_len + extra_len + comment_len) return Fail(error, QStringLiteral("Corrupt central directory entry"));
        entry.name = QString::fromUtf8(reinterpret_cast<const char*>(p + 46), name_len);
        // ZIP64 extra field: only the fields that were 0xFFFFFFFF are present, in this order.
        const uchar* x = p + 46 + name_len;
        const uchar* xend = x + extra_len;
        while (xend - x >= 4) {
            const quint16 id = U16(x), len = U16(x + 2);
            if (xend - x < 4 + len) break;
            if (id == 0x0001) {
                const uchar* f = x + 4;
                const uchar* fend = f + len;
                if (entry.size == 0xFFFFFFFFu && fend - f >= 8) { entry.size = U64(f); f += 8; }
                if (entry.compressed_size == 0xFFFFFFFFu && fend - f >= 8) { entry.compressed_size = U64(f); f += 8; }
                if (entry.local_offset == 0xFFFFFFFFu && fend - f >= 8) { entry.local_offset = U64(f); f += 8; }
            }
            x += 4 + len;
        }
        list.append(entry);
        p += 46 + name_len + extra_len + comment_len;
    }
    return true;
}

bool ZipArchive::extract(const ZipEntry& entry, const QString& destination, QString* error, const std::function<bool(quint64)>& progress) const {
    if (entry.method != 0 && entry.method != 8) return Fail(error, QStringLiteral("%1: unsupported compression method %2").arg(entry.name).arg(entry.method));
    QFile in(archive_path);
    if (!in.open(QIODevice::ReadOnly)) return Fail(error, QStringLiteral("Cannot open %1").arg(archive_path));
    uchar local[30];
    if (!ReadAt(in, static_cast<qint64>(entry.local_offset), local, 30) || U32(local) != 0x04034b50u) {
        return Fail(error, QStringLiteral("%1: bad local header").arg(entry.name));
    }
    const qint64 data_at = static_cast<qint64>(entry.local_offset) + 30 + U16(local + 26) + U16(local + 28);
    if (!in.seek(data_at)) return Fail(error, QStringLiteral("Read error"));

    QDir().mkpath(QFileInfo(destination).absolutePath());
    QFile out(destination);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return Fail(error, QStringLiteral("Cannot write %1").arg(destination));

    constexpr int kChunk = 1 << 20;
    QByteArray src(kChunk, 0), dst(kChunk, 0);
    quint64 remaining = entry.compressed_size, written = 0;
    uLong crc = crc32(0L, Z_NULL, 0);
    z_stream zs{};
    const bool inflating = entry.method == 8;
    if (inflating && inflateInit2(&zs, -15) != Z_OK) return Fail(error, QStringLiteral("zlib init failed"));
    bool ok = true;
    int status = Z_OK;
    while (remaining > 0 || (inflating && status != Z_STREAM_END)) {
        const qint64 got = remaining ? in.read(src.data(), static_cast<qint64>(qMin<quint64>(remaining, kChunk))) : 0;
        if (remaining && got <= 0) { ok = Fail(error, QStringLiteral("%1: truncated").arg(entry.name)); break; }
        remaining -= static_cast<quint64>(got);
        if (!inflating) {
            crc = crc32(crc, reinterpret_cast<const Bytef*>(src.constData()), static_cast<uInt>(got));
            if (out.write(src.constData(), got) != got) { ok = Fail(error, QStringLiteral("Write error on %1").arg(destination)); break; }
            written += static_cast<quint64>(got);
        } else {
            zs.next_in = reinterpret_cast<Bytef*>(src.data());
            zs.avail_in = static_cast<uInt>(got);
            do {
                zs.next_out = reinterpret_cast<Bytef*>(dst.data());
                zs.avail_out = kChunk;
                status = inflate(&zs, Z_NO_FLUSH);
                if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) { ok = Fail(error, QStringLiteral("%1: corrupt data").arg(entry.name)); break; }
                const uInt produced = kChunk - zs.avail_out;
                crc = crc32(crc, reinterpret_cast<const Bytef*>(dst.constData()), produced);
                if (out.write(dst.constData(), produced) != produced) { ok = Fail(error, QStringLiteral("Write error on %1").arg(destination)); break; }
                written += produced;
            } while (zs.avail_out == 0 && status != Z_STREAM_END);
            if (!ok) break;
            if (status == Z_BUF_ERROR && got == 0) { ok = Fail(error, QStringLiteral("%1: truncated").arg(entry.name)); break; }
        }
        if (progress && !progress(written)) { ok = Fail(error, QStringLiteral("cancelled")); break; }
    }
    if (inflating) inflateEnd(&zs);
    out.close();
    if (ok && (written != entry.size || static_cast<quint32>(crc) != entry.crc)) ok = Fail(error, QStringLiteral("%1: checksum mismatch").arg(entry.name));
    if (!ok) QFile::remove(destination);
    return ok;
}
