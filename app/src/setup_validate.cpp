// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_validate.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

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
        return QStringLiteral("This update is a 32-bit build (3.0.3 or older; files named \"v4.0.0\" are often this one). Only updates 3.0.4 "
                              "and 4.0.1 can be recompiled.");
    if (UpdateName(info.version).isEmpty())
        return QStringLiteral("This is update version %1; only updates 3.0.4 (version %2) and 4.0.1 (version %3) are supported.")
            .arg(info.version).arg(kSupportedUpdates[0].version).arg(kSupportedUpdates[1].version);
    return {};
}

QString UpdateName(quint32 version) {
    for (const auto& u : kSupportedUpdates)
        if (u.version == version) return QString::fromLatin1(u.name);
    return {};
}

QString HotListFor(const QString& list304, const QString& updateName) {
    if (updateName.isEmpty() || updateName == QLatin1String("3.0.4")) return list304;
    QString out = list304;
    return out.replace(QStringLiteral("mk8d-3.0.4"), QStringLiteral("mk8d-") + updateName);
}

QString FirmwareTooNew(const QString& path) {
    // Firmware newer than 22.5 makes the game's test boot crash (GitHub issues #2, #8; 23.0.1 reported). Its version is not readable
    // without the keys, but firmware folders and zips are normally named after it ("Firmware.23.0.1", "Firmware 22.5.0.zip").
    static const QRegularExpression re(QStringLiteral("(?:^|[^0-9])([0-9]{1,2})\\.([0-9]{1,2})\\.([0-9]{1,2})(?:[^0-9]|$)"));
    const auto m = re.match(QFileInfo(path).fileName());
    if (!m.hasMatch()) return {};
    const int major = m.captured(1).toInt(), minor = m.captured(2).toInt();
    if (major > kNewestFirmwareMajor || (major == kNewestFirmwareMajor && minor > kNewestFirmwareMinor))
        return QStringLiteral("Firmware %1 is newer than this build supports (up to %2.%3.x): the game crashes while it starts. Use firmware %2.%3.0 or older.")
            .arg(m.captured(1) + "." + m.captured(2) + "." + m.captured(3)).arg(kNewestFirmwareMajor).arg(kNewestFirmwareMinor);
    return {};
}

QString ValidateFirmware(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists()) return QStringLiteral("The firmware was not found.");
    if (const QString why = FirmwareTooNew(path); !why.isEmpty()) return why;
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
    if (const auto p = CheckKeys(in.keys); p != KeyProblem::None) return KeyProblemText(p, MissingKeyName(in.keys));
    if (const QString why = ValidateBase(reader(in.base)); !why.isEmpty()) return why;
    if (const QString why = ValidateUpdate(reader(in.update)); !why.isEmpty()) return why;
    if (const QString why = ValidateFirmware(in.firmware); !why.isEmpty()) return why;
    return ValidateDlc(in.dlc);
}
