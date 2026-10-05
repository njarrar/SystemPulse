# Pulse for Linux (GNOME)

Rust, gtk4-rs and libadwaita. Pulse puts an entry in the top bar and opens a 420px flyout with three tiers: the top bar readout, the overview, and the 10-minute detail and settings views. It reads live data from `/proc`, `/sys` and netlink every 1.5 s and makes no network connections.

## Build

Needs GTK 4.14+, libadwaita 1.5+ and Rust 1.80+.

```
sudo apt install libgtk-4-dev libadwaita-1-dev     # Debian, Ubuntu 24.04
cargo build --release                               # binary: target/release/pulse
cargo test                                          # plural vectors, catalogs, telemetry parsers
```

`dist/pulse` is a release build for x86-64 (Ubuntu 24.04 libraries).

## Run

```
pulse                       # top bar entry; click it to open the flyout
pulse --show-flyout         # open the flyout now and keep it open (no tray host needed)
pulse --lang ar             # language for this run; default is the saved choice
pulse --view cpu            # cpu, mem, nrg, thm, gpu, ssd, net, settings, app:<key>
pulse --theme dark
pulse --help                # all options
```

Esc goes back to the overview, then closes the flyout. The flyout closes when it loses focus unless `--show-flyout` pinned it.

### Top bar entry

Pulse publishes a D-Bus `StatusNotifierItem`. The icon is a status dot (green, or orange during a hog alert) and a 6-bar CPU wave. The `CPU % RAM % ⚡ W ↓ Net` readout goes out as the Ayatana label, which the AppIndicator extension prints next to the icon. Stock GNOME Shell needs the AppIndicator extension (Ubuntu ships it on). KDE, Budgie, Cinnamon and others show the item natively.

Placement: on X11 the flyout opens at the top-end corner under the bar (top-left in RTL). Wayland does not let apps place windows, so GNOME on Wayland puts it where it likes.

## Add a language

Drop a new `<code>.json` file into the repo's `locales/` folder and rebuild. `build.rs` turns every file there into a catalog and adds it to the language list; no code change is needed. See [TRANSLATING.md](../../TRANSLATING.md) for the file format.

## Languages

Settings has a Language row with "Match system" plus every locale, each shown by its own name. "Match system" follows LANGUAGE, LC_ALL, LC_MESSAGES or LANG and falls back to English. Picking a language from Settings or from the header pill switches the app live, direction too, and saves the choice in `$XDG_CONFIG_HOME/pulse/settings.ini` (a GLib key file, since a single binary has no installed GSettings schema).

`build.rs` turns every `../../locales/*.json` into a gettext catalog, `pulse.po` and `pulse.mo`, and embeds the `.mo` files in the binary. Adding a locale file needs no code change, only a rebuild. Copies of the generated catalogs are in `dist/locale/`.

Each JSON value becomes one message: `msgctxt` is the section (`strings`, `hardware`, `apps`, `meta`, `pluralRules`, `plural:<key>`) and `msgid` is the key. Plural forms stay one message per CLDR category, so the CLDR rules from the file pick the form, not gettext's `Plural-Forms`. Exact forms such as `"=0"` win. Values put into a string are wrapped in U+2068/U+2069 in RTL locales, missing keys fall back to English and then to the key, and `ar-EG` resolves to `ar`. `msgfmt --check` accepts the `.po` files.

Extra catalogs, such as the en-XA pseudo-locale:

```
cargo run --bin pulse-catalog -- ../../i18n/test/fixtures/en-XA.json /tmp/xa
pulse --show-flyout --locales-dir /tmp/xa --lang en-XA
```

With two locales the header shows the `EN | ع` pill; with three or more it shows a menu. Switching rebuilds the flyout and flips direction with `gtk_widget_set_default_direction`. Bars, the memory bar, the thermal meter and chevrons mirror in RTL. Numbers and units stay left to right with tabular figures. The 10-minute chart keeps time running left to right in every language, matching its axis labels.

## What is real and what is not

