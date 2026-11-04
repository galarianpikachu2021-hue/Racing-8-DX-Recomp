// SPDX-License-Identifier: GPL-3.0-or-later
#include "nsp_info.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include "build_runner.h"

NspInfo ParseNspInfo(const QByteArray& line) {
    NspInfo info;
    const auto doc = QJsonDocument::fromJson(line.trimmed());
    if (!doc.isObject()) {
        info.error = QStringLiteral("the exporter gave no answer");
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
