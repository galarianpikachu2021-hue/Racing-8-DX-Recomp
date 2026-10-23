// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QList>
#include <QStringList>
#include <functional>

// Add-ons for the game package, shown in the launcher's Mods tab. Today there is one: CTGP Deluxe, imported from the user's own zip.
// Nothing from a pack is ever part of this repository; everything lands in the game package (gitignored `local/`).
//
//   <root>/mods/<title id>/CTGP DX Cups/romfs/...   the tracks in their own folders (see ctgp_convert.h); no course of the game is replaced
namespace ModAddon {

struct PackInfo {
    bool valid = false;
    QString error;
    QString title_id;
    int romfs_files = 0;
    quint64 romfs_bytes = 0;
    int exefs_files = 0;
    int plugin_files = 0;
    quint64 plugin_bytes = 0;
    bool code_is_32bit = false; ///< from the pack's main.npdm (flags byte at 0xC: bit 0 set = 64-bit)
    bool code_known = false;
    quint64 zip_bytes = 0;
};

enum class State { NotImported, Enabled, Disabled };

constexpr const char* kMk8dTitleId = "0100152000022000";

PackInfo AnalyzeCtgpPack(const QString& zip_path);

State CtgpState(const QString& root);
bool SetCtgpEnabled(const QString& root, bool enabled, QString* error);
bool RemoveCtgp(const QString& root, QString* error);

// Why the pack's code cannot be recompiled for this build yet; empty when it can (never today).
QString CodeRecompBlocker(const PackInfo& info);

// ---- ordinary MK8DX mods (data mods in the usual layouts: romfs/, exefs/, an Atmosphere atmosphere/contents/<id>/ tree, or a yuzu/suyu
// load/<id>/<mod>/ tree), kept as <root>/mods/<title id>/<name>/{romfs,exefs}. A folder whose name ends in ".disabled" is switched off.
struct ModEntry {
    QString name;        ///< folder name without ".disabled"
    QString path;
    bool enabled = true;
    bool has_romfs = false;
    bool has_exefs = false;   ///< exefs patches / code: the static build cannot apply these
    int files = 0;
    quint64 bytes = 0;
};

QList<ModEntry> ListMods(const QString& root);
bool ImportModFolder(const QString& source, const QString& root, QString* error, const std::function<bool(int, int, const QString&)>& progress);
bool ImportModZip(const QString& zip_path, const QString& root, QString* error, const std::function<bool(int, int, const QString&)>& progress);
bool SetModEnabled(const QString& root, const QString& name, bool enabled, QString* error);
bool RemoveMod(const QString& root, const QString& name, QString* error);

/// Enabled mod folders (CTGP DX included, the merged-text mod excluded) in the order the game loads them: the first wins a conflict.
QStringList EnabledModDirs(const QString& root);
/// True when two enabled mods replace the same message file, so a merged copy is needed.
bool NeedsTextMerge(const QString& root);
bool HasTextConflict(const QString& root);
/// Rebuilds (or removes, when no longer needed) the "!Merged text" mod. `vanilla_ui_dir` is only read when a merge is needed.
bool UpdateTextMerge(const QString& root, const QString& vanilla_ui_dir, QString* error);
QString VanillaUiDir(const QString& root);   ///< where the game's own message files are cached

QString ModDir(const QString& root);       ///< mods/<tid>/CTGPDX (or CTGPDX.disabled)
QString AddonDir(const QString& root);

} // namespace ModAddon
