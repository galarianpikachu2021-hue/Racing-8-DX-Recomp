// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// A locally built game package. Nothing in it is ever part of this repository.
//
//   <root>/mk8d[.exe]                 strict-static executable built from the user's own dump
//   <root>/content/base.nsp           base game
//   <root>/content/update-3.0.4.nsp   the only update the AArch64 recompiler accepts
//   <root>/content/dlc/*.nsp          optional add-on content
//   <root>/user/                      portable saves/config/logs, created by the executable
//
// Keys and firmware come from a separate suyu data directory ("suyu home").
class GamePackage {
public:
    explicit GamePackage(QString root = {}, QString suyu_home = {});

    QString root() const { return root_dir; }
    QString suyuHome() const { return suyu_home; }
    void setRoot(const QString& dir) { root_dir = dir; }
    void setSuyuHome(const QString& dir) { suyu_home = dir; }

    QString executable() const;
    QString baseNsp() const;
    QString updateNsp() const;
    QString dlcDir() const;
    QStringList dlcFiles() const;
    QString configFile() const;
    QString coverageReport() const;

    // Empty when the package can launch; otherwise what is missing, one item per line.
    QString problems() const;

    QStringList launchArguments(bool fullscreen) const;
    QProcessEnvironment launchEnvironment() const;

    // suyu writes absolute data paths into the config; repoint them at this package
    // so a moved folder never writes saves back to its old location.
    bool fixDataPaths() const;

    // Default locations, looked up next to the app and in the working directory.
    static QString findDefaultRoot();
    static QString findDefaultSuyuHome(const QString& root);

private:
    QString root_dir;
    QString suyu_home;
};
