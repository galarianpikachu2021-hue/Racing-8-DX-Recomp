// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include "ctgp_convert.h"
#include "updater.h"

#include <QThread>
#include <QTimer>

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QThread>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// Values are suyu's enum indices (src/common/settings_enums.h), in enum order.
const QStringList kResolutions{"0.25x", "0.5x", "0.75x", "1x (720p/1080p)", "1.25x", "1.5x",
                               "2x (1440p/2160p)", "3x", "4x", "5x", "6x", "7x", "8x"};
const QStringList kAspects{"16:9", "4:3", "21:9", "16:10", "32:9", "Stretch to window"};
const QStringList kFilters{"Nearest", "Bilinear", "Bicubic", "Gaussian", "Lanczos", "ScaleForce",
                           "AMD FSR", "Area", "Zero-tangent", "B-spline", "Mitchell", "Spline-1",
                           "MMPX", "SGSR", "SGSR edge"};
const QStringList kAntiAliasing{"None", "FXAA", "SMAA"};
const QStringList kVSync{"Off (immediate)", "Mailbox", "FIFO (vsync)", "FIFO relaxed"};
const QStringList kFullscreenModes{"Borderless window", "Exclusive"};
const QStringList kConsoleModes{"Low (handheld, 720p)", "High (docked, 1080p)"};

constexpr int kFsrFilterIndex = 6; // ScalingFilter::Fsr

const QString kRenderer = QStringLiteral("Renderer");
const QString kCore = QStringLiteral("Core");
const QString kSystem = QStringLiteral("System");
const QString kControls = QStringLiteral("Controls");

QComboBox* makeCombo(const QStringList& items) {
    auto* combo = new QComboBox;
    combo->addItems(items);
    return combo;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("KartRecomp"));
    resize(820, 600);

    auto* tabs = new QTabWidget;
    tabs->addTab(buildPlayTab(), tr("Play"));
    tabs->addTab(buildDisplayTab(), tr("Display"));
    tabs->addTab(buildFrameRateTab(), tr("Frame rate"));
    tabs->addTab(buildEnhancementsTab(), tr("Enhancements"));
    tabs->addTab(buildControllersTab(), tr("Controllers"));
    tabs->addTab(buildMiiTab(), tr("Miis"));
    tabs->addTab(buildDlcTab(), tr("DLC"));
    tabs->addTab(buildModsTab(), tr("Mods"));
    setCentralWidget(tabs);

    QSettings prefs;
    QString root = prefs.value(QStringLiteral("game_root")).toString();
    if (root.isEmpty()) {
        root = GamePackage::findDefaultRoot();
    }
    QString home = prefs.value(QStringLiteral("suyu_home")).toString();
    if (home.isEmpty()) {
        home = GamePackage::findDefaultSuyuHome(root);
    }
    root_edit->setText(root);
    home_edit->setText(home);
    reloadPackage();
    connectAutoSave();

    // Updates from the project's GitHub releases: checked at start-up at most once a day, or with the button on the Play tab.
    updater = new Updater(this);
    connect(updater, &Updater::log, this, [this](const QString& line) { log_view->appendPlainText(line); });
    QTimer::singleShot(1500, this, [this] { updater->check(true); });

    connect(&game, &QProcess::readyReadStandardOutput, this, [this] {
        appendLog(QString::fromUtf8(game.readAllStandardOutput()));
    });
    connect(&game, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
        const bool running = state != QProcess::NotRunning;
        play_button->setEnabled(!running);
        stop_button->setEnabled(running);
        // The game rewrites its whole config on exit, so edits made while it runs would
        // be lost. Lock them, then re-read whatever the game saved.
        display_page->setEnabled(!running);
        frame_rate_page->setEnabled(!running);
        enhancements_page->setEnabled(!running);
        refreshMods();
        if (!running) {
            loadSettings();
            loadOverlay();
        }
    });
    connect(&game, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        QString summary = tr("Game exited (code %1).").arg(code);
        QFile report(package.coverageReport());
        if (report.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const auto& line : QString::fromUtf8(report.readAll()).split(QLatin1Char('\n'))) {
                if (line.contains(QLatin1String("static -> JIT")) ||
                    line.contains(QLatin1String("blocks per transition"))) {
                    summary += QLatin1Char('\n') + line.trimmed();
                }
            }
        }
        appendLog(summary + QLatin1Char('\n'));
        status_label->setText(summary);
    });
}

MainWindow::~MainWindow() {
    if (game.state() != QProcess::NotRunning) {
        game.terminate();
        game.waitForFinished(5000);
    }
}

QWidget* MainWindow::buildPlayTab() {
    auto* page = new QWidget;
    auto* form = new QFormLayout;

    const auto pathRow = [this](QLineEdit*& edit, const QString& title) {
        auto* row = new QHBoxLayout;
        edit = new QLineEdit;
        auto* browse = new QPushButton(tr("Browse…"));
        row->addWidget(edit);
        row->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, edit, title] {
            const QString dir = QFileDialog::getExistingDirectory(this, title, edit->text());
            if (!dir.isEmpty()) {
                edit->setText(dir);
                reloadPackage();
            }
        });
        connect(edit, &QLineEdit::editingFinished, this, [this] { reloadPackage(); });
        return row;
    };
    form->addRow(tr("Game folder:"), pathRow(root_edit, tr("Select your built game folder")));
    form->addRow(tr("Keys + firmware:"), pathRow(home_edit, tr("Select the suyu data folder with your keys")));

    renderer_combo = makeCombo({tr("Emulated GPU (default)"), tr("Native Vulkan (experimental)")});
    renderer_combo->setToolTip(
        tr("Native Vulkan runs the game's own NVN graphics calls directly on your GPU with no "
           "emulated GPU. It is new: expect a short pause as each course starts, and please report "
           "anything that looks different from the emulated GPU. The Display tab's internal "
           "resolution applies to both renderers (see Native resolution scaling to turn it off "
           "for native); the upscaling filter, FSR and anti-aliasing apply to the emulated GPU "
           "only for now."));
    renderer_combo->setCurrentIndex(QSettings().value(QStringLiteral("renderer"), 0).toInt());
    connect(renderer_combo, &QComboBox::currentIndexChanged, this,
            [](int index) { QSettings().setValue(QStringLiteral("renderer"), index); });
    form->addRow(tr("Renderer:"), renderer_combo);
    native_res_combo = makeCombo({tr("720p"), tr("1080p"), tr("1440p"), tr("4K (2160p)")});
    native_res_combo->setToolTip(
        tr("The native Vulkan renderer's output resolution: the size of the final picture, the HUD included. "
           "The 3D scene can be drawn smaller than this and upscaled in the in-game menu (F10). This replaces "
           "the Display tab's internal resolution multiplier for native Vulkan; the emulated GPU keeps that one."));
    native_res_combo->setCurrentIndex(QSettings().value(QStringLiteral("native_output_resolution"), 2).toInt());
    connect(native_res_combo, &QComboBox::currentIndexChanged, this,
            [](int index) { QSettings().setValue(QStringLiteral("native_output_resolution"), index); });
    native_res_combo->setEnabled(renderer_combo->currentIndex() == 1);
    connect(renderer_combo, &QComboBox::currentIndexChanged, native_res_combo,
            [this](int index) { native_res_combo->setEnabled(index == 1); });
    form->addRow(tr("Native output resolution:"), native_res_combo);
    auto* ingame_note = new QLabel(
        tr("In game, F10 (or both stick buttons, L3 + R3, on a controller) opens a settings menu: upscaler, render scale, "
           "sharpening, frame generation, HDR, present mode, speed limit, an FPS counter and which controller plays as "
           "Players 1 to 4. The Enhancements tab holds the same settings."));
    ingame_note->setWordWrap(true);
    form->addRow(ingame_note);
