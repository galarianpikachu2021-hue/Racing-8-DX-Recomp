// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

// Several mods can replace the same message file (UI/<lang>/message.sarc: names of tracks, cups, drivers, karts). The game takes the whole
// file from one of them, so the others' texts vanish. This merges them: starting from the game's own file, every text a mod changed is
// applied, the mod that loads first winning when two change the same text. The result is written as one more mod that loads before all.
namespace TextMerge {

constexpr const char* kMergedModName = "!Merged text"; ///< sorts before every other mod folder, so it wins

/// `mod_dirs` are the enabled mods' folders in load order (the first wins). Writes <out_dir>/romfs/UI/<lang>/message.sarc for every
/// language that at least two of them provide. `vanilla_ui_dir` holds the game's own UI/<lang>/message.sarc.
/// Returns the number of languages written, or -1 with `error`. `changed` gets the number of texts taken from mods.
int Merge(const QStringList& mod_dirs, const QString& vanilla_ui_dir, const QString& out_dir, QString* error, int* changed);

/// True when at least two of the mods provide a message file for the same language.
bool HasConflict(const QStringList& mod_dirs);

} // namespace TextMerge
