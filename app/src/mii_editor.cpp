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

namespace {
const char* const kFavoriteColors[] = {"Red", "Orange", "Yellow", "Light green", "Green", "Blue",
                                       "Light blue", "Pink", "Purple", "Brown", "White", "Black"};
}

MiiEditor::MiiEditor(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    auto* note = new QLabel(tr("Miis you make here show up in the game's character select (Mii racer). Values are the game's own "
                               "numbers (the same ones the console's Mii Maker uses), so try a value and look in the game; there is "
                               "no face preview here yet. Close the game before saving."));
    note->setWordWrap(true);
    outer->addWidget(note);

    auto* body = new QHBoxLayout;
    outer->addLayout(body, 1);

    auto* left = new QVBoxLayout;
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
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            if (!loading) setDirty(true);
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
    save_button = new QPushButton(tr("Save Miis"));
    revert = new QPushButton(tr("Undo changes"));
    bottom->addWidget(status, 1);
    bottom->addWidget(revert);
    bottom->addWidget(save_button);
    outer->addLayout(bottom);

    connect(name, &QLineEdit::textEdited, this, [this] { setDirty(true); });
    connect(gender, &QComboBox::currentIndexChanged, this, [this] { if (!loading) setDirty(true); });
    connect(favorite, &QComboBox::currentIndexChanged, this, [this] { if (!loading) setDirty(true); });
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
    form->setEnabled(current >= 0 && !locked);
    if (current < 0) return;
    loading = true;
    const auto& d = db.At(current);
    name->setText(d.Name());
    gender->setCurrentIndex(d.Get("gender"));
    favorite->setCurrentIndex(d.Get("favorite_color"));
    for (auto& [key, spin] : spins) spin->setValue(d.Get(key.c_str()));
    loading = false;
}

void MiiEditor::storeEdits() {
    if (current < 0 || current >= db.Count()) return;
    Mii::StoreData d = db.At(current);
    d.SetName(name->text());
    d.Set("gender", gender->currentIndex());
    d.Set("favorite_color", favorite->currentIndex());
    for (auto& [key, spin] : spins) d.Set(key.c_str(), spin->value());
    d.UpdateChecksums();
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
    for (auto* b : {add_male, add_female, duplicate, remove, up, down}) b->setEnabled(!locked);
    form->setEnabled(current >= 0 && !locked);
    save_button->setEnabled(dirty && !locked);
    revert->setEnabled(dirty && !locked);
    list->setEnabled(true);
    if (locked) status->setText(tr("The game is running: Miis can be edited after it closes."));
}
