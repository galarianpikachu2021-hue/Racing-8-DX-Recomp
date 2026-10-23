// SPDX-License-Identifier: GPL-3.0-or-later
#include "text_merge.h"

#include "ctgp_convert.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

namespace TextMerge {

namespace {

QString MessagePath(const QString& base, const QString& lang) { return QDir(base).filePath(QStringLiteral("UI/%1/message.sarc").arg(lang)); }

QStringList Languages(const QStringList& mod_dirs) {
    QHash<QString, int> count;
    for (const auto& mod : mod_dirs) {
        const QDir ui(QDir(mod).filePath(QStringLiteral("romfs/UI")));
        for (const auto& lang : ui.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFileInfo::exists(ui.filePath(lang + QStringLiteral("/message.sarc")))) ++count[lang];
        }
    }
    QStringList out;
    for (auto it = count.constBegin(); it != count.constEnd(); ++it) {
        if (it.value() >= 2) out << it.key();
    }
    out.sort();
    return out;
}

bool ReadSarc(const QString& path, CtgpConvert::Sarc& out) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && out.read(f.readAll());
}

} // namespace

bool HasConflict(const QStringList& mod_dirs) { return !Languages(mod_dirs).isEmpty(); }

int Merge(const QStringList& mod_dirs, const QString& vanilla_ui_dir, const QString& out_dir, QString* error, int* changed) {
    int taken = 0;
    int written = 0;
    const auto fail = [&](const QString& why) {
        if (error) *error = why;
        return -1;
    };
    for (const QString& lang : Languages(mod_dirs)) {
        CtgpConvert::Sarc base;
        if (!ReadSarc(MessagePath(vanilla_ui_dir, lang), base)) return fail(QStringLiteral("Cannot read the game's message file for %1").arg(lang));
        CtgpConvert::Sarc result = base;
        QHash<QString, CtgpConvert::Msbt> parsed; // result members that are message tables
        for (auto it = base.files.constBegin(); it != base.files.constEnd(); ++it) {
            CtgpConvert::Msbt m;
            if (it.key().endsWith(QLatin1String(".msbt")) && m.read(it.value())) parsed.insert(it.key(), m);
        }
        // Lowest priority first, so the mod that loads first has the last word.
        for (int i = mod_dirs.size() - 1; i >= 0; --i) {
            CtgpConvert::Sarc mod;
            const QString path = MessagePath(QDir(mod_dirs[i]).filePath(QStringLiteral("romfs")), lang);
            if (!QFileInfo::exists(path)) continue;
            if (!ReadSarc(path, mod)) return fail(QStringLiteral("%1 has an unreadable message file for %2").arg(QFileInfo(mod_dirs[i]).fileName(), lang));
            for (auto it = mod.files.constBegin(); it != mod.files.constEnd(); ++it) {
                const QString& name = it.key();
                const auto theirs_it = base.files.constFind(name);
                if (theirs_it == base.files.constEnd() || !parsed.contains(name)) {
                    if (theirs_it == base.files.constEnd() || *theirs_it != it.value()) result.files[name] = it.value(); // new or non-text member
                    continue;
                }
                CtgpConvert::Msbt vanilla, theirs;
                if (!vanilla.read(*theirs_it) || !theirs.read(it.value())) continue;
                CtgpConvert::Msbt& into = parsed[name];
                // Labels a mod ADDED (not in the game's file) are carried over as new labels, in label order so the result is stable.
                QStringList added;
                for (auto l = theirs.labels.constBegin(); l != theirs.labels.constEnd(); ++l) {
                    if (!vanilla.labels.contains(l.key())) added << l.key();
                }
                added.sort();
                for (const QString& label : added) {
                    const QByteArray text = theirs.strings[static_cast<int>(theirs.labels.value(label))];
                    if (into.labels.contains(label)) {
                        into.strings[static_cast<int>(into.labels.value(label))] = text; // a mod loading earlier already added it
                    } else {
                        into.add_label_raw(label, text);
                    }
                    ++taken;
                }
                for (auto l = theirs.labels.constBegin(); l != theirs.labels.constEnd(); ++l) {
                    const auto v = vanilla.labels.constFind(l.key());
                    if (v == vanilla.labels.constEnd()) continue; // handled above
                    if (theirs.strings[static_cast<int>(l.value())] == vanilla.strings[static_cast<int>(v.value())]) continue;
                    into.strings[static_cast<int>(into.labels.value(l.key()))] = theirs.strings[static_cast<int>(l.value())];
                    ++taken;
                }
            }
        }
        for (auto it = parsed.constBegin(); it != parsed.constEnd(); ++it) result.files[it.key()] = it.value().write();
        const QString target = MessagePath(QDir(out_dir).filePath(QStringLiteral("romfs")), lang);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(result.write()) < 0) return fail(QStringLiteral("Cannot write %1").arg(target));
        ++written;
    }
    if (changed) *changed = taken;
    return written;
}

} // namespace TextMerge
