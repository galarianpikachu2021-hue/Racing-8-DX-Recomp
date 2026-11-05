// SPDX-License-Identifier: GPL-3.0-or-later
// Where the builder's parts are (engine, exporter, toolchain: inside the AppImage / install folder, or the development tree) and where
// the user's data lives (the XDG folders), plus installing the user's picked files into that layout.
#pragma once

#include <QString>

#include "setup_validate.h"

struct DataLayout {
    QString dataRoot;  // ~/.local/share/kartrecomp
    QString cacheRoot; // ~/.cache/kartrecomp
    static DataLayout FromRoots(const QString& data, const QString& cache);
    static QString PortableRoot(); // Windows: <folder of kartrecomp.exe>/userdata
    static QString ConfigRoot();   // the launcher's and the engine's settings folder
    static DataLayout Default(); // honours XDG_DATA_HOME / XDG_CACHE_HOME

    QString content() const { return dataRoot + "/content"; }
    QString base() const { return content() + "/base.nsp"; }
    QString update() const { return content() + "/update-3.0.4.nsp"; }
    QString dlcDir() const { return content() + "/dlc"; }
    QString keysFile() const { return dataRoot + "/keys/prod.keys"; }
    QString firmwareDir() const { return dataRoot + "/nand/system/Contents/registered"; }
    QString moduleDir() const { return dataRoot + "/module"; }
    QString modsDir() const { return dataRoot + "/mods"; }
    QString exportDir() const { return cacheRoot + "/export"; }
    QString workDir() const { return cacheRoot + "/build-work"; }
};

struct BuilderPaths {
    QString exporter;      // kartrecomp-export
    QString engine;        // mk8d-engine
    QString engineVersion; // contents of ENGINE_VERSION next to the engine
    QString toolchain;     // folder made by packaging/make-toolchain.sh
    QString hotList;       // data/mk8d-3.0.4/hot_blocks.txt
    /// Environment overrides (KARTRECOMP_EXPORTER, _ENGINE, _TOOLCHAIN, _HOT_LIST), else the install layout next to the launcher.
    static BuilderPaths Detect();
    bool complete(QString* missing) const; // every part exists; `missing` names the first that does not
};

/// Links (large NSPs: no second 13 GB copy) or copies the user's files into the data layout. "" or the reason it failed.
QString InstallInputs(const SetupInputs& in, const DataLayout& layout);

/// The game is built when the module folder holds the modules and a module.json stamped for this engine version.
bool IsBuilt(const DataLayout& layout, const QString& engineVersion);