#ifdef Q_OS_LINUX
    pin_combo = makeCombo({tr("Off (default)"), tr("On")});
    pin_combo->setToolTip(
        tr("Pins each emulated CPU core's thread (and the native renderer's) to its own CPU, so the "
           "caches stay warm instead of being refilled after every migration. It helps a little in "
           "heavy moments (fewer dropped frames). Leave it off if the CPU is busy with other "
           "programs, because pinned threads can't move away from them. Needs 6 or more CPU threads."));
    pin_combo->setCurrentIndex(QSettings().value(QStringLiteral("pin_cpu_threads"), 0).toInt());
    pin_combo->setEnabled(QThread::idealThreadCount() >= 6);
    connect(pin_combo, &QComboBox::currentIndexChanged, this,
            [](int index) { QSettings().setValue(QStringLiteral("pin_cpu_threads"), index); });
    form->addRow(tr("Pin CPU threads (Linux):"), pin_combo);
#endif

#ifdef Q_OS_LINUX
    malloc_check = new QCheckBox(tr("Faster memory allocator (tcmalloc, if installed)"));
    malloc_check->setToolTip(tr("Preloads gperftools' tcmalloc: the renderer and the graphics hooks allocate a lot, and it is worth several fps "
                                "in split-screen races. Install the gperftools package (libtcmalloc_minimal.so.4) to use it."));
    malloc_check->setChecked(QSettings().value(QStringLiteral("use_tcmalloc"), true).toBool());
    connect(malloc_check, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("use_tcmalloc"), on); });
    form->addRow(malloc_check);
#endif
    status_label = new QLabel;
    status_label->setWordWrap(true);
    fullscreen_check = new QCheckBox(tr("Fullscreen"));
    nvn_capture_check = new QCheckBox(tr("Record graphics trace (developer)"));
    nvn_capture_check->setToolTip(
        tr("Logs the game's NVN graphics calls to user/log (nvn_trace.txt, nvn_frames.csv and "
           "two-frame captures every 30 s). Used to build the native GPU backend. Slower."));

    play_button = new QPushButton(tr("Play"));
    play_button->setMinimumHeight(40);
    stop_button = new QPushButton(tr("Stop"));
    stop_button->setEnabled(false);
    connect(play_button, &QPushButton::clicked, this, &MainWindow::launch);
    connect(stop_button, &QPushButton::clicked, this, [this] { game.terminate(); });
#ifdef Q_OS_LINUX
    appimage_button = new QPushButton(tr("Package as AppImage…"));
    appimage_button->setToolTip(tr("Packs the game built on this PC into one AppImage for this PC (never share it: it contains the "
                                   "translated game code). Saves, settings and mods move to ~/.local/share/kartrecomp and "
                                   "~/.config/kartrecomp; your game files, firmware and mods are linked there, not copied."));
    connect(appimage_button, &QPushButton::clicked, this, &MainWindow::packageAppImage);
#endif

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(fullscreen_check);
    buttons->addWidget(nvn_capture_check);
    buttons->addStretch();
    auto* update_button = new QPushButton(tr("Check for updates"));
    connect(update_button, &QPushButton::clicked, this, [this] { updater->check(false); });
    buttons->addWidget(update_button);
    if (appimage_button) buttons->addWidget(appimage_button);
    buttons->addWidget(stop_button);
    buttons->addWidget(play_button);

    log_view = new QPlainTextEdit;
    log_view->setReadOnly(true);
    log_view->setMaximumBlockCount(2000);

    auto* layout = new QVBoxLayout(page);
    layout->addLayout(form);
    layout->addWidget(status_label);
    layout->addLayout(buttons);
    appimage_progress = new QProgressBar;
    appimage_progress->setRange(0, 3);
    appimage_progress->setFormat(tr("Packaging: step %v of %m"));
    appimage_progress->hide();
    layout->addWidget(appimage_progress);
    layout->addWidget(new QLabel(tr("Output:")));
    layout->addWidget(log_view, 1);
    return page;
}

QWidget* MainWindow::buildDisplayTab() {
    auto* page = display_page = new QWidget;
    auto* form = new QFormLayout(page);
    resolution_combo = makeCombo(kResolutions);
    aspect_combo = makeCombo(kAspects);
    filter_combo = makeCombo(kFilters);
    aa_combo = makeCombo(kAntiAliasing);
    vsync_combo = makeCombo(kVSync);
    fullscreen_mode_combo = makeCombo(kFullscreenModes);
    docked_combo = makeCombo(kConsoleModes);
    form->addRow(tr("Internal resolution:"), resolution_combo);
    form->addRow(tr("Aspect ratio:"), aspect_combo);
    form->addRow(tr("Upscaling filter:"), filter_combo);
    fsr_sharpness_spin = new QSpinBox;
    fsr_sharpness_spin->setRange(0, 200);
    fsr_sharpness_spin->setSuffix(QStringLiteral(" %"));
    form->addRow(tr("FSR sharpening:"), fsr_sharpness_spin);
    form->addRow(tr("Anti-aliasing:"), aa_combo);
    form->addRow(tr("VSync:"), vsync_combo);
    form->addRow(tr("Fullscreen mode:"), fullscreen_mode_combo);
    form->addRow(tr("Graphics quality:"), docked_combo);
    auto* note = new QLabel(
        tr("Graphics quality: High is the docked console mode (the game renders at 1080p with its best effects), Low is "
           "handheld (720p, lighter effects). It can also be changed in game (F10). The resolution scale multiplies that. "
           "It only looks sharper when the window or screen is larger than the base resolution, "
           "for example 2x in fullscreen on a 1440p or 4K monitor. AMD FSR (1) with sharpening is "
           "the upscaler built in. Changes are saved immediately and apply from the next launch; "
           "they are locked while the game runs."));
    note->setWordWrap(true);
    form->addRow(note);
    return page;
}

QWidget* MainWindow::buildFrameRateTab() {
    auto* page = frame_rate_page = new QWidget;
    auto* form = new QFormLayout(page);
    limit_check = new QCheckBox(tr("Limit emulation speed"));
    limit_spin = new QSpinBox;
    limit_spin->setRange(10, 400);
    limit_spin->setSuffix(QStringLiteral(" %"));
    connect(limit_check, &QCheckBox::toggled, limit_spin, &QSpinBox::setEnabled);
    form->addRow(limit_check);
    form->addRow(tr("Speed limit:"), limit_spin);
    auto* note = new QLabel(
        tr("100 % is the game's native 60 fps. A different value speeds up or slows down the whole "
           "game. True custom frame rates (for example 120 fps at normal speed) need game-side "
           "patches generated from your own copy. That is planned; see docs/ROADMAP.md."));
    note->setWordWrap(true);
    form->addRow(note);
    return page;
}

