# Pulse for Windows 11

A notification-area app that shows live system vitals in a 420 px flyout.
C# on .NET 8, WinUI 3 (Windows App SDK 1.8), x64 and ARM64, Native AOT.

- **Tier 1:** the tray icon. It draws CPU and RAM meters (orange under a hog
  alert), and its tooltip reads `CPU 68% · RAM 58% · ⚡ 15.2W · ↓ 1.6 MB/s`.
- **Tier 2:** click the icon for the overview: status pill, hog banner, CPU,
  Memory, Energy and Thermal cards with one-minute sparklines, the GPU strip,
  Storage and Network, Top Active Apps (sort by CPU, Memory or GPU; hover a
  row for details and End), and a footer that opens Task Manager.
- **Tier 3:** click any card or app for its detail view: a 10-minute chart you
  can scrub with the pointer, peak and average, and six tiles.

The window has no title bar, stays out of Alt+Tab, uses Desktop Acrylic
(Mica, then a solid fill, where that is missing), asks DWM for round corners
(`DWMWCP_ROUND`), sits above the tray icon, and hides when it loses focus.

## Layout

```
Pulse.Win11.sln
src/Pulse.Core/          net8.0, no Windows code: i18n engine, .resw codec,
                         formatting, models, history, flyout text, demo overlay
src/Pulse.App/           net8.0-windows10.0.19041.0, the WinUI 3 app
  Interop/               LibraryImport P/Invoke (no runtime marshalling)
  Telemetry/             one reader per sensor, the 1.5 s sampler, history
  Tray/                  Shell_NotifyIcon icon and the icon renderer
  Localization/          loads the generated catalog through MRT Core
  UI/                    theme tokens, the flyout views and window
src/Pulse.Launcher/      the small Pulse.exe at the top of the zip; starts data\Pulse.exe
tests/Pulse.Core.Tests/  xUnit tests for Pulse.Core
tools/Pulse.CatalogGen/  locales/*.json to Strings/<lang>/Resources.resw
tools/package.ps1        lays out the zip: Pulse.exe, data\, lang\
```

The UI is built in C#, with no `.xaml` files. That keeps the XAML compiler out
of the build, so the whole app also compiles on Linux (see below). `App`
supplies WinUI's control metadata itself through `IXamlMetadataProvider`.

## Build and run (Windows)

You need the .NET 8 SDK. Visual Studio is not needed; for ARM64 Native AOT on
an x64 machine, install the MSVC ARM64 build tools.

```
cd native/win11
dotnet test tests/Pulse.Core.Tests
dotnet build src/Pulse.App/Pulse.App.csproj -p:Platform=x64 -r win-x64
dotnet publish src/Pulse.App/Pulse.App.csproj -c Release -p:Platform=x64 -r win-x64 -o publish/x64
dotnet publish src/Pulse.App/Pulse.App.csproj -c Release -p:Platform=ARM64 -r win-arm64 -o publish/arm64
publish\x64\Pulse.exe
```

The app is unpackaged and self-contained (`WindowsAppSDKSelfContained`), so
the publish folder runs as is. Only one copy runs per sign-in. Settings
(language, theme, temperature unit, live updates) live in
`%LOCALAPPDATA%\Pulse\settings.json`.

To make the same layout as the zip:

```
dotnet publish src/Pulse.Launcher/Pulse.Launcher.csproj -c Release -r win-x64 -o publish/launcher-x64
./tools/package.ps1 -App publish/x64 -Launcher publish/launcher-x64 -Out package/x64
```

