// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller_mapper.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include "ini_file.h"

namespace {
const QString kSection = QStringLiteral("Controls");

const char* const kButtons[] = {"button_a",     "button_b",      "button_x",     "button_y",      "button_lstick",  "button_rstick",
                                "button_l",     "button_r",      "button_zl",    "button_zr",     "button_plus",    "button_minus",
                                "button_dleft", "button_dup",    "button_dright", "button_ddown", "button_slleft",  "button_srleft",
                                "button_home",  "button_screenshot", "button_slright", "button_srright"};
const char* const kSticks[] = {"lstick", "rstick"};

/// The rows: the Switch input and its setting.
struct Input {
    const char* setting;
    const char* label;
    bool stick;
};
const Input kInputs[] = {
    {"button_a", "A", false},          {"button_b", "B", false},          {"button_x", "X", false},
    {"button_y", "Y", false},          {"button_l", "L", false},          {"button_r", "R", false},
    {"button_zl", "ZL", false},        {"button_zr", "ZR", false},        {"button_plus", "+ (Plus)", false},
    {"button_minus", "- (Minus)", false}, {"button_dup", "D-pad up", false}, {"button_ddown", "D-pad down", false},
    {"button_dleft", "D-pad left", false}, {"button_dright", "D-pad right", false},
    {"button_lstick", "Left stick press", false}, {"button_rstick", "Right stick press", false},
    {"button_home", "Home", false},    {"button_screenshot", "Capture", false},
    {"lstick", "Left stick", true},    {"rstick", "Right stick", true},
};

QMap<QString, QString> Params(const QString& param) {
    QMap<QString, QString> out;
    for (const auto& part : param.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const int c = part.indexOf(QLatin1Char(':'));
        if (c > 0) out.insert(part.left(c), part.mid(c + 1));
    }
    return out;
}

QString Engine(const QString& param) { return Params(param).value(QStringLiteral("engine")); }

/// Same physical controller (or both the keyboard).
bool SameDevice(const QString& a, const QString& b) {
    const auto pa = Params(a), pb = Params(b);
    const QString ea = pa.value(QStringLiteral("engine")), eb = pb.value(QStringLiteral("engine"));
    if (ea.isEmpty() || eb.isEmpty()) return false;
    if (ea == QLatin1String("keyboard")) return eb == QLatin1String("keyboard");
    return ea == eb && pa.value(QStringLiteral("guid")) == pb.value(QStringLiteral("guid")) &&
           pa.value(QStringLiteral("port"), QStringLiteral("0")) == pb.value(QStringLiteral("port"), QStringLiteral("0"));
}

/// "<setting>=<binding>" lines from the engine, with the device taken out of each binding.
QMap<QString, QString> ParseLayout(const QString& out) {
    QMap<QString, QString> d;
    for (const auto& line : out.split(QLatin1Char('\n'))) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0) d.insert(line.left(eq).trimmed(), ControllerMapper::StripDevice(line.mid(eq + 1).trimmed()));
    }
    return d;
}

// Profiles: [section] then setting=binding lines.
using ProfileStore = QMap<QString, QMap<QString, QString>>;

ProfileStore LoadProfiles(const QString& path) {
    ProfileStore store;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return store;
    QString section;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.size() > 2 && line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2);
        } else if (const int eq = line.indexOf(QLatin1Char('=')); !section.isEmpty() && eq > 0) {
            store[section].insert(line.left(eq), line.mid(eq + 1));
        }
    }
    return store;
}

bool SaveProfiles(const QString& path, const ProfileStore& store) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);
    for (auto s = store.cbegin(); s != store.cend(); ++s) {
        out << '[' << s.key() << "]\n";
        for (auto k = s.value().cbegin(); k != s.value().cend(); ++k) out << k.key() << '=' << k.value() << '\n';
        out << '\n';
    }
    out.flush();
    return f.commit();
}

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

/// Waits for one key press (Escape cancels). Returns the Qt key code, or 0.
int AskForKey(QWidget* parent, const QString& what) {
    class KeyDialog : public QDialog {
    public:
        explicit KeyDialog(QWidget* p) : QDialog(p) {}
        int key = 0;
        void keyPressEvent(QKeyEvent* e) override {
            if (e->key() != Qt::Key_Escape) key = e->key();
            accept();
        }
    };
    KeyDialog d(parent);
    d.setWindowTitle(QObject::tr("Press a key"));
    auto* l = new QVBoxLayout(&d);
    l->addWidget(new QLabel(QObject::tr("Press the key for Switch %1 (Escape cancels).").arg(what)));
    d.exec();
    return d.key;
}
} // namespace