QWidget* MainWindow::buildControllersTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* note = new QLabel(tr("Player 1 bindings from the game's config. Controllers connected at "
                               "launch are mapped automatically. A capture-based remapping UI is "
                               "planned."));
    note->setWordWrap(true);
    controls_table = new QTableWidget(0, 2);
    controls_table->setHorizontalHeaderLabels({tr("Input"), tr("Binding")});
    controls_table->horizontalHeader()->setStretchLastSection(true);
    controls_table->verticalHeader()->hide();
    controls_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    players_table = new QTableWidget(4, 3);
    players_table->setHorizontalHeaderLabels({tr("Player"), tr("State"), tr("Mapped to")});
    players_table->horizontalHeader()->setStretchLastSection(true);
    players_table->verticalHeader()->hide();
    players_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    players_table->setMaximumHeight(150);
    auto* assign_note = new QLabel(tr("Players 1 to 4. Controllers are handed out automatically as they connect. To choose who plays on what "
                                      "(a controller, the keyboard, or nobody), open the in-game menu with F10, or both stick buttons "
                                      "on a controller, and use the Player 1 to 4 controller rows."));
    assign_note->setWordWrap(true);
    layout->addWidget(note);
    layout->addWidget(assign_note);
    layout->addWidget(players_table);
    layout->addWidget(controls_table, 1);
    return page;
}

QWidget* MainWindow::buildMiiTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* note = new QLabel(
        tr("Manual Mii entry is planned. Miis are stored in the system save 8000000000000030 "
           "(MiiDatabase.dat) inside the game's user/nand folder. The editor will write valid "
           "records (name, face, hair, colours) there, so they show up in the game's Mii racer "
           "list."));
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildDlcTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* note = new QLabel(tr("Add-on content NSPs from your own dump. Each one is passed to the "
                               "game with --content-dlc. DLC is data only, so no rebuild is needed."));
    note->setWordWrap(true);
    dlc_list = new QListWidget;
    auto* add = new QPushButton(tr("Add DLC…"));
    auto* remove = new QPushButton(tr("Remove selected"));
    connect(add, &QPushButton::clicked, this, [this] {
        const auto files = QFileDialog::getOpenFileNames(this, tr("Add DLC"), {},
                                                         tr("NSP files (*.nsp)"));
        QDir().mkpath(package.dlcDir());
        for (const auto& f : files) {
            const QString target = QDir(package.dlcDir()).filePath(QFileInfo(f).fileName());
            // A hard link costs no space when the file is on the same drive; copy otherwise.
            if (!QFile::exists(target) && !QFile::link(f, target) && !QFile::copy(f, target)) {
                QMessageBox::warning(this, tr("DLC"), tr("Could not add %1").arg(f));
            }
        }
        refreshDlc();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        for (auto* item : dlc_list->selectedItems()) {
            QFile::remove(QDir(package.dlcDir()).filePath(item->text()));
        }
        refreshDlc();
    });
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addWidget(note);
    layout->addWidget(dlc_list, 1);
    layout->addLayout(buttons);
    return page;
}

void MainWindow::reloadPackage() {
    package.setRoot(root_edit->text().trimmed());
    package.setSuyuHome(home_edit->text().trimmed());
    QSettings prefs;
    prefs.setValue(QStringLiteral("game_root"), package.root());
    prefs.setValue(QStringLiteral("suyu_home"), package.suyuHome());

    const QString problems = package.problems();
    status_label->setText(problems.isEmpty() ? tr("Ready.") : problems);
    play_button->setEnabled(problems.isEmpty() && game.state() == QProcess::NotRunning);
    loadSettings();
    refreshDlc();
    refreshControllers();
    loadOverlay();
    refreshMods();
}

void MainWindow::loadSettings() {
    const QSignalBlocker guard(this);
    loading_settings = true;
    config.load(package.configFile());
    // Defaults match suyu's own when the game has not written a config yet.
    resolution_combo->setCurrentIndex(config.intValue(kRenderer, QStringLiteral("resolution_setup"), 3));
    aspect_combo->setCurrentIndex(config.intValue(kRenderer, QStringLiteral("aspect_ratio"), 0));
    filter_combo->setCurrentIndex(config.intValue(kRenderer, QStringLiteral("scaling_filter"), 1));
    aa_combo->setCurrentIndex(config.intValue(kRenderer, QStringLiteral("anti_aliasing"), 0));
    vsync_combo->setCurrentIndex(config.intValue(kRenderer, QStringLiteral("use_vsync"), 2));
    fullscreen_mode_combo->setCurrentIndex(
        config.intValue(kRenderer, QStringLiteral("fullscreen_mode"), 0));
    docked_combo->setCurrentIndex(config.intValue(kSystem, QStringLiteral("use_docked_mode"), 1));
    limit_check->setChecked(config.boolValue(kCore, QStringLiteral("use_speed_limit"), true));
    limit_spin->setValue(config.intValue(kCore, QStringLiteral("speed_limit"), 100));
    limit_spin->setEnabled(limit_check->isChecked());
    // Stored as the slider position: suyu's default is 25 (0 = off, 200 = maximum).
    fsr_sharpness_spin->setValue(config.intValue(kRenderer, QStringLiteral("fsr_sharpening_slider"), 25));
    fsr_sharpness_spin->setEnabled(filter_combo->currentIndex() == kFsrFilterIndex);
    loading_settings = false;
}

bool MainWindow::saveSettings() {
    if (!config.isLoaded()) {
        return true; // first launch: nothing to edit yet, the game writes its defaults
    }
    config.setInt(kRenderer, QStringLiteral("resolution_setup"), resolution_combo->currentIndex());
    config.setInt(kRenderer, QStringLiteral("aspect_ratio"), aspect_combo->currentIndex());
    config.setInt(kRenderer, QStringLiteral("scaling_filter"), filter_combo->currentIndex());
    config.setInt(kRenderer, QStringLiteral("anti_aliasing"), aa_combo->currentIndex());
    config.setInt(kRenderer, QStringLiteral("use_vsync"), vsync_combo->currentIndex());
    config.setInt(kRenderer, QStringLiteral("fullscreen_mode"), fullscreen_mode_combo->currentIndex());
    config.setInt(kSystem, QStringLiteral("use_docked_mode"), docked_combo->currentIndex());
    config.setBool(kCore, QStringLiteral("use_speed_limit"), limit_check->isChecked());
    config.setInt(kCore, QStringLiteral("speed_limit"), limit_spin->value());
    config.setInt(kRenderer, QStringLiteral("fsr_sharpening_slider"), fsr_sharpness_spin->value());
    return config.save();
}

void MainWindow::applySettingsNow() {
    if (loading_settings || game.state() != QProcess::NotRunning) {
        return;
    }
    fsr_sharpness_spin->setEnabled(filter_combo->currentIndex() == kFsrFilterIndex);
    // Re-read first so anything the game wrote since we loaded is kept.
    IniFile fresh;
    if (!fresh.load(package.configFile())) {
        status_label->setText(tr("Launch the game once so it creates its config; settings can be "
                                 "changed after that."));
        return;
    }
    config = fresh;
    if (!saveSettings()) {
        status_label->setText(tr("Could not write %1").arg(package.configFile()));
    }
}

void MainWindow::connectAutoSave() {
    for (auto* combo : {resolution_combo, aspect_combo, filter_combo, aa_combo, vsync_combo,
                        fullscreen_mode_combo, docked_combo}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &MainWindow::applySettingsNow);
    }
    connect(limit_check, &QCheckBox::toggled, this, &MainWindow::applySettingsNow);
    connect(limit_spin, &QSpinBox::valueChanged, this, &MainWindow::applySettingsNow);
    connect(fsr_sharpness_spin, &QSpinBox::valueChanged, this, &MainWindow::applySettingsNow);
}

