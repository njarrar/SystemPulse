# Pulse for Windows 98 SE

A tray monitor for Windows 98 SE in plain C89, Win32 ANSI APIs and GDI. One
32-bit exe (about 95 KB) with no C runtime: it imports only kernel32, user32,
gdi32, advapi32 and shell32, and loads everything newer by name. The same exe
runs on 98 SE, ME, NT 4, 2000, XP and Wine, and says in its status bar which
data sources it found.

```
native/win98/
  src/pulse.c          tray icons, flyout window, layout and drawing
  src/telemetry.c      data sources (9x first, NT fallbacks)
  src/text.c           UTF-8 to ANSI code page or UTF-16 text output
  src/i18n.c, plural.c locale engine and CLDR plural compiler (C89 port of i18n/pulse-i18n.js)
  src/nocrt.c          entry point and memset/memcpy for the CRT-free build
  src/pulse.rc         icon and version block
  tools/gen_catalog.py locales/*.json -> embedded C catalog (build step)
  tools/gen_vectors.py vectors.json -> C test table
  tools/gen_icon.py    16-colour program icon
  tools/screenshot.sh  Xvfb + Wine screenshot
  locales-native/      strings only this app uses (see "Copy" below)
  tests/test_i18n.c    plural and engine tests
  tests/burn.c         CPU load helper for the hog alert check
  tests/memprobe.c     memory figures for a running pulse98.exe
  Makefile             mingw-w64 cross build
  build_vc6.bat        Visual C++ 6.0 build
  dist/                pulse98.exe and screenshots
```

## Build

mingw-w64 (i686), Python 3:

```
make BUILD=/tmp/pulse98            # -> /tmp/pulse98/pulse98.exe
make BUILD=/tmp/pulse98 test       # host gcc tests, then the Win32 test exe under Wine
make BUILD=/tmp/pulse98 dist       # copies the exe to dist/
```

The build uses `-std=c89 -pedantic -Wdeclaration-after-statement`,
`-march=pentium2` with no SSE, `-nostdlib`, PE subsystem 4.0 and no ASLR or
NX flags, so Windows 98 loads it.

Visual C++ 6.0: run `VCVARS32.BAT`, then `build_vc6.bat`. This build links
the normal CRT (msvcrt.dll ships with 98 SE) and leaves `nocrt.c` out. The
code avoids `SIZE_T`, `ULONG_PTR` and other types that VC6 headers lack. It
has not been compiled with VC6 here.

## Run

```
pulse98.exe                      tray icons; click to open the flyout
pulse98.exe --show-flyout        open the flyout without tray icons (testing)
```

Other flags: `--tray` (tray icons even with `--show-flyout`),
`--lang=ar` (else the saved choice, else the Windows UI language),
`--view=cpu|mem|nrg|thm|gpu|ssd|net|settings`, `--sort=mem`,
`--sim-hog`, `--sim-charging` (turn on the Settings demo switches),
`--hog-pct=N` (hog alert threshold, default 50), `--ansi` / `--unicode`
(force the text path).

Language and temperature unit are saved under `HKCU\Software\Pulse98`
(`Lang` holds a locale code or `system`). "Match system" maps the Windows UI
language to a locale and falls back to English.

## The window

- Tier 1, tray: two 16x16 icons. One is a green-on-black bar graph of the
  last five CPU samples, the other shows CPU percent in digits. The tooltip
  reads "CPU 64%". Left click opens the flyout above the taskbar on the side
  where the notification area is; right click gives Open Pulse and Quit. The
  icons come back when Explorer restarts (`TaskbarCreated`).
- Tier 2, flyout: 420 px wide, 18 px navy title bar with the classic close
  button (`DrawFrameControl`), #C0C0C0 face, bevels from `DrawEdge`.
  Cards: CPU (User, Kernel, GPU bars and a 1 minute sparkline), Memory
  (App, Kernel, Disk cache, Free), Energy, Thermal, GPU strip, Drive, Network,
  Top Active Apps with a CPU / Memory / GPU sort control, footer with Open
  System Monitor, Local-only note and Quit, and a status bar naming the data
  sources.
