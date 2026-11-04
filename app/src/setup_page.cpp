// SPDX-License-Identifier: GPL-3.0-or-later
#include "setup_page.h"

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

#include "build_runner.h"
#include "keys_check.h"
#include "nsp_info.h"
#include "setup_pipeline.h"

namespace {
QString StatusText(const QString& reason) { return reason.isEmpty() ? QStringLiteral("✔") : QStringLiteral("✘ ") + reason; }
}

SetupPage::SetupPage(QWidget* parent)
    : QWidget(parent), paths(BuilderPaths::Detect()), layout(DataLayout::Default()) {
    auto* top = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Pick your own game files. Nothing leaves your PC: the game is built here from them, in a few minutes, "
                                "and stays on this PC."));
    intro->setWordWrap(true);
    top->addWidget(intro);
    auto* grid = new QGridLayout;
    top->addLayout(grid);
    base = addRow(grid, 0, tr("Base game"), tr("Mario Kart 8 Deluxe (.nsp)"), [this] { pick(base, false, tr("Select the base game"), tr("NSP files (*.nsp)")); });
    update = addRow(grid, 1, tr("Update 3.0.4"), tr("The 3.0.4 update (.nsp)"), [this] { pick(update, false, tr("Select update 3.0.4"), tr("NSP files (*.nsp)")); });
    dlcRow = addRow(grid, 2, tr("DLC (optional)"), tr("Booster Course Pass and other add-ons (.nsp)"), [this] { pickDlc(); });
    keys = addRow(grid, 3, tr("Keys"), tr("prod.keys from your own console"), [this] { pick(keys, false, tr("Select prod.keys"), tr("Key files (*.keys)")); });
    firmware = addRow(grid, 4, tr("Firmware"), tr("Folder of firmware files, or a .zip"), [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select the firmware folder (cancel to pick a zip instead)"));
        if (!dir.isEmpty()) { firmware.edit->setText(dir); revalidate(); return; }
        pick(firmware, false, tr("Select the firmware zip"), tr("Zip files (*.zip)"));
    });
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
    connect(playButton, &QPushButton::clicked, this, &SetupPage::play);
    QSettings s;
    base.edit->setText(s.value("setup/base").toString());
    update.edit->setText(s.value("setup/update").toString());
    keys.edit->setText(s.value("setup/keys").toString());
    firmware.edit->setText(s.value("setup/firmware").toString());
    dlc = s.value("setup/dlc").toStringList();
    dlcRow.edit->setText(dlc.join("; "));
    QString missing;
    if (!paths.complete(&missing)) message->setText(missing);
    revalidate();
    refreshButtons();
    if (QFileInfo::exists(layout.moduleDir())) emit builtChanged(IsBuilt(layout, paths.engineVersion));
}

SetupPage::~SetupPage() { cancelFlag = true; }

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

SetupInputs SetupPage::inputs() const { return {base.edit->text(), update.edit->text(), keys.edit->text(), firmware.edit->text(), dlc}; }

void SetupPage::revalidate() {
    const SetupInputs in = inputs();
    const bool exporterOk = QFileInfo::exists(paths.exporter);
    const auto show = [](Row& r, const QString& reason, bool empty) { r.status->setText(empty ? QString() : StatusText(reason)); };
    // keys first: opening an NSP needs them
    const KeyProblem kp = in.keys.isEmpty() ? KeyProblem::None : CheckKeys(in.keys);
    show(keys, KeyProblemText(kp), in.keys.isEmpty());
    const bool keysOk = !in.keys.isEmpty() && kp == KeyProblem::None;
    const auto read = [&](const QString& p) { return ReadNspInfo(paths.exporter, p, in.keys); };
    const auto nspReason = [&](const QString& path, bool isBase) -> QString {
        if (!exporterOk) return tr("The exporter is missing: reinstall the launcher.");
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
    buildButton->setEnabled(inputsValid && !building);
    buildButton->setText(QFileInfo::exists(layout.workDir()) ? tr("Rebuild game") : tr("Build game"));
    cancelButton->setEnabled(building);
    playButton->setEnabled(built && !building && game.state() == QProcess::NotRunning);
    if (!building) bar->setValue(built ? bar->maximum() : 0);
}

void SetupPage::build() {
    QString missing;
    if (!paths.complete(&missing)) { QMessageBox::warning(this, tr("Build"), missing); return; }
    saveInputs();
    building = true;
    cancelFlag = false;
    refreshButtons();
    const SetupInputs in = inputs();
    std::thread([this, in] {
        const auto result = RunSetupPipeline(in, layout, paths, [this](int permille, const QString& text) {
            QMetaObject::invokeMethod(this, [this, permille, text] {
                bar->setValue(permille);
                message->setText(text);
            });
        }, &cancelFlag);
        QMetaObject::invokeMethod(this, [this, result] { finishBuild(result.ok, result.message); });
    }).detach();
}

void SetupPage::finishBuild(bool ok, const QString& text) {
    building = false;
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
}

void SetupPage::play() {
    QStringList args = {"--module-dir", layout.moduleDir(), "--content-base", layout.base(), "--content-update", layout.update()};
    for (const QString& f : QDir(layout.dlcDir()).entryList({"*.nsp"}, QDir::Files)) args << "--content-dlc" << layout.dlcDir() + "/" + f;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("KARTRECOMP_APP_NAME", "kartrecomp");
    if (!env.contains("KARTRECOMP_NATIVE_GPU")) env.insert("KARTRECOMP_NATIVE_GPU", "native");
    game.setProcessEnvironment(env);
    game.setWorkingDirectory(QFileInfo(paths.engine).absolutePath());
    connect(&game, &QProcess::finished, this, [this] { refreshButtons(); }, Qt::UniqueConnection);
    game.start(paths.engine, args);
    refreshButtons();
}
