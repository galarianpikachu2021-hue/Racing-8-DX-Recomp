// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's own language (not the game's: that is the Console language on the Play tab). The launcher is translated into the
// languages the game itself ships (app/i18n/kartrecomp_<code>.ts, built into the program); "automatic" follows the system.
#pragma once

#include <QList>
#include <QString>

namespace LauncherLanguage {

struct Language {
    QString code;   ///< "" = automatic, "en", or a translation's code (de, fr, ..., zh_CN, zh_TW)
    QString native; ///< the language's own name, shown untranslated
};

/// Automatic, English, then every built-in translation.
QList<Language> Available();
/// The saved choice ("" = automatic).
QString Chosen();
void SetChosen(const QString& code);
/// Loads the chosen (or the system's) translation into the application. Call once, before any window is made.
void Install();
/// Starts a new launcher with the same arguments (the AppImage, when running from one) and quits this one.
void Restart();

} // namespace LauncherLanguage
