// SPDX-License-Identifier: GPL-3.0-or-later
#include "nsp_info.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include "build_runner.h"

NspInfo ParseNspInfo(const QByteArray& output) {
    NspInfo info;
    // The answer is one JSON line, but the exporter's libraries may print log or key-file warnings around it (newer prod.keys
    // files have key names this version does not know): take the last line that parses as a JSON object.
    QJsonDocument doc;
    const QList<QByteArray> lines = output.split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QByteArray l = it->trimmed();
        if (!l.startsWith('{')) continue;
        doc = QJsonDocument::fromJson(l);
        if (doc.isObject()) break;
    }
    if (!doc.isObject()) {
        const QString shown = QString::fromUtf8(output.trimmed()).left(200);
        info.error = shown.isEmpty() ? QStringLiteral("the exporter gave no answer")
                                     : QStringLiteral("the exporter gave no answer (it said: %1)").arg(shown);
        return info;
    }
    const QJsonObject o = doc.object();
    info.error = o.value("error").toString();
    bool ok = false;
    info.titleId = o.value("titleId").toString().toULongLong(&ok, 16);
    info.version = static_cast<quint32>(o.value("version").toDouble());
    info.aarch64 = o.value("aarch64").toBool();
    info.hasProgram = o.value("hasProgram").toBool();
    return info;
}

NspInfo ReadNspInfo(const QString& exporter, const QString& nsp, const QString& keys, int timeoutMs) {
    QProcess p;
    BuildRunner::DieWithParent(p);
    p.start(exporter, {"--info", nsp, "--keys", keys});
    if (!p.waitForStarted()) {
        NspInfo info;
        info.error = QStringLiteral("cannot start the exporter (%1)").arg(exporter);
        return info;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        NspInfo info;
        info.error = QStringLiteral("reading the file took too long");
        return info;
    }
    if (p.exitStatus() != QProcess::NormalExit) {
        NspInfo info;
        info.error = QStringLiteral("the exporter crashed while reading it: your keys are probably wrong for this file, or the file is damaged");
        return info;
    }
    return ParseNspInfo(p.readAllStandardOutput());
}
