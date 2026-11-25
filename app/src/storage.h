// SPDX-License-Identifier: GPL-3.0-or-later
// Where the big files live (game files, built game, mods, saves, build files): the standard folders, or a folder the user chose on the
// Storage tab, e.g. on an SD card. The choice is one line (the folder) in <config>/storage.txt; the settings themselves always stay in
// the config folder. DataLayout::Default() follows it, so Setup, Play and every other page do too.
#pragma once

#include <QList>
#include <QString>
#include <atomic>
#include <functional>

#include "setup_paths.h"

namespace Storage {

/// The chosen folder ("" = the standard folders).
QString Chosen();
/// Remembers the choice ("" = the standard folders again). "" or the reason it could not be saved.
QString SetChosen(const QString& folder);
/// The chosen folder is not there (an SD card that is out).
bool Missing();
/// This run uses the standard folders whatever is chosen.
void IgnoreChoice();

/// The layout inside a chosen folder (<folder>/data/kartrecomp, <folder>/cache/kartrecomp); "" = the standard one.
DataLayout LayoutFor(const QString& folder);

struct Volume {
    QString name;   ///< label or device
    QString root;   ///< mount point
    qint64 free = 0, total = 0;
};
/// The drives that can be chosen besides the one with the standard folders.
QList<Volume> OtherVolumes();
/// "" when the game can live in `folder` (it is created if needed), or the reason it cannot: not writable, FAT32, programs not allowed.
QString CheckFolder(const QString& folder);
/// Bytes of all files below `dir` (links are not followed).
qint64 SizeOf(const QString& dir, const std::atomic<bool>* cancel = nullptr);
qint64 FreeBytes(const QString& dir);
/// Game data is there already (a folder used before).
bool HoldsData(const DataLayout& layout);

/// The engine's config keeps some folders as full paths (its NAND, SD card, mods, dump and TAS folders): those inside `from`'s data folder
/// are changed to `to`'s. False when the file could not be saved. No config yet is fine.
bool Repoint(const QString& configFile, const DataLayout& from, const DataLayout& to);

/// Moves the data and cache folders of `from` to `to` and then calls `commit` (which saves the new choice): everything is copied first,
/// the originals are removed only after `commit` succeeded, and a failure or cancel removes the copy again. On the same drive the folders
/// are renamed instead. "" or the reason it failed; `leftover` names an original that could not be removed afterwards (the move itself is done then).
QString Move(const DataLayout& from, const DataLayout& to, const std::function<QString()>& commit,
             const std::function<void(qint64 done, qint64 total, const QString& file)>& progress, const std::atomic<bool>* cancel, QString* leftover);

} // namespace Storage