void MainWindow::refreshDlc() {
    dlc_list->clear();
    for (const auto& f : package.dlcFiles()) {
        dlc_list->addItem(QFileInfo(f).fileName());
    }
}

void MainWindow::refreshControllers() {
    for (int p = 0; p < 4; ++p) {
        const QString prefix = QStringLiteral("player_%1_").arg(p);
        const bool connected = config.boolValue(kControls, prefix + QStringLiteral("connected"), p == 0);
        const QString a = config.value(kControls, prefix + QStringLiteral("button_a"));
        QString device = tr("nothing");
        if (a.contains(QLatin1String("engine:keyboard"))) device = tr("keyboard");
        else if (a.contains(QLatin1String("engine:sdl"))) device = tr("controller");
        else if (a.contains(QLatin1String("engine:joycon"))) device = tr("Joy-Con");
        players_table->setItem(p, 0, new QTableWidgetItem(tr("Player %1").arg(p + 1)));
        players_table->setItem(p, 1, new QTableWidgetItem(connected ? tr("connected") : tr("not connected")));
        players_table->setItem(p, 2, new QTableWidgetItem(device));
    }
    controls_table->setRowCount(0);
    const auto binds = config.keysWithPrefix(kControls, QStringLiteral("player_0_"));
    for (const auto& [key, value] : binds) {
        if (!key.contains(QLatin1String("button_")) && !key.contains(QLatin1String("stick"))) {
            continue;
        }
        const int row = controls_table->rowCount();
        controls_table->insertRow(row);
        controls_table->setItem(row, 0, new QTableWidgetItem(key.mid(9)));
        controls_table->setItem(row, 1, new QTableWidgetItem(value));
    }
}

QProcessEnvironment MainWindow::launchEnvironment() const {
    auto env = package.launchEnvironment();
    if (renderer_combo->currentIndex() == 1) {
        env.insert(QStringLiteral("KARTRECOMP_NATIVE_GPU"), QStringLiteral("native"));
        static const int kHeights[] = {720, 1080, 1440, 2160};
        env.insert(QStringLiteral("KARTRECOMP_NATIVE_HEIGHT"),
                   QString::number(kHeights[qBound(0, native_res_combo->currentIndex(), 3)]));
    }
#ifdef Q_OS_LINUX
    if (pin_combo && pin_combo->currentIndex() == 1 && QThread::idealThreadCount() >= 6) {
        // Guest cores 0-3 on four CPUs and the render thread on the next, leaving CPUs 0-1 (or 0) free
        // for the desktop, audio and the rest of the system.
        const int first = QThread::idealThreadCount() >= 8 ? 2 : 1;
        env.insert(QStringLiteral("KARTRECOMP_PIN"),
                   QStringLiteral("%1,%2,%3,%4").arg(first).arg(first + 1).arg(first + 2).arg(first + 3));
        env.insert(QStringLiteral("KARTRECOMP_PIN_RENDER"), QString::number(first + 4));
    }
#endif
#ifdef Q_OS_LINUX
    if (malloc_check && malloc_check->isChecked()) {
        for (const QString& lib : {QStringLiteral("/usr/lib/libtcmalloc_minimal.so.4"), QStringLiteral("/usr/lib64/libtcmalloc_minimal.so.4"),
                                   QStringLiteral("/usr/lib/x86_64-linux-gnu/libtcmalloc_minimal.so.4")}) {
            if (QFileInfo::exists(lib)) {
                env.insert(QStringLiteral("LD_PRELOAD"), lib);
                break;
            }
        }
    }
#endif
    if (nvn_capture_check->isChecked()) {
        env.insert(QStringLiteral("KARTRECOMP_NVN_TRACE"), QStringLiteral("1"));
        env.insert(QStringLiteral("KARTRECOMP_NVN_CAPTURE"), QStringLiteral("every=1800,frames=2"));
    }
    return env;
}

void MainWindow::launch() {
    const QString problems = package.problems();
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, tr("Cannot start"), problems);
        return;
    }
    // Settings are re-read first so edits made by the game since the last load survive.
    IniFile fresh;
    if (fresh.load(package.configFile())) {
        config = fresh;
    }
    if (!package.fixDataPaths() || !saveSettings()) {
        QMessageBox::warning(this, tr("Settings"), tr("Could not write %1").arg(package.configFile()));
        return;
    }
    log_view->clear();
    if (!resolveCtgpConflict(false, false)) return;
    game.setWorkingDirectory(package.root());
    auto env = launchEnvironment();
    game.setProcessEnvironment(env);
    game.setProcessChannelMode(QProcess::MergedChannels);
    game.start(package.executable(), package.launchArguments(fullscreen_check->isChecked()));
    status_label->setText(tr("Running…"));
}


