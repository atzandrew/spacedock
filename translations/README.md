# Translations

SpaceDock uses the desktop locale at startup. If no matching catalog is
available, English text and plural forms are used. Catalogs cover French, German, Spanish, Brazilian Portuguese, Italian,
Russian, Ukrainian, Polish, Simplified Chinese, and Japanese. Qt's own dialog translations are
loaded from the installed Qt translations directory when available.

`spacedock.ts` is the source template, not a translated catalog. To start a
language, copy it to `spacedock_<locale>.ts` (for example,
`spacedock_fr.ts`) and set its target language in Qt Linguist. Translate and
review the messages, including plural forms, before shipping it. Unfinished
messages fall back to English.

After changing source strings, refresh the template and each language catalog
with Qt's `lupdate` (on some distributions it is named `lupdate-qt6`):

```sh
lupdate src qml -no-obsolete -locations relative -ts translations/spacedock.ts
lupdate src qml -no-obsolete -locations relative -ts translations/spacedock_fr.ts
```

CMake discovers `spacedock_*.ts` catalogs, compiles them with Qt Linguist
Tools, and embeds the resulting files in the application. Qt Linguist Tools
must be installed when building with catalogs. Restart SpaceDock after changing
the desktop language. Desktop-entry translations are maintained separately in
`data/spacedock.desktop.in`.

`spacedock_en.ts` contains the English plural forms and is also loaded as a
fallback for untranslated languages. Keep it in sync with all numerus messages;
do not overwrite it with the full source template. The source text and desktop
entry are maintained separately, so update `Comment[fr]` when needed.

The initial language selection uses the October 2026 Steam Hardware & Software
Survey’s Linux language breakdown as a proxy for desktop usage, grouping the
Spanish variants together. Its voluntary gaming sample is not representative
of every Linux user. Catalogs should be reviewed by native speakers.
