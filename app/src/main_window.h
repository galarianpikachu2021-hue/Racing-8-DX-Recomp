// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMainWindow>
#include <QProcess>

#include "game_package.h"
#include <atomic>
#include <functional>

#include "ini_file.h"
#include "mod_addon.h"
#include "overlay_settings.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

signals:
    void importProgress(int done, int total, const QString& name);
    void importFinished(bool ok, const QString& error);

private:
    QWidget* buildPlayTab();
    QWidget* buildDisplayTab();
    QWidget* buildFrameRateTab();
    QWidget* buildEnhancementsTab();
    QWidget* buildModsTab();
    QWidget* buildControllersTab();
    QWidget* buildMiiTab();
    QWidget* buildDlcTab();

    void reloadPackage();
    void loadSettings();
    bool saveSettings();
    void applySettingsNow();
    void connectAutoSave();
    void refreshDlc();
    void refreshControllers();
    void loadOverlay();
    void saveOverlay();
    void refreshMods();
    void syncTextMerge();
    void importCtgp();
    bool resolveCtgpConflict(bool ctgp_wanted, bool other_wanted);
    void runImport(const std::function<bool(QString*, const std::function<bool(int, int, const QString&)>&)>& job);
    void launch();
    void packageAppImage();
    QProcessEnvironment launchEnvironment() const;
    void appendLog(const QString& text);

    GamePackage package;
    IniFile config;
    QProcess game;

    // Play
    QLineEdit* root_edit = nullptr;
    QLineEdit* home_edit = nullptr;
    QLabel* status_label = nullptr;
    QCheckBox* fullscreen_check = nullptr;
    QCheckBox* nvn_capture_check = nullptr;
    QComboBox* renderer_combo = nullptr;
    QComboBox* native_res_combo = nullptr;
    QComboBox* pin_combo = nullptr;
    QPushButton* play_button = nullptr;
    QPushButton* stop_button = nullptr;
    QPushButton* appimage_button = nullptr;
    QProgressBar* appimage_progress = nullptr;
    class Updater* updater = nullptr;
    QPlainTextEdit* log_view = nullptr;
    // Display
    QComboBox* resolution_combo = nullptr;
    QComboBox* aspect_combo = nullptr;
    QComboBox* filter_combo = nullptr;
    QComboBox* aa_combo = nullptr;
    QComboBox* vsync_combo = nullptr;
    QComboBox* fullscreen_mode_combo = nullptr;
    QComboBox* docked_combo = nullptr;
    QSpinBox* fsr_sharpness_spin = nullptr;
    QWidget* display_page = nullptr;
    QWidget* frame_rate_page = nullptr;
    bool loading_settings = false;
    // Frame rate
    QCheckBox* limit_check = nullptr;
    QSpinBox* limit_spin = nullptr;
    // Enhancements (the in-game overlay's settings file)
    OverlaySettings overlay;
    QWidget* enhancements_page = nullptr;
    QComboBox* upscaler_combo = nullptr;
    QComboBox* scale_combo = nullptr;
    QComboBox* sharpness_combo = nullptr;
    QComboBox* framegen_combo = nullptr;
    QCheckBox* jitter_check = nullptr;
    QCheckBox* hdr_check = nullptr;
    QComboBox* hdr_paper_combo = nullptr;
    QComboBox* hdr_peak_combo = nullptr;
    QComboBox* present_combo = nullptr;
    QCheckBox* fps_check = nullptr;
    QStringList upscaler_ids;
    QStringList framegen_ids;
    QCheckBox* malloc_check = nullptr;
    // Mods
    QListWidget* mods_list = nullptr;
    QLabel* mod_details = nullptr;
    QPushButton* mod_import_button = nullptr;
    QPushButton* mod_add_folder_button = nullptr;
    QPushButton* mod_add_zip_button = nullptr;
    QList<ModAddon::ModEntry> mod_entries;
    QPushButton* mod_toggle_button = nullptr;
    QPushButton* mod_remove_button = nullptr;
    QPushButton* mod_recomp_button = nullptr;
    QProgressBar* mod_progress = nullptr;
    std::atomic<bool> import_cancel{false};
    bool importing = false;
    ModAddon::PackInfo mod_info;
    // Controllers / DLC
    QTableWidget* controls_table = nullptr;
    QTableWidget* players_table = nullptr;
    QListWidget* dlc_list = nullptr;
};
