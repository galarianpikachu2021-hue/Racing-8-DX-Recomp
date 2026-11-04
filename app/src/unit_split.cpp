// SPDX-License-Identifier: GPL-3.0-or-later
#include "unit_split.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace UnitSplit {

QSet<QByteArray> ParseHotList(const QByteArray& text) {
    QSet<QByteArray> out;
    for (const QByteArray& name : text.simplified().split(' ')) {
        if (!name.isEmpty()) out.insert(name);
    }
    return out;
}

namespace {
bool Fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}
} // namespace

QList<Piece> Split(const QString& unitPath, const QString& outDir, const QSet<QByteArray>& hot, int parts, QString* error) {
    QFile in(unitPath);
    if (!in.open(QIODevice::ReadOnly)) {
        Fail(error, QStringLiteral("cannot read %1").arg(unitPath));
        return {};
    }
    const QList<QByteArray> lines = in.readAll().split('\n');
    in.close();
    qsizetype first = -1, table = -1;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (first < 0 && lines[i].startsWith("void blk_")) first = i;
        if (table < 0 && lines[i].startsWith("const struct _recomp_ent _seg")) table = i;
    }
    if (first < 0 || table < first) {
        Fail(error, QStringLiteral("%1 is not a generated unit (no block functions or no block table)").arg(unitPath));
        return {};
    }
    const QList<QByteArray> head = lines.mid(0, first), body = lines.mid(first, table - first), tail = lines.mid(table);
    static const QRegularExpression seen_re(QStringLiteral("^static uint32_t recomp_gg_seen\\[(\\d+)\\];$"));
    qsizetype seen_line = -1;
    int seen_matches = 0;
    QString count;
    for (qsizetype i = 0; i < head.size(); ++i) {
        const auto m = seen_re.match(QString::fromLatin1(head[i]));
        if (m.hasMatch()) {
            seen_line = i;
            count = m.captured(1);
            ++seen_matches;
        }
    }
    if (seen_matches != 1) {
        Fail(error, QStringLiteral("unexpected header in %1: no single recomp_gg_seen array").arg(unitPath));
        return {};
    }
    const QString stem = QFileInfo(unitPath).fileName().section(QLatin1Char('.'), 0, 0);
    QString ident = stem;
    ident.replace(QRegularExpression(QStringLiteral("\\W")), QStringLiteral("_"));
    const QString glob_name = QStringLiteral("recomp_gg_seen_") + ident;

    struct Func { QByteArray name; QList<QByteArray> lines; };
    QList<Func> funcs;
    static const QRegularExpression name_re(QStringLiteral("^void (blk_\\w+)\\("));
    for (qsizetype i = 0; i < body.size();) {
        if (!body[i].startsWith("void blk_")) { ++i; continue; }
        qsizetype j = i + 1;
        while (j < body.size() && !body[j].startsWith("void blk_")) ++j;
        const auto m = name_re.match(QString::fromLatin1(body[i]));
        funcs.append({m.captured(1).toLatin1(), body.mid(i, j - i)});
        i = j;
    }
    QList<const Func*> cold, hot_funcs;
    for (const Func& f : funcs) (hot.contains(f.name) ? hot_funcs : cold).append(&f);
    const qsizetype per = std::max<qsizetype>(1, (cold.size() + parts - 1) / parts);
    struct Group { QString name; bool hot; QList<const Func*> funcs; };
    QList<Group> groups;
    for (int k = 0; k < parts; ++k) groups.append({QStringLiteral("%1_p%2.c").arg(stem).arg(k), false, cold.mid(k * per, per)});
    if (!hot.isEmpty()) groups.append({QStringLiteral("%1_hot.c").arg(stem), true, hot_funcs});

    QDir().mkpath(outDir);
    static const QRegularExpression init_re(QStringLiteral("^(int|unsigned|uint32_t|uint64_t) (\\w+)=[^;]*;$"));
    QList<Piece> pieces;
    for (qsizetype k = 0; k < groups.size(); ++k) {
        QList<QByteArray> h = head;
        const QString decl = (k == 0 ? QStringLiteral("uint32_t %1[%2];") : QStringLiteral("extern uint32_t %1[%2];")).arg(glob_name, count);
        h[seen_line] = (decl + QStringLiteral("\n#define recomp_gg_seen ") + glob_name).toLatin1();
        if (k > 0) {
            for (QByteArray& l : h) {
                const auto m = init_re.match(QString::fromLatin1(l));
                if (m.hasMatch()) l = QStringLiteral("extern %1 %2;").arg(m.captured(1), m.captured(2)).toLatin1();
            }
        }
        QList<QByteArray> text = h;
        for (const Func* f : groups[k].funcs) text += f->lines;
        if (k == groups.size() - 1) {
            for (const Func& f : funcs) text.append("void " + f.name + "(GuestContext*);");
            text += tail;
        }
        QFile out(QDir(outDir).filePath(groups[k].name));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Fail(error, QStringLiteral("cannot write %1").arg(out.fileName()));
            return {};
        }
        out.write(text.isEmpty() ? QByteArray() : text.first());
        for (qsizetype i = 1; i < text.size(); ++i) {
            out.write("\n", 1);
            out.write(text[i]);
        }
        pieces.append({groups[k].name, groups[k].hot});
    }
    return pieces;
}

} // namespace UnitSplit