- Tier 3, details: click any card or app row. A 10 minute chart (one point
  every 5 s) drawn with `PolyBezier` from the same Catmull-Rom control points
  as the prototype, area fill through `BeginPath`/`FillPath`, Peak and Avg,
  and six tiles. App details show "PID FFF4A2B1" style 8-digit hex ids and an
  End App button that asks first.
- Settings (slider button): Simulate CPU hog and Charging (demo overlays drawn
  over real readings), Live updates (pauses polling), °C / °F, Language (a
  classic radio list: Match system, then each locale by its own name, such as
  English and العربية), Restore ended
  apps (relaunches what Pulse ended, from the image path it saw), and a list of
  the data source for each card with Live, Simulated or Not available.
- Memory pressure reads Normal under 70% load, High under 90%, else Critical.
- Hog alert: one app above 50% of total CPU for over 2 minutes. End App asks
  for confirmation, then calls `TerminateProcess`; if that fails, a toast says
  "Couldn't end {app}".
- Painting is double-buffered in 96 px strips through one small memory bitmap,
  so the back buffer stays near 160 KB even on a 32-bit display.
- The flyout fits its content; on short screens (800x600, 1024x768) it gets a
  scroll bar, mouse wheel and Page Up / Page Down. Esc goes back or closes.

## Data: real and simulated

| Card | Windows 98 SE | NT host fallback | Under Wine (as tested) |
|---|---|---|---|
| CPU total | `HKEY_DYN_DATA\PerfStats\StatData` `KERNEL\CPUUsage` (started with `StartStat`, stopped with `StopStat` on exit) | `NtQuerySystemInformation` class 8 | real, NT path |
| CPU User / Kernel | 9x reports a total only; split 66 / 34 (simulated) | real, from class 8 times | real |
| Memory load, used, total, swap | `GlobalMemoryStatus` | same; swap tile shows commit charge | real (capped at 2 GB by the API) |
| Kernel, Disk cache | PerfStats `VMM\cpgLocked`, `VMM\cpgDiskcache` (pages or bytes, detected) | psapi `GetPerformanceInfo` | Wine returns zero, shown as "-" |
| System resources % free | `RSRC32.DLL` `_MyGetFreeSystemResources32@4` | "Not used on NT" | not available |
| Processes | Toolhelp32 (names, threads, PIDs; 9x has no per-process CPU, rows show thread counts and say so) | `NtQuerySystemInformation` class 5 (CPU %, working set, threads) | real |
| App names | `FileDescription` from the version block | same, path from `GetModuleFileNameExA` | real |
| Drive | `GetDiskFreeSpaceExA` (else `GetDiskFreeSpaceA`), `GetVolumeInformationA` | same | real |
| Disk read / write | PerfStats `VFAT\BReadsSec`, `VFAT\BWritesSec` | class 2 I/O transfer counts | Wine reports little or nothing |
| Network | PerfStats `Dial-Up Adapter\TotalBytesRecvd` / `TotalBytesXmit` / `ConnectSpeed` | iphlpapi `GetIfTable` | real |
| Energy | `GetSystemPowerStatus` | same | real (no battery) |
| GPU name | `EnumDisplayDevicesA` | same | real ("Wine Adapter") |
| GPU load and temperature | simulated | simulated | simulated |
| Thermal | simulated (no 9x API); follows CPU load | simulated | simulated |

The status bar says which path is in use, for example
"NT host: no HKEY_DYN_DATA · RSRC32. Using NtQuerySystemInformation." on Wine
and NT, or "Data: PerfStats · RSRC32 · Toolhelp32" on 98.

## Languages

