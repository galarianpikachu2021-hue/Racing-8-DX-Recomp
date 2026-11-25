// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_page.h"

#include <QCheckBox>
#include <QDir>
#include <QGuiApplication>
#include <QThread>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <thread>

#include <QComboBox>
#include <algorithm>

#include "build_runner.h"
#include "launcher_language.h"
#include "keys_check.h"
#include "nsp_info.h"
#include "setup_pipeline.h"

namespace {
QString StatusText(const QString& reason) { return reason.isEmpty() ? QStringLiteral("✔") : QStringLiteral("✘ ") + reason; }
}

SetupPage::SetupPage(QWidget* parent)
    : QWidget(parent), paths(BuilderPaths::Detect()), layout(DataLayout::Default()) {
    auto* top = new QVBoxLayout(this);
    {
        // The launcher's language (the game's own is the Console language on the Play tab). It applies after a restart.
        auto* row = new QHBoxLayout;
        auto* language = new QComboBox;
        for (const auto& l : LauncherLanguage::Available())
            language->addItem(l.code.isEmpty() ? tr("Automatic (system language)") : l.native, l.code);
        language->setCurrentIndex(std::max(0, language->findData(LauncherLanguage::Chosen())));
        language->setToolTip(tr("The language of this launcher. The game's own language is the Console language on the Play tab."));
        connect(language, &QComboBox::activated, this, [this, language] {
            const QString code = language->currentData().toString();
            if (code == LauncherLanguage::Chosen()) return;
            LauncherLanguage::SetChosen(code);
            if (QMessageBox::question(this, tr("Launcher language"), tr("The new language is used when the launcher starts again. Restart it now?"))
                == QMessageBox::Yes)
                LauncherLanguage::Restart();
        });
        row->addWidget(new QLabel(tr("Launcher language:")));
        row->addWidget(language);
        row->addStretch();
        top->addLayout(row);
    }
    auto* intro = new QLabel(tr("Pick your own game files. Nothing leaves your PC: the game is built here from them, in a few minutes, "
                                "and stays on this PC."));
    intro->setWordWrap(true);
    top->addWidget(intro);
    // Two ways to give the game: the NSP files with keys and firmware (everything works), or already decrypted files (no keys or
    // firmware; DLC and the screens that need firmware data are not available).
    mode = new QComboBox;
    mode->addItems({tr("Game files (NSP) with keys and firmware"), tr("Decrypted files (no keys or firmware)")});
    top->addWidget(mode);
    nspBox = new QWidget;
    auto* grid = new QGridLayout(nspBox);
    grid->setContentsMargins(0, 0, 0, 0);
    top->addWidget(nspBox);
    decBox = new QWidget;
    auto* dgrid = new QGridLayout(decBox);
    dgrid->setContentsMargins(0, 0, 0, 0);
    top->addWidget(decBox);
    exefs = addRow(dgrid, 0, tr("Game code"), tr("ExeFS folder: main, main.npdm, rtld, sdk, subsdk0 (update 3.0.4 or 4.0.1 applied)"),
                   [this] { pick(exefs, true, tr("Select the ExeFS folder"), {}); });
    romfs = addRow(dgrid, 1, tr("Game data"), tr("RomFS folder (the game's files), or a romfs.bin image"), [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select the RomFS folder (cancel to pick a romfs.bin instead)"));
        if (!dir.isEmpty()) { romfs.edit->setText(dir); revalidate(); return; }
        pick(romfs, false, tr("Select the RomFS image"), tr("RomFS images (*.bin *.romfs);;All files (*)"));
    });
    decKeys = addRow(dgrid, 2, tr("Keys (optional)"), tr("prod.keys, only to read the firmware"), [this] { pick(decKeys, false, tr("Select prod.keys"), tr("Key files (*.keys)")); });
    decFirmware = addRow(dgrid, 3, tr("Firmware (optional)"), tr("Folder of firmware files, or a .zip: gives the game its Mii data"), [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select the firmware folder (cancel to pick a zip instead)"));
        if (!dir.isEmpty()) { decFirmware.edit->setText(dir); revalidate(); return; }
        pick(decFirmware, false, tr("Select the firmware zip"), tr("Zip files (*.zip)"));
    });
    auto* decNote = new QLabel(tr("The code and data are used where they are (nothing is copied or moved), so keep them there. DLC is not available "
                                  "in this mode. Keys and firmware are optional: without them the game runs, but Miis have no faces or bodies "
                                  "(Mii racers, the Mii screens and the Mii editor need the firmware)."));
    decNote->setWordWrap(true);
    dgrid->addWidget(decNote, 8, 0, 1, 3);
    base = addRow(grid, 0, tr("Base game"), tr("Mario Kart 8 Deluxe (.nsp)"), [this] { pick(base, false, tr("Select the base game"), tr("NSP files (*.nsp)")); });
    update = addRow(grid, 1, tr("Update"), tr("The 3.0.4 or 4.0.1 update (.nsp)"), [this] { pick(update, false, tr("Select the 3.0.4 or 4.0.1 update"), tr("NSP files (*.nsp)")); });
    dlcRow = addRow(grid, 2, tr("DLC (optional)"), tr("Booster Course Pass and other add-ons (.nsp)"), [this] { pickDlc(); });
    keys = addRow(grid, 3, tr("Keys"), tr("prod.keys from your own console"), [this] { pick(keys, false, tr("Select prod.keys"), tr("Key files (*.keys)")); });
    firmware = addRow(grid, 4, tr("Firmware"), tr("Folder of firmware files, or a .zip"), [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select the firmware folder (cancel to pick a zip instead)"));
        if (!dir.isEmpty()) { firmware.edit->setText(dir); revalidate(); return; }
        pick(firmware, false, tr("Select the firmware zip"), tr("Zip files (*.zip)"));
    });
    split60 = new QCheckBox(tr("60 fps split-screen in 3-4 player races (the game's own is 30 fps per player). Needs a fast CPU: when the PC can't keep 60, the race slows down. "
                               "Changing it needs Rebuild game, which only rebuilds the part that changes."));
    split60->setToolTip(tr("Built into the game when it is compiled (no JIT): every player's view is drawn every frame."));
    top->addWidget(split60);
    delete_build_files = new QCheckBox(tr("Delete the build files after building (saves about 6 GB; the next build takes a few minutes longer)"));
    delete_build_files->setToolTip(tr("The game runs from the built module alone. The read game code and the compiled pieces are kept only "
                                      "so that a later build can reuse them."));
    top->addWidget(delete_build_files);
    auto* buttons = new QHBoxLayout;
    buildButton = new QPushButton(tr("Build game"));
    cancelButton = new QPushButton(tr("Cancel"));
    playButton = new QPushButton(tr("Play"));
    playButton->setMinimumHeight(40);
    buttons->addWidget(buildButton);
    buttons->addWidget(cancelButton);
    buttons->addStretch();
    buttons->addWidget(playButton);
    top->addLayout(buttons);
    bar = new QProgressBar;
    bar->setRange(0, 1000);
    top->addWidget(bar);
    message = new QLabel;
    message->setWordWrap(true);
    top->addWidget(message);
    top->addStretch();

    connect(buildButton, &QPushButton::clicked, this, &SetupPage::build);
    connect(cancelButton, &QPushButton::clicked, this, &SetupPage::cancel);
    connect(playButton, &QPushButton::clicked, this, &SetupPage::playRequested);
    QSettings s;
    base.edit->setText(s.value("setup/base").toString());
    update.edit->setText(s.value("setup/update").toString());
    keys.edit->setText(s.value("setup/keys").toString());
    firmware.edit->setText(s.value("setup/firmware").toString());
    dlc = s.value("setup/dlc").toStringList();
    exefs.edit->setText(s.value("setup/exefs").toString());
    romfs.edit->setText(s.value("setup/romfs").toString());
    decKeys.edit->setText(s.value("setup/dec_keys").toString());
    decFirmware.edit->setText(s.value("setup/dec_firmware").toString());
    mode->setCurrentIndex(s.value("setup/decrypted", layout.decrypted()).toBool() ? 1 : 0);
    const auto showMode = [this] {
        nspBox->setVisible(mode->currentIndex() == 0);
        decBox->setVisible(mode->currentIndex() == 1);
    };
    showMode();
    connect(mode, &QComboBox::currentIndexChanged, this, [this, showMode] {
        showMode();
        revalidate();
    });
    split60->setChecked(s.value("setup/split60", false).toBool());
    delete_build_files->setChecked(s.value("setup/delete_build_files", false).toBool());
    dlcRow.edit->setText(dlc.join("; "));
    QString missing;
    if (!paths.complete(&missing)) message->setText(missing);
    else if (const QString version = BuiltForVersion(layout); !version.isEmpty() && version != paths.engineVersion)
        message->setText(tr("The game in this folder was built by another version of this program, so Play is off. Press Rebuild game, or use "
                            "the version it was built with. (A game copied from another computer needs the same version there too.)"));
    revalidate();
    refreshButtons();
    if (QFileInfo::exists(layout.moduleDir())) emit builtChanged(IsBuilt(layout, paths.engineVersion));
}

