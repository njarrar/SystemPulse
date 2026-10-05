# Pulse for macOS

A menu bar system monitor for macOS 13 and later, built as a Swift package.
It shows the three tiers from the spec: readouts in the menu bar (Tier 1),
a 420 pt flyout with cards and Top Active Apps (Tier 2), and a 10-minute
detail view for each metric and app (Tier 3).

## Layout

```
Package.swift
Sources/
  PulseCore/        platform-neutral library (builds on Linux and macOS)
    PluralRules.swift   CLDR plural operands, rule compiler, selector
    PulseLocale.swift   locale engine: t(), hw(), plural(), join(), fallback, ar-EG -> ar
    NumberText.swift    fixed decimals (half away from zero), numbering systems
    XCStrings.swift     locale files <-> Localizable.xcstrings
    Validation.swift    locale checks (port of validate())
    Formatting.swift    %, °C/°F, W, GB, MB/s, durations, "4m ago"
    History.swift       ring buffers, 1 min sparkline + 10 min history, hog rule, chart math
    Telemetry.swift     snapshot models, process grouping, CPU tick math
  PulseCatalog/     `pulse-catalog`: writes Localizable.xcstrings from ../../locales/*.json
  CPulseSensors/    C shim: SMC keys, IOHID temperature sensors, responsible pid
  PulseApp/         the app (AppKit + SwiftUI + IOKit, behind #if canImport(AppKit))
    Shell/            NSStatusItem, non-activating NSPanel + NSVisualEffectView, app delegate
    Store/            PulseStore: sampling timer, histories, settings, actions
    Telemetry/        Mach, sysctl, IOKit, libproc, getifaddrs, CoreWLAN readers
    UI/               SwiftUI flyout, cards, detail charts, settings, tokens
    Resources/Localizable.xcstrings   generated, do not edit by hand
Tests/PulseCoreTests/  plural vectors, catalog round trip, grouping, history
scripts/build-app.sh   universal Pulse.app (arm64 + x86_64), Info.plist with LSUIElement
scripts/make-cldr-fixture.js   rebuilds the Intl plural fixture used by the tests
```

## Build and run

Needs Xcode 15.3 or later (Swift 5.10) on macOS 13 or later.

```
cd native/macos
swift run pulse-catalog          # locales/*.json -> Sources/PulseApp/Resources/Localizable.xcstrings
swift test                       # PulseCore tests
bash scripts/build-app.sh        # dist/Pulse.app (universal) and dist/Pulse-macos-universal.zip
open dist/Pulse.app
```

`swift run Pulse` also works for quick checks; it reads the catalog from the
SwiftPM resource bundle. Useful flags:

```
dist/Pulse.app/Contents/MacOS/Pulse --probe          # print two rounds of real telemetry and exit
dist/Pulse.app/Contents/MacOS/Pulse --render DIR     # write flyout PNGs (en/ar, light/dark, detail, settings)
dist/Pulse.app/Contents/MacOS/Pulse --open           # open the flyout at launch
PULSE_PSEUDO=1 bash scripts/build-app.sh             # bundle the 40% longer en-XA locale too (3+ locales -> menu)
```

CI: `.github/workflows/macos.yml` runs on `macos-14`. It checks the catalog
is current, runs `swift test`, builds the universal app, runs `--probe` on the
runner, renders screenshots, and uploads the zip and PNGs.

## Languages

`locales/*.json` is the only place copy lives. `pulse-catalog` reads every
file in that folder at build time (`scripts/build-app.sh` runs it first) and
writes an Xcode String Catalog:

- keys carry their section: `strings.cpu`, `hardware.coreP`, `plurals.thread`, `apps.<name>`
- plurals become `variations.plural` with the CLDR categories the file uses;
  an exact `"=0"` form becomes the catalog's `zero` case
