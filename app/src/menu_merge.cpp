// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_merge.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <vector>

namespace MenuMerge {
namespace {

constexpr int kBlock = 4096;

quint32 U32(const QByteArray& d, int at) {
    quint32 v = 0;
    if (at >= 0 && at + 4 <= d.size()) std::memcpy(&v, d.constData() + at, 4);
    return v;
}
quint16 U16(const QByteArray& d, int at) {
    quint16 v = 0;
    if (at >= 0 && at + 2 <= d.size()) std::memcpy(&v, d.constData() + at, 2);
    return v;
}
void Put32(QByteArray& d, int at, quint32 v) { std::memcpy(d.data() + at, &v, 4); }
void Put16(QByteArray& d, int at, quint16 v) { std::memcpy(d.data() + at, &v, 2); }
quint32 AlignUp(quint32 v, quint32 a) { return (v + a - 1) & ~(a - 1); }

QByteArray Sha1Hex(const QByteArray& d) { return QCryptographicHash::hash(d, QCryptographicHash::Sha1).toHex(); }

bool IsYaz0(const QByteArray& d) { return d.size() >= 16 && d.startsWith("Yaz0"); }
bool IsSarc(const QByteArray& d) { return d.size() >= 0x20 && d.startsWith("SARC"); }

class Merger {
public:
    Merger(const Baseline& b, QStringList* r) : base(b), report(r) {}

    /// Applies `mod`'s changes (relative to the 3.0.4 baseline) onto `cur`. Returns true when `cur` changed.
    bool MergeArchive(const QString& path, Archive& cur, const Archive& mod) {
        bool changed = false;
        for (const auto& m : mod.members) {
            const QString key = path + QLatin1Char('/') + m.name;
            const QByteArray mh = Sha1Hex(m.data);
            const QByteArray b = base.sha1.value(key);
            if (!b.isEmpty() && b == mh) continue; // the mod did not change this member
            const int i = cur.find(m.name);
            if (i < 0) {
                cur.members.append(m);
                std::sort(cur.members.begin(), cur.members.end(), [](const auto& x, const auto& y) { return x.hash < y.hash; });
                Note(key, QStringLiteral("added"));
                changed = true;
                continue;
            }
            auto& c = cur.members[i];
            const QByteArray ch = Sha1Hex(c.data);
            if (ch == mh) continue;
            if (!b.isEmpty() && ch == b) { // the update did not change it: the mod's version as is
                c.data = m.data;
                Note(key, QStringLiteral("taken"));
                changed = true;
                continue;
            }
            // Both changed it: look inside (compressed archives), merge blocks (texture archives), or the mod's version wins.
            const bool cz = IsYaz0(c.data), mz = IsYaz0(m.data);
            const QByteArray ci = cz ? Yaz0Decode(c.data) : c.data;
            const QByteArray mi = mz ? Yaz0Decode(m.data) : m.data;
            if (IsSarc(ci) && IsSarc(mi)) {
                Archive ca, ma;
                if (ca.read(ci) && ma.read(mi)) {
                    if (MergeArchive(key, ca, ma)) {
                        const QByteArray out = ca.write();
                        c.data = cz ? Yaz0Encode(out) : out;
                        changed = true;
                    }
                    continue;
                }
            }
            const auto blocks = base.blocks.value(key);
            if (!cz && !mz && ci.size() == mi.size() && !blocks.isEmpty() && (ci.size() + kBlock - 1) / kBlock == blocks.size()) {
                QByteArray out = ci;
                int taken = 0;
                for (int j = 0; j < blocks.size(); ++j) {
                    const QByteArray mb = mi.mid(j * kBlock, kBlock);
                    if (Sha1Hex(mb).left(16) == blocks[j]) continue; // this block is as in 3.0.4: the update's version stays
                    std::memcpy(out.data() + j * kBlock, mb.constData(), mb.size());
                    ++taken;
                }
                c.data = out;
                Note(key, QStringLiteral("%1 of %2 blocks from the mod").arg(taken).arg(blocks.size()));
                changed = true;
                continue;
            }
            c.data = m.data;
            Note(key, QStringLiteral("mod's version (both changed)"));
            changed = true;
        }
        return changed;
    }

private:
    void Note(const QString& key, const QString& what) {
        if (report) report->append(key + QStringLiteral(": ") + what);
    }
    const Baseline& base;
    QStringList* report;
};

} // namespace

QByteArray Yaz0Decode(const QByteArray& in) {
    if (!IsYaz0(in)) return {};
    const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(in.constData() + 4));
    QByteArray out;
    out.reserve(static_cast<int>(size));
    const auto* s = reinterpret_cast<const uchar*>(in.constData());
    int src = 16;
    const int n = in.size();
    while (static_cast<quint32>(out.size()) < size && src < n) {
        const uchar flags = s[src++];
        for (int bit = 7; bit >= 0 && static_cast<quint32>(out.size()) < size; --bit) {
            if (flags >> bit & 1) {
                if (src >= n) return out;
                out.append(static_cast<char>(s[src++]));
            } else {
                if (src + 1 >= n) return out;
                const uchar b1 = s[src], b2 = s[src + 1];
                src += 2;
                const int dist = ((b1 & 0x0F) << 8 | b2) + 1;
                int len = b1 >> 4;
                if (len == 0) {
                    if (src >= n) return out;
                    len = s[src++] + 0x12;
                } else {
                    len += 2;
                }
                const int from = out.size() - dist;
                if (from < 0) return out;
                for (int k = 0; k < len; ++k) out.append(out.at(from + k));
            }
        }
    }
    return out;
}

