// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

// Line-preserving editor for suyu's sdl2-config.ini.
//
// QSettings is not used on purpose: suyu keys contain backslashes ("key\default"),
// which QSettings reads as groups, and QSettings rewrites the whole file in its own
// order. This edits values in place and leaves every other line untouched.
class IniFile {
public:
    bool load(const QString& path);
    bool save() const;
    bool isLoaded() const { return loaded; }

    QString value(const QString& section, const QString& key, const QString& fallback = {}) const;
    int intValue(const QString& section, const QString& key, int fallback) const;
    bool boolValue(const QString& section, const QString& key, bool fallback) const;

    // Sets key=value and key\default=false, so suyu keeps the value instead of
    // resetting it to its built-in default. Appends the section or key if missing.
    void setValue(const QString& section, const QString& key, const QString& value);
    void setInt(const QString& section, const QString& key, int value);
    void setBool(const QString& section, const QString& key, bool value);
    // Sets key=value exactly (no "key\\default" line): for array bookkeeping such as "size" and "N\\title_id".
    void setRawValue(const QString& section, const QString& key, const QString& value);
    // Removes every key of the section that starts with prefix (and its "\\default" line).
    void removeKeysWithPrefix(const QString& section, const QString& prefix);

    // All keys of a section starting with prefix, in file order (key, value).
    QList<QPair<QString, QString>> keysWithPrefix(const QString& section, const QString& prefix) const;

private:
    int findSection(const QString& section) const;
    int sectionEnd(int section_line) const;
    int findKey(const QString& section, const QString& key) const;
    void setRaw(const QString& section, const QString& key, const QString& value);

    QString file_path;
    QStringList lines;
    bool loaded = false;
};
