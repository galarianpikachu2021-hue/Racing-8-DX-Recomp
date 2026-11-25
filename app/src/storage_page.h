// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's Storage tab: where the big files are, how big they are, and moving them to another drive (an SD card).
#pragma once

#include <QWidget>
#include <atomic>
#include <functional>
#include <thread>

#include "setup_paths.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

class StoragePage : public QWidget {
    Q_OBJECT
public:
    explicit StoragePage(QWidget* parent = nullptr);
    ~StoragePage() override;

    void setLocked(bool locked); ///< while the game runs or is being built
    bool isMoving() const { return moving; }
    /// Asked right before anything is changed: something else uses the files (the game, a build, a mod import).
    void setBusyCheck(std::function<bool()> check) { busy = std::move(check); }

signals:
    void movingChanged(bool moving); ///< nothing else may use the files meanwhile

protected:
    void showEvent(QShowEvent* e) override;

private:
    void refreshDrives();
    void measure();
    void chooseFolder();
    QString target() const; ///< the folder picked ("" = the standard folders)
    void targetChanged();
    void move();
    void useWithoutMoving();
    QString commit(const DataLayout& to, const QString& folder) const;
    void finish(const QString& error, const QString& leftover);
    void refreshButtons();

    std::function<bool()> busy;
    DataLayout layout;
    QString current; ///< the chosen folder now ("" = standard)
    QString custom;  ///< a folder from "Choose folder..."
    QLabel *where = nullptr, *sizes = nullptr, *note = nullptr, *status = nullptr;
    QComboBox* drives = nullptr;
    QPushButton *folder_button = nullptr, *move_button = nullptr, *use_button = nullptr, *cancel_button = nullptr;
    QProgressBar* bar = nullptr;
    std::thread measurer, mover;
    std::atomic<bool> stop{false}, cancel{false};
    bool measured = false, locked = false, moving = false, target_ok = false, target_used = false;
};
