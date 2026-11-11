// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// Mods that replace the game's menu archives (UI/cmn/*.sarc: character icons, kart parts, menu layouts) ship the whole archive as it
// was in the update they were made for, nearly always 3.0.x. A newer update (4.0.1) added and changed members of those archives, and
// the game stalls at its first loading screen on the old ones. This merge starts from the installed update's own archive and applies
// only what each mod changed relative to 3.0.4 (known from shipped hashes, data/mk8d-3.0.4/ui_cmn_hashes.txt; no game data), member by
// member, inside compressed (Yaz0) archives recursively, and block by block inside texture archives both changed. The result is one
// more mod that loads before all others.
namespace MenuMerge {

constexpr const char* kMergedModName = "!Merged menus"; ///< sorts before every other mod folder, so it wins

QByteArray Yaz0Decode(const QByteArray& in);
QByteArray Yaz0Encode(const QByteArray& in);

/// A SARC archive kept close to its original layout: every member's name hash, whether its name is stored, and its alignment.
struct Archive {
    struct Member {
        quint32 hash = 0;
        bool named = false;
        QString name;  ///< the stored name, or "#xxxxxxxx" (the hash) for unnamed members
        QByteArray data;
        quint32 align = 4;
    };
    quint32 hash_key = 0x65;
    QList<Member> members;
    bool read(const QByteArray& data);
    QByteArray write() const;
    int find(const QString& name) const;
    quint32 nameHash(const QString& name) const;
};

/// The 3.0.4 baseline: SHA-1 of every member (path "menu.sarc/mn_x.szs/#hash"), plus 4 KiB block hash prefixes for texture archives.
struct Baseline {
    QHash<QString, QByteArray> sha1;            ///< hex
    QHash<QString, QList<QByteArray>> blocks;   ///< hex prefixes (16 chars) per 4 KiB block
    bool load(const QString& path);
};

/// Merges the menu archives of `mod_dirs` (enabled mods, load order: the first wins) onto the game's own archives in `vanilla_cmn_dir`
/// (UI/cmn of the installed update). Writes <out_dir>/romfs/UI/cmn/<archive>.sarc for every archive a mod replaces. Returns the number
/// of archives written, or -1 with `error`. `report` gets one line per member taken from a mod.
int Merge(const QStringList& mod_dirs, const QString& vanilla_cmn_dir, const Baseline& baseline, const QString& out_dir, QString* error,
          QStringList* report = nullptr);

/// The menu archives (file names) the enabled mods replace.
QStringList ReplacedArchives(const QStringList& mod_dirs);

} // namespace MenuMerge