#ifdef Q_OS_LINUX
// The AppImage keeps nothing of the user's in its own (read-only) folder: data in $XDG_DATA_HOME/kartrecomp, settings in
// $XDG_CONFIG_HOME/kartrecomp (see packaging/AppRun). This fills those from the game folder the first time (links for the big
// files, copies for saves and settings), writes the launcher's choices to launch.env, then runs packaging/make-appimage.sh.
void MainWindow::packageAppImage() {
    const QString app_dir = QCoreApplication::applicationDirPath();
    QString script;
    for (const QString& c : {QStringLiteral("../../packaging/make-appimage.sh"), QStringLiteral("../packaging/make-appimage.sh"),
                             QStringLiteral("packaging/make-appimage.sh")}) {
        if (QFileInfo::exists(QDir(app_dir).filePath(c))) {
            script = QDir(app_dir).absoluteFilePath(c);
            break;
        }
    }
    if (script.isEmpty() || !QFileInfo::exists(package.executable())) {
        QMessageBox::warning(this, tr("AppImage"), tr("Needs the built game (%1) and packaging/make-appimage.sh.").arg(package.executable()));
        return;
    }
    const QString home = QDir::homePath();
    const QString out = QFileDialog::getExistingDirectory(this, tr("Where should the AppImage go?"), QDir(home).filePath(QStringLiteral("Applications")));
    if (out.isEmpty()) return;
    const auto xdg = [&](const char* var, const char* fallback) {
        const QString v = qEnvironmentVariable(var);
        return QDir(v.isEmpty() ? QDir(home).filePath(QLatin1String(fallback)) : v).filePath(QStringLiteral("kartrecomp"));
    };
    const QDir data(xdg("XDG_DATA_HOME", ".local/share")), conf(xdg("XDG_CONFIG_HOME", ".config"));
    const QDir game_dir(package.root());
    QDir().mkpath(data.filePath(QStringLiteral("content/dlc")));
    QDir().mkpath(conf.path());
    const auto link = [](const QString& target, const QString& at) {
        if (QFileInfo::exists(target) && !QFileInfo(at).exists() && !QFileInfo(at).isSymLink()) QFile::link(target, at);
    };
    link(package.baseNsp(), data.filePath(QStringLiteral("content/base.nsp")));
    link(package.updateNsp(), data.filePath(QStringLiteral("content/update-3.0.4.nsp")));
    for (const auto& dlc : QDir(package.dlcDir()).entryInfoList({QStringLiteral("*.nsp")}, QDir::Files)) {
        link(dlc.absoluteFilePath(), data.filePath(QStringLiteral("content/dlc/") + dlc.fileName()));
    }
    link(game_dir.filePath(QStringLiteral("mods")), data.filePath(QStringLiteral("mods")));
    // Saves (and the rest of the emulated NAND) are copied once; the firmware next to them is linked from the installed suyu.
    const QDir suyu(QDir(package.suyuHome()).filePath(QStringLiteral("suyu")));
    if (!data.exists(QStringLiteral("nand"))) {
        QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-a"), game_dir.filePath(QStringLiteral("user/nand")), data.filePath(QStringLiteral("nand"))});
        const QString fw = data.filePath(QStringLiteral("nand/system/Contents"));
        if (QFileInfo::exists(suyu.filePath(QStringLiteral("nand/system/Contents")))) {
            QDir(fw).removeRecursively(); // the game folder's copy has no firmware in it
            QFile::link(suyu.filePath(QStringLiteral("nand/system/Contents")), fw);
        }
    }
    if (!data.exists(QStringLiteral("keys/prod.keys")) && suyu.exists(QStringLiteral("keys"))) {
        QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-a"), suyu.filePath(QStringLiteral("keys")), data.filePath(QStringLiteral("keys"))});
    }
    for (const QString& f : {QStringLiteral("sdl2-config.ini"), QStringLiteral("kartrecomp_overlay.ini")}) {
        if (!conf.exists(f)) QFile::copy(game_dir.filePath(QStringLiteral("user/config/") + f), conf.filePath(f));
    }
    // The launcher's renderer / resolution / pinning choices, read by AppRun.
    QFile env_file(conf.filePath(QStringLiteral("launch.env")));
    if (env_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const auto env = launchEnvironment();
        for (const QString& key : env.keys()) {
            if (key.startsWith(QLatin1String("KARTRECOMP_"))) env_file.write((key + QLatin1Char('=') + env.value(key) + QLatin1Char('\n')).toUtf8());
        }
    }
    log_view->clear();
    appimage_button->setEnabled(false);
    status_label->setText(tr("Packaging…"));
    appimage_progress->setValue(0);
    appimage_progress->show();
    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    // make-appimage.sh prints "[n/3] what it is doing" before each step.
    connect(proc, &QProcess::readyRead, this, [this, proc] {
        const QString text = QString::fromUtf8(proc->readAll()).trimmed();
        log_view->appendPlainText(text);
        static const QRegularExpression step(QStringLiteral("\\[(\\d)/3\\] ([^\\n]*)"));
        for (auto it = step.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            appimage_progress->setValue(m.captured(1).toInt() - 1);
            status_label->setText(tr("Packaging: %1…").arg(m.captured(2)));
        }
    });
    connect(proc, &QProcess::finished, this, [this, proc, out](int code) {
        appimage_button->setEnabled(true);
        appimage_progress->setValue(code == 0 ? 3 : appimage_progress->value());
        if (code != 0) appimage_progress->hide();
        status_label->setText(code == 0 ? tr("AppImage written to %1").arg(QDir(out).filePath(QStringLiteral("KartRecomp-x86_64.AppImage")))
                                        : tr("Packaging failed (see the output)."));
        proc->deleteLater();
    });
    proc->start(QStringLiteral("bash"), {script, package.root(), out});
}
#endif

QWidget* MainWindow::buildEnhancementsTab() {
    auto* page = enhancements_page = new QWidget;
    auto* form = new QFormLayout(page);

    upscaler_combo = new QComboBox;
    upscaler_combo->setToolTip(tr("Upscales (or, at 100%, anti-aliases) the 3D picture; the HUD is never upscaled. FSR 3 and DLSS are plugins "
                                  "found in the game's plugins folder; DLSS needs an NVIDIA GPU."));
    scale_combo = makeCombo({tr("Off (3D drawn at the output resolution)"), tr("100% (DLAA / native anti-aliasing)"), tr("67% (quality)"),
                             tr("58% (balanced)"), tr("50% (performance)"), tr("33% (ultra performance)"), tr("150% (supersampling)"),
                             tr("200% (supersampling)")});
    scale_combo->setToolTip(tr("How large the 3D scene is drawn compared with the output resolution. Below 100% needs a temporal upscaler."));
    sharpness_combo = makeCombo({tr("Off"), tr("Low"), tr("Medium"), tr("High")});
    jitter_check = new QCheckBox(tr("Camera jitter (needed by temporal upscalers; they switch it on themselves)"));
    framegen_combo = new QComboBox;
    framegen_combo->setToolTip(tr("Doubles the frame rate with a generated frame between each pair of real ones. The HUD is drawn over "
                                  "the generated frame, so it never smears. Needs a temporal upscaler."));
    hdr_check = new QCheckBox(tr("HDR10 output (needs the display in HDR mode)"));
    hdr_paper_combo = makeCombo({tr("100 nits"), tr("150 nits"), tr("203 nits (standard)"), tr("250 nits"), tr("300 nits")});
    hdr_peak_combo = makeCombo({tr("400 nits"), tr("600 nits"), tr("800 nits"), tr("1000 nits"), tr("1500 nits")});
    present_combo = makeCombo({tr("Mailbox"), tr("FIFO (vsync)"), tr("Immediate")});
    fps_check = new QCheckBox(tr("Show an FPS counter"));

    form->addRow(tr("Upscaler:"), upscaler_combo);
    form->addRow(tr("3D render scale:"), scale_combo);
    form->addRow(tr("Sharpening:"), sharpness_combo);
    form->addRow(jitter_check);
    form->addRow(tr("Frame generation:"), framegen_combo);
    form->addRow(hdr_check);
    form->addRow(tr("HDR paper white:"), hdr_paper_combo);
    form->addRow(tr("HDR peak brightness:"), hdr_peak_combo);
    form->addRow(tr("Present mode:"), present_combo);
    form->addRow(fps_check);
    auto* note = new QLabel(
        tr("These are the same settings as the in-game menu (F10, or both stick buttons on a controller) and they are shared: a change in "
           "game is saved here, and the values below are what the game starts with. They only apply to the Native Vulkan renderer, and are "
           "locked while the game runs."));
    note->setWordWrap(true);
    form->addRow(note);

    const auto save = [this] { saveOverlay(); };
    for (auto* combo : {upscaler_combo, scale_combo, sharpness_combo, framegen_combo, hdr_paper_combo, hdr_peak_combo, present_combo}) {
        connect(combo, &QComboBox::currentIndexChanged, this, save);
    }
    for (auto* check : {jitter_check, hdr_check, fps_check}) {
        connect(check, &QCheckBox::toggled, this, save);
    }
    connect(hdr_check, &QCheckBox::toggled, this, [this](bool on) {
        hdr_paper_combo->setEnabled(on);
        hdr_peak_combo->setEnabled(on);
    });
    return page;
}

