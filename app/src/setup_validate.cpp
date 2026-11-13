// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_validate.h"

#include <QDir>
#include <QFile>
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

namespace {
// The first 8 bytes of main's build id (NSO header, offset 0x40), as hex.
QString MainBuildId(const QString& exefsDir) {
    QFile f(QDir(exefsDir).filePath("main"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray head = f.read(0x60);
    if (head.size() < 0x48 || !head.startsWith("NSO0")) return {};
    return QString::fromLatin1(head.mid(0x40, 8).toHex());
}
} // namespace

QString DecryptedUpdateName(const QString& exefsDir) {
    const QString id = MainBuildId(exefsDir);
    if (id == QLatin1String("2452c49bc26ec159")) return QStringLiteral("3.0.4");
    if (id == QLatin1String("158c0c7399aabd7b")) return QStringLiteral("4.0.1");
    return {};
}

QString ValidateExefs(const QString& dir) {
    if (!QFileInfo(dir).isDir()) return QStringLiteral("The game code folder was not found.");
    for (const char* name : {"main", "main.npdm", "rtld", "sdk"})
        if (!QFileInfo(QDir(dir).filePath(name)).isFile())
            return QStringLiteral("This folder has no file named %1: pick the ExeFS folder (main, main.npdm, rtld, sdk, subsdk0).").arg(name);
    const QString id = MainBuildId(dir);
    if (id.isEmpty()) return QStringLiteral("The file main is not decrypted game code (it does not start with NSO0).");
    if (DecryptedUpdateName(dir).isEmpty())
        return QStringLiteral("This is not the code of update 3.0.4 or 4.0.1 (build id %1). Dump the game with the update applied.").arg(id);
    return {};
}

QString ValidateRomfs(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists()) return QStringLiteral("The game data was not found.");
    if (fi.isFile()) return fi.size() > (1LL << 30) ? QString() : QStringLiteral("This file is too small to be the game's RomFS image.");
    if (QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) return QStringLiteral("This folder is empty.");
    return {};
}

QString ValidateInputs(const SetupInputs& in, const std::function<NspInfo(const QString&)>& reader) {
    if (in.decrypted()) {
        if (const QString why = ValidateExefs(in.exefs); !why.isEmpty()) return why;
        if (const QString why = ValidateRomfs(in.romfs); !why.isEmpty()) return why;
        // Keys and firmware are optional here, and only useful together (the firmware files are encrypted).
        if (in.keys.isEmpty() != in.firmware.isEmpty()) return QStringLiteral("Give both keys and firmware, or neither.");
        if (in.keys.isEmpty()) return {};
        if (const auto p = CheckKeys(in.keys); p != KeyProblem::None) return KeyProblemText(p, MissingKeyName(in.keys));
        return ValidateFirmware(in.firmware);
    }
    if (const auto p = CheckKeys(in.keys); p != KeyProblem::None) return KeyProblemText(p, MissingKeyName(in.keys));
    if (const QString why = ValidateBase(reader(in.base)); !why.isEmpty()) return why;
    if (const QString why = ValidateUpdate(reader(in.update)); !why.isEmpty()) return why;
    if (const QString why = ValidateFirmware(in.firmware); !why.isEmpty()) return why;
    return ValidateDlc(in.dlc);
}