- `tools/gen_catalog.py` turns every `locales/*.json` into one record in an
  embedded C catalog (UTF-8 strings, sorted keys, plural forms, CLDR rule
  sources). It also picks, per locale, the first Windows ANSI code page that
  holds all of its text (1252 for en, 1256 for ar) and the matching GDI
  charset. Adding a locale file needs no code change. An `.rc` STRINGTABLE is
  not used because 9x converts resource strings with the system code page,
  which breaks Arabic on a Western system.
- The engine port matches `pulse-i18n.js`: CLDR rules from `pluralRules`
  (exact `=0` forms win), `{placeholder}` fill, U+2068/U+2069 isolation of
  inserted values in RTL, locale then English then key, `ar-EG` -> `ar`.
- Text output on 9x converts UTF-8 to the locale's code page and draws with
  `ExtTextOutA` and a Tahoma font of that charset (`ARABIC_CHARSET` for ar),
  which is how Arabic Windows 98 shapes text. On NT it uses `ExtTextOutW`.
  These systems predate FSI/PDI, so the renderer turns each isolate into an
  LRM or RLM mark chosen from the isolated text. Text with no RTL letters
  (numbers with units, Latin names) is drawn left to right.
- Arabic shows only where the system can draw it: code page 1256 must be
  valid and a font with the Arabic charset must exist. Otherwise the ع
  segment is greyed and says why.
- Switcher: two locales give the `EN | ع` pill, three or more give a menu.
  Switching rebuilds fonts and mirrors the layout at once: cards, bars,
  chevrons, sparklines and the chart time axis flip; the title bar and close
  button swap sides.

### Copy

All visible text comes from the catalog. A few strings exist only in this
app (data source notes, "Not used on NT", the per-app CPU note). They live in
`locales-native/en.json` and `ar.json` and are merged at build time; a key in
`locales/` always wins. They should move into `locales/*.json` (keys
`srcTitle`, `srcLive`, `srcSim`, `srcNone`, `srcLine9x`, `srcLineNt`,
`notOnNt`, `perAppNa`, `arabicNa`).

## Add a language

Drop a new `<code>.json` into the repo's `locales/` folder and rebuild. The
Makefile picks up every file there, so the new language shows in the header
switcher and in Settings > Language with no code change. See `TRANSLATING.md`
at the repo root for the file format.

## Tests

```
make BUILD=/tmp/pulse98 test
```

checks every CLDR category in `vectors.json` for en and ar (0 to 230 and the
decimals), every rendered sample (including the U+2068/U+2069 isolates), the
engine rules above, and spot checks for Russian, Welsh, French (with the
`c`/`e` exponent), Latvian, `within` and `is not`. 565 checks, run both as a
Linux binary and as a Win32 exe under Wine.

Screenshots:

```
export WINEPREFIX=/tmp/pulse98-wine     # winecfg -v win98
sh tools/screenshot.sh /tmp/pulse98/pulse98.exe out.png 75 --lang=ar --sim-hog
PULSE_PRE=/tmp/pulse98/cpuburn.exe sh tools/screenshot.sh /tmp/pulse98/pulse98.exe hog.png 140 --hog-pct=15
```

The script runs the app in a Wine virtual desktop (taskbar and tray) on
Xvfb and crops the flyout. The test prefix used the Windows Standard colour
scheme. The Tahoma Bold that this distribution's Wine package ships is not
bold, so the prefix had a bold stand-in font registered as "Tahoma Bold";
real Windows 98 has its own Tahoma.

## Gaps

- Not run on real Windows 98 hardware or a 98 VM here. The PerfStats,
  RSRC32 and Toolhelp32 paths are written to the documented 9x behaviour but
  were only exercised for their failure path under Wine.
- Thermal, GPU load and GPU temperature are simulated; 9x has no API for them.
- 9x gives no per-process CPU time, so on 98 the app list ranks by thread
  count and the hog alert works only with the Simulate CPU hog switch.
- No keyboard focus ring or Tab order inside the flyout yet (Esc, Backspace,
  Page Up and Page Down work).
- The VC6 build file is untested.
