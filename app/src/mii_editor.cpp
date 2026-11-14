// SPDX-License-Identifier: GPL-3.0-or-later
#include "mii_editor.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QPixmap>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include "build_runner.h"
#include <QUrl>

namespace {
const char* const kFavoriteColors[] = {"Red", "Orange", "Yellow", "Light green", "Green", "Blue",
                                       "Light blue", "Pink", "Purple", "Brown", "White", "Black"};
}

MiiEditor::MiiEditor(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    note = new QLabel(tr("<b>Experimental.</b> Keep a copy of your Miis folder before saving (Open Miis folder). "
                         "Miis you make here show up in the game's character select (Mii racer). Values are the game's own "
                         "numbers (the same ones the console's Mii Maker uses); the picture shows the face as you change them. "
                         "Close the game before saving."));
    note->setWordWrap(true);
    outer->addWidget(note);

    auto* body = new QHBoxLayout;
    outer->addLayout(body, 1);

    auto* left = new QVBoxLayout;
    preview = new QLabel;
    preview->setFixedSize(260, 320);
    preview->setAlignment(Qt::AlignCenter);
    preview->setWordWrap(true);
    preview->setFrameShape(QFrame::StyledPanel);
    left->addWidget(preview, 0, Qt::AlignHCenter);
    list = new QListWidget;
    list->setMinimumWidth(170);
    left->addWidget(list, 1);
    auto* row1 = new QHBoxLayout;
    add_male = new QPushButton(tr("New (male)"));
    add_female = new QPushButton(tr("New (female)"));
    row1->addWidget(add_male);
    row1->addWidget(add_female);
    left->addLayout(row1);
    auto* row2 = new QHBoxLayout;
    duplicate = new QPushButton(tr("Duplicate"));
    remove = new QPushButton(tr("Delete"));
    up = new QPushButton(tr("Up"));
    down = new QPushButton(tr("Down"));
    for (auto* b : {duplicate, remove, up, down}) row2->addWidget(b);
    left->addLayout(row2);
    body->addLayout(left);

    form = new QWidget;
    auto* fl = new QVBoxLayout(form);
    auto* top = new QFormLayout;
    name = new QLineEdit;
    name->setMaxLength(Mii::kNameLength);
    top->addRow(tr("Name"), name);
    gender = new QComboBox;
    gender->addItems({tr("Male"), tr("Female")});
    top->addRow(tr("Gender"), gender);
    favorite = new QComboBox;
    for (const char* c : kFavoriteColors) favorite->addItem(tr(c));
    top->addRow(tr("Favourite colour"), favorite);
    fl->addLayout(top);
    // One box per section, a number box per value, in the order of Mii::Fields().
    std::map<std::string, QFormLayout*> groups;
    for (const Mii::Field& f : Mii::Fields()) {
        const std::string g = f.group;
        if (g.empty() || std::string(f.key) == "gender" || std::string(f.key) == "favorite_color") continue;
        if (!groups.count(g)) {
            auto* box = new QGroupBox(tr(f.group));
            groups[g] = new QFormLayout(box);
            fl->addWidget(box);
        }
        auto* spin = new QSpinBox;
        spin->setRange(0, f.max);
        spin->setToolTip(tr("0 to %1").arg(f.max));
        groups[g]->addRow(tr(f.label), spin);
        spins[f.key] = spin;
        // Height and build change the body: the picture shows the whole figure while they are being changed, the face otherwise.
        const bool body_value = std::string(f.key) == "height" || std::string(f.key) == "build";
        connect(spin, &QSpinBox::valueChanged, this, [this, body_value] {
            if (!loading) setDirty(true);
            show_body = body_value;
            updatePreview();
        });
    }
    fl->addStretch();
    auto* scroll = new QScrollArea;
    scroll->setWidget(form);
    scroll->setWidgetResizable(true);
    body->addWidget(scroll, 1);

    auto* bottom = new QHBoxLayout;
    status = new QLabel;
    status->setWordWrap(true);
    auto* open_folder = new QPushButton(tr("Open Miis folder"));
    connect(open_folder, &QPushButton::clicked, this, [this] {
        QDir().mkpath(miis_dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(miis_dir));
    });
    bottom->addWidget(open_folder);
    save_button = new QPushButton(tr("Save Miis"));
    revert = new QPushButton(tr("Undo changes"));
    bottom->addWidget(status, 1);
    bottom->addWidget(revert);
    bottom->addWidget(save_button);
    outer->addLayout(bottom);

    connect(name, &QLineEdit::textEdited, this, [this] { setDirty(true); });
    connect(gender, &QComboBox::currentIndexChanged, this, [this] { if (!loading) setDirty(true); });
    connect(favorite, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading) setDirty(true);
        updatePreview();
    });
    connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
        storeEdits();
        showMii(row);
    });
    const auto add = [this](bool female) {
        storeEdits();
        if (!db.Add(Mii::StoreData::MakeDefault(female ? tr("New Mii") : tr("New Mii"), female))) {
            QMessageBox::information(this, tr("Miis"), tr("The database is full (%1 Miis).").arg(Mii::kMaxMiis - 1));
            return;
        }
        setDirty(true);
        refreshList();
        list->setCurrentRow(db.Count() - 1);
    };
    connect(add_male, &QPushButton::clicked, this, [add] { add(false); });
    connect(add_female, &QPushButton::clicked, this, [add] { add(true); });
    connect(duplicate, &QPushButton::clicked, this, [this] {
        storeEdits();
        if (current < 0) return;
        Mii::StoreData d = db.At(current);
        d.NewCreateId(); // a copy is a new Mii to the game
        d.UpdateChecksums();
        if (!db.Add(d)) return;
        setDirty(true);
        refreshList();
        list->setCurrentRow(db.Count() - 1);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (current < 0) return;
        if (QMessageBox::question(this, tr("Delete Mii"), tr("Delete \"%1\"? Races and records that used this Mii will show a "
                                                            "default Mii instead.").arg(db.At(current).Name())) != QMessageBox::Yes)
            return;
        const int row = current;
        current = -1;
        db.Remove(row);
        setDirty(true);
        refreshList();
        list->setCurrentRow(std::min(row, db.Count() - 1));
    });
    const auto move = [this](int delta) {
        storeEdits();
        const int to = current + delta;
        if (current < 0 || to < 0 || to >= db.Count()) return;
        db.Move(current, to);
        current = -1;
        setDirty(true);
        refreshList();
        list->setCurrentRow(to);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(+1); });
    connect(save_button, &QPushButton::clicked, this, [this] { save(); });
    connect(revert, &QPushButton::clicked, this, [this] { reload(); });
    setLocked(false);
}

