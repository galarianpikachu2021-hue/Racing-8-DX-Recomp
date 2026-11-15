// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's Multiplayer tab: play the game's local wireless mode with other people over the internet, through a multiplayer
// room (a relay server, compatible with the suyu/yuzu-family room servers). The engine joins a room, or hosts one on this machine
// and joins it (docs/NATIVE_ENGINE.md, "Multiplayer rooms").
#pragma once

#include <QProcessEnvironment>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class MultiplayerPage : public QWidget {
    Q_OBJECT
public:
    explicit MultiplayerPage(QWidget* parent = nullptr);
    /// The engine's switches for the chosen mode (nothing when multiplayer is off).
    void apply(QProcessEnvironment& env) const;
    /// What is wrong with the settings (empty when they are usable or multiplayer is off).
    QString problem() const;

private:
    void save() const;
    void refresh();

    QComboBox* mode = nullptr;
    QLineEdit* nickname = nullptr;
    QLineEdit* address = nullptr;
    QSpinBox* port = nullptr;
    QLineEdit* password = nullptr;
    QSpinBox* members = nullptr;
    QLabel* hint = nullptr;
};
