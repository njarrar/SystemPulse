<img src="brand/pulse-icon.svg" width="96" alt="Pulse icon">

# Pulse

Your system's vitals, in a flash. صحة جهازك بنظرة واحدة.

Pulse is a small, local-only system monitor (no network use) with one native app per platform: macOS, Windows 11, Linux (GNOME), ChromeOS, Windows XP and Windows 98 SE. A tray or menu bar readout opens a 420px panel with CPU, memory, energy, thermal, GPU, storage, network and top apps, each with a 10-minute history.

## Screenshots

From the design page (`build/prototype`), in English and Arabic.

| Platform | English | العربية |
|---|---|---|
| **macOS** | <img src="docs/screenshots/macos-en.png" width="300" alt="Pulse on macOS, English"> | <img src="docs/screenshots/macos-ar.png" width="300" alt="Pulse on macOS, Arabic"> |
| **Windows 11** | <img src="docs/screenshots/windows-11-en.png" width="300" alt="Pulse on Windows 11, English"> | <img src="docs/screenshots/windows-11-ar.png" width="300" alt="Pulse on Windows 11, Arabic"> |
| **Linux (GNOME)** | <img src="docs/screenshots/linux-en.png" width="300" alt="Pulse on Linux (GNOME), English"> | <img src="docs/screenshots/linux-ar.png" width="300" alt="Pulse on Linux (GNOME), Arabic"> |
| **ChromeOS** | <img src="docs/screenshots/chromeos-en.png" width="300" alt="Pulse on ChromeOS, English"> | <img src="docs/screenshots/chromeos-ar.png" width="300" alt="Pulse on ChromeOS, Arabic"> |
| **Windows XP** | <img src="docs/screenshots/windows-xp-en.png" width="300" alt="Pulse on Windows XP, English"> | <img src="docs/screenshots/windows-xp-ar.png" width="300" alt="Pulse on Windows XP, Arabic"> |
| **Windows 98 SE** | <img src="docs/screenshots/windows-98-en.png" width="300" alt="Pulse on Windows 98 SE, English"> | <img src="docs/screenshots/windows-98-ar.png" width="300" alt="Pulse on Windows 98 SE, Arabic"> |

## Download

Ready-to-run apps live in `build/`, one folder per platform:

| Folder | What is in it |
|---|---|
| `build/macos/` | `Pulse.app` (zip), built by GitHub Actions on macOS |
| `build/windows-11/` | Zips for x64 and ARM64 (extract, then run `Pulse.exe`), built and test-started by GitHub Actions on Windows |
| `build/linux/` | `pulse`, one binary (needs GTK 4 and libadwaita) |
| `build/windows-xp/` | `pulse.exe`, 32-bit, runs on XP SP2 and later |
| `build/windows-98/` | `pulse98.exe`, 32-bit, runs on Windows 98 SE and later |
| `build/chromeos/` | A preview of the ChromeOS screen with sample data; the real app ships inside a ChromeOS build (see the README there) |
| `build/prototype/` | The interactive design page that shows all six platforms |

Each folder's README says how to run it.

## What's new

**1.2.1** (October 7, 2026)

- Windows 11: Pulse now opens. Before, it closed at once on every PC, because some of its screen parts lacked a code marking that the fast-start build needs, so Windows could not lay them out. The build now stops if that marking is missing.
- Windows 11: the zip is tidy. The top holds only `Pulse.exe` plus the `data` and `lang` folders; each language has its own folder in `lang`.
- Windows 11: if Pulse ever fails to start, it says so in a message box and saves the details to `%LOCALAPPDATA%\Pulse\crash.log`.
- Windows 11: each build is now started from a clean folder on two Windows machines (Windows 11 on Arm and Windows Server) before it is published.

**1.2.0** (October 5, 2026)

- New icon: five bars, one for each vital (CPU, energy, memory, thermal, GPU), on a white tile. Every app uses it as its program icon and as the logo in its header. macOS gets an app icon for the first time.
- `brand/` holds the icon as SVG, a 512 px PNG and the GitHub social preview. `node brand/render.mjs` redraws every icon file from the SVG.
- Rebuilt every app in `build/`.

**1.1.0** (October 5, 2026)

- The memory badge shows pressure by color: normal below 70%, high from 70%, critical from 90%.
- New text in English and Arabic for a "Data sources" note, which says which readings are live, simulated or missing. It also covers Windows 98 limits: no per-app CPU, and Arabic needs code page 1256 and an Arabic font.
- Windows 98 now reads its text from `locales/` like every other app, so a new language file reaches it too.
- Builds and tests no longer fail on machines that lack some tools. Linux can build its core without GTK, and the Windows XP, Windows 98 and ChromeOS tests skip steps whose tools are missing.
- Removed stray tool files from the Windows 11 source.
- Rebuilt every app in `build/`.

**1.0.0**: first release.

## Languages

Pulse ships in English and Arabic, with right-to-left layout for Arabic. Pick a language in **Settings > Language**, or with the `EN | ع` switch in the header. **Match system** follows your computer's language.

**Add a language:** add one file to `locales/`. See [TRANSLATING.md](TRANSLATING.md). Every app picks it up on its next build, with no code changes.

## Source

```
locales/       one file per language (the only place app text lives)
i18n/          shared language engine, checks and tests
native/        one app per platform (see native/README.md)
prototype/     source of the design page
build/         ready-to-run apps
docs/          screenshots
brand/         icon and social preview (render.mjs redraws them)
.github/       builds and checks that run on every push
```

`AUDIT.md` lists what changed against the first design and what is still open.

## Build from source

Each app's README under `native/` gives its build steps. In short:

```
cd native/linux && cargo build --release          # Linux
cd native/winxp && sh build.sh                    # Windows XP (MinGW)
cd native/win98 && make                           # Windows 98 (MinGW)
cd native/macos && bash scripts/build-app.sh      # macOS (Xcode 15+)
cd native/win11 && dotnet publish src/Pulse.App/Pulse.App.csproj -c Release -p:Platform=x64 -r win-x64 -o publish/x64   # Windows 11
python3 prototype/build.py                        # design page
```

## Tests

```
node i18n/validate.js && node i18n/test/test.js
```
