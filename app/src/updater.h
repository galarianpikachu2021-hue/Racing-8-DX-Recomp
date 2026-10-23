// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QWidget;

// Checks the project's GitHub releases for a newer launcher and installs it: a source checkout pulls and rebuilds, a launcher
// AppImage downloads its replacement, anything else gets the release page. The game itself is never downloaded (it is built
// on the player's machine from their own files; see LEGAL.md).
class Updater : public QObject {
    Q_OBJECT
public:
    static constexpr const char* kRepo = "galarianpikachu2021-hue/Racing-8-DX-Recomp";
    explicit Updater(QWidget* parent);
    /// `quiet`: only speak up when there is an update (the start-up check, at most once a day).
    void check(bool quiet);

signals:
    /// Progress text for the launcher's output box.
    void log(const QString& line);

private:
    void offer(const QString& tag, const QString& notes, const QString& page, const QString& asset_url);
    void updateCheckout(const QString& root);
    void updateAppImage(const QString& url);
    QWidget* parent_widget;
    QNetworkAccessManager* net;
};
