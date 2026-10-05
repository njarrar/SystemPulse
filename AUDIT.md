# Pulse prototype audit: v2.2 against Parts 1 and 3

I checked `Pulse_System_Monitor_v2_2.html` against Parts 1 and 3 of the master prompt, fixed what I found, and rebuilt it as `build/prototype/Pulse_System_Monitor.html`. Every finding below was checked in Chromium on all six platforms, in English, Arabic and a 40%-longer pseudo-locale, across the overview, all eight detail views, settings, the app tooltip and the end-app dialog.

## Part 3: language engine

| Area | v2.2 | Now |
|---|---|---|
| Where copy lives | A `T` table for two languages, plus about 120 inline `tr(en, ar)` pairs and `[en, ar]` arrays in the hardware profiles. A third language meant code changes in dozens of places. | All copy lives in `locales/<code>.json`. The component asks for keys and never checks a language code. |
| Arabic plurals | Hand-written rule: 1, 2, then "few" for every n up to 10 and "many" for every n above. 0 gave "0 عمليات", 100 gave "100 خيطاً", 103 gave "103 خيطاً". | CLDR rules from the locale file, all six forms. 0 gives the zero form, 100 to 102 give "other", 103 to 110 give "few". Checked against `Intl.PluralRules`. |
| Zero counts | An `if` in code picked "No apps ended yet". | An exact `"=0"` form in en.json, and the CLDR zero form in ar.json. |
| Language switcher | Hard-coded `EN | ع` button. | Built from the registered files: the pill for two locales, a menu for three or more (arrow keys, Home, End, Escape, click outside). |
| Bidi | A regex pass wrapped numbers in isolates for ten chosen output fields. New strings had to be added to that list by hand. | The engine wraps every value it inserts, and every item it joins, in isolates (U+2068 … U+2069) in RTL locales. |
| Direction and fonts | Keyed off `lang === 'ar'`. Arabic fonts came from a per-platform `ar` field in code. | Keyed off the file's `dir` and `fonts`, with per-platform overrides (IBM Plex Sans Arabic on macOS and Windows 11, Noto Sans Arabic on ChromeOS and Linux, Tahoma on XP and 98). |
| Numbers | `toFixed` everywhere. | `num()` formats with the file's `numberingSystem` (Latin digits for Arabic, per guideline 04; `arabext` would give Persian digits). |
| Fallback | None. | Missing keys fall back to English, then to the key, with one console warning each. `ar-EG` resolves to `ar`. |
| Page guideline 07 | Said Arabic has four forms (1, 2, 3 to 10, 11+). | Corrected to six CLDR forms with the 100 to 102 case. |

## Part 1: interface gaps

| Spec item | v2.2 | Now |
|---|---|---|
| Tier 1 readouts | Windows 11 lacked `⚡ W`. Linux lacked `⚡ W` and `↓ Net`. ChromeOS showed battery 84% instead of watts. | All three match the spec. Bar labels come from the locale. |
| Status pill under hog | "Hog Alert" with no number. | "Hog Alert · 53%". |
| Hog banner subtitle | Only in a hover tooltip. | A visible second line. Full title on hover when it truncates. |
| Memory subtitle | "9.3 of 16 GB · 6.7 free". | "9.3 GB of 16 GB". |
| Memory legend | Three items; Free missing. | Four items, Free included, all tied to live RAM %. |
| Memory badge | "Normal". | "Normal pressure". |
| Thermal note | Built in code, never shown. | Shown under the meter; turns red when throttled. |
| CPU temperature under hog | Settled at 54°C. | Settles at 52°C (still Warm). |
| GPU | A third small tile beside Storage and Network. | A full-width strip: model, utilization bar, %, temperature badge. Storage and Network sit below as two columns. |
| "1 min" sparkline label | `top: -4px`. | `top: -2px`. |
| Top apps rows | 32px. | 34px; tooltip offsets follow. |
| Split chart scrub pill | Held to [24%, 76%]. | Tracks across [16%, 84%] and shifts by its own width, so it never leaves the chart. |
| Text growth | Thermal stage labels, the memory badge and the footer spilled out of the 420px flyout with 40% longer text. | All truncate with an ellipsis; footer stays on one row. |
| °C / °F buttons in RTL | Rendered as "C°" and "F°". | `dir="ltr"` on the buttons. |

## Still open

- **Part 2 native apps.** Now in `native/`. See `native/README.md` for what is built and tested and what still needs a Mac, a PC or a Chromium checkout.
- **Flyout height.** The fixes add rows the spec asks for, so the flyout grew from 799px to 842px on macOS (881px on XP). It shows no inner scrollbar at a browser height of 1000px or more, about a 1080p screen. At 900px it scrolls, as v2.2 already did.
- **Fixture values.** Hardware sample values such as "240 GB" and "−52 dBm" are written with Latin digits in code. A locale with native digits would show those unconverted. Live telemetry would go through `num()`.
- **Hog timing.** The "over 50% for 2 min" rule is simulated: the alert shows at once.
- **Clocks.** The fake menu bar and taskbar clocks are locale strings, not `Intl.DateTimeFormat`.