void MainWindow::loadOverlay() {
    loading_settings = true;
    const QSignalBlocker b1(upscaler_combo), b2(scale_combo), b3(sharpness_combo), b4(framegen_combo), b5(hdr_paper_combo), b6(hdr_peak_combo),
        b7(present_combo), b8(jitter_check), b9(hdr_check), b10(fps_check);
    overlay.load(QDir(package.root()).filePath(QStringLiteral("user/config/kartrecomp_overlay.ini")));

    upscaler_ids = QStringList{QStringLiteral("off"), QStringLiteral("fsr1")} + FindPluginIds(package.root(), QStringLiteral("upscaler"));
    upscaler_combo->clear();
    for (const auto& id : upscaler_ids) {
        upscaler_combo->addItem(id == QLatin1String("off")    ? tr("Off")
                                : id == QLatin1String("fsr1") ? tr("FSR 1 (spatial)")
                                : id == QLatin1String("fsr3") ? tr("FSR 3.1 (temporal)")
                                : id == QLatin1String("dlss") ? tr("NVIDIA DLSS")
                                                              : id);
    }
    upscaler_combo->setCurrentIndex(qMax(0, upscaler_ids.indexOf(overlay.value(QStringLiteral("upscaler"), QStringLiteral("off")))));

    framegen_ids = QStringList{QStringLiteral("off"), QStringLiteral("builtin")} + FindPluginIds(package.root(), QStringLiteral("framegen"));
    if (!framegen_ids.contains(QStringLiteral("fsr3")) && FindPluginIds(package.root(), QStringLiteral("upscaler")).contains(QStringLiteral("fsr3"))) {
        framegen_ids << QStringLiteral("fsr3"); // the FSR 3 library carries its frame generation
    }
    framegen_combo->clear();
    for (const auto& id : framegen_ids) {
        framegen_combo->addItem(id == QLatin1String("off")       ? tr("Off")
                                : id == QLatin1String("builtin") ? tr("Built-in (2x)")
                                : id == QLatin1String("fsr3")    ? tr("FSR 3 frame generation (2x)")
                                                                 : id + QStringLiteral(" (2x)"));
    }
    framegen_combo->setCurrentIndex(qMax(0, framegen_ids.indexOf(overlay.value(QStringLiteral("framegen"), QStringLiteral("off")))));

    scale_combo->setCurrentIndex(qBound(0, overlay.intValue(QStringLiteral("quality"), 0), scale_combo->count() - 1));
    sharpness_combo->setCurrentIndex(qBound(0, overlay.intValue(QStringLiteral("sharpness"), 3), 3));
    jitter_check->setChecked(overlay.intValue(QStringLiteral("jitter"), 0) != 0);
    hdr_check->setChecked(overlay.intValue(QStringLiteral("hdr"), 0) != 0);
    hdr_paper_combo->setCurrentIndex(qBound(0, overlay.intValue(QStringLiteral("hdr_paper"), 1), 4));
    hdr_peak_combo->setCurrentIndex(qBound(0, overlay.intValue(QStringLiteral("hdr_peak"), 0), 4));
    hdr_paper_combo->setEnabled(hdr_check->isChecked());
    hdr_peak_combo->setEnabled(hdr_check->isChecked());
    present_combo->setCurrentIndex(qBound(0, overlay.intValue(QStringLiteral("present"), 0), 2));
    fps_check->setChecked(overlay.intValue(QStringLiteral("fps"), 0) != 0);
    loading_settings = false;
}

void MainWindow::saveOverlay() {
    if (loading_settings || game.state() != QProcess::NotRunning || package.root().isEmpty()) return;
    // Re-read first, so anything the game saved since (the speed limit, the resolution rows) is kept.
    overlay.load(QDir(package.root()).filePath(QStringLiteral("user/config/kartrecomp_overlay.ini")));
    overlay.setValue(QStringLiteral("upscaler"), upscaler_ids.value(upscaler_combo->currentIndex(), QStringLiteral("off")));
    overlay.setValue(QStringLiteral("framegen"), framegen_ids.value(framegen_combo->currentIndex(), QStringLiteral("off")));
    overlay.setInt(QStringLiteral("quality"), scale_combo->currentIndex());
    overlay.setInt(QStringLiteral("sharpness"), sharpness_combo->currentIndex());
    overlay.setInt(QStringLiteral("jitter"), jitter_check->isChecked() ? 1 : 0);
    overlay.setInt(QStringLiteral("hdr"), hdr_check->isChecked() ? 1 : 0);
    overlay.setInt(QStringLiteral("hdr_paper"), hdr_paper_combo->currentIndex());
    overlay.setInt(QStringLiteral("hdr_peak"), hdr_peak_combo->currentIndex());
    overlay.setInt(QStringLiteral("present"), present_combo->currentIndex());
    overlay.setInt(QStringLiteral("fps"), fps_check->isChecked() ? 1 : 0);
    if (!overlay.save()) {
        status_label->setText(tr("Could not write %1").arg(overlay.path()));
    }
}

