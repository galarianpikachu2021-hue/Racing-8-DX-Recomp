# Launcher translations

The launcher is translated into the languages the game ships: German, Spanish, French, Italian, Dutch, Portuguese, Russian,
Japanese, Korean, Simplified and Traditional Chinese (English is the source). The language is picked on the Setup page
("Launcher language"; "Automatic" follows the system) and applies after a restart. The game's own language is separate (Play tab,
Console language).

- `strings.json`: every translatable text as `[context, source, plural]`, in a fixed order. lupdate finds the `tr()` and
  `QCoreApplication::translate("Messages", ...)` texts; the run-time ones (combo lists in `main_window.cpp`, the Mii editor's
  field labels, colours) were added by hand.
- `translations/<lang>.py`: `T = {index: text}` per language. A missing index stays English (used for numbers, units and
  names such as FSR, DLSS, AppImage).
- `make_ts.py strings.json translations/<lang>.py <lang> ../../app/i18n/kartrecomp_<lang>.ts` writes the Qt file and refuses a
  translation that loses a placeholder (`%1`, `%n`, ...) or a line break.
- The build turns `app/i18n/*.ts` into `.qm` files inside the launcher when Qt's LinguistTools are installed (otherwise English
  only), together with Qt's own `qtbase_<lang>.qm` for standard buttons and dialogs.

Adding or changing a text: run lupdate over `app/src` into a scratch `.ts`, append the new `[context, source, plural]` entries
to the end of `strings.json` (never reorder: the indices are the keys), add the translations to each `translations/<lang>.py`,
and regenerate the eleven `.ts` files with `make_ts.py`.
