// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's Mii editor: lists the Miis in <data root>/miis/MiiDatabase.dat, edits every value the game stores (name, body,
// face, hair, eyes, eyebrows, nose, mouth, facial hair, glasses, mole) and writes a database the game's Mii service accepts.
#pragma once

#include <QWidget>
#include <map>
#include <string>

#include <QProcessEnvironment>
#include <QStringList>

#include "mii_db.h"
#include "mii_render.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProcess;
class QPushButton;
class QSpinBox;

class MiiEditor : public QWidget {
    Q_OBJECT
public:
    explicit MiiEditor(QWidget* parent = nullptr);

    /// Where the database lives (created on save) and the older console location read when it does not exist yet.
    void setPaths(const QString& miis_dir, const QString& nand_database);
    /// The game must not run while the database is written (it saves its own copy when it closes).
    void setLocked(bool locked);
    void reload();
    /// How to run the engine, and whether console firmware is installed. Miis are a firmware feature: without it the editor is
    /// off (the game cannot show Miis either). With it, the preview's face data is read from the firmware once, by the engine,
    /// into the launcher's cache folder.
    void setEngine(const QString& engine, const QStringList& arguments, const QProcessEnvironment& environment, const QString& working_dir,
                   bool has_firmware);

private:
    void showMii(int row);
    void storeEdits();
    void refreshList();
    void setDirty(bool dirty);
    bool save();
    QString databasePath() const;
    Mii::StoreData edited() const;      ///< the shown Mii with the form's values
    void updatePreview();
    void prepareResources();            ///< loads the face data, extracting it with the engine first when needed
    void runEngine(const QStringList& arguments, const QProcessEnvironment& environment, int step);
    QString resourceDir() const;

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
    QLabel* status = nullptr;
    QLabel* preview = nullptr;
    QLabel* note = nullptr;
    Mii::Renderer renderer;
    QProcess* helper = nullptr;
    QString engine, engine_dir;
    QStringList engine_args;
    QProcessEnvironment engine_env;
    bool has_firmware = true, tried_extract = false, preview_queued = false, show_body = false;
};
