// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's Mii editor: lists the Miis in <data root>/miis/MiiDatabase.dat, edits every value the game stores (name, body,
// face, hair, eyes, eyebrows, nose, mouth, facial hair, glasses, mole) and writes a database the game's Mii service accepts.
#pragma once

#include <QWidget>
#include <map>
#include <string>

#include "mii_db.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class MiiPreview;

class MiiEditor : public QWidget {
    Q_OBJECT
public:
    explicit MiiEditor(QWidget* parent = nullptr);

    /// Where the database lives (created on save) and the older console location read when it does not exist yet.
    void setPaths(const QString& miis_dir, const QString& nand_database);
    /// The game must not run while the database is written (it saves its own copy when it closes).
    void setLocked(bool locked);
    void reload();

private:
    void showMii(int row);
    void updatePreview();
    void storeEdits();
    void refreshList();
    void setDirty(bool dirty);
    bool save();
    QString databasePath() const;

    Mii::Database db;
    QString miis_dir, nand_database;
    bool locked = false, dirty = false, loading = false;
    int current = -1;

    QListWidget* list = nullptr;
    QLineEdit* name = nullptr;
    QComboBox* gender = nullptr;
    QComboBox* favorite = nullptr;
    std::map<std::string, QSpinBox*> spins;
    QPushButton *add_male = nullptr, *add_female = nullptr, *duplicate = nullptr, *remove = nullptr, *up = nullptr, *down = nullptr,
                *save_button = nullptr, *revert = nullptr;
    QWidget* form = nullptr;
    MiiPreview* preview = nullptr;
    QLabel* status = nullptr;
};
