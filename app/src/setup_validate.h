// SPDX-License-Identifier: GPL-3.0-or-later
// The Setup page's rules for the user's files. Every function returns "" when fine, else the reason shown next to the file.
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#include "nsp_info.h"

constexpr quint64 kBaseTitleId = 0x0100152000022000ULL;
constexpr quint64 kUpdateTitleId = 0x0100152000022800ULL;
constexpr quint32 kSupportedUpdateVersion = 20u << 16; // 3.0.4: the only AArch64 build the recompiler accepts besides 4.0.1

struct SetupInputs {
    QString base, update, keys, firmware;
    QStringList dlc;
};

QString ValidateBase(const NspInfo& info);
QString ValidateUpdate(const NspInfo& info);
// Newest firmware known to work (22.5.x). Newer firmware (23.0.1 reported) crashes the game's test boot.
inline constexpr int kNewestFirmwareMajor = 22;
inline constexpr int kNewestFirmwareMinor = 5;
QString FirmwareTooNew(const QString& path);   // a reason when the folder or zip name says a newer firmware, else empty
QString ValidateFirmware(const QString& path); // a folder with .nca files, or a .zip
QString ValidateDlc(const QStringList& files);
/// All of it, in the order the user fills the page in; `reader` answers questions about an NSP (ReadNspInfo in the app, a fake in tests).
QString ValidateInputs(const SetupInputs& in, const std::function<NspInfo(const QString&)>& reader);