int ControllerMapper::SdlScancode(int qt) {
    if (qt >= Qt::Key_A && qt <= Qt::Key_Z) return 4 + (qt - Qt::Key_A);
    if (qt >= Qt::Key_1 && qt <= Qt::Key_9) return 30 + (qt - Qt::Key_1);
    if (qt == Qt::Key_0) return 39;
    if (qt >= Qt::Key_F1 && qt <= Qt::Key_F9) return 58 + (qt - Qt::Key_F1); // F10 = in-game menu, F11 = fullscreen
    if (qt == Qt::Key_F12) return 69;
    for (const KeyName& k : kKeys)
        if (k.qt == qt) return k.sdl;
    return 0;
}

QString ControllerMapper::SdlKeyName(int sdl) {
    if (sdl >= 4 && sdl <= 29) return QString(QLatin1Char(static_cast<char>('A' + sdl - 4)));
    if (sdl >= 30 && sdl <= 38) return QString::number(sdl - 29);
    if (sdl == 39) return QStringLiteral("0");
    if (sdl >= 58 && sdl <= 69) return QStringLiteral("F%1").arg(sdl - 57);
    for (const KeyName& k : kKeys)
        if (k.sdl == sdl) return QString::fromLatin1(k.name);
    if (sdl == 228) return QStringLiteral("Right Ctrl");
    if (sdl == 229) return QStringLiteral("Right Shift");
    return QStringLiteral("Key #%1").arg(sdl);
}