QWidget* MainWindow::buildModsTab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* note = new QLabel(tr("Mods for the game. Standard Mario Kart 8 Deluxe data mods work: a folder or zip with a romfs (and exefs) folder, "
                               "in the usual layouts (plain, Atmosphere's atmosphere/contents/..., or yuzu's load/...). Their files are layered over "
                               "the game's own. Code patches (exefs, IPS) cannot be applied to this recompiled build and are marked. Everything "
                               "is kept inside the game folder."));
    note->setWordWrap(true);
    mods_list = new QListWidget;
    mod_details = new QLabel;
    mod_details->setWordWrap(true);
    mod_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mod_progress = new QProgressBar;
    mod_progress->hide();

    mod_import_button = new QPushButton(tr("Convert CTGP DX zip…"));
    mod_add_folder_button = new QPushButton(tr("Add mod folder…"));
    mod_add_zip_button = new QPushButton(tr("Add mod zip…"));
    mod_toggle_button = new QPushButton(tr("Disable"));
    mod_remove_button = new QPushButton(tr("Remove"));
    mod_recomp_button = new QPushButton(tr("About this conversion"));
    connect(mods_list, &QListWidget::currentRowChanged, this, [this] { refreshMods(); });
    // Each mod has its own check box: ticking turns it on, clearing turns it off (the folder is renamed, nothing is deleted).
    connect(mods_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (importing || game.state() != QProcess::NotRunning) return;
        const int row = mods_list->row(item);
        const bool want = item->checkState() == Qt::Checked;
        QString error;
        bool ok = true;
        // CTGP's extra cups only run with every other mod off (modded drivers, karts and tracks crash races beside them):
        // turning one side on turns the other side off, after asking.
        if (want && !resolveCtgpConflict(row == 0, row != 0)) {
            const QSignalBlocker block(mods_list);
            item->setCheckState(Qt::Unchecked);
            return;
        }
        if (row == 0) {
            if (ModAddon::CtgpState(package.root()) != ModAddon::State::NotImported) ok = ModAddon::SetCtgpEnabled(package.root(), want, &error);
        } else if (row - 1 < mod_entries.size() && mod_entries[row - 1].enabled != want) {
            ok = ModAddon::SetModEnabled(package.root(), mod_entries[row - 1].name, want, &error);
        }
        if (!ok) QMessageBox::warning(this, tr("Mods"), error);
        refreshMods();
        syncTextMerge();
    });
    connect(mod_import_button, &QPushButton::clicked, this, &MainWindow::importCtgp);
    connect(mod_add_folder_button, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select a mod folder (or a folder of mods)"));
        if (dir.isEmpty()) return;
        // A folder that is itself a mod, or a folder holding several mods (one per sub-folder).
        QStringList sources;
        const QDir d(dir);
        const bool is_mod = QFileInfo(d.filePath(QStringLiteral("romfs"))).isDir() || QFileInfo(d.filePath(QStringLiteral("exefs"))).isDir() ||
                            QFileInfo(d.filePath(QStringLiteral("atmosphere"))).isDir();
        if (is_mod) sources << dir;
        else for (const auto& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) sources << d.filePath(sub);
        const QString root = package.root();
        runImport([sources, root](QString* error, const std::function<bool(int, int, const QString&)>& progress) {
            bool any = false;
            for (const auto& s : sources) {
                QString e;
                if (ModAddon::ImportModFolder(s, root, &e, progress)) any = true;
                else if (!e.contains(QLatin1String("No romfs or exefs")) && error) *error += e + QLatin1Char('\n'); // sub-folders that are not mods stay quiet
            }
            if (!any && error && error->isEmpty()) *error = QStringLiteral("No mods found in that folder.");
            return any || (error && !error->isEmpty());
        });
    });
    connect(mod_add_zip_button, &QPushButton::clicked, this, [this] {
        const QStringList zips = QFileDialog::getOpenFileNames(this, tr("Select mod zips"), {}, tr("Zip files (*.zip)"));
        if (zips.isEmpty()) return;
        const QString root = package.root();
        runImport([zips, root](QString* error, const std::function<bool(int, int, const QString&)>& progress) {
            bool any = false;
            for (const auto& z : zips) {
                QString e;
                if (ModAddon::ImportModZip(z, root, &e, progress)) any = true;
                else if (error) *error += QFileInfo(z).fileName() + QStringLiteral(": ") + e + QLatin1Char('\n');
            }
            return any;
        });
    });
    connect(mod_toggle_button, &QPushButton::clicked, this, [this] {
        QString error;
        const int row = mods_list->currentRow();
        bool ok = true;
        if (row <= 0) {
            const bool enable = ModAddon::CtgpState(package.root()) == ModAddon::State::Disabled;
            ok = ModAddon::SetCtgpEnabled(package.root(), enable, &error);
        } else if (row - 1 < mod_entries.size()) {
            const auto& m = mod_entries[row - 1];
            ok = ModAddon::SetModEnabled(package.root(), m.name, !m.enabled, &error);
        }
        if (!ok) QMessageBox::warning(this, tr("Mods"), error);
        refreshMods();
        syncTextMerge();
    });
    connect(mod_remove_button, &QPushButton::clicked, this, [this] {
        const int row = mods_list->currentRow();
        const QString name = row <= 0 ? tr("CTGP DX") : (row - 1 < mod_entries.size() ? mod_entries[row - 1].name : QString{});
        if (name.isEmpty() || QMessageBox::question(this, tr("Remove mod"), tr("Delete %1 from the game folder?").arg(name)) != QMessageBox::Yes) return;
        QString error;
        const bool ok = row <= 0 ? ModAddon::RemoveCtgp(package.root(), &error) : ModAddon::RemoveMod(package.root(), name, &error);
        if (!ok) QMessageBox::warning(this, tr("Mods"), error);
        refreshMods();
        syncTextMerge();
    });
    connect(mod_recomp_button, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, tr("CTGP DX"), ModAddon::CodeRecompBlocker(mod_info));
    });
    connect(this, &MainWindow::importProgress, this, [this](int done, int total, const QString& name) {
        mod_progress->setMaximum(total);
        mod_progress->setValue(done);
        mod_progress->setFormat(QStringLiteral("%1 / %2  %3").arg(done).arg(total).arg(name));
    });
    connect(this, &MainWindow::importFinished, this, [this](bool ok, const QString& error) {
        importing = false;
        mod_progress->hide();
        if (!ok || !error.isEmpty()) QMessageBox::warning(this, tr("Import"), error.isEmpty() ? tr("Nothing was imported.") : error);
        refreshMods();
        syncTextMerge();
    });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(mod_add_folder_button);
    buttons->addWidget(mod_add_zip_button);
    buttons->addWidget(mod_import_button);
    buttons->addStretch();
    auto* buttons2 = new QHBoxLayout;
    buttons2->addWidget(mod_toggle_button);
    buttons2->addWidget(mod_remove_button);
    buttons2->addStretch();
    buttons2->addWidget(mod_recomp_button);
    layout->addWidget(note);
    layout->addWidget(mods_list, 1);
    layout->addWidget(mod_details);
    layout->addWidget(mod_progress);
    layout->addLayout(buttons);
    layout->addLayout(buttons2);
    return page;
}

