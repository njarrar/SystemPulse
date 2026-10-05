# Pulse for Windows XP

A tray system monitor for Windows XP SP2 and SP3, 32-bit x86. Plain C and C++ on
Win32, GDI+ and uxtheme. No runtime to install: one 200 KB `pulse.exe`.

```
native/winxp/
  build.sh                   build (and with "test", run the unit tests)
  src/app.cpp                window, tray icon, layout, drawing, input
  src/telemetry.cpp/.h       all system readings
  src/i18n.c/.h              C port of i18n/pulse-i18n.js
  src/pulse.rc               icon, version info, includes the generated catalog
  tools/gen_catalog.py       locales/*.json -> STRINGTABLE (.rc) + key header
  tools/make_icon.py         writes pulse.ico
  tools/check_xp_imports.py  fails the build if the exe needs anything newer than XP
  tests/test_i18n.c          plural and string tests against vectors.json
  tests/vectors2tsv.py       flattens vectors.json for the C tests
  tests/spin.c               test load: spin.exe [seconds] [duty %] on every CPU
  dist/                      pulse.exe and screenshots
```

## Build

Needs mingw-w64 (`i686-w64-mingw32-gcc`, `g++`, `windres`) and Python 3.

```
sh native/winxp/build.sh            # writes native/winxp/build/pulse.exe
BUILD=/tmp/x sh native/winxp/build.sh test   # other output folder, plus tests
```

The script:

1. runs `tools/gen_catalog.py`, which reads every `locales/*.json` and writes
   `catalog.rc` (one UTF-16 STRINGTABLE per locale, for Microsoft RC),
   `catalog.windres.rc` (the same tables with `\xHHHH` escapes, since GNU
   windres cannot read UTF-16), `catalog_keys.h` and `catalog.tsv`;
2. compiles with `_WIN32_WINNT=0x0501`, `-march=i686`, static libgcc and
   libstdc++, `-fno-threadsafe-statics` (the thread-safe statics in libstdc++
   need Vista condition variables), and PE OS and subsystem version 5.1;
3. links `gdiplus`, `psapi`, `iphlpapi`, `shell32`, `user32`, `gdi32`, `advapi32`;
4. runs `tools/check_xp_imports.py` on the result.

`uxtheme.dll` and `ntdll!NtQuerySystemInformation` are loaded with
`GetProcAddress`, so the exe still starts with the Classic theme or without
`uxtheme.dll`. Every other import exists on XP SP2.

## Run

```
pulse.exe                      tray icon; click it to open the flyout
pulse.exe --show-flyout        open the flyout at once, no tray icon (for tests)
pulse.exe --lang ar            use a locale for this run only (default: the saved choice)
pulse.exe --view cpu           start in a detail view (cpu mem nrg thm gpu ssd net settings)
pulse.exe --simulate-hog       add the simulated hog row (same as the Settings switch)
pulse.exe --seed-history       fill the 10-minute charts with a random walk (screenshots only)
pulse.exe --script steps.txt   run test steps: wait, capture, lang, view, chart, hover-row, ...
pulse.exe --selftest vectors.tsv out.txt   run the i18n tests against the embedded catalog
```

The flyout opens at the bottom end of the work area (bottom right, bottom left
in RTL), closes when it loses focus, and scrolls with the wheel if the screen
is too short for it (about 820 px with the hog banner). Tab and Shift+Tab move
focus, Enter or Space act, Esc goes back or closes. The footer button runs
`taskmgr.exe`. Quit removes the tray icon and exits.

## Real telemetry and what is simulated