QString ControllerMapper::StripDevice(const QString& binding) {
    QStringList kept;
    for (const auto& part : binding.split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (!part.startsWith(QLatin1String("guid:")) && !part.startsWith(QLatin1String("port:"))) kept << part;
    return kept.join(QLatin1Char(','));
}

QString ControllerMapper::BindToDevice(const QString& stored, const QString& dev) {
    if (stored.isEmpty() || Engine(stored) != QLatin1String("sdl")) return stored;
    const auto d = Params(dev);
    return stored + QStringLiteral(",guid:%1,port:%2").arg(d.value(QStringLiteral("guid")), d.value(QStringLiteral("port"), QStringLiteral("0")));
}

ControllerMapper::ControllerMapper(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    auto* note = new QLabel(tr("Choose a player and what it plays on. Each controller keeps its own layout: whichever player gets it "
                               "gets its buttons. To change one, press Change and then the button you want for it. Swap A/B and "
                               "X/Y suits Xbox and PlayStation controllers (the button labelled A then acts as Switch A). Close "
                               "the game before changing anything."));
    note->setWordWrap(true);
    outer->addWidget(note);

    auto* top = new QHBoxLayout;
    player = new QComboBox;
    for (int i = 1; i <= 4; ++i) player->addItem(tr("Player %1").arg(i));
    device = new QComboBox;
    device->setMinimumWidth(260);
    refresh = new QPushButton(tr("Refresh"));
    top->addWidget(player);
    top->addWidget(new QLabel(tr("plays on")));
    top->addWidget(device, 1);
    top->addWidget(refresh);
    outer->addLayout(top);

    auto* actions = new QHBoxLayout;
    standard = new QPushButton(tr("Standard layout"));
    swap = new QPushButton(tr("Swap A/B and X/Y"));
    auto_assign = new QCheckBox(tr("Give controllers to players automatically when they connect"));
    actions->addWidget(standard);
    actions->addWidget(swap);
    actions->addStretch();
    actions->addWidget(auto_assign);
    outer->addLayout(actions);

    auto* grid_host = new QWidget;
    auto* grid = new QGridLayout(grid_host);
    grid->setHorizontalSpacing(10);
    grid->setColumnMinimumWidth(4, 24);
    int row = 0, col = 0;
    QFont bold;
    bold.setBold(true);
    for (const Input& in : kInputs) {
        // Left: what the player presses (the controller's own name); right: the Switch input it acts as.
        auto* binding = new QLabel;
        binding->setFont(bold);
        binding->setMinimumWidth(130);
        auto* as = new QLabel(tr("→ Switch %1").arg(tr(in.label)));
        as->setMinimumWidth(150);
        auto* change = new QPushButton(tr("Change"));
        auto* clear = new QPushButton(tr("Clear"));
        const QString setting = QString::fromLatin1(in.setting);
        bound[setting] = binding;
        row_buttons.push_back(change);
        row_buttons.push_back(clear);
        grid->addWidget(binding, row, col * 5);
        grid->addWidget(as, row, col * 5 + 1);
        grid->addWidget(change, row, col * 5 + 2);
        grid->addWidget(clear, row, col * 5 + 3);
        connect(change, &QPushButton::clicked, this, [this, setting] { capture(setting); });
        connect(clear, &QPushButton::clicked, this, [this, setting] {
            if (writePlayer(player->currentIndex(), {{setting, QString()}})) rememberProfile(player->currentIndex());
        });
        if (++col == 2) {
            col = 0;
            ++row;
        }
    }
    outer->addWidget(grid_host);

    auto* bottom = new QHBoxLayout;
    status = new QLabel;
    status->setWordWrap(true);
    cancel = new QPushButton(tr("Cancel"));
    cancel->hide();
    bottom->addWidget(status, 1);
    bottom->addWidget(cancel);
    outer->addLayout(bottom);
    outer->addStretch();

    question_timer = new QTimer(this);
    question_timer->setSingleShot(true);
    connect(question_timer, &QTimer::timeout, this, [this] { cancelQuestion(); });

    connect(player, &QComboBox::currentIndexChanged, this, [this] { showPlayer(); });
    connect(device, &QComboBox::activated, this, [this](int i) { chooseDevice(i); });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshDevices(); });
    connect(cancel, &QPushButton::clicked, this, [this] { cancelQuestion(); });
    connect(auto_assign, &QCheckBox::toggled, this, [this](bool on) {
        IniFile ini;
        if (locked || !ini.load(config_path)) return;
        ini.setBool(kSection, QStringLiteral("auto_assign_controllers"), on);
        ini.save();
    });
    connect(swap, &QPushButton::clicked, this, [this] {
        const int p = player->currentIndex();
        const Bindings b = playerBindings(p);
        if (writePlayer(p, {{QStringLiteral("button_a"), b.value(QStringLiteral("button_b"))},
                            {QStringLiteral("button_b"), b.value(QStringLiteral("button_a"))},
                            {QStringLiteral("button_x"), b.value(QStringLiteral("button_y"))},
                            {QStringLiteral("button_y"), b.value(QStringLiteral("button_x"))}})) {
            rememberProfile(p);
            status->setText(tr("A/B and X/Y swapped for %1.").arg(device->currentText()));
        }
    });
    connect(standard, &QPushButton::clicked, this, [this] {
        const QString dev = deviceOf(player->currentIndex());
        if (dev.isEmpty()) return;
        const QString key = profileKey(dev);
        ProfileStore store = LoadProfiles(profilesPath());
        if (store.remove(key)) SaveProfiles(profilesPath(), store);
        const auto apply = [this, key, dev] {
            Bindings layout;
            for (auto it = defaults[key].cbegin(); it != defaults[key].cend(); ++it) layout.insert(it.key(), BindToDevice(it.value(), dev));
            applyLayout(layout, false);
            status->setText(tr("Standard layout for %1.").arg(device->currentText()));
        };
        if (defaults.contains(key)) {
            apply();
            return;
        }
        ask({QStringLiteral("--input-capture"), QStringLiteral("defaults"), dev}, 15000, [this, key, apply](const QString& out) {
            const Bindings d = ParseLayout(out);
            if (d.isEmpty()) {
                status->setText(tr("The controller didn't answer (is it still connected?)."));
                return;
            }
            defaults[key] = d;
            apply();
        });
        setBusy(question != nullptr, tr("Reading the standard layout..."));
    });
    setLocked(false);
}

ControllerMapper::~ControllerMapper() {
    if (question) {
        question->disconnect();
        question->kill();
        question->waitForFinished(1000);
    }
}

void ControllerMapper::setPaths(const QString& config, const QString& engine, const QString& work_dir, const QProcessEnvironment& env) {
    config_path = config;
    engine_path = engine;
    engine_dir = work_dir;
    engine_env = env;
    devices_loaded = false;
    devices.clear();
    showPlayer();
    if (isVisible()) refreshDevices();
}

