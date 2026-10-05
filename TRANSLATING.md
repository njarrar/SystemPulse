# Add or fix a translation

Every language Pulse speaks lives in one file: `locales/<code>.json`. To add a language, you add one file. No code changes.

## Add a new language on GitHub (no tools needed)

1. Open the `locales` folder and click `en.json`.
2. Copy the whole file.
3. Go back to `locales`, click **Add file > Create new file**, and name it after your language code, for example `de.json`, `fr.json`, `pt-BR.json` or `ur.json`.
4. Paste, then change the top fields:
   - `code`: the same code as the file name.
   - `label`: two or three letters in your own script, shown on the switch (for example `DE`, `FR`, `اردو`).
   - `name`: your language's name in your language (for example `Deutsch`). It shows in **Settings > Language**.
   - `dir`: `rtl` for right-to-left scripts (Arabic, Hebrew, Persian, Urdu), else `ltr`.
5. Translate the text on the right side of each line. Leave the keys on the left alone, and keep every `{placeholder}` as it is, for example `{app}` or `{n}`.
6. Plurals: copy your language's rules from the [CLDR plural table](https://www.unicode.org/cldr/charts/latest/supplemental/language_plural_rules.html) into `pluralRules`, and give each entry under `plurals` one form per category your language uses, plus `other`. If you are unsure, delete `pluralRules` and the app uses the system's rules.
7. Click **Commit changes**, choose **Create a new branch and start a pull request**, and open the pull request.

A check runs on your pull request and lists anything missing or broken. Keys you have not translated yet fall back to English, so a partial file still works.

## Fix a word in an existing language

Open the file in `locales`, click the pencil icon, change the text, and open a pull request.

## Test it on your computer (optional)

```
node i18n/validate.js          # lists errors and missing keys
node i18n/test/test.js         # plural and direction tests
python3 prototype/build.py     # rebuilds the demo page with every language
```

Open `build/prototype/Pulse_System_Monitor.html` in a browser and pick your language under **Settings > Language**.

## How each app uses the files

Each app reads every file in `locales/` when it is built and turns it into its own format, so the next build of every app includes your language:

| App | Format it builds |
|---|---|
| macOS | `Localizable.xcstrings` |
| Windows 11 | `Strings/<lang>/Resources.resw` |
| Linux | gettext `.po` / `.mo` |
| ChromeOS | Chromium `.grd` / `.xtb` |
| Windows XP | UTF-16 string table |
| Windows 98 | embedded table with a code page per language |

Windows 98 can only show a language whose script has a Windows code page (for example Arabic uses 1256). Other scripts show in English there.