| Card | Source | Real? |
|---|---|---|
| CPU total, User, Sys, Core 0, Core 1 | `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` deltas | real |
| CPU model, threads | registry `ProcessorNameString`, `GetPerformanceInfo` | real |
| Memory %, used, free, App, Kernel, Cache, page file, commit charge | `GlobalMemoryStatusEx`, `GetPerformanceInfo` | real |
| Storage used and free, file system | `GetDiskFreeSpaceExW`, `GetVolumeInformationW` | real |
| Disk read and write | `IOCTL_DISK_PERFORMANCE` on `\\.\C:` (then `PhysicalDrive0`); if both fail, the sum of `GetProcessIoCounters` | real |
| Network down and up, link speed, session totals | `GetIfTable` (busiest connected non-loopback interface) | real |
| Top apps: CPU, memory, process count, threads, PID | `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS \| TH32CS_SNAPTHREAD)` for the list and threads; CPU times and working sets from `NtQuerySystemInformation(SystemProcessInformation)` (falls back to `GetProcessTimes` and `GetProcessMemoryInfo`); grouped by exe name | real |
| Tooltip User / Kernel split | per-process user and kernel times (same source) | real |
| Hog alert | a group above 50% of total CPU for over 2 minutes | real |
| End App | `TerminateProcess` on every process in the group, after the inline confirm; a toast says when it fails | real |
| Restore ended apps | `CreateProcessW` on the saved image path (no arguments) | real, lossy |
| GPU name, video memory | `EnumDisplayDevicesW`, registry `HardwareInformation.MemorySize` | real |
| Battery %, charging, time left | `GetSystemPowerStatus` | real when a battery exists |
| Battery on a desktop | shows "No battery" and "On AC power"; the Charging demo switch shows a demo battery instead | real state, demo values |
| Memory pressure (Normal, High, Critical) | the higher of RAM in use and commit charge: 85% and up is High, 95% and up is Critical | real |
| Power draw (W) | estimate from CPU load | simulated |
| CPU and GPU temperature, thermal stage, fans | model driven by CPU load | simulated |
| GPU load, per-app GPU | XP has no counters; GPU load is a random walk, per-app GPU is 0 | simulated |
| Latency, core clock, GPU power | not measured, shown as a dash | none |

"Simulate CPU hog" in Settings adds one row named `demo-load.exe` at about 54%
and adds its share to the CPU totals. Ending it only removes the row.

Polling runs every 1.5 s. The 1-minute sparklines hold 60 samples; the
10-minute charts hold 121 points at 5 s, each the mean of the samples in its
slot, and fill up from the right while Pulse runs. Each app with CPU activity
gets its own 10-minute history. Pulse makes no network connections.

## Drawing

One `WS_POPUP` window, drawn into a 32-bit DIB section and blitted in
`WM_PAINT` (double buffered). GDI+ draws shapes with `SmoothingModeAntiAlias`;
sparklines and charts are Catmull-Rom curves turned into Bezier segments and
drawn with `GraphicsPath::AddBeziers`, with a gradient fill under the line.
Text goes through GDI `DrawTextW`, so Uniscribe shapes Arabic. When a visual
style is active, the close button and push buttons come from uxtheme
(`WP_CLOSEBUTTON`, `BP_PUSHBUTTON`); otherwise they are drawn in Luna colors.
Tokens follow Part 1.1 for `xp`: `#ECE9D8` body, white cards with `#ACA899`
borders and 3 px corners, `#FFFFE1` tooltips with a black border, the Luna
caption gradient `#0997FF -> #0053EE -> #003DD7`, Tahoma for UI and numbers,
Lucida Console for the PID tile.

Tier 1 on XP is "wave + CPU %": two 16x16 32-bit alpha tray icons, rebuilt
every poll and sent with `Shell_NotifyIconW` (`NOTIFYICONDATAW_V2_SIZE`). The
first draws the last six CPU samples as bars, the second draws the CPU percent
in a pixel font (both turn orange at 50% or more). The tooltip reads
`Pulse · CPU 66%`. Left click toggles the flyout, right click opens a menu
(Open Pulse, Quit). The icons come back after an Explorer restart
(`TaskbarCreated`).

## Add a language