void ControllerMapper::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!devices_loaded && !busy && !locked) refreshDevices(); // find the controllers as soon as the tab is opened
}

QString ControllerMapper::profilesPath() const {
    return QFileInfo(config_path).dir().filePath(QStringLiteral("controller_profiles.ini"));
}

QString ControllerMapper::profileKey(const QString& dev) const {
    const QString engine = Engine(dev);
    if (engine == QLatin1String("keyboard")) return QStringLiteral("keyboard");
    if (engine == QLatin1String("sdl")) return QStringLiteral("sdl:") + Params(dev).value(QStringLiteral("guid"));
    return {};
}

ControllerMapper::Bindings ControllerMapper::playerBindings(int p) const {
    Bindings out;
    IniFile ini;
    if (!ini.load(config_path)) return out;
    for (const char* s : kButtons) out.insert(QString::fromLatin1(s), ini.value(kSection, QStringLiteral("player_%1_%2").arg(p).arg(QLatin1String(s))));
    for (const char* s : kSticks) out.insert(QString::fromLatin1(s), ini.value(kSection, QStringLiteral("player_%1_%2").arg(p).arg(QLatin1String(s))));
    return out;
}

QString ControllerMapper::deviceOf(int p) const {
    IniFile ini;
    if (!ini.load(config_path)) return {};
    if (!ini.boolValue(kSection, QStringLiteral("player_%1_connected").arg(p), p == 0)) return {};
    const auto a = Params(ini.value(kSection, QStringLiteral("player_%1_button_a").arg(p)));
    const QString engine = a.value(QStringLiteral("engine"));
    if (engine == QLatin1String("sdl"))
        return QStringLiteral("engine:sdl,guid:%1,port:%2").arg(a.value(QStringLiteral("guid")), a.value(QStringLiteral("port"), QStringLiteral("0")));
    if (engine.isEmpty() || engine == QLatin1String("keyboard")) return QStringLiteral("engine:keyboard");
    return QStringLiteral("engine:") + engine;
}

