# Pulse for ChromeOS

Pulse as a ChromeOS system web app (chrome://pulse) with a shelf readout.
The files under `chromium/` mirror their paths in a Chromium checkout.

## What is here

| Path | What it is |
| --- | --- |
| `chromium/ash/webui/pulse_ui/mojom/pulse_ui.mojom` | Page handler interface: snapshots, ten-minute history, end and restore a group, live updates on or off, open Diagnostics. |
| `chromium/ash/webui/pulse_ui/telemetry/` | `PulseSampler`: cros_healthd probes (kCpu, kThermal, kFan, kBattery every 1.5 s; kNetwork, kNonRemovableBlockDevices every 5 s), `/proc/meminfo`, `/sys/block/zram0/mm_stat`, `/proc/*/stat` and `cmdline`, devfreq for the GPU. Parsers and the process grouper (ash, Chrome, Crostini, ARCVM, system) have unit tests. |
| `chromium/ash/webui/pulse_ui/pulse_ui.*`, `pulse_page_handler.*` | `PulseUIConfig`, `PulseUI` (MojoWebUIController) and the handler. |
| `chromium/ash/webui/pulse_ui/resources/` | The page: TypeScript plain web components, no Lit or Polymer. Material 3 tokens in `pulse_tokens.css`. |
| `chromium/ash/webui/pulse_ui/tools/gen_pulse_catalog.py` | Turns `locales/*.json` into `pulse_catalog.json` (for the page), a GRIT `.grd` plus one `.xtb` per Chrome pak locale, and an IDS table. |
| `chromium/ash/system/pulse/`, `chromium/ash/public/cpp/pulse/` | Tier 1: `PulseTray` in the shelf status area (dot, six-bar CPU wave, CPU %, W). |
| `chromium/chrome/browser/ash/system_web_apps/apps/` | `PulseSystemAppDelegate` (420 x 860 at the bottom-end corner) and `ChromePulseUIDelegate` (Diagnostics, stop and restart Crostini). |
| `chromium/chrome/browser/ui/ash/pulse/` | `PulseTrayBridge`: feeds the shelf readout from the sampler. |
| `chromium/patches/` | Hand-written edits to existing Chromium files (SWA type, registration, binders, locale paks, shelf, histograms). |
| `dev/` | A harness that runs the same page against a mock Mojo handler. |
| `dist/` | Screenshots from the harness. |

## Build in a Chromium checkout

1. Run `sh tools/make_icons.sh` to draw the app icons from `app_icon.svg`.
2. Copy `chromium/` over the checkout and copy the Pulse `locales/*.json`
   to `ash/webui/pulse_ui/locales/` (or set the GN arg
   `pulse_locales_dir`).
3. Apply `chromium/patches/*.diff`. They were written by hand; the hunk
   line numbers and context are a guide, so expect to apply some by hand.
4. `gn gen out/cros --args='target_os="chromeos" use_remoteexec=true'`
5. `autoninja -C out/cros chrome ash_webui_unittests`
6. Run with `--enable-features=PulseShelfReadout` for the shelf readout.

## Add a language

Drop one file, `locales/<code>.json`, into the repo's `locales/` folder and
rebuild. See [TRANSLATING.md](../../TRANSLATING.md) for the file format. The
`pulse_catalog` action lists the folder in its depfile, so the build picks
up the new file, fills its `.xtb`, and the language shows up in Settings >
Language and in the header switcher. No code changes.

## Language setting

Settings > Language is a native dropdown with "Match system" plus every
locale, each under its own name. A pick switches the app and its text
direction at once. The choice lives in the profile pref
`ash.pulse.language` ("" means match system, which follows the ChromeOS
language and falls back to English). The header switcher writes the same
pref. In the harness the mock keeps it in `localStorage`.

## Run the dev harness

Needs Node 22, Python 3, g++ and libgtest-dev.

```sh
export WORK=/tmp/pulse-native-chromeos     # scratch and build output
dev/build.sh                                # catalog, tsc, plural test, site
node dev/shoot.mjs $WORK/build/www dist     # screenshots (Playwright)
dev/build_cpp_tests.sh                      # parser and grouper tests
node dev/render_icons.mjs chromium/ash/webui/pulse_ui/resources
```

`dev/build.sh` installs TypeScript into `$WORK/tools` the first time. Open
`$WORK/build/www/index.html` through any static server. Query knobs:
`?lang=ar`, `?theme=dark`, `?view=cpu|mem|nrg|thm|gpu|ssd|net|settings`,
`?hog=0` (no busy VM), `?charging=1`, `?live=0`, `?sim=hog,charging`,
`?end=1`, `?menu=1`, `?catalog=xa` (adds the 40% longer en-XA locale),
`?pref=ar` (saved language), `?reset=1`, `?battery=0` (Chromebox),
`?pressure=high|critical`, `?fail=1&endfail=1` (ending an app fails).

The process list in the harness comes from the mock, not from the host, so
the screenshots need no PID namespace.

## Real and simulated

* Real on a device: every number comes from cros_healthd, procfs, sysfs,
  power_manager (charger rating) and the GPU info. Ending a group works
  for Crostini only (`CrostiniManager::StopVm`); Restore restarts it.
  ARCVM, Chrome, ash and system daemons are read-only.
* Settings > Simulate CPU hog and Charging are a demo overlay on the real
  readings. They change what the page shows, never the device. Live
  updates calls `SetLiveUpdates`, which pauses the 1.5 s push.
* In the harness everything is simulated: `dev/mock/` returns data shaped
  like a MediaTek Kompanio 520 Chromebook with the Linux VM busy. The
  shelf in the harness is an HTML copy of the views code in
  `ash/system/pulse/`.

## What has been compiled

* Compiled and tested: all TypeScript (strict `tsc`, page and test), the
  plural engine port (512 checks against `i18n/test/fixtures/vectors.json`),
  `proc_parsers` and `process_grouper` (g++ and clang++, 13 gtest tests,
  built against small `base` shims in `dev/cpp_shim/`), the catalog
  generator.
* Not compiled: the mojom, `PulseSampler`, the page handler, `PulseUI`,
  the delegates, the shelf tray and bridge, the SWA delegate, the BUILD.gn
  files and the patches. This machine cannot build Chromium, and the
  Chromium source was not reachable, so their API use is from memory.

## Known gaps

* Per-group GPU share is not available from the kernel; the page shows a
  dash.
* The Network detail has no latency tile source yet, and "Today" shows
  totals since boot.
* GRIT message IDs are computed the way GRIT does it but were not checked
  against GRIT itself.