The zip holds `Pulse.exe` (the launcher), `data\` (the real app with the
Windows App SDK files, which must sit beside it) and `lang\<code>\Resources.resw`
(one folder per language). The app reads `lang\` first. The many culture
folders in `data\` (such as `fr-FR`) hold WinUI's own control text; Windows
only finds them beside WinUI's DLLs, so they stay in `data\`.

If start-up fails, Pulse writes `%LOCALAPPDATA%\Pulse\crash.log` and shows a
message box. Set `PULSE_NO_DIALOG=1` to skip the box (CI does this).

`Pulse.exe --quit` closes a running copy (it signals the `Local\Pulse.Win11.Quit`
event), the same as Quit in the tray menu. Errors after start-up go to
`crash.log` without closing Pulse.

Set `PULSE_ARTIFACTS` to a folder to keep `bin/` and `obj/` out of the tree.

CI: `.github/workflows/win11.yml` runs the tests, builds and publishes x64
and ARM64 on `windows-latest`, lays out the zip, then unzips it into an empty
folder and starts it on two machines: x64 on Windows Server 2025 and ARM64 on
Windows 11 (`windows-11-arm`). The job fails if Pulse does not stay up.

## Add a language

Drop a new `<code>.json` into the repo's `locales/` folder, one file per
language. The next build turns it into `Strings/<code>/Resources.resw` and the
language shows up in Settings and the header switcher. No code change is
needed. See `TRANSLATING.md` at the repo root for the steps and the rules.

## Languages

`locales/*.json` is the only place copy lives. Before every build,
`Pulse.CatalogGen` (run by the `PulseGenerateCatalog` target) checks each file
against `en.json` (errors fail the build) and writes
`src/Pulse.App/Strings/<lang>/Resources.resw`. MRT Core packs those into
`resources.pri`. Adding a locale file needs no code change.

`.resw` is flat and has no plurals, so the generator writes:

| Key | Holds |
|---|---|
| `meta_code`, `meta_label`, `meta_name`, `meta_dir`, `meta_numberingSystem`, `meta_fallback`, `meta_order`, `meta_fontUi`, `meta_fontHero` | file metadata, fonts for Windows |
| `meta_locales` | every generated locale, for the switcher |
| `meta_rules`, `rule_<category>` | the CLDR `pluralRules` text |
| `forms_<key>`, `p_<key>_<form>` | plural forms; `=0` is written `eq0` |
| `s_`, `h_`, `a_` + key | strings, hardware, app names |

MRT fills any key a language lacks from English. The `forms_` and
`meta_rules` lists stop that from leaking English plural forms (such as
"No apps ended yet") into another language; a test covers it.

At run time `Catalog` reads each language through its own MRT
`ResourceContext` and decodes it back into the engine, a C# port of
`i18n/pulse-i18n.js`: CLDR plural rules compiled from the file (exact `=0`
forms win), `{placeholder}` fill, FSI/PDI isolates around inserted values in
RTL, English fallback then the key, `ar-EG` resolving to `ar`, and digits in
the file's numbering system. The zip's `lang\` folder comes first; a dev
build without it uses `resources.pri`, then the `.resw` copies next to the exe.

Settings has a **Language** row: a drop-down with "Match system" and every
locale the catalog holds, each shown by its own `name` in its own script.
"Match system" follows the Windows display language and falls back to
English. The choice is saved in `settings.json` and kept across launches.
The header keeps its quick switcher: two locales show the `EN | ع` pill;
three or more show a menu with radio items (arrow keys, Home, End and Escape
work). Switching rebuilds the
flyout with `FlowDirection.RightToLeft`, which mirrors layout, meters,
gradients and chevrons. Numbers, signs and units sit in left-to-right runs.
Segoe UI figures are tabular. Charts keep time running left to right. Long
text ends in an ellipsis and shows in full in a tooltip.

## Real telemetry and what is simulated

Pulse polls every 1.5 s on a background thread. Nothing leaves the machine.

| Reading | Source |
|---|---|
| CPU total, user, system, per core | `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` |
| P-core and E-core load | `GetSystemCpuSetInformation` `EfficiencyClass` (highest class = P). CPUs with one class show user and kernel instead. |
| Load average | one-minute damped average of busy CPUs plus `\System\Processor Queue Length` (Windows has no load average of its own) |
| Apps: CPU, memory, threads, PIDs | `NtQuerySystemInformation(SystemProcessInformation)`, grouped by image name; memory is the private working set; names from the image's file description |
| App GPU share | `\GPU Engine(*)\Utilization Percentage`, busiest engine per process |
| RAM, commit | `GlobalMemoryStatusEx`, `GetPerformanceInfo` |
| Compressed memory | working set of the Memory Compression process |
| Battery level, rate, time | `CallNtPowerInformation(SystemBatteryState)` |
| Design and full capacity, cycles, battery temperature | `IOCTL_BATTERY_QUERY_TAG`, `IOCTL_BATTERY_QUERY_INFORMATION` |
| CPU temperature, throttling | `\Thermal Zone Information(*)` counters (the WMI thermal zone provider, read through PDH), `% Passive Limit`, `\Processor Information(_Total)\% Performance Limit` |
| GPU name, video memory | DXGI `IDXGIAdapter3::QueryVideoMemoryInfo` (called through the vtable) |
| GPU utilization | `D3DKMTQueryStatistics` node running time; GPU Engine counters if that fails |
| GPU temperature, fan, power, clock | `D3DKMTQueryAdapterInfo` adapter and node perf data (WDDM 2.4+ drivers that report them) |
| Disk size, file system | `GetDiskFreeSpaceExW`, `GetVolumeInformationW` |
| Disk read and write | `IOCTL_DISK_PERFORMANCE` on the system volume |
| Network rates, today's totals | `GetIfTable2` on the interface `GetBestInterface` picks (a route lookup; nothing is sent) |
| Wi-Fi RSSI, standard, band | `WlanQueryInterface` (RSSI, current connection), `WlanGetNetworkBssList` (frequency) |
| End app | `OpenProcess` + `TerminateProcess` on every PID of the app |
| Open Task Manager | starts `taskmgr.exe` |

Simulated, by design: the Settings switches **Simulate CPU hog** and
**Charging** lay demo values over the real readings (a fake "Antimalware
Service" at 52% CPU, and +48 W charging). The fake hog is not a process;
ending it turns the switch off. **Restore ended apps** relaunches the images
Pulse ended (and turns a demo hog back on).

The real hog alert needs one app above 50% of total CPU for 2 minutes.

Memory pressure reads Normal, High (under 10% of RAM free or commit over 90%
of its limit) or Critical (under 5% free or commit over 95%). Desktops without
a battery show "No battery" and "On AC power" on the Energy card.

Shown as a dash: latency (measuring it would mean sending packets), storage
health and temperature, GPU hotspot, and any sensor the machine does not report.
Many laptops expose only a board or skin sensor as an ACPI thermal zone,
and some report a fixed value.

## What has been compiled, and where

Built on Linux (Ubuntu 24.04, .NET SDK 8.0.131 from the Ubuntu archive):

- `Pulse.Core`, `Pulse.CatalogGen` and `Pulse.Core.Tests`: built, all 63 tests
  pass, including every vector in `i18n/test/fixtures/vectors.json` (read from
  JSON and again after a trip through `.resw`) and a ten-language check against
  `Intl.PluralRules` (`tests/Pulse.Core.Tests/fixtures/intl-plurals.json`, made
  by `make-intl-plurals.js` with Node).
- `Pulse.App` (all C#, x64 and ARM64): compiles with 0 warnings and 0 errors,
  with the AOT, trim and CsWinRT analyzers on, using
  `dotnet build -p:EnableWindowsTargeting=true -p:Platform=x64 -r win-x64 -p:WindowsAppSDKSelfContained=false -p:AppxGeneratePriEnabled=false`.
  Those two switches turn off steps that run Windows-only tools.
- Plain `dotnet build -p:EnableWindowsTargeting=true` fails on Linux: with no
  runtime identifier, "WindowsAppSDKSelfContained requires a supported Windows
  architecture"; with `-r win-x64`, the self-contained manifest step fails
  (its MSIX input is never made on Linux); with that off, `makepri.exe` cannot
  run (it is a Windows binary).

Not done here: running the app, MRT `resources.pri` generation, Native AOT
publishing and any check of the P/Invoke layouts against real Windows. Those
need Windows; the CI workflow covers build and publish.

## Gaps

- Only processor group 0 is read (up to 64 logical processors).
- The `D3DKMTQueryStatistics` buffer offsets and the `KMTQAITYPE_*` values for
  perf data are written from the documented struct layouts; utilization
  falls back to the GPU Engine counters if the node query fails.
- Windows 11 24H2 can hide Wi-Fi BSS details from apps without location
  access; the band and "6E" then show as unknown.
- Protected processes (for example Microsoft Defender) refuse
  `TerminateProcess`; the flyout then shows "Couldn't end {app}".
- Card icons are Segoe Fluent Icons glyphs, not the prototype's SVG icons.