| Area | Source |
|---|---|
| CPU total, user, system, idle | `/proc/stat` deltas |
| Core groups | Intel hybrid: `/sys/devices/cpu_core` and `cpu_atom` (P-Cores, E-Cores). Others: `acpi_cppc/highest_perf`; two classes more than 15% apart are split (Zen 4 and Zen 4c on AMD). Uniform CPUs show two halves ("Core 0–1", "Core 2–3"). |
| Load average | `/proc/loadavg` |
| Memory | `/proc/meminfo`: used = total minus available, split into App, Buffers and the zswap pool (or ZRAM from `/sys/block/zram0/mm_stat`); swap; PSI from `/proc/pressure/memory` |
| Battery | `/sys/class/power_supply/BAT*`: capacity, `power_now` (or current × voltage), energy now, full and design, cycles, status, time left or to full |
| Power without a battery | RAPL `/sys/class/powercap/intel-rapl:0/energy_uj` (often root only) |
| Temperatures, fans | `/sys/class/hwmon` (k10temp, coretemp, zenpower, amdgpu, nvme and more), `/sys/class/thermal` as fallback; throttle events from `thermal_throttle` |
| GPU | `/sys/class/drm/card*/device`: `gpu_busy_percent`, `mem_info_vram_*`, `pp_dpm_sclk`, hwmon power and temperature; i915/xe frequency; NVML loaded at run time on NVIDIA; name from `pci.ids` |
| Per-app GPU | DRM fdinfo engine times (`drm-engine-*`) |
| Storage | `statvfs("/")`, filesystem type from `/proc/mounts`, read and write from `/proc/diskstats` |
| Network | default-route interface from `/proc/net/route`, rates from `/proc/net/dev`, Wi-Fi signal and bitrate from nl80211 `GET_STATION` over generic netlink, `/proc/net/wireless` as fallback |
| Apps | `/proc/[pid]/stat`, grouped by systemd app scope (`app-gnome-<id>-<pid>.scope`, names from `.desktop` files) or by binary |
| End app | `kill(pid, SIGTERM)` to every process in the group |
| Open System Monitor | starts `gnome-system-monitor` |
| Memory pressure | PSI "some avg10": under 10% normal, 10% to 40% high, 40% and up critical |
| Hog alert | one app above 50% of total CPU for 2 minutes (a dip under 4 s does not reset the clock) |

A missing source shows a dash and the layout stays put. Inside a container with no battery, hwmon or DRM, the Energy, Thermal and GPU cards show dashes, as in the screenshots.

Settings has the switches from Part 1:

- **Simulate CPU hog** and **Charging** are a demo layer over real readings. Simulate CPU hog lifts the busiest real app to at least 53% of total CPU, raises totals, power and CPU temperature to match, and shows the alert at once. Charging shows the battery charging at 48 W, or a sample battery (84%, 95% health, 118 cycles) on a machine without one. Both are off by default; `--sim-hog` and `--sim-charging` turn them on at start.
- **Live updates** pauses polling.
- **°C / °F** switches the temperature unit.
- **Language** picks the app language (see above).
- **Restore ended apps** starts again each app Pulse ended this session, with the command line and working directory it had.

If ending an app fails (for example, it belongs to another user), Pulse shows a "Couldn't end" toast. A desktop with no battery shows "No battery" on the Energy card and "On AC power" as the source.

## Not done

- Latency has no local source and Pulse sends no packets, so the Latency tile shows a dash.
- "Today" on the network view counts traffic since Pulse started.
- The storage Health tile shows the NVMe temperature, not SMART health, which needs root.
- The Arabic flyout is about 40px taller than the English one because Noto Sans Arabic has taller lines; it fits a 1080p screen.
- Measured in Xvfb with software rendering: about 0.2% of one core with the flyout hidden and 0.5% with it open, 120 MB RSS (mostly GTK).

## Layout

```
build.rs              locales/*.json -> .po and .mo, embedded
src/catalog.rs        JSON to catalog entries, .po and .mo writer, .mo reader
src/i18n.rs           CLDR plural engine, interpolation, isolation, fallback
src/telemetry/        cpu, mem, power, thermal, gpu, disk, net, nl80211, procs
src/sni.rs            StatusNotifierItem over D-Bus (zbus)
src/ui/               flyout window, views, drawing, tokens, CSS and saved settings
src/bin/pulse-catalog.rs   converts extra locale files
tests/                plural vectors and engine tests, telemetry tests
dist/                 release binary, catalogs, screenshots
```