SetupPage::~SetupPage() { stopAndWait(); }

void SetupPage::stopAndWait() {
    cancelFlag = true;
    if (worker.joinable()) worker.join();
}

void SetupPage::setGameRunning(bool running) {
    gameRunning = running;
    refreshButtons();
}

SetupPage::Row SetupPage::addRow(QGridLayout* grid, int r, const QString& title, const QString& hint, std::function<void()> browse) {
    Row row;
    grid->addWidget(new QLabel("<b>" + title + "</b>"), r * 2, 0);
    row.edit = new QLineEdit;
    row.edit->setPlaceholderText(hint);
    row.edit->setReadOnly(true);
    auto* button = new QPushButton(tr("Browse…"));
    connect(button, &QPushButton::clicked, this, [browse] { browse(); });
    grid->addWidget(row.edit, r * 2, 1);
    grid->addWidget(button, r * 2, 2);
    row.status = new QLabel;
    row.status->setWordWrap(true);
    grid->addWidget(row.status, r * 2 + 1, 1, 1, 2);
    grid->setColumnStretch(1, 1);
    return row;
}

void SetupPage::pick(Row& row, bool folder, const QString& title, const QString& filter) {
    const QString start = row.edit->text().isEmpty() ? QDir::homePath() : QFileInfo(row.edit->text()).absolutePath();
    const QString path = folder ? QFileDialog::getExistingDirectory(this, title, start) : QFileDialog::getOpenFileName(this, title, start, filter);
    if (path.isEmpty()) return;
    row.edit->setText(path);
    revalidate();
}

