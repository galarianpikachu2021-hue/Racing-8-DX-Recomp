// SPDX-License-Identifier: GPL-3.0-or-later
// Splits a generated recompiler unit (recompiled_<mod>_<n>.c) into smaller pieces so several compile in parallel: a port of
// tools/recomp/split_units.py. The cut is at block-function boundaries (every block declares the blocks it calls), the unit's
// guard array becomes one global defined in piece 0 and extern elsewhere, other initialised globals are defined in piece 0 only,
// and the block table goes in the last piece with a declaration of every block.
#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

namespace UnitSplit {

struct Piece {
    QString name; // file name inside the output folder, e.g. recompiled_main_0_p0.c or recompiled_main_0_hot.c
    bool hot;     // compiled with full optimisation
};

/// Block function names (blk_<module>_<16 hex digits>), separated by whitespace.
QSet<QByteArray> ParseHotList(const QByteArray& text);

/// Writes <stem>_p0.c .. <stem>_p<parts-1>.c holding the cold blocks in order, and <stem>_hot.c with the blocks named in `hot`
/// (only when `hot` is not empty). Returns the pieces in compile order, or an empty list with `error` set.
QList<Piece> Split(const QString& unitPath, const QString& outDir, const QSet<QByteArray>& hot, int parts, QString* error);

} // namespace UnitSplit