bool ControllerMapper::writePlayer(int p, const Bindings& values, const QString& connected) {
    if (locked) return false;
    IniFile ini;
    if (!ini.load(config_path)) {
        status->setText(tr("Start the game once so it creates its config, then map the controls."));
        return false;
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        ini.setValue(kSection, QStringLiteral("player_%1_%2").arg(p).arg(it.key()), QLatin1Char('"') + it.value() + QLatin1Char('"'));
    if (!connected.isEmpty()) ini.setValue(kSection, QStringLiteral("player_%1_connected").arg(p), connected);
    if (!ini.save()) {
        status->setText(tr("Could not write %1").arg(QDir::toNativeSeparators(config_path)));
        return false;
    }
    showPlayer();
    return true;
}

void ControllerMapper::rememberProfile(int p) {
    const QString dev = deviceOf(p);
    const QString key = profileKey(dev);
    if (key.isEmpty()) return;
    ProfileStore store = LoadProfiles(profilesPath());
    auto& profile = store[key];
    if (key == QLatin1String("keyboard")) profile.insert(QStringLiteral("name"), tr("Keyboard"));
    for (const Device& d : devices)
        if (SameDevice(d.param, dev)) profile.insert(QStringLiteral("name"), d.label);
    const Bindings b = playerBindings(p);
    for (auto it = b.cbegin(); it != b.cend(); ++it) profile.insert(it.key(), StripDevice(it.value()));
    SaveProfiles(profilesPath(), store);
}

void ControllerMapper::applyLayout(const Bindings& layout, bool remember) {
    const int p = player->currentIndex();
    if (writePlayer(p, layout, QStringLiteral("true")) && remember) rememberProfile(p);
}

QString ControllerMapper::describe(const QString& binding, const QString& dev) const {
    if (binding.isEmpty()) return tr("(nothing)");
    const auto p = Params(binding);
    const QString engine = p.value(QStringLiteral("engine"));
    if (engine == QLatin1String("keyboard")) return SdlKeyName(p.value(QStringLiteral("code")).toInt());
    if (engine == QLatin1String("analog_from_button")) return tr("Keys");
    const QString key = profileKey(dev);
    if (p.contains(QStringLiteral("axis_x"))) {
        const auto d = defaults.value(key);
        for (const char* stick : kSticks) {
            const auto s = Params(d.value(QString::fromLatin1(stick)));
            if (!s.isEmpty() && s.value(QStringLiteral("axis_x")) == p.value(QStringLiteral("axis_x")) &&
                s.value(QStringLiteral("axis_y")) == p.value(QStringLiteral("axis_y")))
                return QLatin1String(stick) == QLatin1String("lstick") ? tr("Left stick") : tr("Right stick");
        }
        return tr("Stick (axes %1/%2)").arg(p.value(QStringLiteral("axis_x")), p.value(QStringLiteral("axis_y")));
    }
    for (const auto& [param, name] : names.value(key)) {
        const auto c = Params(param);
        const auto same = [&](const char* k) { return p.value(QLatin1String(k)) == c.value(QLatin1String(k)); };
        if (p.contains(QStringLiteral("button")) && c.contains(QStringLiteral("button")) && same("button")) return name;
        if (p.contains(QStringLiteral("hat")) && c.contains(QStringLiteral("hat")) && same("hat") && same("direction")) return name;
        if (p.contains(QStringLiteral("axis")) && c.contains(QStringLiteral("axis")) && same("axis") &&
            p.value(QStringLiteral("invert"), QStringLiteral("+")) == c.value(QStringLiteral("invert"), QStringLiteral("+")))
            return name;
    }
    if (p.contains(QStringLiteral("hat"))) return tr("D-pad %1").arg(p.value(QStringLiteral("direction")));
    if (p.contains(QStringLiteral("axis"))) return tr("Axis %1").arg(p.value(QStringLiteral("axis")));
    if (p.contains(QStringLiteral("button"))) return tr("Button %1").arg(p.value(QStringLiteral("button")));
    return engine;
}

void ControllerMapper::showPlayer() {
    const int p = player->currentIndex();
    const QString dev = deviceOf(p);
    IniFile ini;
    const bool ok = ini.load(config_path);
    {
        const QSignalBlocker b(auto_assign);
        auto_assign->setChecked(ok && ini.boolValue(kSection, QStringLiteral("auto_assign_controllers"), true));
    }
    // The device combo: nobody, keyboard, the controllers found, and this player's controller if it isn't plugged in.
    QList<Device> list{{tr("Nobody (not playing)"), QString(), true}, {tr("Keyboard"), QStringLiteral("engine:keyboard"), true}};
    for (const Device& d : devices) list << d;
    int current = dev.isEmpty() ? 0 : -1;
    for (int i = 1; i < list.size() && current < 0; ++i)
        if (SameDevice(list[i].param, dev)) current = i;
    if (current < 0) {
        const auto profiles = LoadProfiles(profilesPath());
        const QString name = profiles.value(profileKey(dev)).value(QStringLiteral("name"), tr("Controller"));
        list << Device{devices_loaded ? tr("%1 (not connected)").arg(name) : name, dev, false};
        current = static_cast<int>(list.size()) - 1;
    }
    {
        const QSignalBlocker b(device);
        device->clear();
        for (const Device& d : list) device->addItem(d.label);
        device->setCurrentIndex(current);
    }
    combo_devices = list;
    const Bindings b = ok ? playerBindings(p) : Bindings{};
    for (const Input& in : kInputs) {
        const QString setting = QString::fromLatin1(in.setting);
        bound[setting]->setText(dev.isEmpty() ? QStringLiteral("-") : describe(b.value(setting), dev));
    }
    const bool usable = !locked && !busy && !dev.isEmpty();
    for (auto* button : row_buttons) button->setEnabled(usable);
    standard->setEnabled(usable);
    swap->setEnabled(usable && Engine(dev) != QLatin1String("keyboard"));
    // The names of this controller's inputs ("LB", "Cross") and its standard layout, once per session.
    if (Engine(dev) == QLatin1String("sdl") && !names.contains(profileKey(dev)) && !busy && list[current].present)
        loadNames(dev, [this] { showPlayer(); });
}

void ControllerMapper::loadNames(const QString& dev, std::function<void()> then) {
    const QString key = profileKey(dev);
    names[key]; // asked once, even if it fails
    ask({QStringLiteral("--input-capture"), QStringLiteral("names"), dev}, 10000, [this, key, dev, then](const QString& out) {
        QList<QPair<QString, QString>> list;
        for (const auto& line : out.split(QLatin1Char('\n'))) {
            const int tab = line.indexOf(QLatin1Char('\t'));
            if (tab > 0) list << qMakePair(line.left(tab), line.mid(tab + 1).trimmed());
        }
        names[key] = list;
        if (defaults.contains(key)) {
            then();
            return;
        }
        // The standard layout too: it names the sticks and is what "Standard layout" restores.
        QTimer::singleShot(0, this, [this, key, dev, then] {
            ask({QStringLiteral("--input-capture"), QStringLiteral("defaults"), dev}, 10000, [this, key, then](const QString& out2) {
                const Bindings d = ParseLayout(out2);
                if (!d.isEmpty()) defaults[key] = d;
                then();
            });
        });
    });
}

void ControllerMapper::ask(const QStringList& args, int timeout_ms, std::function<void(const QString&)> done) {
    if (question) return;
    if (engine_path.isEmpty() || !QFileInfo::exists(engine_path)) {
        status->setText(tr("The game engine isn't set up yet (Setup page)."));
        return;
    }
    question = new QProcess(this);
    question_done = std::move(done);
    question->setProcessEnvironment(engine_env);
    question->setWorkingDirectory(engine_dir);
    connect(question, &QProcess::finished, this, [this] {
        QProcess* q = question;
        const QString out = QString::fromUtf8(q->readAllStandardOutput()).trimmed();
        q->deleteLater();
        question = nullptr;
        question_timer->stop();
        auto done_now = std::move(question_done);
        question_done = nullptr;
        setBusy(false);
        if (done_now) done_now(out);
    });
    connect(question, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !question) return;
        question->deleteLater();
        question = nullptr;
        question_done = nullptr;
        question_timer->stop();
        setBusy(false);
        status->setText(tr("Could not start the game engine to ask about controllers."));
    });
    question_timer->start(timeout_ms);
    question->start(engine_path, args);
}

