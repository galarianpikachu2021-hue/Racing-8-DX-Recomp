// SPDX-License-Identifier: GPL-3.0-or-later
#include "overlay_settings.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QTextStream>

bool OverlaySettings::load(const QString& path) {
    file_path = path;
    items.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0) items.append({line.left(eq), line.mid(eq + 1)});
    }
    return true;
}

bool OverlaySettings::save() const {
    if (file_path.isEmpty()) return false;
    QDir().mkpath(QFileInfo(file_path).absolutePath());
    QSaveFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&file);
    for (const auto& [key, value] : items) out << key << '=' << value << '\n';
    out.flush();
    return file.commit();
}

QString OverlaySettings::value(const QString& key, const QString& fallback) const {
    for (const auto& [k, v] : items) {
        if (k == key) return v;
    }
    return fallback;
}

int OverlaySettings::intValue(const QString& key, int fallback) const {
    bool ok = false;
    const int n = value(key).toInt(&ok);
    return ok ? n : fallback;
}

void OverlaySettings::setValue(const QString& key, const QString& value) {
    for (auto& [k, v] : items) {
        if (k == key) {
            v = value;
            return;
        }
    }
    items.append({key, value});
}

void OverlaySettings::setInt(const QString& key, int value) { setValue(key, QString::number(value)); }

QStringList FindPluginIds(const QString& root, const QString& kind) {
    QStringList ids;
    const QString prefix = QStringLiteral("libkr_%1_").arg(kind);
    for (const auto& name : QDir(QDir(root).filePath(QStringLiteral("plugins"))).entryList(QDir::Files, QDir::Name)) {
        if (name.startsWith(prefix) && name.endsWith(QLatin1String(".so"))) {
            const QString id = name.mid(prefix.size(), name.size() - prefix.size() - 3);
            if (id != QLatin1String("dump") && !ids.contains(id)) ids << id;
        }
    }
    return ids;
}
