// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP Deluxe's course and cup tables, read from the user's own CTGPDX.nro at import (the plugin builds them in code at start-up).
// A small ARM32 interpreter replays the two table-building functions of the plugin with their allocation and list calls stubbed;
// nothing of the plugin runs natively, and no code is translated (interpretation only). Output: the text the engine reads
// (core/arm/recomp/ctgp_dx.cpp, docs/CTGP_PORT.md).
#pragma once

#include <QByteArray>
#include <QString>

struct CtgpTablesResult {
    bool ok = false;
    QString error;   ///< why not, for the user
    QByteArray text; ///< ctgp_tables.txt
    int courses = 0, cups = 0;
};

/// `nro` = the bytes of romfs/skyline/plugins/CTGPDX.nro from the CTGP DX pack (version 1.1.1).
CtgpTablesResult ReadCtgpTables(const QByteArray& nro);