void ControllerMapper::cancelQuestion() {
    if (!question) return;
    question_done = nullptr; // the answer is dropped
    question->kill();
    status->setText(tr("Cancelled."));
}

void ControllerMapper::setBusy(bool b, const QString& text) {
    busy = b;
    cancel->setVisible(b);
    if (!text.isEmpty()) status->setText(text);
    player->setEnabled(!b && !locked);
    device->setEnabled(!b && !locked);
    refresh->setEnabled(!b && !locked);
    auto_assign->setEnabled(!b && !locked);
    if (!b) {
        showPlayer();
    } else {
        for (auto* button : row_buttons) button->setEnabled(false);
        standard->setEnabled(false);
        swap->setEnabled(false);
    }
}

void ControllerMapper::refreshDevices() {
    if (question) return;
    ask({QStringLiteral("--input-capture"), QStringLiteral("devices")}, 10000, [this](const QString& out) {
        devices.clear();
        for (const auto& line : out.split(QLatin1Char('\n'))) {
            const auto p = Params(line.trimmed());
            if (p.value(QStringLiteral("engine")) != QLatin1String("sdl")) continue; // controllers (the keyboard is always listed)
            QString name = p.value(QStringLiteral("display"), tr("Controller"));
            if (name.endsWith(QLatin1String(" 0"))) name.chop(2); // "Xbox One Controller 0": the number only tells identical ones apart
            devices << Device{name,
                              QStringLiteral("engine:sdl,guid:%1,port:%2").arg(p.value(QStringLiteral("guid")), p.value(QStringLiteral("port"), QStringLiteral("0"))),
                              true};
        }
        devices_loaded = true;
        status->setText(devices.isEmpty() ? tr("No controller found (is it connected and switched on?).")
                                          : tr("%n controller(s) found.", nullptr, static_cast<int>(devices.size())));
    });
    setBusy(question != nullptr, tr("Looking for controllers..."));
}