- `{name}` placeholders become positional specifiers (`%1$@`, `%1$lld` for a
  plural's `{n}`), and each entry's comment lists the argument names
- per-locale engine data (direction, label, plural rules, numbering system,
  fonts) sits in `_locale.*` entries marked `shouldTranslate: false`

The app loads that catalog at launch and hands it to the PulseCore engine, a
Swift port of `i18n/pulse-i18n.js`: plural rules are compiled from each
file's `pluralRules`, so any language with rules works without code. The
generator reads the catalog back after writing it and fails if anything was
lost. Adding `locales/de.json` and rebuilding is enough to add German.

Switching language is live. With two locales the header shows the `EN | ع`
pill; with three or more it shows a menu. The flyout sets
`.environment(\.layoutDirection)` and `.environment(\.locale)` from the
locale's `dir` and `code`, so the layout mirrors, the flyout anchors to the
left edge of the status item in RTL, and `chevron.forward` / `chevron.backward`
flip. Numbers, units and signs sit in left-to-right runs with tabular digits;
inserted values carry U+2068/U+2069 isolates in RTL. Charts and sparklines
always run left to right. Long labels truncate with an ellipsis and show the
full text on hover.

## Telemetry: what is real

All values come from the Mac Pulse runs on. Nothing is simulated, and Pulse
makes no network requests.

| Area | Source |
|---|---|
| CPU total, user, system, per core | `host_processor_info(PROCESSOR_CPU_LOAD_INFO)` tick deltas |
| P / E cores | `sysctl hw.nperflevels`, `hw.perflevel0/1.logicalcpu`; cluster load averages the cores of each level (efficiency cores are numbered first on Apple silicon). Intel Macs show User / Kernel instead. |
| Load average | `getloadavg` |
| Memory split (App, Wired, Compressed, Free) | `host_statistics64(HOST_VM_INFO64)`, the same split Activity Monitor uses; total from `hw.memsize` |
| Swap, pressure | `sysctl vm.swapusage`, `kern.memorystatus_vm_pressure_level` |
| Battery %, charging, time left / to full | `IOPSCopyPowerSourcesInfo` |
| Health, cycles, capacity, draw, adapter | `AppleSmartBattery` registry entry (raw max / design capacity, voltage x amperage) |
| System power | SMC key `PSTR` when the Mac reports it |
| CPU / GPU / SSD / battery temperature | IOHID temperature sensors on Apple silicon (`pACC`/`eACC MTR`, `GPU MTR`, `NAND`, gas gauge); SMC `Tp*`/`Te*`/`TC*`/`Tg*`/`TH*`/`TB*` keys otherwise |
| Fans | SMC `FNum`, `F<n>Ac` (fanless Macs show "Fanless") |
| Thermal stage | CPU temperature bands (48 / 70 / 90 °C) and `ProcessInfo.thermalState` (serious or critical = Throttled) |
| GPU utilization, memory in use, model, cores | `IOAccelerator` `PerformanceStatistics`, `model`, `gpu-core-count` |
| GPU time per app | `AppUsage.accumulatedGPUTime` on the accelerator's user clients |
| Disk size, free, format | `statfs("/System/Volumes/Data")` |
| Disk read / write | `IOBlockStorageDriver` `Statistics` byte counter deltas |
| Network rates, today | `getifaddrs` link counters on `en*` interfaces (32-bit wrap handled); "Today" counts since midnight while Pulse runs |
| Wi-Fi standard, band, RSSI, link speed | CoreWLAN `CWWiFiClient` |
| Top Active Apps | `proc_listallpids` + `proc_pidinfo` (task and BSD info), grouped under the app that owns them: `responsibility_get_pid_responsible_for_pid` when present, else the parent chain, matched to `NSRunningApplication`. Processes with no app group by name. |
| Hog alert | one app above 50% of total CPU for 2 minutes without a break |
| End App | confirm sheet, then `NSRunningApplication.terminate()` (apps only, not background processes) |
| Open Activity Monitor | `NSWorkspace.openApplication` with `com.apple.ActivityMonitor` |

Values shown as a dash:

- GPU core clock and GPU power: no public API.
- Latency: measuring it would need network traffic, and Pulse is local-only.
- Storage health: SMART status needs a private API.
- Processes owned by other users (root daemons such as WindowServer):
  `proc_pidinfo` refuses them without root, so they are left out of Top Active Apps.

The IOHID event system calls and the responsible-pid call are not in the
public SDK headers. Both are looked up at runtime or declared in the C shim,
and Pulse falls back to SMC keys and the parent chain when they are missing.
The app is not sandboxed (the SMC and libproc need that), so it is meant for
direct distribution, not the Mac App Store.

## Add a language

Drop `xx.json` into the repo's `locales/` folder (see
[TRANSLATING.md](../../TRANSLATING.md)), then run `swift run pulse-catalog`
or `bash scripts/build-app.sh`. Both read every file in that folder, so no
code change is needed. The new language shows up in the header switcher and
in Settings > Language. CI fails if the checked-in catalog is out of date.

## Settings

Each switch is a native macOS switch, so VoiceOver reads it as a switch.

- **Simulate CPU hog** lays a demo on top of real data. The busiest app
  rises to about 52% of total CPU, total CPU rises with it, power draw
  goes up 5.4 W and CPU heat goes up 19 °C. The hog banner shows at once.
- **Charging** shows the charging pill, a 48 W charge rate and "Full in"
  time, as if a USB-C 96 W adapter were plugged in.
- **Live updates** pauses or resumes the 1.5 s sampling.
- **Temperature unit** picks °C or °F.
- **Language** is a native popup with "Match system" and each language
  by its own name. A pick switches text and direction at once, like the
  header switcher, and is kept in `UserDefaults`. "Match system" follows
  the macOS language order and falls back to English.
- **Restore ended apps** relaunches every app Pulse ended this session,
  from the bundle URL saved when it was ended. The row shows the count.

The two demo switches are never saved. Turning one on or off, ending an
app or restoring apps updates CPU, power, heat and the sparklines at once,
even while live updates are paused. An ended app leaves the list right
away. Unit, live updates, theme and language are kept in `UserDefaults`.