QString MiiEditor::databasePath() const {
    return QDir(miis_dir).filePath(QStringLiteral("MiiDatabase.dat"));
}

void MiiEditor::setPaths(const QString& miis, const QString& nand) {
    miis_dir = miis;
    nand_database = nand;
    reload();
}

void MiiEditor::reload() {
    QString error;
    QString from = databasePath();
    if (!QFileInfo::exists(from) && QFileInfo::exists(nand_database)) from = nand_database;
    db = Mii::Database();
    if (QFileInfo::exists(from)) {
        auto loaded = Mii::Database::Load(from, &error);
        if (loaded) {
            db = *loaded;
            status->setText(tr("%n Mii(s) in %1", nullptr, db.Count()).arg(QDir::toNativeSeparators(from)));
        } else {
            status->setText(tr("Could not read %1 (%2). Saving writes a new database.").arg(QDir::toNativeSeparators(from), error));
        }
    } else {
        status->setText(tr("No Miis yet. They will be saved to %1.").arg(QDir::toNativeSeparators(databasePath())));
    }
    current = -1;
    refreshList();
    if (db.Count() > 0) list->setCurrentRow(0);
    else showMii(-1);
    dirty = false;
    setLocked(locked);
}

void MiiEditor::refreshList() {
    const QSignalBlocker block(list);
    list->clear();
    for (int i = 0; i < db.Count(); i++) {
        const auto& d = db.At(i);
        list->addItem(d.IsSpecial() ? tr("%1 (special)").arg(d.Name()) : d.Name());
    }
    if (current >= 0 && current < db.Count()) list->setCurrentRow(current);
}

