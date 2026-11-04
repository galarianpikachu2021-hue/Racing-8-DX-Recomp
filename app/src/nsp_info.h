// SPDX-License-Identifier: GPL-3.0-or-later
// What the exporter (kartrecomp-export --info) reports about an NSP: the launcher has no crypto of its own, so the same code that will
// open the file for the real export answers "which title, which version, is the program 64-bit".
#pragma once

#include <QString>
#include <QtGlobal>

struct NspInfo {
    quint64 titleId = 0;
    quint32 version = 0;
    bool aarch64 = false;
    bool hasProgram = false;
    QString error; // empty when the file was read
};

/// Parses the single JSON line printed by `kartrecomp-export --info`; anything else gives an error.
NspInfo ParseNspInfo(const QByteArray& line);

/// Runs `<exporter> --info <nsp> --keys <keys>` (blocking, killed after timeoutMs).
NspInfo ReadNspInfo(const QString& exporter, const QString& nsp, const QString& keys, int timeoutMs = 120000);
