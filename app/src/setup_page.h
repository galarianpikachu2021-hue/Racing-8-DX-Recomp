// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's first page: one picker per input (base game, update 3.0.4, DLC, keys, firmware), a live check of each, Build, and Play.
#pragma once

#include <QProcess>
#include <QWidget>
#include <atomic>
#include <memory>

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

signals:
    void builtChanged(bool built);

private:
    struct Row {
        QLineEdit* edit = nullptr;
        QLabel* status = nullptr;
    };
    Row addRow(class QGridLayout* grid, int row, const QString& title, const QString& hint, std::function<void()> browse);
    void pick(Row& row, bool folder, const QString& title, const QString& filter);
    void pickDlc();
    void revalidate();
    SetupInputs inputs() const;
    void build();
    void cancel();
    void finishBuild(bool ok, const QString& message);
    void play();
    void refreshButtons();
    void saveInputs() const;

    BuilderPaths paths;
    DataLayout layout;
    Row base, update, keys, firmware, dlcRow;
    QStringList dlc;
    QPushButton *buildButton, *cancelButton, *playButton;
    QProgressBar* bar;
    QLabel* message;
    QProcess game;
    std::atomic<bool> cancelFlag{false};
    bool building = false;
    bool inputsValid = false;
};
