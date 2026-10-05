# Pulse native apps

Six apps, one per platform in Part 2 of the spec. Each reads `../locales/*.json` at build time and turns it into the platform's own catalog, so a new language needs only a new locale file and a rebuild. Each one ports the plural engine and checks it against `../i18n/test/fixtures/vectors.json`.

| Folder | Stack | Built here | Tested here | Screenshots |
|---|---|---|---|---|
| `linux/` | Rust, GTK 4, libadwaita, D-Bus tray item | Yes, release binary in `dist/` | 19 tests pass; runs under Xvfb | `dist/screenshots/` |
| `winxp/` | C/C++, Win32, GDI+, uxtheme | Yes, `dist/pulse.exe` (205 KB) | 515 tests pass; runs under Wine set to XP | `dist/` |
| `win98/` | C89, Win32 ANSI, GDI | Yes, `dist/pulse98.exe` (96 KB) | 565 tests pass; runs under Wine set to 98 | `dist/` |
| `chromeos/` | System Web App: TypeScript, Mojo, C++ in ash | Page and two C++ parsers only | 512 plural checks and 13 C++ tests pass; page runs against a mock backend | `dist/` |
| `macos/` | Swift, AppKit, SwiftUI | Core library only (on Linux) | 34 tests pass | None yet; CI renders them |
| `win11/` | C#, .NET 8, WinUI 3 | Core library and app code compile | 60 tests pass | None yet |

## What still needs a real machine

- **macOS:** the AppKit, SwiftUI and IOKit code has only been syntax checked. Build it on a Mac with `bash macos/scripts/build-app.sh`, or push to GitHub and let `.github/workflows/macos.yml` build it.
- **Windows 11:** the app compiles, but it has never run, and the resource and Native AOT steps need Windows. Build on a PC with .NET 8, or through `.github/workflows/win11.yml`.
- **ChromeOS:** the mojom, page handler, sampler and shelf code need a Chromium checkout to compile. See `chromeos/README.md` for where each file goes.
- **XP and 98:** tested under Wine only. Real XP and 98 machines or VMs would confirm the older APIs (PerfStats and RSRC32 on 98) and the RAM targets.
- **Linux:** the top bar item was checked over D-Bus only, not inside a GNOME session.

Each folder's README covers build steps, flags, and which readings are real and which are demo data. The Settings switches for CPU hog and Charging lay demo data over live readings in every app.

## Open items across apps

- No locale strings yet for moderate and critical memory pressure, for "could not end this app", or for a desktop on AC power with no battery. The apps show "High", nothing, or a fallback for now.
- The Windows 98 app keeps a few of its own strings in `win98/locales-native/`. They should move into `locales/`.
- The `simNote` string still says "in this prototype".
