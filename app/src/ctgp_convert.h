// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <functional>

// Turns a CTGP Deluxe pack (the user's own zip) into an ADDITIVE data mod for the static, no-JIT build.
//
// The pack's code is a 32-bit Skyline plugin made for game version 3.0.3 and cannot run in this build. Its tracks are data, though, and are
// copied unchanged into their own folders (Course/CT_xx, pBGM_CT_xx, ym_CoursePict_CT_xx): not one of the game's own courses is replaced.
// The in-game "Extra cup" hook (see docs/CTGP.md) runs them. Their names are added to the game's own message files as NEW labels
// (21000 + track number, +100 for the title form); the pack's own message and UI files are never copied.
namespace CtgpConvert {

// Minimal SARC (little endian) and MSBT (UTF-16 LE) containers.
struct Sarc {
    QList<QString> order;
    QMap<QString, QByteArray> files;
    bool read(const QByteArray& data);
    QByteArray write() const;
};

struct Msbt {
    QByteArray bom_and_header;           ///< bytes 8..0x0F of the header
    QList<QPair<QByteArray, QByteArray>> sections; ///< tag, body
    QHash<QString, quint32> labels;
    QList<QByteArray> strings;
    QList<QList<QPair<QString, quint32>>> groups; ///< LBL1 hash groups, in file order
    bool lbl_dirty = false;
    bool read(const QByteArray& data);
    QByteArray write() const;
    /// Adds a new label (it must not exist) with plain text.
    void add_label(const QString& name, const QString& text);
    /// Same with the string's raw UTF-16 bytes (control tags and the terminator included).
    void add_label_raw(const QString& name, const QByteArray& raw);
};

struct Options {
    QString cups_list_path; ///< where to write the cup list the in-game "Extra cup" menu reads (extra_cups.txt); empty = not written
};

using Progress = std::function<bool(int done, int total, const QString& what)>;

// `vanilla_ui_dir` holds UI/<lang>/message.sarc dumped from the user's game (no mods). Writes <out_dir>/romfs/... Returns false with `error`.
bool Convert(const QString& zip_path, const QString& vanilla_ui_dir, const QString& out_dir, const Options& options, QString* error,
             const Progress& progress);

} // namespace CtgpConvert