void SetupPage::pickDlc() {
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Select DLC (.nsp)"), QDir::homePath(), tr("NSP files (*.nsp)"));
    if (files.isEmpty()) return;
    dlc = files;
    dlcRow.edit->setText(dlc.join("; "));
    revalidate();
}

SetupInputs SetupPage::inputs() const {
    SetupInputs in{base.edit->text(), update.edit->text(), keys.edit->text(), firmware.edit->text(), dlc, {}, {}, {}};
    if (mode->currentIndex() == 1) {
        in.exefs = exefs.edit->text().isEmpty() ? QStringLiteral(" ") : exefs.edit->text(); // decrypted mode even while still empty
        in.romfs = romfs.edit->text();
        in.keys = decKeys.edit->text();
        in.firmware = decFirmware.edit->text();
    }
    if (split60->isChecked()) in.patches << QStringLiteral("split60");
    in.deleteBuildFiles = delete_build_files->isChecked();
    return in;
}

// Once a game file has been moved into the data folder, the original path is gone: follow the file to where it now lives.
void SetupPage::adoptInstalled() {
    if (!QFileInfo::exists(base.edit->text()) && QFileInfo::exists(layout.base())) base.edit->setText(layout.base());
    if (!QFileInfo::exists(update.edit->text()) && QFileInfo::exists(layout.update())) update.edit->setText(layout.update());
    for (QString& d : dlc)
        if (!QFileInfo::exists(d) && QFileInfo::exists(layout.dlcDir() + "/" + QFileInfo(d).fileName())) d = layout.dlcDir() + "/" + QFileInfo(d).fileName();
    // The settings may be gone while the game is installed (a new settings folder, a copied data folder): show what is installed.
    if (!QFileInfo(keys.edit->text()).isFile() && !layout.installedKeys().isEmpty()) keys.edit->setText(layout.installedKeys());
    if (!QFileInfo::exists(firmware.edit->text()) && !layout.installedFirmware().isEmpty()) firmware.edit->setText(layout.installedFirmware());
    if (dlc.isEmpty() && !layout.installedDlc().isEmpty()) dlc = layout.installedDlc();
    if (!dlc.isEmpty()) dlcRow.edit->setText(dlc.join("; "));
}

