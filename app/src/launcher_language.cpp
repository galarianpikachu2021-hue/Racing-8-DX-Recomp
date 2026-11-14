// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher_language.h"

#include <QApplication>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QTranslator>

namespace LauncherLanguage {

QList<Language> Available() {
    // The game's languages, by their own names.
    static const QList<Language> all{
        {QString(), QString()},              // automatic: its label is translated where it is shown
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("es"), QStringLiteral("Español")},
        {QStringLiteral("fr"), QStringLiteral("Français")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("nl"), QStringLiteral("Nederlands")},
        {QStringLiteral("pt"), QStringLiteral("Português")},
        {QStringLiteral("ru"), QStringLiteral("Русский")},
        {QStringLiteral("ja"), QStringLiteral("日本語")},
        {QStringLiteral("ko"), QStringLiteral("한국어")},
        {QStringLiteral("zh_CN"), QStringLiteral("简体中文")},
        {QStringLiteral("zh_TW"), QStringLiteral("繁體中文")},
    };
    QList<Language> out;
    for (const Language& l : all) {
        // Only what this build carries (a build without Qt's translation tools has English only).
        if (l.code.isEmpty() || l.code == QLatin1String("en") || QFile::exists(QStringLiteral(":/i18n/kartrecomp_%1.qm").arg(l.code))) out << l;
    }
    return out;
}

QString Chosen() {
    return QSettings().value(QStringLiteral("launcher_language")).toString();
}

void SetChosen(const QString& code) {
    QSettings().setValue(QStringLiteral("launcher_language"), code);
}

void Install() {
    const QString chosen = Chosen();
    if (chosen == QLatin1String("en")) return;
    // Automatic: the first of the system's languages that has a translation (zh_HK and zh_MO read traditional Chinese).
    QLocale locale = chosen.isEmpty() ? QLocale::system() : QLocale(chosen);
    if (chosen.isEmpty() && locale.language() == QLocale::Chinese && locale.script() == QLocale::TraditionalChineseScript)
        locale = QLocale(QStringLiteral("zh_TW"));
    static QTranslator app, qt;
    if (app.load(locale, QStringLiteral("kartrecomp"), QStringLiteral("_"), QStringLiteral(":/i18n"))) QApplication::installTranslator(&app);
    else return; // a language without a translation stays English throughout, Qt's own texts included
    // Qt's own texts (standard buttons, file dialogs): built in where the build had them, else from the Qt installation.
    if (qt.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), QStringLiteral(":/i18n")) ||
        qt.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qt);
}

void Restart() {
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    const QString program = appimage.isEmpty() ? QApplication::applicationFilePath() : appimage;
    QProcess::startDetached(program, QApplication::arguments().mid(1));
    QApplication::quit();
}

} // namespace LauncherLanguage
