// SPDX-License-Identifier: GPL-3.0-or-later
#include "updater.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QUrl>
#include <QVersionNumber>
#include <QWidget>

namespace {
constexpr char kLauncherAsset[] = "KartRecomp-Launcher-x86_64.AppImage";

QNetworkRequest Request(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("KartRecomp-Launcher/%1").arg(QCoreApplication::applicationVersion()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

/// The source checkout this launcher was built from (<repo>/build/app/kartrecomp), if any.
QString CheckoutRoot() {
    const QDir dir(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../..")));
    return QFileInfo::exists(dir.filePath(QStringLiteral(".git"))) && QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt")))
               ? dir.absolutePath()
               : QString{};
}
} // namespace

Updater::Updater(QWidget* parent) : QObject(parent), parent_widget(parent), net(new QNetworkAccessManager(this)) {}

void Updater::check(bool quiet) {
    QSettings settings;
    if (quiet) {
        const auto last = settings.value(QStringLiteral("update_checked")).toDateTime();
        if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 3600) return;
    }
    settings.setValue(QStringLiteral("update_checked"), QDateTime::currentDateTimeUtc());
    auto* reply = net->get(Request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(kRepo)))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, quiet] {
        reply->deleteLater();
        const auto json = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = json.value(QStringLiteral("tag_name")).toString();
        if (reply->error() != QNetworkReply::NoError || tag.isEmpty()) {
            if (!quiet) QMessageBox::information(parent_widget, tr("Updates"), tr("Could not check for updates: %1").arg(reply->errorString()));
            return;
        }
        const auto latest = QVersionNumber::fromString(tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag);
        const auto current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        if (latest <= current) {
            if (!quiet) QMessageBox::information(parent_widget, tr("Updates"), tr("You have the latest version (%1).").arg(current.toString()));
            return;
        }
        QString asset;
        for (const auto& a : json.value(QStringLiteral("assets")).toArray()) {
            if (a.toObject().value(QStringLiteral("name")).toString() == QLatin1String(kLauncherAsset)) {
                asset = a.toObject().value(QStringLiteral("browser_download_url")).toString();
            }
        }
        offer(tag, json.value(QStringLiteral("body")).toString(), json.value(QStringLiteral("html_url")).toString(), asset);
    });
}

void Updater::offer(const QString& tag, const QString& notes, const QString& page, const QString& asset_url) {
    const QString root = CheckoutRoot();
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    QString how;
    if (!root.isEmpty()) how = tr("Update pulls the new source and rebuilds the launcher.");
    else if (!appimage.isEmpty() && !asset_url.isEmpty()) how = tr("Update downloads the new launcher and replaces this one.");
    else how = tr("Update opens the release page.");
    QMessageBox box(parent_widget);
    box.setWindowTitle(tr("Update available"));
    box.setText(tr("Racing 8 DX Recomp %1 is available (you have %2).").arg(tag, QCoreApplication::applicationVersion()));
    box.setInformativeText(how + QLatin1Char('\n') +
                           tr("Changes to the game itself take effect after you rebuild the game from your files."));
    box.setDetailedText(notes);
    box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    box.button(QMessageBox::Ok)->setText(tr("Update"));
    box.button(QMessageBox::Cancel)->setText(tr("Later"));
    if (box.exec() != QMessageBox::Ok) return;
    if (!root.isEmpty()) updateCheckout(root);
    else if (!appimage.isEmpty() && !asset_url.isEmpty()) updateAppImage(asset_url);
    else QDesktopServices::openUrl(QUrl(page));
}

void Updater::updateCheckout(const QString& root) {
    // git pull --ff-only (never touches local changes it can't fast-forward over), then rebuild the launcher in its build folder.
    auto* pull = new QProcess(this);
    pull->setWorkingDirectory(root);
    pull->setProcessChannelMode(QProcess::MergedChannels);
    connect(pull, &QProcess::readyRead, this, [this, pull] { emit log(QString::fromUtf8(pull->readAll()).trimmed()); });
    connect(pull, &QProcess::finished, this, [this, pull, root](int code) {
        pull->deleteLater();
        if (code != 0) {
            QMessageBox::warning(parent_widget, tr("Update"), tr("git pull failed (see the output). Local changes in %1?").arg(root));
            return;
        }
        auto* build = new QProcess(this);
        build->setWorkingDirectory(root);
        build->setProcessChannelMode(QProcess::MergedChannels);
        connect(build, &QProcess::readyRead, this, [this, build] { emit log(QString::fromUtf8(build->readAll()).trimmed()); });
        connect(build, &QProcess::finished, this, [this, build](int c) {
            build->deleteLater();
            if (c != 0) {
                QMessageBox::warning(parent_widget, tr("Update"), tr("The launcher did not build (see the output)."));
                return;
            }
            if (QMessageBox::question(parent_widget, tr("Update"), tr("Updated. Restart the launcher now?")) == QMessageBox::Yes) {
                QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
                QCoreApplication::quit();
            }
        });
        emit log(tr("Rebuilding the launcher…"));
        build->start(QStringLiteral("cmake"), {QStringLiteral("--build"), QStringLiteral("build"), QStringLiteral("--target"), QStringLiteral("kartrecomp")});
    });
    emit log(tr("Pulling the new version…"));
    pull->start(QStringLiteral("git"), {QStringLiteral("pull"), QStringLiteral("--ff-only")});
}

void Updater::updateAppImage(const QString& url) {
    const QString target = qEnvironmentVariable("APPIMAGE");
    auto* reply = net->get(Request(QUrl(url)));
    emit log(tr("Downloading the new launcher…"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, target] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        QFile out(target + QStringLiteral(".new"));
        if (reply->error() != QNetworkReply::NoError || data.size() < 1024 * 1024 || !out.open(QIODevice::WriteOnly) ||
            out.write(data) != data.size()) {
            QMessageBox::warning(parent_widget, tr("Update"), tr("The download failed: %1").arg(reply->errorString()));
            return;
        }
        out.close();
        out.setPermissions(out.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
        QFile::remove(target);
        if (!QFile::rename(out.fileName(), target)) {
            QMessageBox::warning(parent_widget, tr("Update"), tr("Could not replace %1.").arg(target));
            return;
        }
        if (QMessageBox::question(parent_widget, tr("Update"), tr("Updated. Restart the launcher now?")) == QMessageBox::Yes) {
            QProcess::startDetached(target, {});
            QCoreApplication::quit();
        }
    });
}
