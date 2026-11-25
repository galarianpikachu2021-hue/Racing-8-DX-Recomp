// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_page.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include <chrono>
#include <memory>

#include "launcher_language.h"
#include "storage.h"

namespace {
QString Gb(qint64 bytes) {
    return bytes < 100'000'000 ? QStringLiteral("%1 MB").arg(double(bytes) / 1e6, 0, 'f', 0) : QStringLiteral("%1 GB").arg(double(bytes) / 1e9, 0, 'f', 1);
}
} // namespace

StoragePage::StoragePage(QWidget* parent) : QWidget(parent), layout(DataLayout::Default()), current(Storage::Chosen()) {
    auto* top = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Where the game is kept: your game files, the built game, mods, saves and the build's work files. Put them on an SD card "
                                "or another drive when this one is full; the game is then built and played from there. The settings stay on this device."));
    intro->setWordWrap(true);
    top->addWidget(intro);
    where = new QLabel;
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(where);
    sizes = new QLabel(tr("Measuring..."));
    sizes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(sizes);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Keep the game on:")));
    drives = new QComboBox;
    row->addWidget(drives, 1);
    folder_button = new QPushButton(tr("Choose folder..."));
    row->addWidget(folder_button);
    top->addLayout(row);
    note = new QLabel;
    note->setWordWrap(true);
    top->addWidget(note);

    auto* actions = new QHBoxLayout;
    move_button = new QPushButton(tr("Move everything there"));
    use_button = new QPushButton(tr("Use it without moving"));
    use_button->setToolTip(tr("The files stay where they are. The launcher uses what is in the new place, or starts empty there (then use the Setup page)."));
    cancel_button = new QPushButton(tr("Cancel"));
    cancel_button->hide();
    actions->addWidget(move_button);
    actions->addWidget(use_button);
    actions->addWidget(cancel_button);
    actions->addStretch();
    top->addLayout(actions);
    bar = new QProgressBar;
    bar->setRange(0, 1000);
    bar->hide();
    top->addWidget(bar);
    status = new QLabel;
    status->setWordWrap(true);
    top->addWidget(status);
    top->addStretch();

    connect(drives, &QComboBox::activated, this, [this] { targetChanged(); });
    connect(folder_button, &QPushButton::clicked, this, [this] { chooseFolder(); });
    connect(move_button, &QPushButton::clicked, this, [this] { move(); });
    connect(use_button, &QPushButton::clicked, this, [this] { useWithoutMoving(); });
    connect(cancel_button, &QPushButton::clicked, this, [this] {
        cancel = true;
        status->setText(tr("Cancelling…"));
    });
    refreshDrives();
}

StoragePage::~StoragePage() {
    stop = true;
    cancel = true;
    if (measurer.joinable()) measurer.join();
    if (mover.joinable()) mover.join();
}

void StoragePage::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (moving) return;
    refreshDrives(); // a card put in since
    if (!measured) measure();
}

void StoragePage::setLocked(bool l) {
    locked = l;
    refreshButtons();
}

void StoragePage::refreshDrives() {
    const QString keep = drives->count() ? drives->currentData().toString() : current;
    drives->clear();
    const DataLayout standard = Storage::LayoutFor({});
    drives->addItem(tr("This device (standard folders), %1 free").arg(Gb(Storage::FreeBytes(standard.dataRoot))), QString());
    for (const Storage::Volume& v : Storage::OtherVolumes())
        drives->addItem(tr("%1 (%2), %3 free of %4").arg(v.name, QDir::toNativeSeparators(v.root), Gb(v.free), Gb(v.total)), QDir::cleanPath(v.root + "/KartRecomp"));
    for (const QString& f : {current, custom})
        if (!f.isEmpty() && drives->findData(f) < 0) drives->addItem(tr("%1, %2 free").arg(QDir::toNativeSeparators(f), Gb(Storage::FreeBytes(f))), f);
    drives->setCurrentIndex(std::max(0, drives->findData(keep)));
    where->setText(tr("Now in: %1 (%2 free)").arg(QDir::toNativeSeparators(layout.dataRoot), Gb(Storage::FreeBytes(layout.dataRoot))));
    targetChanged();
}

void StoragePage::measure() {
    measured = true;
    if (measurer.joinable()) measurer.join();
    measurer = std::thread([this, l = layout] {
        const qint64 content = Storage::SizeOf(l.content(), &stop), module = Storage::SizeOf(l.moduleDir(), &stop);
        const qint64 mods = Storage::SizeOf(l.modsDir(), &stop) + Storage::SizeOf(l.dataRoot + "/addons", &stop);
        const qint64 all = Storage::SizeOf(l.dataRoot, &stop), build = Storage::SizeOf(l.cacheRoot, &stop);
        if (stop) return;
        QMetaObject::invokeMethod(this, [this, content, module, mods, all, build] {
            sizes->setText(tr("Game files: %1\nBuilt game: %2\nMods: %3\nSaves, firmware and the rest: %4\nBuild files and caches: %5\nTogether: %6")
                               .arg(Gb(content), Gb(module), Gb(mods), Gb(all - content - module - mods), Gb(build), Gb(all + build)));
        }, Qt::QueuedConnection);
    });
}

QString StoragePage::target() const { return drives->currentData().toString(); }

void StoragePage::chooseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Where should the game be kept?"), target().isEmpty() ? QDir::homePath() : QFileInfo(target()).absolutePath());
    if (dir.isEmpty()) return;
    // A "KartRecomp" folder of its own, unless one was picked.
    custom = QDir::cleanPath(QFileInfo(dir).fileName() == QLatin1String("KartRecomp") ? dir : dir + "/KartRecomp");
    if (drives->findData(custom) < 0) drives->addItem(tr("%1, %2 free").arg(QDir::toNativeSeparators(custom), Gb(Storage::FreeBytes(custom))), custom);
    drives->setCurrentIndex(drives->findData(custom));
    targetChanged();
}

