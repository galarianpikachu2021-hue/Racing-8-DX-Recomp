// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's Online play tab: play the game's Wireless Play mode with other people over the internet, through a multiplayer
// room (a relay server, compatible with the suyu/yuzu-family room servers). The engine joins a room, or hosts one on this machine
// and joins it (docs/NATIVE_ENGINE.md, "Multiplayer rooms"). Everything is off until "Enable online play" is ticked.
#pragma once

#include <QProcessEnvironment>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class MultiplayerPage : public QWidget {
    Q_OBJECT
public:
    explicit MultiplayerPage(QWidget* parent = nullptr);
    /// The engine's switches for the chosen mode (nothing when online play is off).
    void apply(QProcessEnvironment& env) const;
    /// What is wrong with the settings (empty when they are usable or online play is off).
    QString problem() const;

private:
    enum Mode { Host = 0, Join = 1 };
    void save() const;
    void refresh();
    int mode() const;
    /// What the others type in to join (address:port of this computer).
    QString joinDetails() const;

    /// The room nickname: the field, or the profile name when it is empty.
    QString roomNickname() const;

    QLineEdit* profile_name = nullptr;
    QCheckBox* enabled = nullptr;
    QGroupBox* settings = nullptr;
    QButtonGroup* modes = nullptr;
    QLineEdit* nickname = nullptr;
    QLineEdit* room_name = nullptr;
    QSpinBox* members = nullptr;
    QLineEdit* address = nullptr;
    QSpinBox* port = nullptr;
    QLineEdit* password = nullptr;
    QLabel* host_addresses = nullptr;
    QPushButton* copy_details = nullptr;
    QLabel* hint = nullptr;
};