QByteArray Yaz0Encode(const QByteArray& in) {
    // Greedy LZ77 with hash chains over the last 4 KiB (the format's window), lengths up to 0x111.
    const auto* s = reinterpret_cast<const uchar*>(in.constData());
    const int n = in.size();
    QByteArray out(16, '\0');
    std::memcpy(out.data(), "Yaz0", 4);
    qToBigEndian<quint32>(static_cast<quint32>(n), reinterpret_cast<uchar*>(out.data() + 4));
    out.reserve(16 + n + n / 8 + 16);
    constexpr int kWindow = 0x1000, kMax = 0x111, kChain = 48;
    std::vector<int> head(1 << 15, -1), prev(n > 0 ? n : 1, -1);
    const auto h3 = [&](int i) { return ((s[i] << 10) ^ (s[i + 1] << 5) ^ s[i + 2]) & 0x7FFF; };
    int i = 0;
    while (i < n) {
        const int flag_pos = out.size();
        out.append('\0');
        uchar flags = 0;
        for (int bit = 7; bit >= 0 && i < n; --bit) {
            int best_len = 0, best_dist = 0;
            if (i + 3 <= n) {
                int cand = head[h3(i)];
                for (int c = 0; c < kChain && cand >= 0 && i - cand <= kWindow; ++c, cand = prev[cand]) {
                    int l = 0;
                    const int lim = std::min(kMax, n - i);
                    while (l < lim && s[cand + l] == s[i + l]) ++l;
                    if (l > best_len) {
                        best_len = l;
                        best_dist = i - cand;
                        if (l == lim) break;
                    }
                }
            }
            const auto insert = [&](int p) {
                if (p + 3 <= n) {
                    const int h = h3(p);
                    prev[p] = head[h];
                    head[h] = p;
                }
            };
            if (best_len >= 3) {
                const int d = best_dist - 1;
                if (best_len < 0x12) {
                    out.append(static_cast<char>(((best_len - 2) << 4) | (d >> 8)));
                    out.append(static_cast<char>(d & 0xFF));
                } else {
                    out.append(static_cast<char>(d >> 8));
                    out.append(static_cast<char>(d & 0xFF));
                    out.append(static_cast<char>(best_len - 0x12));
                }
                for (int k = 0; k < best_len; ++k) insert(i + k);
                i += best_len;
            } else {
                flags |= 1 << bit;
                out.append(static_cast<char>(s[i]));
                insert(i);
                ++i;
            }
        }
        out[flag_pos] = static_cast<char>(flags);
    }
    return out;
}

bool Archive::read(const QByteArray& d) {
    members.clear();
    if (!IsSarc(d) || U16(d, 6) != 0xFEFF || std::memcmp(d.constData() + 0x14, "SFAT", 4) != 0) return false;
    const quint32 data_off = U32(d, 0x0C);
    const int count = U16(d, 0x1A);
    hash_key = U32(d, 0x1C);
    const int nodes = 0x20, names = nodes + count * 16 + 8;
    if (names > d.size() || std::memcmp(d.constData() + nodes + count * 16, "SFNT", 4) != 0) return false;
    for (int i = 0; i < count; ++i) {
        const int node = nodes + i * 16;
        Member m;
        m.hash = U32(d, node);
        const quint32 attr = U32(d, node + 4);
        const quint32 start = U32(d, node + 8), end = U32(d, node + 12);
        if (data_off + end > static_cast<quint32>(d.size()) || end < start) return false;
        m.named = (attr >> 24) == 1;
        m.name = m.named ? QString::fromUtf8(d.constData() + names + (attr & 0xFFFFFF) * 4)
                         : QStringLiteral("#%1").arg(m.hash, 8, 16, QLatin1Char('0'));
        m.data = d.mid(static_cast<int>(data_off + start), static_cast<int>(end - start));
        // Keep the member's alignment (texture archives need 4 KiB): the largest power of two dividing its offset, up to 8 KiB.
        const quint32 abs = data_off + start;
        quint32 a = 4;
        while (a < 0x2000 && abs % (a * 2) == 0) a *= 2;
        m.align = a;
        members.append(m);
    }
    return true;
}

