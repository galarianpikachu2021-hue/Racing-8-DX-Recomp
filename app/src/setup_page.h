// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's first page: one picker per input (base game, update 3.0.4, DLC, keys, firmware), a live check of each, Build, and Play.
#pragma once

#include <QProcess>
#include <QWidget>
#include <atomic>
#include <memory>
#include <thread>

#include "setup_paths.h"
#include "setup_validate.h"

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

class SetupPage : public QWidget {
    Q_OBJECT
public:
    explicit SetupPage(QWidget* parent = nullptr);
    ~SetupPage() override;

    bool isBuilding() const { return building; }
    /// Cancels a running build and waits until it has stopped (every child process is killed). Safe to call any time.
    void stopAndWait();
    /// The main window owns the game process: while it runs, building (which replaces the modules) and a second Play are off.
    void setGameRunning(bool running);

signals:
    void builtChanged(bool built);
    void playRequested();

private:
    struct Row {
        QLineEdit* edit = nullptr;
        QLabel* status = nullptr;
    };
    Row addRow(class QGridLayout* grid, int row, const QString& title, const QString& hint, std::function<void()> browse);
    void pick(Row& row, bool folder, const QString& title, const QString& filter);
    void pickDlc();
    void revalidate();
    void adoptInstalled();
    SetupInputs inputs() const;
    void build();
    void cancel();
    void finishBuild(bool ok, const QString& message);
    void refreshButtons();
    void saveInputs() const;

    BuilderPaths paths;
    DataLayout layout;
    Row base, update, keys, firmware, dlcRow;
    QStringList dlc;
    QPushButton *buildButton, *cancelButton, *playButton;
    QProgressBar* bar;
    QLabel* message;
    std::thread worker;
    bool gameRunning = false;
    std::atomic<bool> cancelFlag{false};
    bool building = false;
    bool inputsValid = false;
};
