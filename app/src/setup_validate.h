// SPDX-License-Identifier: GPL-3.0-or-later
// The Setup page's rules for the user's files. Every function returns "" when fine, else the reason shown next to the file.
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#include "nsp_info.h"

constexpr quint64 kBaseTitleId = 0x0100152000022000ULL;
constexpr quint64 kUpdateTitleId = 0x0100152000022800ULL;
constexpr quint32 kSupportedUpdateVersion = 20u << 16; // 3.0.4, the version everything was developed on
// The 64-bit updates that can be recompiled. 4.0.1 (version 23 << 16) was added in v0.2.4. ("v4.0.0" files are often the 32-bit 3.0.3.)
struct SupportedUpdate {
    quint32 version;
    const char* name;
};
inline constexpr SupportedUpdate kSupportedUpdates[] = {{20u << 16, "3.0.4"}, {23u << 16, "4.0.1"}};
QString UpdateName(quint32 version); // "3.0.4", "4.0.1", or "" when not supported
// The hot block list for an update: the 3.0.4 list's path with the folder renamed (data/mk8d-<name>/hot_blocks.txt).
QString HotListFor(const QString& list304, const QString& updateName);

struct SetupInputs {
    QString base, update, keys, firmware;
    QStringList dlc;
    QStringList patches; ///< optional game changes built in (kartrecomp-export --patch), e.g. "split60"
    /// "Decrypted files" instead of the NSPs, keys and firmware: the game's code folder (main, main.npdm, rtld, sdk, subsdk0, with
    /// the update applied) and its data (a folder of the RomFS files, or a romfs.bin image). Used when `exefs` is set.
    QString exefs, romfs;
    bool decrypted() const { return !exefs.isEmpty(); }
};

/// Which supported update an ExeFS folder holds ("3.0.4", "4.0.1"), from the build id of its main; "" when it is neither.
QString DecryptedUpdateName(const QString& exefsDir);
QString ValidateExefs(const QString& dir);
QString ValidateRomfs(const QString& path);

QString ValidateBase(const NspInfo& info);
QString ValidateUpdate(const NspInfo& info);
QString ValidateFirmware(const QString& path); // a folder with .nca files, or a .zip
QString ValidateDlc(const QStringList& files);
/// All of it, in the order the user fills the page in; `reader` answers questions about an NSP (ReadNspInfo in the app, a fake in tests).
QString ValidateInputs(const SetupInputs& in, const std::function<NspInfo(const QString&)>& reader);
