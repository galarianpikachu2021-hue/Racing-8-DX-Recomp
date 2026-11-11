// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's controller mapping (Controllers tab), like Eden's. Per player: which device it plays on (a controller, the
// keyboard, or nobody); every Switch button and stick listed with what the player presses for it (the controller's own name
// for that input, e.g. "LB" or "Cross"); change one by pressing the new button; the controller's standard layout; an A/B + X/Y
// swap for controllers with the other face-button layout.
//
// Layouts belong to the controller, not the player: each controller (by SDL GUID) and the keyboard has its own profile in
// controller_profiles.ini beside the game's config (format in the fork's suyu_cmd/controller_profiles.h), and whichever player
// gets that controller, here or when the game hands controllers out, gets its layout.
//
// Controller questions go to the engine (`--input-capture`, it has the controller library), always asynchronously.
#pragma once

#include <QMap>
#include <QProcess>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

class ControllerMapper : public QWidget {
    Q_OBJECT
public:
    explicit ControllerMapper(QWidget* parent = nullptr);
    ~ControllerMapper() override;

    /// The game's config file, and how to run the engine for controller questions (its executable, working folder, environment).
    void setPaths(const QString& config, const QString& engine, const QString& work_dir, const QProcessEnvironment& env);
    void setLocked(bool locked); ///< while the game runs (it rewrites the config when it closes)

    // Exposed for tests.
    static int SdlScancode(int qt_key);
    static QString SdlKeyName(int scancode);
    /// "k:v,k:v" binding without guid and port (what a profile stores) / with this device's guid and port.
    static QString StripDevice(const QString& binding);
    static QString BindToDevice(const QString& stored, const QString& device);

protected:
    void showEvent(QShowEvent* e) override;

private:
    struct Device {
        QString label;  ///< shown in the combo
        QString param;  ///< "engine:sdl,guid:..,port:.." / "engine:keyboard" / "" (disconnected)
        bool present = true;
    };
    using Bindings = QMap<QString, QString>; ///< setting -> binding

    // The engine, asynchronously: a helper (`mk8d-engine --input-capture serve`) kept running while the tab is used, so a press is
    // caught the moment it happens. One question at a time; `done` gets the answer.
    void ask(QStringList args, int timeout_ms, std::function<void(const QString&)> done);
    void cancelQuestion();
    bool startHelper();
    void stopHelper();
    void helperOutput();
    void autoAssign(); ///< with automatic assignment on: controllers nobody has go to players on the keyboard, as the game does

    void refreshDevices();
    void showPlayer();
    void chooseDevice(int index);
    void capture(const QString& setting);
    void applyLayout(const Bindings& layout, bool remember);
    Bindings playerBindings(int player) const;
    QString deviceOf(int player) const; ///< the device param a player's bindings point at ("" = disconnected)
    QString profileKey(const QString& device) const;
    bool writePlayer(int player, const Bindings& values, const QString& connected = {});
    void rememberProfile(int player);
    QString describe(const QString& binding, const QString& device) const;
    void loadNames(const QString& device, std::function<void()> then);
    void setBusy(bool busy, const QString& text = {});
    QString profilesPath() const;

    QString config_path, engine_path, engine_dir;
    QProcessEnvironment engine_env;
    bool locked = false, busy = false, devices_loaded = false;
    QProcess* helper = nullptr;
    bool helper_ready = false;
    QByteArray helper_buffer;
    QString pending_line;                     ///< question waiting for the helper to say "ready"
    bool question = false;                    ///< a question is out
    QTimer* question_timer = nullptr;
    std::function<void(const QString&)> question_done;

    QComboBox* player = nullptr;
    QComboBox* device = nullptr;
    QCheckBox* auto_assign = nullptr;
    QPushButton *refresh = nullptr, *standard = nullptr, *swap = nullptr, *cancel = nullptr;
    QLabel* status = nullptr;
    std::map<QString, QLabel*> bound;
    std::vector<QPushButton*> row_buttons;
    QList<Device> devices;       ///< controllers found
    QList<Device> combo_devices; ///< what the device combo lists now
    QMap<QString, QList<QPair<QString, QString>>> names; ///< profile key -> (binding, printed name)
    QMap<QString, Bindings> defaults;                    ///< profile key -> standard layout
};