QByteArray Archive::write() const {
    QByteArray names;
    QList<quint32> name_off;
    for (const auto& m : members) {
        name_off << static_cast<quint32>(names.size() / 4);
        if (m.named) {
            names += m.name.toUtf8();
            names.append('\0');
            while (names.size() % 4) names.append('\0');
        }
    }
    const int count = members.size();
    quint32 max_align = 0x100;
    for (const auto& m : members) max_align = std::max(max_align, m.align);
    const quint32 header_end = 0x14 + 0x0C + count * 16 + 8 + names.size();
    const quint32 data_off = AlignUp(header_end, max_align);
    QByteArray out(static_cast<int>(data_off), '\0');
    std::memcpy(out.data(), "SARC", 4);
    Put16(out, 4, 0x14);
    Put16(out, 6, 0xFEFF);
    Put32(out, 0x0C, data_off);
    Put16(out, 0x10, 0x0100);
    std::memcpy(out.data() + 0x14, "SFAT", 4);
    Put16(out, 0x18, 0x0C);
    Put16(out, 0x1A, static_cast<quint16>(count));
    Put32(out, 0x1C, hash_key);
    const int sfnt = 0x20 + count * 16;
    std::memcpy(out.data() + sfnt, "SFNT", 4);
    Put16(out, sfnt + 4, 8);
    std::memcpy(out.data() + sfnt + 8, names.constData(), names.size());
    quint32 pos = 0;
    QByteArray data;
    for (int i = 0; i < count; ++i) {
        const auto& m = members[i];
        const quint32 start = AlignUp(pos, m.align);
        data.append(QByteArray(static_cast<int>(start - pos), '\0'));
        data.append(m.data);
        pos = start + static_cast<quint32>(m.data.size());
        const int node = 0x20 + i * 16;
        Put32(out, node, m.hash);
        Put32(out, node + 4, m.named ? (0x01000000u | name_off[i]) : 0u);
        Put32(out, node + 8, start);
        Put32(out, node + 12, pos);
    }
    out.append(data);
    Put32(out, 8, static_cast<quint32>(out.size()));
    return out;
}

int Archive::find(const QString& name) const {
    for (int i = 0; i < members.size(); ++i)
        if (members[i].name == name) return i;
    return -1;
}

quint32 Archive::nameHash(const QString& name) const {
    quint32 h = 0;
    for (const char c : name.toUtf8()) h = h * hash_key + static_cast<quint32>(static_cast<signed char>(c));
    return h;
}

bool Baseline::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    while (!f.atEnd()) {
        const QList<QByteArray> parts = f.readLine().trimmed().split('\t');
        if (parts.size() < 2) continue;
        const QString key = QString::fromUtf8(parts[0]);
        sha1.insert(key, parts[1]);
        if (parts.size() > 2) {
            QList<QByteArray> b;
            for (int i = 0; i + 16 <= parts[2].size(); i += 16) b << parts[2].mid(i, 16);
            blocks.insert(key, b);
        }
    }
    return !sha1.isEmpty();
}

QStringList ReplacedArchives(const QStringList& mod_dirs) {
    QStringList out;
    for (const auto& mod : mod_dirs) {
        for (const auto& f : QDir(QDir(mod).filePath(QStringLiteral("romfs/UI/cmn"))).entryList({QStringLiteral("*.sarc")}, QDir::Files))
            if (!out.contains(f)) out << f;
    }
    return out;
}

int Merge(const QStringList& mod_dirs, const QString& vanilla_cmn_dir, const Baseline& baseline, const QString& out_dir, QString* error,
          QStringList* report) {
    const QStringList archives = ReplacedArchives(mod_dirs);
    int written = 0;
    Merger merger(baseline, report);
    for (const auto& name : archives) {
        QFile vf(QDir(vanilla_cmn_dir).filePath(name));
        Archive cur;
        if (!vf.open(QIODevice::ReadOnly) || !cur.read(vf.readAll())) {
            if (error) *error = QStringLiteral("cannot read the game's own %1").arg(name);
            return -1;
        }
        // Last-loading mod first, so the first one's changes are applied last and win.
        for (auto it = mod_dirs.crbegin(); it != mod_dirs.crend(); ++it) {
            QFile mf(QDir(*it).filePath(QStringLiteral("romfs/UI/cmn/") + name));
            if (!mf.exists()) continue;
            Archive mod;
            if (!mf.open(QIODevice::ReadOnly) || !mod.read(mf.readAll())) continue;
            if (report) report->append(QStringLiteral("%1 <- %2").arg(name, QFileInfo(*it).fileName()));
            merger.MergeArchive(name, cur, mod);
        }
        const QString target = QDir(out_dir).filePath(QStringLiteral("romfs/UI/cmn/") + name);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(cur.write()) < 0 || !out.commit()) {
            if (error) *error = QStringLiteral("cannot write %1").arg(target);
            return -1;
        }
        ++written;
    }
    return written;
}

} // namespace MenuMerge