void MiiEditor::showMii(int row) {
    current = (row >= 0 && row < db.Count()) ? row : -1;
    show_body = false;
    form->setEnabled(current >= 0 && !locked && has_firmware);
    if (current < 0) return;
    loading = true;
    const auto& d = db.At(current);
    name->setText(d.Name());
    gender->setCurrentIndex(d.Get("gender"));
    favorite->setCurrentIndex(d.Get("favorite_color"));
    for (auto& [key, spin] : spins) spin->setValue(d.Get(key.c_str()));
    loading = false;
    updatePreview();
}

Mii::StoreData MiiEditor::edited() const {
    Mii::StoreData d = db.At(current);
    d.SetName(name->text());
    d.Set("gender", gender->currentIndex());
    d.Set("favorite_color", favorite->currentIndex());
    for (auto& [key, spin] : spins) d.Set(key.c_str(), spin->value());
    d.UpdateChecksums();
    return d;
}

QString MiiEditor::resourceDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/mii-model");
}

void MiiEditor::setEngine(const QString& path, const QStringList& arguments, const QProcessEnvironment& environment, const QString& working_dir,
                          bool firmware) {
    engine = path, engine_args = arguments, engine_env = environment, engine_dir = working_dir;
    has_firmware = firmware;
    tried_extract = false;
    setLocked(locked);
    prepareResources();
}

// The face data comes from the player's own firmware: the engine writes the firmware's Mii archive into the cache folder (once),
// then decodes its ASTC textures. Nothing happens without firmware or without an engine that knows these two jobs.
void MiiEditor::prepareResources() {
    if (!has_firmware) {
        preview->setText(tr("Miis need your console's firmware.\nAdd it on the Setup page."));
        return;
    }
    if (renderer.Load(resourceDir()) && !renderer.NeedsAstc()) return updatePreview();
    if (tried_extract || helper || engine.isEmpty() || !QFileInfo::exists(engine)) {
        if (!helper) preview->setText(tr("No preview: the Mii face data could not be read from the firmware."));
        return;
    }
    tried_extract = true;
    QDir().mkpath(resourceDir());
    preview->setText(tr("Reading the Mii face data from your firmware..."));
    if (!renderer.Loaded()) {
        QProcessEnvironment env = engine_env;
        env.insert(QStringLiteral("KARTRECOMP_SYSARCHIVE_EXTRACT"), QStringLiteral("0100000000000802=") + resourceDir());
        env.insert(QStringLiteral("KARTRECOMP_NATIVE_GPU"), QStringLiteral("off"));
        env.insert(QStringLiteral("SDL_VIDEODRIVER"), QStringLiteral("dummy"));
        env.insert(QStringLiteral("SDL_AUDIODRIVER"), QStringLiteral("dummy"));
        env.insert(QStringLiteral("KARTRECOMP_NO_MODS"), QStringLiteral("1"));
        runEngine(engine_args, env, 0);
    } else {
        runEngine({}, engine_env, 1);
    }
}

