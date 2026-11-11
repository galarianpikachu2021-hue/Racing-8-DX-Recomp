// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's controller mapping (Controllers tab), like Eden's: per player a device, every Switch button and stick bound to a
// key or a controller input by pressing it, the controller's standard layout in one click, and an A/B + X/Y swap for controllers
// with the other face-button layout (Xbox, PlayStation). Controller inputs are read by the engine (`--input-capture`), which has the
// controller library; keys are read here. Everything is written to the game's config (sdl2-config.ini, [Controls]).
#pragma once

#include <QProcess>
#include <QWidget>
#include <map>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

class ControllerMapper : public QWidget {
    Q_OBJECT
public:
    explicit ControllerMapper(QWidget* parent = nullptr);

    /// The game's config file, and how to run the engine for input capture (its executable, working folder and environment).
    void setPaths(const QString& config, const QString& engine, const QString& work_dir, const QProcessEnvironment& env);
    void setLocked(bool locked); ///< while the game runs (it rewrites the config when it closes)
    void reload();

    /// "Button 1", "Axis 2 +", "D-pad up", "Key C" ... for a stored binding.
    static QString Describe(const QString& param);

private:
    QString key(const QString& setting) const; ///< player_<n>_<setting>
    QString value(const QString& setting) const;
    bool write(const QMap<QString, QString>& values);
    void refreshDevices();
    void capture(const QString& setting);
    void showPlayer();
    QString runEngine(const QStringList& args, int timeout_ms) const;

    QString config_path, engine_path, engine_dir;
    QProcessEnvironment engine_env;
    bool locked = false;
    QComboBox* player = nullptr;
    QComboBox* device = nullptr;
    QCheckBox* connected = nullptr;
    QPushButton *refresh = nullptr, *automap = nullptr, *swap = nullptr;
    QLabel* status = nullptr;
    QWidget* grid_host = nullptr;
    std::map<QString, QLabel*> bound;
    std::map<QString, QPushButton*> set_buttons;
    QStringList device_params; ///< parallel to the device combo ("" = keyboard)
};
