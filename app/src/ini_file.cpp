// SPDX-License-Identifier: GPL-3.0-or-later
#include "ini_file.h"

#include <QFile>
#include <QSaveFile>
#include <QTextStream>

bool IniFile::load(const QString& path) {
    file_path = path;
    lines.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        loaded = false;
        return false;
    }
    QTextStream in(&f);
    while (!in.atEnd()) {
        lines << in.readLine();
    }
    loaded = true;
    return true;
}

bool IniFile::save() const {
    QSaveFile f(file_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream out(&f);
    for (const auto& line : lines) {
        out << line << '\n';
    }
    return f.commit();
}

int IniFile::findSection(const QString& section) const {
    const QString header = QLatin1Char('[') + section + QLatin1Char(']');
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].trimmed() == header) {
            return i;
        }
    }
    return -1;
}

int IniFile::sectionEnd(int section_line) const {
    for (int i = section_line + 1; i < lines.size(); ++i) {
        if (lines[i].startsWith(QLatin1Char('['))) {
            return i;
        }
    }
    return static_cast<int>(lines.size());
}

int IniFile::findKey(const QString& section, const QString& key) const {
    const int s = findSection(section);
    if (s < 0) {
        return -1;
    }
    const QString prefix = key + QLatin1Char('=');
    const int end = sectionEnd(s);
    for (int i = s + 1; i < end; ++i) {
        if (lines[i].startsWith(prefix)) {
            return i;
        }
    }
    return -1;
}

QString IniFile::value(const QString& section, const QString& key, const QString& fallback) const {
    const int i = findKey(section, key);
    if (i < 0) {
        return fallback;
    }
    QString v = lines[i].mid(key.size() + 1);
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) {
        v = v.mid(1, v.size() - 2);
    }
    return v;
}

int IniFile::intValue(const QString& section, const QString& key, int fallback) const {
    bool ok = false;
    const int v = value(section, key).toInt(&ok);
    return ok ? v : fallback;
}

bool IniFile::boolValue(const QString& section, const QString& key, bool fallback) const {
    const QString v = value(section, key);
    if (v.isEmpty()) {
        return fallback;
    }
    return v == QLatin1String("true") || v == QLatin1String("1");
}

void IniFile::setRaw(const QString& section, const QString& key, const QString& value) {
    const QString line = key + QLatin1Char('=') + value;
    const int i = findKey(section, key);
    if (i >= 0) {
        lines[i] = line;
        return;
    }
    int s = findSection(section);
    if (s < 0) {
        if (!lines.isEmpty() && !lines.last().isEmpty()) {
            lines << QString{};
        }
        lines << QLatin1Char('[') + section + QLatin1Char(']');
        s = static_cast<int>(lines.size()) - 1;
    }
    // Insert after the section's last non-empty line, keeping the blank separator.
    int at = sectionEnd(s);
    while (at > s + 1 && lines[at - 1].trimmed().isEmpty()) {
        --at;
    }
    lines.insert(at, line);
}

void IniFile::setValue(const QString& section, const QString& key, const QString& value) {
    setRaw(section, key + QLatin1String("\\default"), QStringLiteral("false"));
    setRaw(section, key, value);
}

void IniFile::setInt(const QString& section, const QString& key, int value) {
    setValue(section, key, QString::number(value));
}

void IniFile::setBool(const QString& section, const QString& key, bool value) {
    setValue(section, key, value ? QStringLiteral("true") : QStringLiteral("false"));
}

void IniFile::setRawValue(const QString& section, const QString& key, const QString& value) {
    setRaw(section, key, value);
}

void IniFile::removeKeysWithPrefix(const QString& section, const QString& prefix) {
    const int s = findSection(section);
    if (s < 0) {
        return;
    }
    for (int i = sectionEnd(s) - 1; i > s; --i) {
        if (lines[i].startsWith(prefix)) {
            lines.removeAt(i);
        }
    }
}

QList<QPair<QString, QString>> IniFile::keysWithPrefix(const QString& section,
                                                        const QString& prefix) const {
    QList<QPair<QString, QString>> out;
    const int s = findSection(section);
    if (s < 0) {
        return out;
    }
    const int end = sectionEnd(s);
    for (int i = s + 1; i < end; ++i) {
        const auto& line = lines[i];
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || !line.startsWith(prefix)) {
            continue;
        }
        const QString key = line.left(eq);
        if (key.endsWith(QLatin1String("\\default"))) {
            continue;
        }
        QString v = line.mid(eq + 1);
        if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) {
            v = v.mid(1, v.size() - 2);
        }
        out.append({key, v});
    }
    return out;
}
