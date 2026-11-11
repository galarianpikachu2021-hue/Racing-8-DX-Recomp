// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller_mapper.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "ini_file.h"

namespace {
const char* const kSection = "Controls";

struct Input {
    const char* setting; ///< the config's name (Settings::NativeButton / NativeAnalog mapping)
    const char* label;
    bool stick;
};
const Input kInputs[] = {
    {"button_a", "A", false},        {"button_b", "B", false},          {"button_x", "X", false},
    {"button_y", "Y", false},        {"button_l", "L", false},          {"button_r", "R", false},
    {"button_zl", "ZL", false},      {"button_zr", "ZR", false},        {"button_plus", "+ (Plus)", false},
    {"button_minus", "- (Minus)", false}, {"button_dup", "D-pad up", false}, {"button_ddown", "D-pad down", false},
    {"button_dleft", "D-pad left", false}, {"button_dright", "D-pad right", false},
    {"button_lstick", "Left stick press", false}, {"button_rstick", "Right stick press", false},
    {"button_home", "Home", false},  {"button_screenshot", "Capture", false},
    {"lstick", "Left stick", true},  {"rstick", "Right stick", true},
};

QString Unquote(QString v) {
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) v = v.mid(1, v.size() - 2);
    return v;
}

QMap<QString, QString> Params(const QString& param) {
    QMap<QString, QString> out;
    for (const auto& part : param.split(QLatin1Char(','))) {
        const int c = part.indexOf(QLatin1Char(':'));
        if (c > 0) out.insert(part.left(c), part.mid(c + 1));
    }
    return out;
}

/// The game reads keys as SDL scancodes (USB HID usage ids), not Qt key codes.
struct KeyName {
    int qt;
    int sdl;
    const char* name;
};
const KeyName kKeys[] = {
    {Qt::Key_Return, 40, "Enter"}, {Qt::Key_Enter, 40, "Enter"}, {Qt::Key_Backspace, 42, "Backspace"}, {Qt::Key_Tab, 43, "Tab"},
    {Qt::Key_Space, 44, "Space"}, {Qt::Key_Minus, 45, "-"}, {Qt::Key_Equal, 46, "="}, {Qt::Key_BracketLeft, 47, "["},
    {Qt::Key_BracketRight, 48, "]"}, {Qt::Key_Backslash, 49, "\\"}, {Qt::Key_Semicolon, 51, ";"}, {Qt::Key_Apostrophe, 52, "'"},
    {Qt::Key_QuoteLeft, 53, "`"}, {Qt::Key_Comma, 54, ","}, {Qt::Key_Period, 55, "."}, {Qt::Key_Slash, 56, "/"},
    {Qt::Key_CapsLock, 57, "Caps Lock"}, {Qt::Key_Insert, 73, "Insert"}, {Qt::Key_Home, 74, "Home"}, {Qt::Key_PageUp, 75, "Page Up"},
    {Qt::Key_Delete, 76, "Delete"}, {Qt::Key_End, 77, "End"}, {Qt::Key_PageDown, 78, "Page Down"}, {Qt::Key_Right, 79, "Right"},
    {Qt::Key_Left, 80, "Left"}, {Qt::Key_Down, 81, "Down"}, {Qt::Key_Up, 82, "Up"}, {Qt::Key_Control, 224, "Ctrl"},
    {Qt::Key_Shift, 225, "Shift"}, {Qt::Key_Alt, 226, "Alt"}, {Qt::Key_Meta, 227, "Super"}, {Qt::Key_AltGr, 230, "Right Alt"},
};

/// Qt key -> SDL scancode; 0 = not supported (F10 opens the menu and F11 is fullscreen in game, so they are not offered).
int SdlScancode(int qt) {
    if (qt >= Qt::Key_A && qt <= Qt::Key_Z) return 4 + (qt - Qt::Key_A);
    if (qt >= Qt::Key_1 && qt <= Qt::Key_9) return 30 + (qt - Qt::Key_1);
    if (qt == Qt::Key_0) return 39;
    if (qt >= Qt::Key_F1 && qt <= Qt::Key_F9) return 58 + (qt - Qt::Key_F1);
    if (qt == Qt::Key_F12) return 69;
    for (const KeyName& k : kKeys)
        if (k.qt == qt) return k.sdl;
    return 0;
}