void SetupPage::revalidate() {
    adoptInstalled();
    const SetupInputs in = inputs();
    const bool exporterOk = QFileInfo::exists(paths.exporter);
    const auto show = [](Row& r, const QString& reason, bool empty) { r.status->setText(empty ? QString() : StatusText(reason)); };
    if (mode->currentIndex() == 1) {
        const QString code = exefs.edit->text(), data = romfs.edit->text();
        show(exefs, ValidateExefs(code), code.isEmpty());
        show(romfs, ValidateRomfs(data), data.isEmpty());
        const QString k = decKeys.edit->text(), fw = decFirmware.edit->text();
        const KeyProblem kp = k.isEmpty() ? KeyProblem::None : CheckKeys(k);
        show(decKeys, k.isEmpty() ? tr("Give the keys too: the firmware cannot be read without them.") : KeyProblemText(kp, MissingKeyName(k)), k.isEmpty() && fw.isEmpty());
        show(decFirmware, fw.isEmpty() ? tr("Give the firmware too.") : ValidateFirmware(fw), fw.isEmpty() && k.isEmpty());
        inputsValid = exporterOk && !code.isEmpty() && !data.isEmpty() && exefs.status->text() == StatusText({}) && romfs.status->text() == StatusText({}) &&
                      (decKeys.status->text().isEmpty() || decKeys.status->text() == StatusText({})) &&
                      (decFirmware.status->text().isEmpty() || decFirmware.status->text() == StatusText({}));
        refreshButtons();
        return;
    }
    // keys first: opening an NSP needs them
    const KeyProblem kp = in.keys.isEmpty() ? KeyProblem::None : CheckKeys(in.keys);
    show(keys, KeyProblemText(kp, MissingKeyName(in.keys)), in.keys.isEmpty());
    const bool keysOk = !in.keys.isEmpty() && kp == KeyProblem::None;
    const auto read = [&](const QString& p) { return ReadNspInfo(paths.exporter, p, in.keys); };
    const auto nspReason = [&](const QString& path, bool isBase) -> QString {
        if (!exporterOk) return tr("The exporter is missing: unzip the whole download and run kartrecomp.exe from inside that folder (tools\\kartrecomp-export.exe must be beside it).");
        if (!keysOk) return tr("Pick your keys first.");
        const NspInfo info = read(path);
        return isBase ? ValidateBase(info) : ValidateUpdate(info);
    };
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    show(base, in.base.isEmpty() ? QString() : nspReason(in.base, true), in.base.isEmpty());
    show(update, in.update.isEmpty() ? QString() : nspReason(in.update, false), in.update.isEmpty());
    QGuiApplication::restoreOverrideCursor();
    show(firmware, ValidateFirmware(in.firmware), in.firmware.isEmpty());
    show(dlcRow, ValidateDlc(dlc), dlc.isEmpty());
    inputsValid = exporterOk && !in.base.isEmpty() && !in.update.isEmpty() && !in.keys.isEmpty() && !in.firmware.isEmpty() &&
                  base.status->text() == StatusText({}) && update.status->text() == StatusText({}) && keys.status->text() == StatusText({}) &&
                  firmware.status->text() == StatusText({}) && (dlc.isEmpty() || dlcRow.status->text() == StatusText({}));
    refreshButtons();
}

