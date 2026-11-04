// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_validate.h"

#include <QDir>
#include <QFileInfo>

#include "keys_check.h"

namespace {
QString Hex(quint64 id) { return QStringLiteral("%1").arg(id, 16, 16, QLatin1Char('0')); }
} // namespace

QString ValidateBase(const NspInfo& info) {
    if (!info.error.isEmpty()) return QStringLiteral("Cannot read this file: %1.").arg(info.error);
    if (info.titleId != kBaseTitleId)
        return QStringLiteral("This is not the Mario Kart 8 Deluxe base game (title id %1, expected %2).").arg(Hex(info.titleId), Hex(kBaseTitleId));
    return {};
}

QString ValidateUpdate(const NspInfo& info) {
    if (!info.error.isEmpty()) return QStringLiteral("Cannot read this file: %1.").arg(info.error);
    if (info.titleId != kUpdateTitleId)
        return QStringLiteral("This is not the Mario Kart 8 Deluxe update (title id %1, expected %2).").arg(Hex(info.titleId), Hex(kUpdateTitleId));
    if (!info.aarch64)
        return QStringLiteral("This update is a 32-bit build (3.0.3 or older). Only update 3.0.4 can be recompiled.");
    if (info.version != kSupportedUpdateVersion)
        return QStringLiteral("This is update version %1; only update 3.0.4 (version %2) is supported.").arg(info.version).arg(kSupportedUpdateVersion);
    return {};
}

QString ValidateFirmware(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists()) return QStringLiteral("The firmware was not found.");
    if (fi.isFile()) return path.endsWith(".zip", Qt::CaseInsensitive) ? QString() : QStringLiteral("The firmware must be a folder of .nca files or a .zip.");
    if (QDir(path).entryList({"*.nca", "*.cnmt.nca"}, QDir::Files).isEmpty()) return QStringLiteral("This folder holds no firmware (.nca files).");
    return {};
}

QString ValidateDlc(const QStringList& files) {
    for (const QString& f : files) {
        if (!f.endsWith(".nsp", Qt::CaseInsensitive) || !QFileInfo(f).isFile())
            return QStringLiteral("%1 is not an .nsp file.").arg(QFileInfo(f).fileName());
    }
    return {};
}

QString ValidateInputs(const SetupInputs& in, const std::function<NspInfo(const QString&)>& reader) {
    if (const auto p = CheckKeys(in.keys); p != KeyProblem::None) return KeyProblemText(p);
    if (const QString why = ValidateBase(reader(in.base)); !why.isEmpty()) return why;
    if (const QString why = ValidateUpdate(reader(in.update)); !why.isEmpty()) return why;
    if (const QString why = ValidateFirmware(in.firmware); !why.isEmpty()) return why;
    return ValidateDlc(in.dlc);
}