void StoragePage::targetChanged() {
    const QString t = target();
    target_ok = t != current;
    target_used = false;
    if (!target_ok) {
        note->setText(QString());
    } else {
        target_used = Storage::HoldsData(Storage::LayoutFor(t));
        const QString place = QDir::toNativeSeparators(Storage::LayoutFor(t).dataRoot);
        note->setText(target_used ? tr("%1 already holds game data: it can be used, but nothing can be moved onto it.").arg(place) : tr("New place: %1").arg(place));
    }
    refreshButtons();
}

void StoragePage::refreshButtons() {
    const bool free = !locked && !moving;
    drives->setEnabled(free);
    folder_button->setEnabled(free);
    move_button->setEnabled(free && target_ok && !target_used && Storage::HoldsData(layout));
    use_button->setEnabled(free && target_ok);
    cancel_button->setVisible(moving);
    if (locked) status->setText(tr("Close the game (or let the build finish) before changing this."));
    else if (!moving && status->text() == tr("Close the game (or let the build finish) before changing this.")) status->clear();
}

QString StoragePage::commit(const DataLayout& to, const QString& folder) const {
    const QString config = DataLayout::ConfigRoot() + "/sdl2-config.ini";
    if (!Storage::Repoint(config, layout, to)) return tr("Could not write %1").arg(QDir::toNativeSeparators(config));
    const QString why = Storage::SetChosen(folder);
    if (!why.isEmpty()) Storage::Repoint(config, to, layout);
    return why;
}

void StoragePage::move() {
    const QString folder = target();
    const DataLayout to = Storage::LayoutFor(folder);
    if (const QString why = Storage::CheckFolder(folder.isEmpty() ? QFileInfo(to.dataRoot).absolutePath() : folder); !why.isEmpty()) {
        status->setText(why);
        return;
    }
    if (QMessageBox::question(this, tr("Move the game"), tr("Move everything to %1?\n\nThe files are copied first and removed from %2 only when all of them have arrived. "
                                                             "This can take a while on an SD card. The launcher restarts afterwards.")
                                                              .arg(QDir::toNativeSeparators(to.dataRoot), QDir::toNativeSeparators(layout.dataRoot))) != QMessageBox::Yes)
        return;
    if (locked || (busy && busy())) { // started while the question was open, or a mod import is running
        status->setText(tr("Something is using the files (the game, a build or a mod import). Try again when it is done."));
        return;
    }
    if (mover.joinable()) mover.join();
    cancel = false;
    moving = true;
    bar->setValue(0);
    bar->show();
    emit movingChanged(true);
    refreshButtons();
    mover = std::thread([this, to, folder, from = layout] {
        auto last = std::make_shared<std::chrono::steady_clock::time_point>();
        QString leftover;
        const QString error = Storage::Move(from, to, [this, to, folder] { return commit(to, folder); },
            [this, last](qint64 done, qint64 total, const QString& file) {
                const auto now = std::chrono::steady_clock::now();
                if (now - *last < std::chrono::milliseconds(150)) return;
                *last = now;
                QMetaObject::invokeMethod(this, [this, done, total, file] {
                    if (!moving || cancel) return;
                    bar->setValue(total > 0 ? static_cast<int>(done * 1000 / total) : 0);
                    status->setText(tr("%1 of %2: %3").arg(Gb(done), Gb(total), file));
                }, Qt::QueuedConnection);
            }, &cancel, &leftover);
        QMetaObject::invokeMethod(this, [this, error, leftover] { finish(error, leftover); }, Qt::QueuedConnection);
    });
}

void StoragePage::finish(const QString& error, const QString& leftover) {
    if (mover.joinable()) mover.join();
    moving = false;
    bar->hide();
    emit movingChanged(false);
    if (!error.isEmpty()) {
        status->setText(tr("Nothing was moved: %1").arg(error));
        refreshDrives();
        return;
    }
    refreshButtons();
    QMessageBox::information(this, tr("Move the game"), leftover.isEmpty() ? tr("Done. The launcher now restarts.")
                                 : tr("Done, but some old files could not be removed: %1. You can delete that folder by hand. The launcher now restarts.").arg(QDir::toNativeSeparators(leftover)));
    LauncherLanguage::Restart();
}

void StoragePage::useWithoutMoving() {
    const QString folder = target();
    const DataLayout to = Storage::LayoutFor(folder);
    if (const QString why = Storage::CheckFolder(folder.isEmpty() ? QFileInfo(to.dataRoot).absolutePath() : folder); !why.isEmpty()) {
        status->setText(why);
        return;
    }
    const QString what = target_used ? tr("Use the game data that is already in %1?") : tr("Start with an empty place in %1? The game must then be set up there (Setup page).");
    if (QMessageBox::question(this, tr("Storage"), what.arg(QDir::toNativeSeparators(to.dataRoot)) + "\n\n" +
                                                       tr("The files in %1 stay there. The launcher restarts.").arg(QDir::toNativeSeparators(layout.dataRoot))) != QMessageBox::Yes)
        return;
    if (locked || (busy && busy())) { // started while the question was open, or a mod import is running
        status->setText(tr("Something is using the files (the game, a build or a mod import). Try again when it is done."));
        return;
    }
    if (const QString why = commit(to, folder); !why.isEmpty()) {
        status->setText(why);
        return;
    }
    LauncherLanguage::Restart();
}
