// SPDX-License-Identifier: GPL-3.0-or-later
#include "keys_check.h"

#include <QFile>
#include <QSet>

KeyProblem CheckKeys(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return KeyProblem::NoFile;
    QSet<QByteArray> names;
    bool master = false;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        const int eq = line.indexOf('=');
        if (eq <= 0) continue;
        const QByteArray name = line.left(eq).trimmed().toLower();
        if (line.mid(eq + 1).trimmed().isEmpty()) continue; // a name with no value is not a key
        names.insert(name);
        if (name.startsWith("master_key_") && name.size() == 13) master = true;
    }
    if (!names.contains("header_key")) return KeyProblem::MissingHeaderKey;
    for (const char* k : {"aes_kek_generation_source", "key_area_key_application_source", "aes_key_generation_source", "titlekek_source"})
        if (!names.contains(k)) return KeyProblem::MissingDerivationKey;
    return master ? KeyProblem::None : KeyProblem::MissingMasterKey;
}

QString KeyProblemText(KeyProblem p) {
    switch (p) {
    case KeyProblem::None: return {};
    case KeyProblem::NoFile: return QStringLiteral("prod.keys could not be read.");
    case KeyProblem::MissingHeaderKey: return QStringLiteral("This prod.keys has no header_key; dump it again from your own console.");
    case KeyProblem::MissingMasterKey: return QStringLiteral("This prod.keys has no master_key_XX entries; dump it again from your own console.");
    case KeyProblem::MissingDerivationKey: return QStringLiteral("This prod.keys is missing key-derivation entries (aes_kek_generation_source, key_area_key_application_source, aes_key_generation_source or titlekek_source).");
    }
    return {};
}