Drop a new `locales/<code>.json` into the repo and rebuild. `build.sh` turns
every file in `locales/` into the catalog, so no code changes. See
[TRANSLATING.md](../../TRANSLATING.md) for the file format.

## Languages

`tools/gen_catalog.py` turns each locale file into a STRINGTABLE block. Ids are
`1024 + slot * 2048 + offset`: metadata (code, label, name, dir, fonts,
numbering system, fallback), `pluralRules`, plural forms (`=N` exact forms in
one extra entry), `strings`, `hardware`, `apps`. At start the program probes
slot ids with `LoadStringW` until one is missing, so a new locale file needs
no code change: add `locales/de.json`, rebuild.

`src/i18n.c` ports the engine: CLDR operands (n i v w f t c e), a compiler for
CLDR rule text (and, or, `=`, `!=`, `is`, `in`, `within`, `%`, ranges),
`=N` forms winning over categories, `{name}` fill, FSI/PDI around every
inserted value in RTL locales, list joins, numbering systems (latn, arab,
arabext and a few more), fallback to en and then to the key, and `ar-EG`
resolving to `ar`.

- Two locales: an `EN | ع` segmented pill in the header. Three or more: a
  button that opens a native popup menu with every locale name.
- Settings has a Language row: a drop-down that opens a native popup menu
  with "Match system" and every locale by its own `name`. A pick switches
  text and direction at once, like the header switch. Both save the choice
  in `HKCU\Software\Pulse`, value `Language` (a locale code, or empty for
  "Match system"). "Match system" follows `GetUserDefaultUILanguage`, and
  falls back to English when no locale file matches.
- Arabic uses `fonts.platforms.xp` from `ar.json` (Tahoma). The first
  installed family in a stack wins.
- RTL: the window gets `WS_EX_LAYOUTRTL`, so mouse input arrives in logical
  coordinates. Shapes are mirrored with a GDI+ world transform
  (`x_rtl = width - x - w`), so bars fill from the right and chevrons, the
  back arrow and the external link icon point the other way. The back
  buffer is blitted with `SetLayout(hdc, 0)` so it is not flipped twice.
- Numbers stay LTR: text is split into runs at the FSI/PDI marks and the runs
  are placed in visual order by the program, each drawn on its own with
  `DT_RTLREADING` only when the run is RTL. XP's Uniscribe predates Unicode
  6.3 isolates, so the marks are never passed to GDI. Long text ends in an
  ellipsis at its logical end.

## Tests

```
BUILD=/tmp/x sh native/winxp/build.sh test
```

`tests/test_i18n.c` runs every CLDR category in `vectors.json` (en and ar,
0 to 230 and the decimal cases), the rendered samples with their U+2068 and
U+2069 marks, `ar-EG`, fallback to en and to the key, `=0`, joins, app names
and harder rules (Bosnian, Romanian, `is not`, `within`, `c`). The same tests
run inside the exe against the embedded catalog:

```
wine pulse.exe --selftest 'Z:\path\vectors.tsv' 'Z:\path\out.txt'
```

Screenshots in `dist/` come from Wine 9 (prefix set to Windows XP) on Xvfb,
using `--script` and its `capture` step, which writes the back buffer to PNG.

## Known gaps

- Thermal, power, GPU load and the desktop battery are simulated (see the table).
- Wine's bundled Tahoma Bold draws at regular weight and lacks arrow glyphs,
  so bold text in the screenshots looks lighter than on XP.
- Working set measured under Wine 9 is 26.7 to 28.9 MB (peak 31.5 MB) because it counts Wine's own
  libraries; it says nothing about XP. The program trims its working set with
  `SetProcessWorkingSetSize(-1, -1)` whenever the flyout closes.
- No MSAA (`IAccessible`) provider yet: keyboard focus works, screen readers
  see one window.
- No open or close animation and no per-monitor placement.
- Restore relaunches the exe without its original command line.
