// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QPair>

// The in-game overlay's saved settings (user/config/kartrecomp_overlay.ini): flat `key=value` lines that the native renderer reads at
// start and rewrites on every change. The launcher edits the same file while the game is not running. Unknown lines are kept.
//
//   upscaler   off | fsr1 | passthrough | <plugin id>       quality   0 off, 1 100% (DLAA) ... 7 200% (index into the overlay's rows)
//   sharpness  0..3                                          jitter    0/1
//   framegen   off | builtin | <plugin id>                   hdr       0/1, hdr_paper 0..4, hdr_peak 0..4
//   present    0 mailbox, 1 fifo, 2 immediate                fps       0/1
class OverlaySettings {
public:
    bool load(const QString& path);
    bool save() const;
    QString path() const { return file_path; }

    QString value(const QString& key, const QString& fallback = {}) const;
    int intValue(const QString& key, int fallback) const;
    void setValue(const QString& key, const QString& value);
    void setInt(const QString& key, int value);

private:
    QString file_path;
    QList<QPair<QString, QString>> items;
};

// Plugin ids found in <root>/plugins: libkr_upscaler_<id>.so and libkr_framegen_<id>.so (Windows: .dll without the lib prefix).
QStringList FindPluginIds(const QString& root, const QString& kind);