void SetupPage::refreshButtons() {
    const bool built = IsBuilt(layout, paths.engineVersion);
    buildButton->setEnabled(inputsValid && !building && !gameRunning);
    buildButton->setText(QFileInfo::exists(layout.workDir()) ? tr("Rebuild game") : tr("Build game"));
    cancelButton->setEnabled(building);
    playButton->setEnabled(built && !building && !gameRunning);
    if (!building) bar->setValue(built ? bar->maximum() : 0);
}

void SetupPage::build() {
    QString missing;
    if (!paths.complete(&missing)) { QMessageBox::warning(this, tr("Build"), missing); return; }
    // The NSPs are moved, not copied (13 GB): say so before touching the user's files.
    QStringList toMove;
    auto outside = [](const QString& from, const QString& to) {
        return !from.isEmpty() && !(QFileInfo(to).exists() && QFileInfo(to).canonicalFilePath() == QFileInfo(from).canonicalFilePath());
    };
    if (mode->currentIndex() == 0) {
        if (outside(base.edit->text(), layout.base())) toMove << base.edit->text();
        if (outside(update.edit->text(), layout.update())) toMove << update.edit->text();
        for (const QString& d : dlc)
            if (outside(d, layout.dlcDir() + "/" + QFileInfo(d).fileName())) toMove << d;
    }
    if (!toMove.isEmpty()) {
        const auto answer = QMessageBox::warning(
            this, tr("Move your game files?"),
            tr("To build the game, these files will be MOVED out of their current folder into the launcher's data folder (%1). "
               "They will no longer be where they are now. Keep your own backup if you want one.\n\n%2\n\nContinue?")
                .arg(QFileInfo(layout.base()).absolutePath(), toMove.join('\n')),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }
    saveInputs();
    building = true;
    emit buildingChanged(true);
    cancelFlag = false;
    refreshButtons();
    const SetupInputs in = inputs();
    if (worker.joinable()) worker.join(); // the previous run has finished (its result was delivered)
    worker = std::thread([this, in] {
        const auto result = RunSetupPipeline(in, layout, paths, [this](int permille, const QString& text) {
            QMetaObject::invokeMethod(this, [this, permille, text] {
                bar->setValue(permille);
                message->setText(text);
            });
        }, &cancelFlag);
        QMetaObject::invokeMethod(this, [this, result] { finishBuild(result.ok, result.message); });
    });
}

void SetupPage::finishBuild(bool ok, const QString& text) {
    building = false;
    emit buildingChanged(false);
    adoptInstalled();
    saveInputs();
    if (ok) {
        message->setText(tr("The game is built. Press Play."));
    } else {
        message->setText(tr("Build failed: %1").arg(text));
    }
    refreshButtons();
    emit builtChanged(ok);
}

void SetupPage::cancel() {
    cancelFlag = true;
    message->setText(tr("Cancelling…"));
}

void SetupPage::saveInputs() const {
    QSettings s;
    s.setValue("setup/base", base.edit->text());
    s.setValue("setup/update", update.edit->text());
    s.setValue("setup/keys", keys.edit->text());
    s.setValue("setup/firmware", firmware.edit->text());
    s.setValue("setup/dlc", dlc);
    s.setValue("setup/split60", split60->isChecked());
    s.setValue("setup/delete_build_files", delete_build_files->isChecked());
    s.setValue("setup/exefs", exefs.edit->text());
    s.setValue("setup/romfs", romfs.edit->text());
    s.setValue("setup/dec_keys", decKeys.edit->text());
    s.setValue("setup/dec_firmware", decFirmware.edit->text());
    s.setValue("setup/decrypted", mode->currentIndex() == 1);
}