void ControllerMapper::chooseDevice(int index) {
    if (index < 0 || index >= combo_devices.size() || locked) return;
    const int p = player->currentIndex();
    const Device chosen = combo_devices[index];
    const QString old_dev = deviceOf(p);
    if (chosen.param.isEmpty()) {
        writePlayer(p, {}, QStringLiteral("false"));
        status->setText(tr("Player %1 isn't playing.").arg(p + 1));
        return;
    }
    if (SameDevice(chosen.param, old_dev)) return;
    const QString key = profileKey(chosen.param);
    const auto give = [this, p, chosen, key, old_dev] {
        // A player that had this controller takes this player's old device (players can share the keyboard).
        if (Engine(chosen.param) == QLatin1String("sdl")) {
            const Bindings mine = playerBindings(p);
            for (int q = 0; q < 4; ++q) {
                if (q == p || !SameDevice(deviceOf(q), chosen.param)) continue;
                writePlayer(q, mine, old_dev.isEmpty() ? QStringLiteral("false") : QStringLiteral("true"));
            }
        }
        const auto profiles = LoadProfiles(profilesPath());
        const bool saved = profiles.contains(key);
        const auto source = saved ? profiles.value(key) : defaults.value(key);
        Bindings layout;
        for (const char* s : kButtons) layout.insert(QString::fromLatin1(s), BindToDevice(source.value(QString::fromLatin1(s)), chosen.param));
        for (const char* s : kSticks) layout.insert(QString::fromLatin1(s), BindToDevice(source.value(QString::fromLatin1(s)), chosen.param));
        // Chosen by hand: the game must not hand the controllers out differently at start.
        IniFile ini;
        if (ini.load(config_path)) {
            ini.setBool(kSection, QStringLiteral("auto_assign_controllers"), false);
            ini.save();
        }
        applyLayout(layout, false);
        status->setText(tr("Player %1 plays on %2%3. Automatic assignment is off now (tick it to let the game hand controllers out).")
                            .arg(p + 1)
                            .arg(chosen.label, saved ? tr(" with its saved layout") : QString()));
    };
    if (LoadProfiles(profilesPath()).contains(key) || defaults.contains(key)) {
        give();
        return;
    }
    ask({QStringLiteral("--input-capture"), QStringLiteral("defaults"), chosen.param}, 10000, [this, key, give](const QString& out) {
        const Bindings d = ParseLayout(out);
        if (d.isEmpty()) {
            status->setText(tr("The controller didn't answer (is it still connected?)."));
            return;
        }
        defaults[key] = d;
        give();
    });
    setBusy(question != nullptr, tr("Reading the controller's layout..."));
}

void ControllerMapper::capture(const QString& setting) {
    const Input* in = nullptr;
    for (const Input& i : kInputs)
        if (setting == QLatin1String(i.setting)) in = &i;
    const int p = player->currentIndex();
    const QString dev = deviceOf(p);
    if (!in || dev.isEmpty() || locked || busy) return;
    const QString label = tr(in->label);
    if (Engine(dev) == QLatin1String("keyboard")) {
        if (in->stick) {
            status->setText(tr("A stick on the keyboard is four keys; Standard layout gives W/A/S/D and the arrow keys."));
            return;
        }
        const int k = AskForKey(this, label);
        if (!k) return;
        const int code = SdlScancode(k);
        if (!code) {
            status->setText(tr("That key can't be used (F10 and F11 are the game's menu and fullscreen keys)."));
            return;
        }
        if (writePlayer(p, {{setting, QStringLiteral("engine:keyboard,code:%1,toggle:0").arg(code)}})) {
            rememberProfile(p);
            status->setText(tr("%1 now acts as Switch %2.").arg(SdlKeyName(code), label));
        }
        return;
    }
    ask({QStringLiteral("--input-capture"), in->stick ? QStringLiteral("stick") : QStringLiteral("button"), QStringLiteral("8"), dev}, 12000,
        [this, p, setting, label](const QString& out) {
            const int tab = out.indexOf(QLatin1Char('\t'));
            const QString binding = tab > 0 ? out.left(tab) : out;
            if (binding.isEmpty()) {
                status->setText(tr("Nothing pressed: Switch %1 is unchanged.").arg(label));
                return;
            }
            if (writePlayer(p, {{setting, binding}})) {
                rememberProfile(p);
                status->setText(tr("%1 now acts as Switch %2.").arg(tab > 0 ? out.mid(tab + 1).trimmed() : binding, label));
            }
        });
    if (question)
        setBusy(true, in->stick ? tr("Move the stick you want for Switch %1 (8 s)...").arg(label)
                                : tr("Press the button you want for Switch %1 on the controller (8 s)...").arg(label));
}

void ControllerMapper::setLocked(bool l) {
    locked = l;
    if (l) cancelQuestion();
    setBusy(question != nullptr);
    if (l) status->setText(tr("The game is running: controls can be changed after it closes."));
}
