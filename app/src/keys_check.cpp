// SPDX-License-Identifier: GPL-3.0-or-later
#include "keys_check.h"

#include <QFile>
#include <QSet>

namespace {
const char* const kDerivationKeys[] = {"aes_kek_generation_source", "key_area_key_application_source", "aes_key_generation_source", "titlekek_source"};

struct Scan {
    bool opened = false;
    QSet<QByteArray> names;
    bool master = false;
};
Scan ScanKeys(const QString& path) {
    Scan scan;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return scan;
    scan.opened = true;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        const int eq = line.indexOf('=');
        if (eq <= 0) continue;
        const QByteArray name = line.left(eq).trimmed().toLower();
        if (line.mid(eq + 1).trimmed().isEmpty()) continue; // a name with no value is not a key
        scan.names.insert(name);
        if (name.startsWith("master_key_") && name.size() == 13) scan.master = true;
    }
    return scan;
}
} // namespace

KeyProblem CheckKeys(const QString& path) {
    const Scan scan = ScanKeys(path);
    if (!scan.opened) return KeyProblem::NoFile;
    if (!scan.names.contains("header_key")) return KeyProblem::MissingHeaderKey;
    for (const char* k : kDerivationKeys)
        if (!scan.names.contains(k)) return KeyProblem::MissingDerivationKey;
    return scan.master ? KeyProblem::None : KeyProblem::MissingMasterKey;
}

QString MissingKeyName(const QString& path) {
    const Scan scan = ScanKeys(path);
    if (!scan.opened) return {};
    if (!scan.names.contains("header_key")) return QStringLiteral("header_key");
    for (const char* k : kDerivationKeys)
        if (!scan.names.contains(k)) return QString::fromLatin1(k);
    return scan.master ? QString() : QStringLiteral("master_key_XX");
}

QString KeyProblemText(KeyProblem p, const QString& name) {
    switch (p) {
    case KeyProblem::None: return {};
    case KeyProblem::NoFile: return QStringLiteral("prod.keys could not be read.");
    case KeyProblem::MissingHeaderKey: return QStringLiteral("This prod.keys has no header_key; dump it again from your own console.");
    case KeyProblem::MissingMasterKey: return QStringLiteral("This prod.keys has no master_key_XX entries; dump it again from your own console.");
    case KeyProblem::MissingDerivationKey:
        return QStringLiteral("This prod.keys is missing the entry %1; dump it again from your own console.").arg(name.isEmpty() ? QStringLiteral("(a key-derivation source)") : name);
    }
    return {};
}