void MiiEditor::runEngine(const QStringList& arguments, const QProcessEnvironment& environment, int step) {
    QStringList args = arguments;
    if (step == 1) {
        QFile job(resourceDir() + QStringLiteral("/astc.job"));
        if (!job.open(QIODevice::WriteOnly) || job.write(renderer.AstcJob()) <= 0) return prepareResources();
        job.close();
        args = {QStringLiteral("--astc-decode"), job.fileName(), resourceDir() + QStringLiteral("/astc.rgba")};
    }
    helper = new QProcess(this);
    BuildRunner::DieWithParent(*helper);
    helper->setProcessEnvironment(environment);
    helper->setWorkingDirectory(engine_dir);
    helper->setProcessChannelMode(QProcess::MergedChannels);
    // An engine that does not know the job would start the game instead: never let it run on.
    QTimer::singleShot(30000, helper, [p = helper] { p->kill(); });
    connect(helper, &QProcess::finished, this, [this, step](int, QProcess::ExitStatus) {
        helper->deleteLater();
        helper = nullptr;
        QFile::remove(resourceDir() + QStringLiteral("/astc.job"));
        if (step == 0 && renderer.Load(resourceDir()) && renderer.NeedsAstc()) return runEngine({}, engine_env, 1);
        prepareResources();
    });
    connect(helper, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || !helper) return;
        helper->deleteLater();
        helper = nullptr;
        prepareResources();
    });
    helper->start(engine, args);
}

void MiiEditor::updatePreview() {
    if (loading || preview_queued) return;
    preview_queued = true; // one picture per burst of changes
    QTimer::singleShot(0, this, [this] {
        preview_queued = false;
        if (!renderer.Loaded() || renderer.NeedsAstc() || !has_firmware) return;
        if (current < 0 || current >= db.Count()) return preview->setPixmap(QPixmap());
        const qreal ratio = devicePixelRatioF();
        QImage img = renderer.Render(edited(), static_cast<int>(256 * ratio), static_cast<int>(316 * ratio), show_body);
        img.setDevicePixelRatio(ratio);
        preview->setPixmap(QPixmap::fromImage(img));
    });
}

void MiiEditor::storeEdits() {
    if (current < 0 || current >= db.Count()) return;
    const Mii::StoreData d = edited();
    if (d.raw != db.At(current).raw) {
        db.Replace(current, d);
        if (QListWidgetItem* it = list->item(current)) it->setText(d.Name());
    }
}

void MiiEditor::setDirty(bool d) {
    dirty = d;
    save_button->setEnabled(dirty && !locked);
    revert->setEnabled(dirty && !locked);
    if (dirty) status->setText(tr("Unsaved changes."));
}

bool MiiEditor::save() {
    storeEdits();
    if (locked) return false;
    QDir().mkpath(miis_dir);
    QString error;
    // Keep the previous file once as MiiDatabase.dat.bak (the editor never deletes the player's data).
    if (QFileInfo::exists(databasePath())) {
        QFile::remove(databasePath() + QStringLiteral(".bak"));
        QFile::copy(databasePath(), databasePath() + QStringLiteral(".bak"));
    }
    for (int i = 0; i < db.Count(); i++) {
        QString why;
        if (!db.At(i).IsValid(&why)) {
            QMessageBox::warning(this, tr("Miis"), tr("\"%1\" is not valid (%2); nothing was saved.").arg(db.At(i).Name(), why));
            return false;
        }
    }
    if (!db.Save(databasePath(), &error)) {
        QMessageBox::warning(this, tr("Miis"), error);
        return false;
    }
    dirty = false;
    setDirty(false);
    status->setText(tr("Saved %n Mii(s) to %1. They appear in the game the next time it starts.", nullptr, db.Count())
                        .arg(QDir::toNativeSeparators(databasePath())));
    return true;
}

void MiiEditor::setLocked(bool l) {
    locked = l;
    const bool off = locked || !has_firmware; // Miis are a firmware feature
    for (auto* b : {add_male, add_female, duplicate, remove, up, down}) b->setEnabled(!off);
    form->setEnabled(current >= 0 && !off);
    save_button->setEnabled(dirty && !off);
    revert->setEnabled(dirty && !off);
    list->setEnabled(has_firmware);
    if (!has_firmware) status->setText(tr("Miis need your console's firmware (Setup page). Without it the game has no Mii racers either."));
    else if (locked) status->setText(tr("The game is running: Miis can be edited after it closes."));
}