QString SdlKeyName(int sdl) {
    if (sdl >= 4 && sdl <= 29) return QString(QLatin1Char(static_cast<char>('A' + sdl - 4)));
    if (sdl >= 30 && sdl <= 38) return QString::number(sdl - 29);
    if (sdl == 39) return QStringLiteral("0");
    if (sdl >= 58 && sdl <= 69) return QStringLiteral("F%1").arg(sdl - 57);
    for (const KeyName& k : kKeys)
        if (k.sdl == sdl) return QString::fromLatin1(k.name);
    if (sdl == 228) return QStringLiteral("Right Ctrl");
    if (sdl == 229) return QStringLiteral("Right Shift");
    return QStringLiteral("#%1").arg(sdl);
}

/// Waits for one key press (Escape cancels). Returns the Qt key code, or 0.
int AskForKey(QWidget* parent, const QString& what) {
    class KeyDialog : public QDialog {
    public:
        int key = 0;
        void keyPressEvent(QKeyEvent* e) override {
            if (e->key() != Qt::Key_Escape) key = e->key();
            accept();
        }
    };
    KeyDialog d;
    d.setParent(parent, Qt::Dialog);
    d.setWindowTitle(QObject::tr("Press a key"));
    auto* l = new QVBoxLayout(&d);
    l->addWidget(new QLabel(QObject::tr("Press the key for %1 (Escape cancels).").arg(what)));
    d.exec();
    return d.key;
}
} // namespace

ControllerMapper::ControllerMapper(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    auto* note = new QLabel(tr("Choose a player and the controller (or keyboard) it uses, then press Set next to a button and press the "
                               "key or controller button you want for it. Auto map gives a controller its standard layout. "
                               "Swap A/B and X/Y is for controllers with the other face-button layout (Xbox, PlayStation): the button "
                               "labelled A then acts as A. Close the game before changing anything."));
    note->setWordWrap(true);
    outer->addWidget(note);

    auto* top = new QHBoxLayout;
    player = new QComboBox;
    for (int i = 1; i <= 4; ++i) player->addItem(tr("Player %1").arg(i));
    connected = new QCheckBox(tr("Connected"));
    device = new QComboBox;
    device->setMinimumWidth(220);
    refresh = new QPushButton(tr("Find controllers"));
    automap = new QPushButton(tr("Auto map"));
    swap = new QPushButton(tr("Swap A/B and X/Y"));
    top->addWidget(player);
    top->addWidget(connected);
    top->addWidget(new QLabel(tr("Device:")));
    top->addWidget(device, 1);
    top->addWidget(refresh);
    top->addWidget(automap);
    top->addWidget(swap);
    outer->addLayout(top);

    grid_host = new QWidget;
    auto* grid = new QGridLayout(grid_host);
    int row = 0, col = 0;
    for (const Input& in : kInputs) {
        auto* name = new QLabel(tr(in.label));
        auto* binding = new QLabel;
        binding->setMinimumWidth(150);
        auto* set = new QPushButton(tr("Set"));
        auto* clear = new QPushButton(tr("Clear"));
        const QString setting = QString::fromLatin1(in.setting);
        bound[setting] = binding;
        set_buttons[setting] = set;
        grid->addWidget(name, row, col * 4);
        grid->addWidget(binding, row, col * 4 + 1);
        grid->addWidget(set, row, col * 4 + 2);
        grid->addWidget(clear, row, col * 4 + 3);
        connect(set, &QPushButton::clicked, this, [this, setting] { capture(setting); });
        connect(clear, &QPushButton::clicked, this, [this, setting] { write({{setting, QString()}}); });
        if (++col == 2) {
            col = 0;
            ++row;
        }
    }
    outer->addWidget(grid_host);
    status = new QLabel;
    status->setWordWrap(true);
    outer->addWidget(status);
    outer->addStretch();

    connect(player, &QComboBox::currentIndexChanged, this, [this] { showPlayer(); });
    connect(connected, &QCheckBox::toggled, this, [this](bool on) {
        IniFile ini;
        if (!ini.load(config_path)) return;
        if (locked) return;
        ini.setBool(QLatin1String(kSection), QStringLiteral("player_%1_connected").arg(player->currentIndex()), on);
        ini.save();
    });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshDevices(); });
    connect(automap, &QPushButton::clicked, this, [this] {
        const QString dev = device_params.value(device->currentIndex());
        if (dev.isEmpty()) {
            status->setText(tr("Auto map is for controllers; for the keyboard, set each key."));
            return;
        }
        QMap<QString, QString> values;
        for (const auto& line : runEngine({QStringLiteral("--input-capture"), QStringLiteral("defaults"), dev}, 15000).split(QLatin1Char('\n'))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) values.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
        }
        if (values.isEmpty()) {
            status->setText(tr("The engine gave no layout for this controller (is it still connected?)."));
            return;
        }
        if (write(values)) status->setText(tr("Mapped %1 inputs to %2.").arg(values.size()).arg(device->currentText()));
    });
    connect(swap, &QPushButton::clicked, this, [this] {
        write({{QStringLiteral("button_a"), value(QStringLiteral("button_b"))}, {QStringLiteral("button_b"), value(QStringLiteral("button_a"))},
               {QStringLiteral("button_x"), value(QStringLiteral("button_y"))}, {QStringLiteral("button_y"), value(QStringLiteral("button_x"))}});
        status->setText(tr("A/B and X/Y swapped for player %1.").arg(player->currentIndex() + 1));
    });
    setLocked(false);
}