void MainWindow::refreshMods() {
    const QString root = package.root();
    const bool have_root = !root.isEmpty();
    const bool idle = !importing && game.state() == QProcess::NotRunning;
    const int keep_row = mods_list->currentRow();
    const QSignalBlocker block(mods_list);
    mods_list->clear();
    const auto ctgp = have_root ? ModAddon::CtgpState(root) : ModAddon::State::NotImported;
    const auto add_item = [this](const QString& text, bool checkable, bool checked) {
        auto* item = new QListWidgetItem(text);
        item->setFlags(item->flags() | (checkable ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
        if (checkable) item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        mods_list->addItem(item);
    };
    add_item(ctgp == ModAddon::State::NotImported ? tr("CTGP DX  (not imported)")
             : ctgp == ModAddon::State::Enabled   ? tr("CTGP DX")
                                                  : tr("CTGP DX  (off)"),
             ctgp != ModAddon::State::NotImported, ctgp == ModAddon::State::Enabled);
    mod_entries = have_root ? ModAddon::ListMods(root) : QList<ModAddon::ModEntry>{};
    for (const auto& m : mod_entries) {
        add_item(m.name + (m.has_exefs ? tr("  [has code patches]") : QString{}), true, m.enabled);
    }
    mods_list->setCurrentRow(qBound(0, keep_row < 0 ? 0 : keep_row, mods_list->count() - 1));
    const int row = mods_list->currentRow();

    QString text;
    bool selected_enabled = true, selected_exists = true;
    if (row <= 0) {
        switch (ctgp) {
        case ModAddon::State::NotImported:
            text = tr("CTGP DX is not added yet. Choose your own CTGP Deluxe zip (the release for Mario Kart 8 Deluxe, the one with an "
                      "atmosphere/contents folder) and its 64 tracks are added as extra cups with their own names, pictures and music. None of the "
                      "game's courses are changed. Pick a cup in the in-game menu (F10, \"Extra cup\") before starting a Grand Prix. The pack's code "
                      "cannot run in this recompiled build; see \"About this conversion\".");
            selected_exists = false;
            break;
        case ModAddon::State::Enabled:
        case ModAddon::State::Disabled: {
            selected_enabled = ctgp == ModAddon::State::Enabled;
            text = selected_enabled ? tr("CTGP DX: added, enabled. 16 extra cups (F10 menu, \"Extra cup\"); the game's own courses are untouched.")
                                    : tr("CTGP DX: added, turned off (the game plays normally).");
            text += QLatin1Char('\n') + ModAddon::CodeRecompBlocker(mod_info);
            break;
        }
        }
    } else if (row - 1 < mod_entries.size()) {
        const auto& m = mod_entries[row - 1];
        selected_enabled = m.enabled;
        text = tr("%1: %2, %3 files (%4 MB).").arg(m.name, m.enabled ? tr("enabled") : tr("disabled")).arg(m.files).arg(m.bytes / (1024 * 1024));
        text += QLatin1Char('\n') + (m.has_romfs ? tr("Data (romfs): applied over the game's files.") : tr("No romfs data."));
        if (m.has_exefs) text += QLatin1Char('\n') + tr("Code patches (exefs): this recompiled build cannot apply them, so they are ignored.");
    } else {
        selected_exists = false;
    }
    mod_details->setText(text);
    mod_import_button->setEnabled(have_root && idle);
    mod_add_folder_button->setEnabled(have_root && idle);
    mod_add_zip_button->setEnabled(have_root && idle);
    mod_toggle_button->setEnabled(idle && selected_exists);
    mod_toggle_button->setText(selected_enabled ? tr("Disable") : tr("Enable"));
    mod_remove_button->setEnabled(idle && selected_exists);
    mod_recomp_button->setEnabled(row <= 0 && ctgp != ModAddon::State::NotImported);
    mod_recomp_button->setToolTip(tr("Shows why the pack's code cannot be recompiled into this build yet."));
}

void MainWindow::runImport(const std::function<bool(QString*, const std::function<bool(int, int, const QString&)>&)>& job) {
    importing = true;
    import_cancel = false;
    mod_progress->setValue(0);
    mod_progress->show();
    refreshMods();
    QThread* worker = QThread::create([this, job] {
        QString error;
        const bool ok = job(&error, [this](int done, int total, const QString& name) {
            emit importProgress(done, total, name);
            return !import_cancel.load();
        });
        emit importFinished(ok, error);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

namespace {

// Reads the game's own UI/<lang>/message.sarc files (no mods, update applied) into `dir`; the game exits after writing them.
bool DumpVanillaUi(const QString& root, const QStringList& args, QProcessEnvironment env, const QString& dir, QString* error) {
    QDir(dir).removeRecursively();
    QProcess dump;
    env.insert(QStringLiteral("KARTRECOMP_ROMFS_DUMP"), QStringLiteral("tree=UI=%1/UI=message.sarc").arg(dir));
    env.insert(QStringLiteral("KARTRECOMP_NO_MODS"), QStringLiteral("1"));
    dump.setProcessEnvironment(env);
    dump.setWorkingDirectory(root);
    dump.start(QDir(root).filePath(QStringLiteral("mk8d")), args);
    if (!dump.waitForFinished(240000) || dump.exitCode() != 0) {
        if (error) *error = QStringLiteral("Could not read the game's message files (is the game package set up?)");
        return false;
    }
    return true;
}

} // namespace

void MainWindow::syncTextMerge() {
    const QString root = package.root();
    if (importing || root.isEmpty()) return;
    if (!ModAddon::NeedsTextMerge(root)) {
        if (!ModAddon::HasTextConflict(root)) {
            QString error;
            ModAddon::UpdateTextMerge(root, {}, &error); // removes a stale merged copy when the conflict is gone
        }
        return;
    }
    const QStringList args = package.launchArguments(false);
    const auto env = package.launchEnvironment();
    runImport([root, args, env](QString* error, const std::function<bool(int, int, const QString&)>& progress) {
        progress(0, 1, QStringLiteral("merging mod texts"));
        const QString vanilla = ModAddon::VanillaUiDir(root);
        if (!QFileInfo::exists(QDir(vanilla).filePath(QStringLiteral("UI/USen/message.sarc"))) && !DumpVanillaUi(root, args, env, vanilla, error)) return false;
        const bool ok = ModAddon::UpdateTextMerge(root, vanilla, error);
        progress(1, 1, QStringLiteral("done"));
        return ok;
    });
}

// CTGP DX's cups and other mods exclude each other. Returns false when the user keeps things as they were / cancels.
// `ctgp_wanted` = the user just ticked CTGP, `other_wanted` = another mod was just ticked; both false = the game is about to start.
bool MainWindow::resolveCtgpConflict(bool ctgp_wanted, bool other_wanted) {
    const QString root = package.root();
    if (ModAddon::CtgpState(root) == ModAddon::State::NotImported) return true;
    const bool ctgp_on = ctgp_wanted || ModAddon::CtgpState(root) == ModAddon::State::Enabled;
    QStringList others;
    for (const auto& m : ModAddon::ListMods(root)) {
        if (m.enabled) others << m.name;
    }
    if (!ctgp_on || (others.isEmpty() && !other_wanted)) return true;
    if (ctgp_wanted) {
        const auto answer = QMessageBox::question(this, tr("CTGP DX"),
            tr("CTGP DX's cups only work with every other mod turned off.\n\nTurn off these mods?\n\n%1").arg(others.join(QLatin1Char('\n'))));
        if (answer != QMessageBox::Yes) return false;
        for (const auto& name : others) ModAddon::SetModEnabled(root, name, false, nullptr);
        refreshMods();
        syncTextMerge();
        return true;
    }
    const auto answer = QMessageBox::question(this, tr("CTGP DX"),
        tr("CTGP DX's cups only work with every other mod turned off.\n\nTurn CTGP DX off and keep your other mods?"));
    if (answer != QMessageBox::Yes) return false;
    ModAddon::SetCtgpEnabled(root, false, nullptr);
    refreshMods();
    syncTextMerge();
    return true;
}

void MainWindow::importCtgp() {
    const QString zip = QFileDialog::getOpenFileName(this, tr("Select the CTGP DX zip"), {}, tr("Zip files (*.zip)"));
    if (zip.isEmpty()) return;
    const auto info = ModAddon::AnalyzeCtgpPack(zip);
    if (!info.valid) {
        QMessageBox::warning(this, tr("CTGP DX"), info.error);
        return;
    }
    mod_info = info;
    if (QMessageBox::question(this, tr("Add CTGP DX"),
                              tr("This adds CTGP DX's 64 tracks as extra cups. None of the game's own courses are changed or replaced.\n\n%1\n\n"
                                 "It takes a few minutes and about 5 GB. Continue?").arg(ModAddon::CodeRecompBlocker(info))) != QMessageBox::Yes) return;
    const QString root = package.root();
    const QString out = QDir(root).filePath(QStringLiteral("mods/%1/CTGP DX Cups").arg(QLatin1String(ModAddon::kMk8dTitleId)));
    // Where the converted mod goes; an older conversion (or a disabled one) is replaced.
    const QStringList args = package.launchArguments(false);
    const auto env = package.launchEnvironment();
    runImport([zip, root, out, args, env](QString* error, const std::function<bool(int, int, const QString&)>& progress) {
        // The game's own message files come from the game itself (it can list and extract its RomFS), so the names are edited in the user's copy.
        const QString tmp = ModAddon::VanillaUiDir(root);
        if (!DumpVanillaUi(root, args, env, tmp, error)) return false;
        CtgpConvert::Options options;
        options.cups_list_path = QDir(root).filePath(QStringLiteral("extra_cups.txt"));
        QString old_error;
        ModAddon::SetModEnabled(root, QStringLiteral("CTGP DX (tracks)"), false, &old_error); // the old slot-replacing conversion must not stay on
        return CtgpConvert::Convert(zip, tmp, out, options, error, progress);
    });
}

void MainWindow::appendLog(const QString& text) {
    // suyu colours its console output; strip the ANSI escapes for the log view.
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*m"));
    QString clean = text;
    clean.remove(ansi);
    log_view->moveCursor(QTextCursor::End);
    log_view->insertPlainText(clean);
    log_view->moveCursor(QTextCursor::End);
}