void ControllerMapper::setPaths(const QString& config, const QString& engine, const QString& work_dir, const QProcessEnvironment& env) {
    config_path = config;
    engine_path = engine;
    engine_dir = work_dir;
    engine_env = env;
    reload();
}

void ControllerMapper::reload() {
    device->clear();
    device_params.clear();
    device->addItem(tr("Keyboard"));
    device_params << QString();
    showPlayer();
}

QString ControllerMapper::key(const QString& setting) const {
    return QStringLiteral("player_%1_%2").arg(player->currentIndex()).arg(setting);
}

QString ControllerMapper::value(const QString& setting) const {
    IniFile ini;
    if (!ini.load(config_path)) return {};
    return Unquote(ini.value(QLatin1String(kSection), key(setting)));
}

bool ControllerMapper::write(const QMap<QString, QString>& values) {
    if (locked) return false;
    IniFile ini;
    if (!ini.load(config_path)) {
        status->setText(tr("Start the game once so it creates its config, then map the controls."));
        return false;
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        ini.setValue(QLatin1String(kSection), key(it.key()), QLatin1Char('"') + it.value() + QLatin1Char('"'));
    // Mapped by hand: the game must not hand the controllers out again at start.
    ini.setValue(QLatin1String(kSection), QStringLiteral("auto_assign_controllers"), QStringLiteral("false"));
    if (!ini.save()) {
        status->setText(tr("Could not write %1").arg(QDir::toNativeSeparators(config_path)));
        return false;
    }
    showPlayer();
    return true;
}

void ControllerMapper::showPlayer() {
    IniFile ini;
    const bool ok = ini.load(config_path);
    {
        const QSignalBlocker b(connected);
        connected->setChecked(ok && ini.boolValue(QLatin1String(kSection), QStringLiteral("player_%1_connected").arg(player->currentIndex()),
                                                   player->currentIndex() == 0));
    }
    for (const Input& in : kInputs) {
        const QString setting = QString::fromLatin1(in.setting);
        bound[setting]->setText(ok ? Describe(Unquote(ini.value(QLatin1String(kSection), key(setting)))) : QString());
    }
}

QString ControllerMapper::Describe(const QString& param) {
    if (param.isEmpty()) return tr("(none)");
    const auto p = Params(param);
    const QString engine = p.value(QStringLiteral("engine"));
    if (engine == QLatin1String("keyboard")) {
        return tr("Key %1").arg(SdlKeyName(p.value(QStringLiteral("code")).toInt()));
    }
    if (p.contains(QStringLiteral("axis_x"))) return tr("Axes %1/%2").arg(p.value(QStringLiteral("axis_x")), p.value(QStringLiteral("axis_y")));
    if (p.contains(QStringLiteral("hat"))) return tr("D-pad %1").arg(p.value(QStringLiteral("direction")));
    if (p.contains(QStringLiteral("axis")))
        return tr("Axis %1 %2").arg(p.value(QStringLiteral("axis")), p.value(QStringLiteral("invert"), QStringLiteral("+")));
    if (p.contains(QStringLiteral("button"))) return tr("Button %1").arg(p.value(QStringLiteral("button")));
    return engine;
}

QString ControllerMapper::runEngine(const QStringList& args, int timeout_ms) const {
    if (engine_path.isEmpty() || !QFileInfo::exists(engine_path)) return {};
    QProcess p;
    p.setProcessEnvironment(engine_env);
    p.setWorkingDirectory(engine_dir);
    p.start(engine_path, args);
    if (!p.waitForStarted(5000)) return {};
    QEventLoop loop; // keep the window painting while the engine waits for a button
    QTimer::singleShot(timeout_ms, &loop, &QEventLoop::quit);
    connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (p.state() != QProcess::NotRunning) {
        p.kill();
        p.waitForFinished(2000);
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void ControllerMapper::refreshDevices() {
    const QString before = device->currentText();
    device->clear();
    device_params.clear();
    device->addItem(tr("Keyboard"));
    device_params << QString();
    status->setText(tr("Looking for controllers..."));
    QApplication::processEvents();
    for (const auto& line : runEngine({QStringLiteral("--input-capture"), QStringLiteral("devices")}, 15000).split(QLatin1Char('\n'))) {
        const auto p = Params(line.trimmed());
        if (p.value(QStringLiteral("engine")) != QLatin1String("sdl")) continue; // controllers only (keyboard is listed above)
        device->addItem(p.value(QStringLiteral("display"), tr("Controller")));
        device_params << line.trimmed();
    }
    const int i = device->findText(before);
    if (i >= 0) device->setCurrentIndex(i);
    status->setText(device->count() > 1 ? tr("%n controller(s) found.", nullptr, device->count() - 1)
                                        : tr("No controller found (is it connected and switched on?)."));
}

void ControllerMapper::capture(const QString& setting) {
    const Input* in = nullptr;
    for (const Input& i : kInputs)
        if (setting == QLatin1String(i.setting)) in = &i;
    if (!in) return;
    const QString dev = device_params.value(device->currentIndex());
    if (dev.isEmpty()) {
        if (in->stick) {
            status->setText(tr("Sticks on the keyboard use four keys; the game's defaults (W/A/S/D and the arrow keys) are kept."));
            return;
        }
        const int k = AskForKey(this, tr(in->label));
        if (!k) return;
        const int code = SdlScancode(k);
        if (!code) {
            status->setText(tr("That key can't be used (F10 and F11 are the game's menu and fullscreen keys)."));
            return;
        }
        write({{setting, QStringLiteral("engine:keyboard,code:%1,toggle:0").arg(code)}});
        return;
    }
    status->setText(in->stick ? tr("Move the stick for %1 on the controller (10 s)...").arg(tr(in->label))
                              : tr("Press the button for %1 on the controller (10 s)...").arg(tr(in->label)));
    setEnabled(false);
    QApplication::processEvents();
    const QString got = runEngine({QStringLiteral("--input-capture"), in->stick ? QStringLiteral("stick") : QStringLiteral("button"),
                                   QStringLiteral("10")},
                                  15000);
    setEnabled(!locked);
    if (got.isEmpty()) {
        status->setText(tr("Nothing pressed: %1 is unchanged.").arg(tr(in->label)));
        return;
    }
    if (write({{setting, got}})) status->setText(tr("%1 set to %2.").arg(tr(in->label), Describe(got)));
}

void ControllerMapper::setLocked(bool l) {
    locked = l;
    setEnabled(!l);
    if (l && status) status->setText(tr("The game is running: controls can be changed after it closes."));
}
