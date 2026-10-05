// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview <pulse-app>, the Pulse system monitor page. Tier 2 is the
 * overview (four metric cards, GPU strip, storage and network, top process
 * groups); tier 3 is a ten-minute detail view per metric or group. All copy
 * comes from the locale catalog; nothing here branches on a language code.
 */

import {chart, CHART_H, CHART_W, resample, spark} from './charts.js';
import type {Spark} from './charts.js';
import {Formatter, toNumber} from './format.js';
import {LocaleRegistry} from './i18n.js';
import type {Catalog, Locale} from './i18n.js';
import {icon} from './icons.js';
import {PulseBrowserProxyImpl} from './browser_proxy.js';
import type {PulseBrowserProxy} from './browser_proxy.js';
import {PULSE_CSS} from './pulse_styles.js';
import {HistoryMetric, MemoryPressure, NetworkKind, ProcessGroupKind, ThermalLevel} from './pulse_ui.mojom-webui.js';
import type {ProcessGroup, Snapshot} from './pulse_ui.mojom-webui.js';
import {h, patch} from './vdom.js';
import type {Child, VNode} from './vdom.js';

const PLATFORM = 'chrome';
const SPARK_POINTS = 40;  // One minute at the 1.5 s push rate.
const ROW_HEIGHT = 34;
const HISTORY_STEP_MS = 5000;

// Settings > Simulate CPU hog and Charging are a demo overlay on the real
// readings: they change what the page shows, never the device. These are
// the values the overlay holds the page at, from the Part 1 demo.
const SIM_HOG = {cpu: 66, groupCpu: 51.6, watts: 14.8, celsius: 52};
const SIM_CHARGE_SHARE = 0.85;  // Share of the adapter rating that reaches the battery.
// Rating assumed when no charger is attached: a typical Chromebook adapter.
const SIM_ADAPTER_FALLBACK_W = 45;

type MetricView = 'cpu'|'mem'|'nrg'|'thm'|'gpu'|'ssd'|'net';
type View = {type: MetricView}|{type: 'app', kind: ProcessGroupKind}|
    {type: 'settings'}|null;
type SortKey = 'cpu'|'mem'|'gpu';
type Accent = 'cpu'|'mem'|'nrg'|'thm'|'gpu'|'warn'|'crit';

interface Tile {
  k: string;
  v: string;
}

interface Detail {
  acc: Accent;
  acc2: Accent;
  title: string;
  hero: string;
  sub: string;
  tiles: Tile[];
  a: number[];
  b: number[]|null;
  zero: boolean;
  fa: (x: number) => string;
  fb: (x: number) => string;
  unit: string;
  aLabel: string;
  bLabel: string;
  isApp: boolean;
  canEnd: boolean;
}

interface History {
  key: string;
  a: number[];
  b: number[]|null;
}

// Display name, avatar letter and hue per process group. Names resolve
// through the locale's "apps" table; "System" uses the hardware table.
const GROUP_LOOK: Record<ProcessGroupKind, {app?: string, hw?: string, mono: string, hue: number, bg: boolean}> = {
  [ProcessGroupKind.kAsh]: {app: 'System UI', mono: 'S', hue: 165, bg: true},
  [ProcessGroupKind.kChrome]: {app: 'Chrome', mono: 'C', hue: 220, bg: false},
  [ProcessGroupKind.kCrostini]: {app: 'Linux (Crostini)', mono: 'L', hue: 45, bg: false},
  [ProcessGroupKind.kArcvm]: {app: 'Android (ARCVM)', mono: 'A', hue: 140, bg: false},
  [ProcessGroupKind.kSystem]: {hw: 'system', mono: '', hue: 250, bg: true},
};

const METRICS: Record<MetricView, {a: HistoryMetric, b?: HistoryMetric}> = {
  cpu: {a: HistoryMetric.kCpu},
  mem: {a: HistoryMetric.kMemory},
  nrg: {a: HistoryMetric.kPower},
  thm: {a: HistoryMetric.kThermal},
  gpu: {a: HistoryMetric.kGpu},
  ssd: {a: HistoryMetric.kStorageRead, b: HistoryMetric.kStorageWrite},
  net: {a: HistoryMetric.kNetworkDown, b: HistoryMetric.kNetworkUp},
};

function load<T>(key: string, fallback: T): T {
  try {
    const v = window.localStorage.getItem('pulse.' + key);
    return v == null ? fallback : JSON.parse(v) as T;
  } catch {
    return fallback;
  }
}

function save(key: string, value: unknown) {
  try {
    window.localStorage.setItem('pulse.' + key, JSON.stringify(value));
  } catch {
    // Storage can be off; the setting then lasts for this window only.
  }
}

/** A group's GPU share, or a dash where the driver gives no per-process data. */
function gpuText(g: ProcessGroup, F: Formatter): string {
  return g.gpuPercent == null ? '\u2014' : F.pct(g.gpuPercent);
}

/** Puts locale fonts first without letting a generic family win early. */
function stack(first: string, second: string): string {
  const strip = (s: string) =>
      s.split(',')
          .map(x => x.trim())
          .filter(x => !/^(system-ui|sans-serif|serif|ui-sans-serif)$/.test(x))
          .join(', ');
  return `${strip(first)}, ${second}`;
}

export class PulseAppElement extends HTMLElement {
  private readonly root_: ShadowRoot;
  private proxy_: PulseBrowserProxy|null = null;
  private registry_: LocaleRegistry|null = null;
  private lc_: Locale|null = null;
  // Latest readings from ash, and the same with the demo overlay applied.
  private real_: Snapshot|null = null;
  private snap_: Snapshot|null = null;
  private sim_ = {hog: false, charging: false, live: true};
  private simHogSince_ = 0;
  // Groups ended from this page in this session, for Restore ended apps.
  private ended_ = new Set<ProcessGroupKind>();
  private sparks_: Record<'cpu'|'mem'|'nrg'|'thm', number[]> =
      {cpu: [], mem: [], nrg: [], thm: []};
  private view_: View = null;
  private history_: History|null = null;
  private sort_: SortKey = 'cpu';
  private hoverRow_ = -1;
  private chartHover_: number|null = null;
  private hogDismissed_: ProcessGroupKind|null = null;
  private confirm_: ProcessGroupKind|null = null;
  private toast_: string|null = null;
  private toastTimer_ = 0;
  private toastError_ = false;
  private langMenu_ = false;
  private theme_: 'light'|'dark'|null = null;
  private fahrenheit_ = false;
  private renderQueued_ = false;
  private listenerId_: number|null = null;
  private keyboardNav_ = false;
  // System UI language, and the user's pick ("" = match system).
  private uiLocale_ = 'en';
  private langPref_ = '';

  constructor() {
    super();
    this.root_ = this.attachShadow({mode: 'open'});
    const sheet = new CSSStyleSheet();
    sheet.replaceSync(PULSE_CSS);
    this.root_.adoptedStyleSheets = [sheet];
  }

  /**
   * Starts the page. `catalog` is pulse_catalog.json; `uiLocale` is the
   * system UI language (document lang).
   */
  async start(catalog: Catalog, uiLocale: string, proxy?: PulseBrowserProxy) {
    this.registry_ = LocaleRegistry.fromCatalog(catalog);
    this.theme_ = load<'light'|'dark'|null>('theme', null);
    this.fahrenheit_ = load('fahrenheit', false);
    this.applyTheme_();
    this.proxy_ = proxy || PulseBrowserProxyImpl.getInstance();
    // The saved language lives in the ash.pulse.language profile pref; ""
    // means match the system language.
    this.uiLocale_ = uiLocale;
    this.langPref_ = (await this.proxy_.handler.getLanguage()).code;
    this.setLocale_(this.langPref_ || uiLocale, false);
    this.listenerId_ = this.proxy_.callbackRouter.onSnapshot.addListener(
        (s: Snapshot) => this.onSnapshot_(s));
    await this.seedSparks_();
    const {snapshot} = await this.proxy_.handler.getSnapshot();
    this.onSnapshot_(snapshot);
    this.addEventListener('keydown', e => {
      this.keyboardNav_ = true;
      this.onKey_(e);
    });
    this.addEventListener('pointerdown', () => {
      this.keyboardNav_ = false;
    });
  }

  disconnectedCallback() {
    if (this.proxy_ && this.listenerId_ != null) {
      this.proxy_.callbackRouter.removeListener(this.listenerId_);
    }
  }

  // --- Data ----------------------------------------------------------------

  private async seedSparks_() {
    const take = async (m: HistoryMetric) => {
      const {samples} = await this.proxy_!.handler.getHistory(m);
      return resample(samples.slice(-13), SPARK_POINTS);
    };
    this.sparks_ = {
      cpu: await take(HistoryMetric.kCpu),
      mem: await take(HistoryMetric.kMemory),
      nrg: await take(HistoryMetric.kPower),
      thm: await take(HistoryMetric.kThermal),
    };
  }

  private onSnapshot_(real: Snapshot) {
    if (!this.sim_.live && this.real_) {
      return;  // Paused: a push already in flight is dropped.
    }
    this.real_ = real;
    this.applyFrame_();
  }

  /** Fetches fresh readings even while live updates are paused. */
  private async refresh_() {
    if (!this.proxy_) {
      return;
    }
    const {snapshot} = await this.proxy_.handler.getSnapshot();
    this.real_ = snapshot;
    this.applyFrame_();
  }

  /**
   * Applies the demo overlay to the latest real readings and appends the
   * result to the sparklines, so a Settings change shows at once.
   */
  private applyFrame_() {
    if (!this.real_) {
      return;
    }
    const s = this.overlay_(this.real_);
    this.snap_ = s;
    const push = (a: number[], v: number) =>
        [...a.slice(-(SPARK_POINTS - 1)), v];
    const memPct = this.memPercent_(s);
    this.sparks_ = {
      cpu: push(this.sparks_.cpu, s.cpu.usagePercent),
      mem: push(this.sparks_.mem, memPct),
      nrg: push(
          this.sparks_.nrg, s.battery ? Math.abs(s.battery.powerWatts) : 0),
      thm: push(this.sparks_.thm, s.thermal.cpuCelsius),
    };
    if (!s.hog || s.hog.kind !== this.hogDismissed_) {
      this.hogDismissed_ = s.hog ? this.hogDismissed_ : null;
    }
    if (this.view_ && this.view_.type !== 'settings') {
      this.fetchHistory_();
    }
    this.requestRender_();
  }

  /** The group the simulated hog lands on: the Linux VM when it runs. */
  private simHogGroup_(s: Snapshot): ProcessGroup|undefined {
    return s.groups.find(g => g.kind === ProcessGroupKind.kCrostini) ||
        [...s.groups].filter(g => g.kind !== ProcessGroupKind.kAsh)
            .sort((a, b) => b.cpuPercent - a.cpuPercent)[0];
  }

  private overlay_(real: Snapshot): Snapshot {
    if (!this.sim_.hog && !this.sim_.charging) {
      return real;
    }
    const s = structuredClone(real);
    const bat = s.battery;
    if (this.sim_.hog) {
      const g = this.simHogGroup_(s);
      // A small wobble so the demo reads as live, not pinned.
      const wobble = Math.sin(Date.now() / 2300);
      if (g) {
        g.cpuPercent = Math.max(g.cpuPercent, SIM_HOG.groupCpu + wobble);
      }
      const cpu = s.cpu;
      const total = Math.max(cpu.usagePercent, SIM_HOG.cpu + 3 * wobble);
      const scale = cpu.usagePercent > 0 ? total / cpu.usagePercent : 1;
      cpu.userPercent = cpu.usagePercent > 0 ? cpu.userPercent * scale : total * 0.66;
      cpu.systemPercent = total - cpu.userPercent;
      cpu.perfClusterPercent = Math.min(100, Math.max(cpu.perfClusterPercent, total * 1.22));
      cpu.efficiencyClusterPercent = Math.max(cpu.efficiencyClusterPercent, total * 0.58);
      cpu.usagePercent = total;
      const th = s.thermal;
      th.cpuCelsius = Math.max(th.cpuCelsius, SIM_HOG.celsius + 0.4 * wobble);
      if (th.level !== ThermalLevel.kThrottled) {
        th.level = th.cpuCelsius < 48 ? ThermalLevel.kCool :
            th.cpuCelsius < 70 ? ThermalLevel.kWarm : ThermalLevel.kHot;
      }
      if (bat && !bat.charging) {
        bat.powerWatts = -Math.max(Math.abs(bat.powerWatts), SIM_HOG.watts + 0.6 * wobble);
      }
      if (g && (!s.hog || s.hog.kind !== g.kind)) {
        s.hog = {kind: g.kind, name: g.name, cpuPercent: g.cpuPercent,
                 secondsOverThreshold: 120, canEnd: g.canEnd};
      } else if (g && s.hog) {
        s.hog.cpuPercent = g.cpuPercent;
      }
    }
    if (bat && this.sim_.charging && !bat.charging) {
      const watts = (bat.adapterWatts > 0 ? bat.adapterWatts : SIM_ADAPTER_FALLBACK_W) *
          SIM_CHARGE_SHARE;
      bat.charging = true;
      bat.powerWatts = watts;
      const toFullWh = bat.fullWh * (100 - bat.chargePercent) / 100;
      bat.minutesRemaining = Math.round(toFullWh / watts * 60);
    }
    if (bat && !bat.charging && bat.powerWatts < 0) {
      const leftWh = bat.fullWh * bat.chargePercent / 100;
      bat.minutesRemaining = Math.round(leftWh / -bat.powerWatts * 60);
    }
    return s;
  }

  /** Lifts the samples taken since the simulated hog began. */
  private overlayHistory_(samples: number[], target: number): number[] {
    if (!this.sim_.hog || !samples.length) {
      return samples;
    }
    const n = Math.min(samples.length,
        Math.floor((Date.now() - this.simHogSince_) / HISTORY_STEP_MS) + 1);
    return samples.map((v, i) => i >= samples.length - n ? Math.max(v, target) : v);
  }

  private async fetchHistory_() {
    const v = this.view_;
    if (!v || v.type === 'settings' || !this.proxy_) {
      return;
    }
    const key = v.type === 'app' ? 'app:' + v.kind : v.type;
    let a: number[];
    let b: number[]|null = null;
    if (v.type === 'app') {
      a = (await this.proxy_.handler.getGroupHistory(v.kind)).samples;
      const hogGroup = this.real_ ? this.simHogGroup_(this.real_) : undefined;
      if (hogGroup && hogGroup.kind === v.kind) {
        a = this.overlayHistory_(a, SIM_HOG.groupCpu);
      }
    } else {
      const m = METRICS[v.type];
      a = (await this.proxy_.handler.getHistory(m.a)).samples;
      if (m.b !== undefined) {
        b = (await this.proxy_.handler.getHistory(m.b)).samples;
      }
      const lift: Partial<Record<MetricView, number>> = {
        cpu: SIM_HOG.cpu, thm: SIM_HOG.celsius,
        nrg: this.sim_.charging ? 0 : SIM_HOG.watts,
      };
      const target = lift[v.type];
      if (target) {
        a = this.overlayHistory_(a, target);
      }
    }
    // The newest point matches the readings on screen.
    const shown = this.snap_;
    if (shown && a.length && v.type !== 'app' && !b) {
      const last: Partial<Record<MetricView, number>> = {
        cpu: shown.cpu.usagePercent, thm: shown.thermal.cpuCelsius,
        nrg: shown.battery ? Math.abs(shown.battery.powerWatts) : a[a.length - 1]!,
      };
      const lv = last[v.type];
      if (lv !== undefined) {
        a = [...a.slice(0, -1), lv];
      }
    }
    // Ignore a late answer for a view the user already left.
    const now = this.view_;
    const nowKey = !now || now.type === 'settings' ? '' :
        now.type === 'app' ? 'app:' + now.kind : now.type;
    if (nowKey === key) {
      this.history_ = {key, a, b};
      this.requestRender_();
    }
  }

  private memPercent_(s: Snapshot): number {
    const total = toNumber(s.memory.totalBytes);
    return total ? (total - toNumber(s.memory.availableBytes)) / total * 100 : 0;
  }

  // --- Actions -------------------------------------------------------------

  private setLocale_(code: string, persist = true) {
    const lc = this.registry_!.get(code);
    this.lc_ = lc;
    this.setAttribute('dir', lc.dir);
    this.setAttribute('lang', lc.code);
    document.documentElement.dir = lc.dir;
    document.documentElement.lang = lc.code;
    const f = lc.fonts(PLATFORM);
    const css = getComputedStyle(document.documentElement);
    const hero = css.getPropertyValue('--pulse-type-family-display').trim();
    const text = css.getPropertyValue('--pulse-type-family-text').trim();
    this.style.setProperty('--pulse-font-hero', f.hero ? stack(hero, f.hero) : hero);
    this.style.setProperty('--pulse-font-text', f.ui ? stack(f.ui, text) : text);
    document.title = lc.t('winTitle');
    if (persist) {
      this.savePref_(lc.code);
    }
    this.requestRender_();
  }

  private savePref_(pref: string) {
    this.langPref_ = pref;
    this.proxy_?.handler.setLanguage(pref);
  }

  /** Settings > Language. "" follows the system language. */
  private chooseLanguage_(pref: string) {
    this.setLocale_(pref || this.uiLocale_, false);
    this.savePref_(pref);
  }

  private applyTheme_() {
    if (this.theme_) {
      document.documentElement.dataset['theme'] = this.theme_;
    } else {
      delete document.documentElement.dataset['theme'];
    }
  }

  private isDark_(): boolean {
    if (this.theme_) {
      return this.theme_ === 'dark';
    }
    return window.matchMedia('(prefers-color-scheme: dark)').matches;
  }

  private toggleTheme_() {
    this.theme_ = this.isDark_() ? 'light' : 'dark';
    save('theme', this.theme_);
    this.applyTheme_();
    this.requestRender_();
  }

  private open_(v: View) {
    this.view_ = v;
    this.history_ = null;
    this.chartHover_ = null;
    this.hoverRow_ = -1;
    this.langMenu_ = false;
    this.fetchHistory_();
    this.requestRender_();
    // Move focus into the new view when the user drives the page with the
    // keyboard. A mouse click leaves focus alone, so no ring appears.
    if (!this.keyboardNav_) {
      return;
    }
    requestAnimationFrame(() => {
      const target = this.root_.querySelector<HTMLElement>(
          v ? '.back' : '.metric');
      target?.focus({preventScroll: true});
    });
  }

  private showToast_(text: string, error = false) {
    clearTimeout(this.toastTimer_);
    this.toast_ = text;
    this.toastError_ = error;
    this.requestRender_();
    this.toastTimer_ = window.setTimeout(() => {
      this.toast_ = null;
      this.requestRender_();
    }, 2800);
  }

  private async confirmEnd_() {
    const kind = this.confirm_;
    if (kind == null || !this.proxy_) {
      return;
    }
    const name = this.groupName_(kind);
    this.confirm_ = null;
    if (this.view_ && this.view_.type === 'app' && this.view_.kind === kind) {
      this.view_ = null;
    }
    this.requestRender_();
    const {ok} = await this.proxy_.handler.endGroup(kind);
    if (ok) {
      this.ended_.add(kind);
      this.showToast_(this.lc_!.t('toastEnded', {app: name}));
      await this.refresh_();
    } else {
      this.showToast_(this.lc_!.t('endFailed', {app: name}), true);
    }
  }

  private async restoreEnded_() {
    if (!this.proxy_ || !this.ended_.size) {
      return;
    }
    for (const kind of [...this.ended_]) {
      await this.proxy_.handler.restoreGroup(kind);
      this.ended_.delete(kind);
    }
    this.hogDismissed_ = null;
    this.showToast_(this.lc_!.t('toastRestored'));
    await this.refresh_();
  }

  private setSim_(key: 'hog'|'charging'|'live', on: boolean) {
    this.sim_ = {...this.sim_, [key]: on};
    if (key === 'hog') {
      this.simHogSince_ = Date.now();
      this.hogDismissed_ = null;
    }
    if (key === 'live') {
      this.proxy_?.handler.setLiveUpdates(on);
      if (on) {
        this.refresh_();
      }
      this.requestRender_();
      return;
    }
    this.applyFrame_();
  }

  private openDiagnostics_() {
    this.proxy_?.handler.openDiagnostics();
    this.showToast_(this.lc_!.t(
        'toastMonitor', {monitor: this.lc_!.hw('diagnostics')}));
  }

  private onKey_(e: KeyboardEvent) {
    if (e.key === 'Escape') {
      if (this.langMenu_) {
        this.langMenu_ = false;
        this.requestRender_();
        this.root_.querySelector<HTMLElement>('.lang-trigger')?.focus();
      } else if (this.confirm_ != null) {
        this.confirm_ = null;
        this.requestRender_();
      } else if (this.view_) {
        this.open_(null);
      }
    }
  }

  private activate_(fn: () => void) {
    return (e: Event) => {
      const k = (e as KeyboardEvent).key;
      if (k === 'Enter' || k === ' ') {
        e.preventDefault();
        fn();
      }
    };
  }

  private langMenuKey_(e: KeyboardEvent) {
    const items = Array.from(
        this.root_.querySelectorAll<HTMLElement>('[role=menuitemradio]'));
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      if (!this.langMenu_) {
        this.langMenu_ = true;
        this.render_();
      }
      const list = Array.from(
          this.root_.querySelectorAll<HTMLElement>('[role=menuitemradio]'));
      let i = list.indexOf(this.root_.activeElement as HTMLElement);
      if (i < 0) {
        i = Math.max(0, list.findIndex(x => x.getAttribute('aria-checked') === 'true'));
      } else {
        i = (i + (e.key === 'ArrowDown' ? 1 : -1) + list.length) % list.length;
      }
      list[i]?.focus();
    } else if ((e.key === 'Home' || e.key === 'End') && items.length) {
      e.preventDefault();
      items[e.key === 'Home' ? 0 : items.length - 1]!.focus();
    }
  }

  // --- Rendering -----------------------------------------------------------

  private requestRender_() {
    if (this.renderQueued_) {
      return;
    }
    this.renderQueued_ = true;
    requestAnimationFrame(() => {
      this.renderQueued_ = false;
      this.render_();
    });
  }

  private groupName_(kind: ProcessGroupKind): string {
    const look = GROUP_LOOK[kind];
    return look.app ? this.lc_!.app(look.app) : this.lc_!.hw(look.hw!);
  }

  private render_() {
    const s = this.snap_;
    const lc = this.lc_;
    if (!s || !lc) {
      return;
    }
    const F = new Formatter(lc, this.fahrenheit_);
    const v = this.view_;
    let body: VNode;
    if (!v) {
      body = this.renderOverview_(s, lc, F);
    } else if (v.type === 'settings') {
      body = this.renderSettings_(lc, F);
    } else {
      body = this.renderDetail_(s, lc, F, v);
    }
    patch(this.root_, [
      this.renderHeader_(s, lc, F),
      this.renderBanners_(s, lc, F),
      body,
      this.renderFooter_(lc),
    ]);
    // A <select> keeps the user's last pick over the selected attribute, so
    // set its value after the header switcher changes the language.
    const select = this.root_.querySelector<HTMLSelectElement>('.lang-select');
    if (select && select.value !== this.langPref_) {
      select.value = this.langPref_;
    }
  }

  private renderHeader_(s: Snapshot, lc: Locale, F: Formatter): VNode {
    const hog = s.hog && this.hogDismissed_ !== s.hog.kind ? s.hog : null;
    const watts = s.battery ? Math.abs(s.battery.powerWatts) : 0;
    const status = hog ?
        lc.t('pillValue', {label: lc.t('hogPill'), value: F.pct(hog.cpuPercent)}) :
        s.battery ? lc.t('pillValue', {label: lc.t('calm'), value: F.wattsShort(watts)}) :
                    lc.t('calm');
    const themeLabel = this.isDark_() ? lc.t('aLight') : lc.t('aDark');
    return h('header', {'class': 'head'},
      h('div', {'class': 'head-start'},
        h('span', {'class': 'logo', 'aria-hidden': 'true'},
          h('span', {'class': 'a'}), h('span', {'class': 'b'})),
        h('span', {'class': 'brand'}, 'Pulse'),
        h('span', {'class': 'status ' + (hog ? 'hog' : 'calm'), 'role': 'status'},
          h('span', {'class': 'dot'}),
          h('span', {'class': 'ell num-mix'}, status))),
      h('div', {'class': 'head-end'},
        h('button', {
          'class': 'icon-btn', 'aria-label': themeLabel, 'title': themeLabel,
          'on': {click: () => this.toggleTheme_()},
        }, icon(this.isDark_() ? 'sun' : 'moon')),
        this.renderLangSwitch_(lc),
        h('button', {
          'class': 'icon-btn', 'aria-label': lc.t('aSettings'), 'title': lc.t('aSettings'),
          'data-view': 'settings',
          'aria-pressed': String(!!this.view_ && this.view_.type === 'settings'),
          'on': {click: () => this.open_(this.view_ && this.view_.type === 'settings' ? null : {type: 'settings'})},
        }, icon('tune'))));
  }

  /** Two locales: the EN | ع pill. Three or more: a compact menu. */
  private renderLangSwitch_(lc: Locale): VNode {
    const list = this.registry_!.list();
    const idx = Math.max(0, list.findIndex(x => x.code === lc.code));
    const fontFor = (code: string) => {
      const f = this.registry_!.get(code).fonts(PLATFORM);
      return f.ui || 'inherit';
    };
    if (list.length <= 2) {
      const next = list[(idx + 1) % list.length]!;
      return h('button', {
        'class': 'lang-pill', 'title': lc.t('aLang'),
        'aria-label': `${lc.t('aLang')}: ${next.name}`,
        'style': {'inline-size': `${Math.max(2, list.length) * 33}px`},
        'on': {click: () => this.setLocale_(next.code)},
      },
        h('span', {'class': 'thumb', 'style': {
          'inline-size': `calc((100% - 4px) / ${list.length})`,
          'inset-inline-start': `calc(2px + ${idx} * (100% - 4px) / ${list.length})`,
        }}),
        list.map(x => h('span', {
          'class': 'opt', 'lang': x.code,
          'style': {
            'font-family': fontFor(x.code),
            'color': x.code === lc.code ? 'var(--pulse-sys-on-surface)' : 'var(--pulse-sys-on-surface-variant)',
          },
        }, x.label)));
    }
    const cur = list[idx]!;
    return h('div', {
      'class': 'lang-menu-wrap',
      'on': {
        keydown: (e: Event) => this.langMenuKey_(e as KeyboardEvent),
        focusout: (e: Event) => {
          const next = (e as FocusEvent).relatedTarget as Node | null;
          if (!next || !(e.currentTarget as Node).contains(next)) {
            if (this.langMenu_) {
              this.langMenu_ = false;
              this.requestRender_();
            }
          }
        },
      },
    },
      h('button', {
        'class': 'lang-trigger', 'aria-haspopup': 'menu', 'aria-expanded': String(this.langMenu_),
        'aria-label': `${lc.t('aLang')}: ${lc.name}`, 'title': lc.t('aLang'),
        'style': {'font-family': fontFor(cur.code)},
        'on': {click: () => {
          this.langMenu_ = !this.langMenu_;
          this.render_();
          this.root_.querySelector<HTMLElement>('[role=menuitemradio][aria-checked=true]')?.focus();
        }},
      }, h('span', {'lang': cur.code}, cur.label), icon('down', 10)),
      this.langMenu_ && h('div', {'class': 'lang-menu', 'role': 'menu', 'aria-label': lc.t('aLang')},
        list.map(x => h('button', {
          'class': 'lang-item', 'role': 'menuitemradio', 'aria-checked': String(x.code === lc.code),
          'lang': x.code, 'dir': x.dir, 'tabindex': '-1', 'style': {'font-family': fontFor(x.code)},
          'on': {click: () => {
            this.langMenu_ = false;
            this.setLocale_(x.code);
            requestAnimationFrame(() => this.root_.querySelector<HTMLElement>('.lang-trigger')?.focus());
          }},
        }, h('span', {'class': 'code'}, x.label), h('span', {'class': 'ell', 'style': {'flex': '1'}}, x.name),
           icon('check', 12)))));
  }

  private renderBanners_(s: Snapshot, lc: Locale, F: Formatter): Child {
    if (this.confirm_ != null) {
      const name = this.groupName_(this.confirm_);
      const title = lc.t('confirmTitle', {app: name});
      return h('div', {'class': 'dialog', 'role': 'alertdialog', 'aria-label': title},
        h('div', {'class': 'row'},
          h('span', {'class': 'badge'}, icon('power')),
          h('div', {'style': {'flex': '1', 'min-inline-size': '0'}},
            h('div', {'class': 'title'}, title),
            h('div', {'class': 'sub ink2'}, lc.t('confirmSub')))),
        h('div', {'class': 'actions'},
          h('button', {'class': 'btn tall', 'on': {click: () => {
            this.confirm_ = null;
            this.requestRender_();
          }}}, lc.t('cancel')),
          h('button', {'class': 'btn tall danger', 'on': {click: () => this.confirmEnd_()}},
            lc.t('confirmBtn'))));
    }
    if (this.toast_) {
      return h('div', {'class': 'toast' + (this.toastError_ ? ' error' : ''), 'role': 'status'},
        h('span', {'class': 'tick'}, icon(this.toastError_ ? 'warn' : 'check', 12)),
        h('span', {'class': 'msg'}, this.toast_));
    }
    const hog = s.hog;
    if (hog && this.hogDismissed_ !== hog.kind && !this.view_) {
      const title = lc.t('hogTitle', {app: this.groupName_(hog.kind), pct: F.pct(hog.cpuPercent)});
      return h('div', {'class': 'banner hog', 'role': 'alert'},
        h('span', {'class': 'badge'}, icon('warn')),
        h('div', {'class': 'text'},
          h('div', {'class': 'title ell', 'title': title}, title),
          h('div', {'class': 'sub ink2 ell'}, lc.t('hogSub', {pct: F.pct(50)}))),
        hog.canEnd && h('button', {'class': 'btn warn', 'on': {click: () => {
          this.confirm_ = hog.kind;
          this.requestRender_();
        }}}, lc.t('endApp')),
        h('button', {
          'class': 'ghost', 'aria-label': lc.t('aDismiss'), 'title': lc.t('aDismiss'),
          'on': {click: () => {
            this.hogDismissed_ = hog.kind;
            this.requestRender_();
          }},
        }, icon('close', 12)));
    }
    return null;
  }

  private sparkSvg_(sp: Spark, color: string, id: string): VNode {
    return h('div', {'class': 'spark'},
      h('span', {'class': 'tag'}, this.lc_!.t('min1')),
      h('svg', {'viewBox': '0 0 172 28', 'preserveAspectRatio': 'none', 'aria-hidden': 'true'},
        h('defs', null,
          h('linearGradient', {'id': id, 'x1': 0, 'y1': 0, 'x2': 0, 'y2': 1},
            h('stop', {'offset': 0, 'style': {'stop-color': color, 'stop-opacity': 'var(--pulse-sys-spark-top)'}}),
            h('stop', {'offset': 1, 'style': {'stop-color': color, 'stop-opacity': '0'}}))),
        h('path', {'d': sp.area, 'fill': `url(#${id})`}),
        h('path', {'d': sp.line, 'fill': 'none', 'stroke-width': 1.6, 'stroke-linecap': 'round',
                   'vector-effect': 'non-scaling-stroke', 'style': {'stroke': color}})));
  }

  private cardHead_(iconName: string, tint: string, label: string, extra?: Child): VNode {
    return h('div', {'class': 'card-head'},
      h('span', {'class': 'chip-icon ' + tint}, icon(iconName)),
      h('span', {'class': 'label ell'}, label),
      extra,
      icon('chevron', 14, 'chev'));
  }

  private tap_(cls: string, label: string, view: View, ...children: Child[]): VNode {
    const go = () => this.open_(view);
    return h('div', {
      'class': 'card tap ' + cls, 'role': 'button', 'tabindex': '0', 'aria-label': label,
      'data-view': view ? view.type : '',
      'on': {click: go, keydown: this.activate_(go)},
    }, ...children);
  }

  private thermalNames_(lc: Locale): string[] {
    return [lc.t('cool'), lc.t('warm'), lc.t('hot'), lc.t('throttled')];
  }

  private stageVars_(level: ThermalLevel): Record<string, string> {
    const key = ['mem', 'nrg', 'thm', 'crit'][level]!;
    return {'--stage': `var(--pulse-${key})`, '--stage-ink': `var(--pulse-${key}-ink)`,
            '--stage-tint': `var(--pulse-${key}-tint)`};
  }

  private renderOverview_(s: Snapshot, lc: Locale, F: Formatter): VNode {
    const cpu = s.cpu;
    const high = cpu.usagePercent >= 50;
    const gpuPct = s.gpu && s.gpu.usagePercent != null ? s.gpu.usagePercent : 0;
    const memPct = this.memPercent_(s);
    const M = s.memory;
    const total = toNumber(M.totalBytes);
    const used = total - toNumber(M.availableBytes);
    const w = (b: number) => (Math.max(0, b) / total * 100).toFixed(1) + '%';
    const app = toNumber(M.appBytes);
    const sys = toNumber(M.systemBytes);
    const zram = toNumber(M.zramResidentBytes);
    const free = Math.max(0, total - used);
    const pressureOk = M.pressure === MemoryPressure.kNormal;
    const critical = M.pressure === MemoryPressure.kCritical;
    const pressureText = pressureOk ? lc.t('pressure') :
        critical ? lc.t('pressureCritical') : lc.t('pressureHigh');
    const pressureShort = pressureOk ? lc.t('pressureShort') :
        critical ? lc.t('pressureCriticalShort') : lc.t('pressureHighShort');
    const coreA = cpu.heterogeneous ? lc.hw('coreP') : lc.t('cpu');
    const coreB = cpu.heterogeneous ? lc.hw('coreE') : null;
    const T = s.thermal;
    const names = this.thermalNames_(lc);
    const bat = s.battery;
    const gpuC = T.gpuCelsius;

    const cpuCard = this.tap_('metric cpu' + (high ? ' high' : ''), lc.t('cpu'), {type: 'cpu'},
      this.cardHead_('cpu', 't-cpu', lc.t('cpu'), high && h('span', {'class': 'high-chip'}, lc.t('high'))),
      h('div', null,
        h('div', {'class': 'hero num' + (high ? ' warn' : '')}, F.pct(cpu.usagePercent)),
        h('div', {'class': 'sub ell'}, lc.t('userSys', {user: F.pct(cpu.userPercent), sys: F.pct(cpu.systemPercent)}))),
      h('div', {'class': 'bars'},
        this.barRow_(coreA, cpu.perfClusterPercent, 'var(--pulse-cpu)', F),
        coreB && this.barRow_(coreB, cpu.efficiencyClusterPercent, 'var(--pulse-cpu-soft)', F),
        this.barRow_(lc.t('gpu'), gpuPct, 'var(--pulse-gpu)', F)),
      this.sparkSvg_(spark(this.sparks_.cpu), 'var(--pulse-cpu)', 'spCpu'));

    const memCard = this.tap_('metric mem', lc.t('mem'), {type: 'mem'},
      this.cardHead_('mem', 't-mem', lc.t('mem')),
      h('div', null,
        h('div', {'style': {'display': 'flex', 'align-items': 'center', 'justify-content': 'space-between', 'gap': '6px'}},
          h('span', {'class': 'hero num', 'style': {'flex': 'none'}}, F.pct(memPct)),
          h('span', {'class': 'pressure ' + (pressureOk ? 'ok' : M.pressure === MemoryPressure.kCritical ? 'crit' : 'bad'),
                     'title': pressureText},
            h('span', {'class': 'pdot'}),
            h('span', {'class': 'ell', 'aria-label': pressureText}, pressureShort))),
        h('div', {'class': 'sub ell'}, lc.t('memOf', {used: F.gb(used), total: F.gb(total, 0)}))),
      h('div', {'class': 'col'},
        h('div', {'class': 'stack'},
          h('span', {'class': 'seg-app', 'style': {'inline-size': w(app)}}),
          h('span', {'class': 'seg-sys', 'style': {'inline-size': w(sys)}}),
          h('span', {'class': 'seg-zram', 'style': {'inline-size': w(zram)}})),
        h('div', {'class': 'legend'},
          h('span', null, h('i', {'class': 'seg-app'}), lc.t('segApp'), ' ', h('bdi', {'class': 'num'}, F.gb(app))),
          h('span', null, h('i', {'class': 'seg-sys'}), lc.hw('system'), ' ', h('bdi', {'class': 'num'}, F.gb(sys))),
          h('span', null, h('i', {'class': 'seg-zram'}), 'ZRAM ', h('bdi', {'class': 'num'}, F.gb(zram))),
          h('span', null, h('i', {'class': 'seg-free'}), lc.t('segFree'), ' ', h('bdi', {'class': 'num'}, F.gb(free))))),
      this.sparkSvg_(spark(this.sparks_.mem), 'var(--pulse-mem)', 'spMem'));

    const nrgCard = this.tap_('metric nrg', lc.t('nrg'), {type: 'nrg'},
      this.cardHead_('nrg', 't-nrg', lc.t('nrg')),
      h('div', null,
        bat ? h('div', {'class': 'hero num'}, F.pct(bat.chargePercent)) :
              h('div', {'class': 'hero ell nobat'}, lc.t('noBattery')),
        h('div', {'class': 'sub ell'}, !bat ? lc.t('acPower') : bat.minutesRemaining >= 0 ?
          (bat.charging ? lc.t('fullIn', {time: F.duration(bat.minutesRemaining)}) :
                          lc.t('timeLeft', {time: F.duration(bat.minutesRemaining)})) : '')),
      bat && h('div', {'class': 'col', 'style': {'gap': '7px'}},
        h('div', {'class': 'flow'},
          h('span', {'class': 'flow-chip num'}, h('span', {'class': 'dot-live'}), F.flow(bat.powerWatts)),
          h('span', {'class': 'ink2', 'style': {'font-size': '11px'}}, bat.charging ? lc.t('charging') : lc.t('onBatt'))),
        h('div', {'class': 'ink2 ell', 'style': {'font-size': '11px'}},
          lc.t('health', {pct: F.pct(bat.healthPercent), cycles: lc.plural('cycle', bat.cycleCount)}))),
      bat && this.sparkSvg_(spark(this.sparks_.nrg), 'var(--pulse-nrg)', 'spNrg'));

    const thmCard = this.tap_('metric thm', lc.t('thm'), {type: 'thm'},
      this.cardHead_('thm', 't-thm', lc.t('thm')),
      h('div', null,
        h('div', {'class': 'hero stage ell'}, names[T.level]),
        h('div', {'class': 'sub ell'}, lc.t('tempLine', {cpu: F.temp(T.cpuCelsius), gpu: gpuC != null ? F.temp(gpuC) : '—'}))),
      h('div', {'class': 'col', 'role': 'meter', 'aria-valuemin': '1', 'aria-valuemax': '4',
                'aria-valuenow': String(T.level + 1), 'aria-label': names[T.level], 'style': this.stageVars_(T.level)},
        h('div', {'class': 'meter'}, [0, 1, 2, 3].map(i => h('span', {'class': i === T.level ? 'on' : ''}))),
        h('div', {'class': 'stages'}, names.map((n, i) =>
          h('span', {'class': 'ell' + (i === T.level ? ' on' : ''), 'title': n}, n)))),
      h('div', {'class': 'note ell' + (T.level === ThermalLevel.kThrottled ? ' crit' : '')},
        T.level === ThermalLevel.kThrottled ? lc.t('throttling') : lc.t('zero')),
      this.sparkSvg_(spark(this.sparks_.thm), `var(--pulse-${['mem', 'nrg', 'thm', 'crit'][T.level]})`, 'spThm'));

    return h('main', {'class': 'view', 'key': 'overview'},
      h('div', {'class': 'grid2'}, cpuCard, memCard, nrgCard, thmCard),
      s.gpu && this.renderGpuStrip_(s, lc, F),
      h('div', {'class': 'grid2'}, this.renderStorage_(s, lc, F), this.renderNetwork_(s, lc, F)),
      this.renderApps_(s, lc, F));
  }

  private barRow_(label: string, pct: number, color: string, F: Formatter): VNode {
    return h('div', {'class': 'bar-row'},
      h('span', {'class': 'ink2 ell'}, label),
      h('span', {'class': 'track'}, h('span', {'style': {'inline-size': Math.max(2, pct) + '%', 'background': color}})),
      h('span', {'class': 'v num'}, F.pct(pct)));
  }

  private gpuTempKey_(c: number|null): string {
    return c == null || c < 60 ? 'mem' : c < 80 ? 'nrg' : 'thm';
  }

  private renderGpuStrip_(s: Snapshot, lc: Locale, F: Formatter): VNode {
    const g = s.gpu!;
    const pct = g.usagePercent;
    const c = s.thermal.gpuCelsius;
    const key = this.gpuTempKey_(c);
    return this.tap_('strip', lc.t('gpu'), {type: 'gpu'},
      h('span', {'class': 'chip-icon sm t-gpu'}, icon('gpu', 12)),
      h('span', {'class': 'names'},
        h('span', {'class': 'label'}, lc.t('gpu')),
        h('span', {'class': 'model ell', 'dir': 'auto', 'title': g.name}, g.name)),
      h('span', {'class': 'track', 'role': 'meter', 'aria-label': lc.t('util'), 'aria-valuemin': '0', 'aria-valuemax': '100',
                 'aria-valuenow': String(Math.round(pct || 0))},
        h('span', {'style': {'inline-size': Math.max(2, pct || 0) + '%', 'background': 'var(--pulse-gpu)'}})),
      h('span', {'class': 'val num', 'title': lc.t('util')}, pct != null ? F.pct(pct) : '—'),
      c != null && h('span', {'class': 'temp-chip num', 'style': {
        'background': `var(--pulse-${key}-tint)`, 'color': `var(--pulse-${key}-ink)`}}, F.temp(c)));
  }

  private renderStorage_(s: Snapshot, lc: Locale, F: Formatter): VNode {
    const st = s.storage;
    const name = lc.hw('internalStorage');
    const total = st ? toNumber(st.totalBytes) : 0;
    const free = st ? toNumber(st.freeBytes) : 0;
    const usedPct = total ? (total - free) / total * 100 : 0;
    const usedTxt = lc.t('used', {pct: F.pct(usedPct)});
    return this.tap_('mini', name, {type: 'ssd'},
      h('div', {'class': 'top'},
        h('span', {'class': 'chip-icon sm t-cpu'}, icon('ssd', 12)),
        h('span', {'class': 'name ell', 'title': name}, name)),
      h('div', {'class': 'line'},
        h('span', {'class': 'strong'}, usedTxt),
        h('span', {'class': 'light ell'}, lc.t('free', {value: F.gb(free, 0)}))),
      h('span', {'class': 'track', 'role': 'meter', 'aria-label': usedTxt, 'aria-valuemin': '0', 'aria-valuemax': '100',
                 'aria-valuenow': String(Math.round(usedPct))},
        h('span', {'class': 'ssd-fill', 'style': {'inline-size': usedPct.toFixed(1) + '%'}})));
  }

  private networkName_(s: Snapshot): string {
    const n = s.network;
    if (!n) {
      return this.lc_!.t('network');
    }
    return n.name || this.lc_!.t('network');
  }

  private renderNetwork_(s: Snapshot, lc: Locale, F: Formatter): VNode {
    const n = s.network;
    const name = this.networkName_(s);
    return this.tap_('mini', name, {type: 'net'},
      h('div', {'class': 'top'},
        h('span', {'class': 'chip-icon sm t-mem'}, icon(n && n.kind === NetworkKind.kEthernet ? 'ethernet' : 'wifi', 12)),
        h('span', {'class': 'name ell', 'dir': 'auto', 'title': name}, name)),
      h('div', {'class': 'rates'},
        h('span', {'class': 'rate-down num', 'aria-label': lc.t('down')}, '↓', F.rate(n ? n.rxBytesPerSecond : 0)),
        h('span', {'class': 'rate-up num', 'aria-label': lc.t('up')}, '↑', F.rate(n ? n.txBytesPerSecond : 0))));
  }

  private sortedGroups_(s: Snapshot): ProcessGroup[] {
    const key = this.sort_;
    const val = (g: ProcessGroup) => key === 'cpu' ? g.cpuPercent :
        key === 'mem' ? toNumber(g.memoryBytes) : (g.gpuPercent || 0);
    return [...s.groups].sort((a, b) => val(b) - val(a)).slice(0, 5);
  }

  private renderApps_(s: Snapshot, lc: Locale, F: Formatter): VNode {
    const key = this.sort_;
    const rows = this.sortedGroups_(s);
    const val = (g: ProcessGroup) => key === 'cpu' ? g.cpuPercent :
        key === 'mem' ? toNumber(g.memoryBytes) : (g.gpuPercent || 0);
    const mx = rows.length ? Math.max(val(rows[0]!), 0.001) : 1;
    const dark = this.isDark_();
    const sorts: Array<[SortKey, string]> = [['cpu', lc.t('cpu')], ['mem', lc.t('mem')], ['gpu', lc.t('gpu')]];
    const idx = sorts.findIndex(x => x[0] === key);
    const hogKind = s.hog ? s.hog.kind : null;
    const hr = this.hoverRow_ >= 0 ? rows[this.hoverRow_] : undefined;
    const totalMem = toNumber(s.memory.totalBytes) || 1;

    const rowNodes = rows.map((g, i) => {
      const look = GROUP_LOOK[g.kind];
      const name = this.groupName_(g.kind);
      const hogRow = hogKind === g.kind && key === 'cpu';
      const hue = look.hue;
      const monoBg = dark ? `oklch(0.4 0.07 ${hue} / ${look.bg ? 0.45 : 0.75})` :
          look.bg ? `oklch(0.95 0.035 ${hue})` : `oklch(0.89 0.075 ${hue})`;
      const monoInk = dark ? `oklch(0.9 0.08 ${hue})` : `oklch(${look.bg ? 0.45 : 0.4} 0.11 ${hue})`;
      const metric = key === 'mem' ? F.memory(toNumber(g.memoryBytes)) :
          key === 'cpu' ? F.pct(g.cpuPercent, 1) : gpuText(g, F);
      const open = () => this.open_({type: 'app', kind: g.kind});
      const hover = (on: boolean) => () => {
        if (on ? this.hoverRow_ !== i : this.hoverRow_ === i) {
          this.hoverRow_ = on ? i : -1;
          this.requestRender_();
        }
      };
      return h('div', {
        'key': 'g' + g.kind,
        'class': 'row' + (this.hoverRow_ === i ? ' hover' : ''), 'role': 'button', 'tabindex': '0', 'aria-label': name,
        'data-view': 'app',
        'on': {click: open, keydown: this.activate_(open), mouseenter: hover(true), mouseleave: hover(false),
               focusin: hover(true), focusout: hover(false)},
      },
        h('span', {'class': 'mono', 'style': {'background': monoBg, 'color': monoInk}},
          look.mono || icon('tune', 12)),
        h('span', {'class': 'who'},
          h('span', {'class': 'name ell', 'dir': 'auto'}, name),
          g.processCount > 1 && h('span', {'class': 'procs num'}, '×' + F.num(g.processCount))),
        h('span', {'class': 'track'}, h('span', {'style': {
          'inline-size': (Math.max(0.03, val(g) / mx) * 100).toFixed(1) + '%',
          'background': hogRow ? 'linear-gradient(calc(90deg * var(--flip)), var(--pulse-warn), var(--pulse-crit))' :
              key === 'cpu' ? 'var(--pulse-cpu)' : key === 'mem' ? 'var(--pulse-mem)' : 'var(--pulse-gpu)',
        }})),
        h('span', {'class': 'val num' + (hogRow ? ' hog' : '') + (g.canEnd ? ' can-end' : '')}, metric),
        g.canEnd && h('button', {
          'class': 'end-btn', 'tabindex': this.hoverRow_ === i ? '0' : '-1',
          'aria-label': lc.t('endNamed', {app: name}),
          'on': {click: (e: Event) => {
            e.stopPropagation();
            this.confirm_ = g.kind;
            this.hoverRow_ = -1;
            this.requestRender_();
          }},
        }, lc.t('end')));
    });

    let tip: Child = null;
    if (hr) {
      const up = this.hoverRow_ >= 3;
      const top = up ? this.hoverRow_ * ROW_HEIGHT - 142 : (this.hoverRow_ + 1) * ROW_HEIGHT + 2;
      const trow = (k: string, v: string) => h('div', {'class': 'trow'}, h('span', {'class': 'ink2'}, k), h('span', {'class': 'tv num'}, v));
      tip = h('div', {'class': 'tip', 'style': {'inset-block-start': top + 'px'}},
        h('div', {'class': 'tname', 'dir': 'auto'}, this.groupName_(hr.kind)),
        trow(lc.t('tipCore'), F.pct(hr.cpuPercent, 1)),
        trow(lc.t('tipRam'), F.pct(toNumber(hr.memoryBytes) / totalMem * 100, 1)),
        trow(lc.t('tipGpu'), gpuText(hr, F)),
        trow(lc.t('tipProc'), F.num(hr.processCount)),
        h('div', {'class': 'tfoot'}, lc.t('tipClick')));
    }

    return h('section', {'class': 'card apps', 'aria-label': lc.t('top')},
      h('div', {'class': 'apps-head'},
        h('span', {'class': 'title ell'}, lc.t('top')),
        h('div', {'class': 'seg', 'role': 'tablist'},
          h('span', {'class': 'thumb', 'style': {
            'inline-size': 'calc((100% - 4px) / 3)',
            'inset-inline-start': `calc(2px + ${idx} * (100% - 4px) / 3)`,
          }}),
          sorts.map(([k, label]) => h('button', {
            'role': 'tab', 'aria-selected': String(k === key),
            'on': {click: () => {
              this.sort_ = k;
              this.hoverRow_ = -1;
              this.requestRender_();
            }},
          }, h('span', {'class': 'ell', 'style': {'display': 'block'}}, label))))),
      h('div', {'class': 'rows'}, rowNodes, tip));
  }

  private renderFooter_(lc: Locale): VNode {
    const label = lc.t('openMonitor', {monitor: lc.hw('diagnostics')});
    return h('footer', {'class': 'foot'},
      h('button', {'class': 'btn open', 'title': label, 'on': {click: () => this.openDiagnostics_()}},
        h('span', {'class': 'ell'}, label), icon('external', 12)),
      h('span', {'class': 'privacy', 'title': lc.t('privacy')}, icon('lock', 11), h('span', {'class': 'ell'}, lc.t('privacy'))),
      h('button', {'class': 'quit', 'on': {click: () => window.close()}}, lc.t('quit')));
  }

  // --- Detail --------------------------------------------------------------

  private buildDetail_(s: Snapshot, lc: Locale, F: Formatter, v: Exclude<View, null|{type: 'settings'}>): Detail {
    const T = (k: string, val: string): Tile => ({k, v: val});
    const hist = this.history_;
    const a = hist ? hist.a : [];
    const b = hist ? hist.b : null;
    const base = {acc2: 'cpu' as Accent, b: null, zero: false, unit: '', aLabel: '', bLabel: '',
                  fb: (x: number) => String(x), isApp: false, canEnd: false};
    const cpu = s.cpu;
    switch (v.type) {
      case 'cpu': {
        const cores = [lc.plural('core', cpu.physicalCores)];
        if (cpu.logicalCores !== cpu.physicalCores) {
          cores.push(lc.plural('thread', cpu.logicalCores));
        }
        if (cpu.modelName) {
          cores.push(cpu.modelName);
        }
        const tiles = [T(lc.t('tUser'), F.pct(cpu.userPercent)), T(lc.t('tSystem'), F.pct(cpu.systemPercent)),
                       T(lc.t('tIdle'), F.pct(Math.max(0, 100 - Math.round(cpu.usagePercent))))];
        if (cpu.heterogeneous) {
          tiles.push(T(lc.hw('coreP'), F.pct(cpu.perfClusterPercent)), T(lc.hw('coreE'), F.pct(cpu.efficiencyClusterPercent)));
        }
        tiles.push(T(lc.t('tLoad'), F.num(cpu.loadAverage1m, 2)));
        return {...base, acc: 'cpu', acc2: 'cpu', title: lc.t('cpu'), hero: F.pct(cpu.usagePercent),
                sub: lc.t('cpuDetailSub', {user: F.pct(cpu.userPercent), sys: F.pct(cpu.systemPercent), cores: lc.join(cores)}),
                a, zero: true, fa: x => F.pct(x), unit: lc.t('unitCpu'), tiles};
      }
      case 'mem': {
        const M = s.memory;
        const total = toNumber(M.totalBytes);
        const vmShare = (toNumber(M.crostiniBytes) + toNumber(M.arcvmBytes)) / (total || 1) * 100;
        const vm = (bytes: bigint) => `${F.num(toNumber(bytes) / 1024 ** 3, 2)} GB · ${F.pct(toNumber(bytes) / (total || 1) * 100)}`;
        return {...base, acc: 'mem', acc2: 'mem', title: lc.t('mem'), hero: F.pct(this.memPercent_(s)),
                sub: lc.t('zramSub', {orig: F.gb(toNumber(M.zramOriginalBytes)), comp: F.gb(toNumber(M.zramCompressedBytes)), pct: F.pct(vmShare)}),
                a, fa: x => F.pct(x), unit: lc.t('unitRam'),
                tiles: [T(lc.t('segApp'), F.gb(toNumber(M.appBytes))), T(lc.hw('system'), F.gb(toNumber(M.systemBytes))),
                        T(lc.t('tZramStored'), `${F.num(toNumber(M.zramOriginalBytes) / 1024 ** 3, 1)} → ${F.gb(toNumber(M.zramCompressedBytes))}`),
                        T(lc.t('tZramRatio'), M.zramRatio ? F.num(M.zramRatio, 1) + '×' : '—'),
                        T(lc.app('Linux (Crostini)'), vm(M.crostiniBytes)), T(lc.app('Android (ARCVM)'), vm(M.arcvmBytes))]};
      }
      case 'nrg': {
        const bt = s.battery;
        const left = bt && bt.minutesRemaining >= 0 ? F.duration(bt.minutesRemaining) : '—';
        return {...base, acc: 'nrg', acc2: 'nrg', title: lc.t('nrg'), hero: bt ? F.pct(bt.chargePercent) : lc.t('noBattery'),
                sub: !bt ? lc.t('acPower') : bt.charging ? lc.t('chargingFullIn', {time: left}) : lc.t('onBattLeft', {time: left}),
                a, zero: true, fa: x => F.watts(x),
                tiles: !bt ? [] : [T(lc.t('tDraw'), F.flow(bt.powerWatts)),
                                   T(lc.t('tTimeLeft'), bt.charging ? lc.t('fullIn', {time: left}) : left),
                                   T(lc.t('tHealth'), F.pct(bt.healthPercent)), T(lc.t('tCycles'), F.num(bt.cycleCount)),
                                   T(lc.t('tCapacity'), `${F.num(bt.fullWh, 1)} / ${F.num(bt.designWh, 1)} Wh`),
                                   T(lc.t('tSource'), bt.charging ? lc.t('charging') : lc.t('tBattery'))]};
      }
      case 'thm': {
        const th = s.thermal;
        const names = this.thermalNames_(lc);
        const fans = s.fans.speedsRpm.length ?
            lc.join(s.fans.speedsRpm.map(r => F.num(r) + ' RPM')) : lc.hw('fanless');
        const opt = (c: number|null) => c == null ? '—' : F.temp(c, 1);
        return {...base, acc: (['mem', 'nrg', 'thm', 'crit'] as Accent[])[th.level]!, acc2: 'thm', title: lc.t('thm'),
                hero: F.temp(th.cpuCelsius),
                sub: lc.join([names[th.level], th.level === ThermalLevel.kThrottled ? lc.t('throttling') : lc.t('zero')]),
                a, fa: x => F.temp(x, 1),
                tiles: [T(lc.t('tCpuDie'), F.temp(th.cpuCelsius, 1)), T(lc.t('gpu'), opt(th.gpuCelsius)),
                        T(lc.t('tStorage'), opt(th.storageCelsius)), T(lc.t('tBattery'), opt(th.batteryCelsius)),
                        T(lc.t('tFans'), fans), T(lc.t('tThrottling'), F.pct(th.throttlePercent))]};
      }
      case 'gpu': {
        const g = s.gpu;
        const c = s.thermal.gpuCelsius;
        const tiles = [T(lc.t('util'), g && g.usagePercent != null ? F.pct(g.usagePercent) : '—'),
                       T(lc.t('tTemperature'), c != null ? F.temp(c, 1) : '—')];
        if (g && g.sharedMemoryBytes != null) {
          tiles.push(T(lc.t('tVideoMem'), lc.hw('vramShared', {size: F.gb(toNumber(g.sharedMemoryBytes))})));
        }
        if (g && g.clockMhz != null) {
          tiles.push(T(lc.t('tCoreClock'), F.num(g.clockMhz) + ' MHz'));
        }
        return {...base, acc: 'gpu', acc2: 'gpu', title: lc.t('gpu'),
                hero: g && g.usagePercent != null ? F.pct(g.usagePercent) : '—',
                sub: lc.join([g ? g.name : '', c != null ? F.temp(c) : '']), a, zero: true, fa: x => F.pct(x),
                unit: lc.t('unitGpu'), tiles};
      }
      case 'ssd': {
        const st = s.storage;
        const total = st ? toNumber(st.totalBytes) : 0;
        const free = st ? toNumber(st.freeBytes) : 0;
        return {...base, acc: 'cpu', acc2: 'nrg', title: lc.t('storage'), hero: F.pct(total ? (total - free) / total * 100 : 0),
                sub: lc.join([lc.hw('internalStorage'), lc.t('free', {value: F.gb(free, 0)})]),
                a, b, zero: true, fa: x => F.rate(x), fb: x => F.rate(x), aLabel: lc.t('read'), bLabel: lc.t('write'),
                tiles: [T(lc.t('tUsed'), F.gb(total - free, 0)), T(lc.t('tFree'), F.gb(free, 0)),
                        T(lc.t('read'), F.rate(st ? st.readBytesPerSecond : 0)), T(lc.t('write'), F.rate(st ? st.writeBytesPerSecond : 0)),
                        T(lc.t('tFormat'), st ? `${st.filesystem} · ${st.deviceType}` : '—')]};
      }
      case 'net': {
        const n = s.network;
        const kind = n && n.kind === NetworkKind.kWiFi ? 'Wi-Fi' : n && n.kind === NetworkKind.kEthernet ? 'Ethernet' : '';
        const tiles = [T(lc.t('down'), F.rate(n ? n.rxBytesPerSecond : 0)), T(lc.t('up'), F.rate(n ? n.txBytesPerSecond : 0)),
                       T(lc.t('tInterface'), n ? lc.join([n.interfaceName, kind]) : '—')];
        if (n && n.signalPercent != null) {
          tiles.push(T(lc.hw('signal'), F.pct(n.signalPercent)));
        }
        if (n) {
          tiles.push(T(lc.t('tToday'), `↓ ${F.size(toNumber(n.rxBytesTotal))} · ↑ ${F.size(toNumber(n.txBytesTotal))}`));
        }
        const state = n && n.online ? (n.signalPercent != null && n.signalPercent >= 60 ? lc.hw('strongSignal') : lc.hw('connected')) : '';
        return {...base, acc: 'mem', acc2: 'cpu', title: lc.t('network'), hero: F.rate(n ? n.rxBytesPerSecond : 0),
                sub: n ? lc.hw('netSub', {link: n.name, detail: n.interfaceName, state}) : '',
                a, b, zero: true, fa: x => '↓ ' + F.rate(x), fb: x => '↑ ' + F.rate(x),
                aLabel: lc.t('down'), bLabel: lc.t('up'), tiles};
      }
      case 'app': {
        const g = s.groups.find(x => x.kind === v.kind);
        const isHog = !!s.hog && s.hog.kind === v.kind;
        const name = this.groupName_(v.kind);
        if (!g) {
          return {...base, acc: 'cpu', acc2: 'cpu', title: name, hero: '—', sub: '', a, fa: x => F.pct(x, 1), tiles: [], isApp: true};
        }
        return {...base, acc: isHog ? 'warn' : 'cpu', acc2: 'cpu', title: name, hero: F.pct(g.cpuPercent, 1),
                sub: lc.join([lc.plural('process', g.processCount), lc.plural('thread', g.threadCount), 'PID ' + g.representativePid]),
                a, zero: true, fa: x => F.pct(x, 1), unit: lc.t('unitOfCpu'), isApp: true, canEnd: g.canEnd,
                tiles: [T(lc.t('cpu'), F.pct(g.cpuPercent, 1)), T(lc.t('mem'), F.memory(toNumber(g.memoryBytes))),
                        T(lc.t('gpu'), gpuText(g, F)), T(lc.t('tipProc'), F.num(g.processCount)),
                        T(lc.t('tThreads'), F.num(g.threadCount)), T('PID', String(g.representativePid))]};
      }
    }
  }

  private renderDetail_(s: Snapshot, lc: Locale, F: Formatter, v: Exclude<View, null|{type: 'settings'}>): VNode {
    const d = this.buildDetail_(s, lc, F, v);
    const acc = (k: Accent) => ({c: `var(--pulse-${k})`, ink: `var(--pulse-${k}-ink)`, tint: `var(--pulse-${k}-tint)`});
    const A = acc(d.acc);
    const B = acc(d.acc2);
    const key = v.type === 'app' ? 'app' + v.kind : v.type;
    const n = d.a.length;
    const geo = n >= 2 ? chart(d.a, d.b, d.zero) : null;
    let stat = '';
    if (n >= 2) {
      if (d.b) {
        stat = lc.t('peakSplit', {a: d.fa(Math.max(...d.a)), b: d.fb(Math.max(...d.b))});
      } else {
        const avg = d.a.reduce((x, y) => x + y, 0) / n;
        stat = lc.t('peakAvg', {peak: d.fa(Math.max(...d.a)), avg: d.fa(avg)});
      }
    }
    let scrub: Child[] = [];
    if (geo && this.chartHover_ != null) {
      const i = Math.max(0, Math.min(n - 1, this.chartHover_));
      const secs = (n - 1 - i) * 5;
      const m = Math.floor(secs / 60);
      const sc = secs % 60;
      const ago = secs === 0 ? lc.t('now') : !m ? lc.t('agoS', {s: sc}) : sc ? lc.t('agoMS', {m, s: sc}) : lc.t('agoM', {m});
      const val = d.b ? lc.join([d.fa(d.a[i]!), d.fb(d.b[i] ?? 0)]) : `${d.fa(d.a[i]!)}${d.unit ? ' ' + d.unit : ''}`;
      const left = i / (n - 1) * 100;
      const pill = Math.max(16, Math.min(84, left));
      scrub = [
        h('div', {'class': 'scrub-line', 'style': {'left': left.toFixed(2) + '%'}}),
        h('div', {'class': 'scrub-dot', 'style': {'left': left.toFixed(2) + '%', 'top': geo.pa[i]![1].toFixed(1) + 'px', 'background': A.c}}),
        geo.pb && h('div', {'class': 'scrub-dot', 'style': {'left': left.toFixed(2) + '%', 'top': geo.pb[i]![1].toFixed(1) + 'px', 'background': B.c}}),
        h('div', {'class': 'scrub-pill', 'dir': lc.dir, 'style': {'left': pill.toFixed(2) + '%', 'transform': `translateX(-${pill.toFixed(2)}%)`}},
          lc.join([ago, val])),
      ];
    }
    const tiles = d.tiles.map((t, i) => h('div', {'class': 'card tile' + (d.tiles.length % 2 && i === d.tiles.length - 1 ? ' wide' : '')},
      h('span', {'class': 'k ell'}, t.k),
      h('span', {'class': 'v ell num-mix', 'dir': 'auto', 'title': t.v}, t.v)));
    const chartMove = (e: Event) => {
      const el = e.currentTarget as HTMLElement;
      const r = el.getBoundingClientRect();
      const fr = Math.max(0, Math.min(1, ((e as PointerEvent).clientX - r.left) / r.width));
      const i = Math.round(fr * (n - 1));
      if (i !== this.chartHover_) {
        this.chartHover_ = i;
        this.requestRender_();
      }
    };
    return h('main', {'class': 'view detail', 'key': 'detail-' + key, 'style': {
      '--acc': A.c, '--acc-ink': A.ink, '--acc-tint': A.tint}},
      h('div', {'class': 'detail-bar'},
        h('button', {'class': 'btn back', 'on': {click: () => this.open_(null)}}, icon('back'), lc.t('back')),
        h('span', {'class': 'title-chip'}, h('span', {'class': 'ring'}, h('span')), h('span', {'class': 'ell', 'dir': 'auto'}, d.title)),
        d.isApp && d.canEnd && h('button', {'class': 'btn danger-outline end', 'on': {click: () => {
          if (v.type === 'app') {
            this.confirm_ = v.kind;
            this.requestRender_();
          }
        }}}, lc.t('endApp'))),
      h('div', {'class': 'hero-line'},
        h('span', {'class': 'big num'}, d.hero),
        h('span', {'class': 'desc'}, d.sub)),
      h('div', {'class': 'card chart-card'},
        h('div', {'class': 'chart-head'},
          h('span', {'class': 'h'}, lc.t('last10')),
          h('span', {'class': 'stat num-mix'}, stat),
          d.b && h('div', {'class': 'chart-legend'},
            h('span', {'style': {'color': A.ink}}, h('i', {'style': {'background': A.c}}), d.aLabel),
            h('span', {'style': {'color': B.ink}}, h('i', {'style': {'background': B.c}}), d.bLabel))),
        h('div', {'class': 'chart', 'dir': 'ltr', 'on': {pointermove: chartMove, pointerleave: () => {
          this.chartHover_ = null;
          this.requestRender_();
        }}},
          h('svg', {'viewBox': `0 0 ${CHART_W} ${CHART_H}`, 'preserveAspectRatio': 'none', 'aria-hidden': 'true'},
            h('defs', null,
              h('linearGradient', {'id': 'detA', 'x1': 0, 'y1': 0, 'x2': 0, 'y2': 1},
                h('stop', {'offset': 0, 'style': {'stop-color': A.c, 'stop-opacity': 'var(--pulse-sys-spark-top)'}}),
                h('stop', {'offset': 1, 'style': {'stop-color': A.c, 'stop-opacity': '0'}})),
              h('linearGradient', {'id': 'detB', 'x1': 0, 'y1': 1, 'x2': 0, 'y2': 0},
                h('stop', {'offset': 0, 'style': {'stop-color': B.c, 'stop-opacity': 'var(--pulse-sys-spark-top)'}}),
                h('stop', {'offset': 1, 'style': {'stop-color': B.c, 'stop-opacity': '0'}}))),
            h('path', {'d': 'M0 35H372M0 105H372', 'fill': 'none', 'stroke-width': 1, 'stroke-dasharray': '2 4',
                       'vector-effect': 'non-scaling-stroke', 'style': {'stroke': 'var(--pulse-sys-outline)'}}),
            h('path', {'d': 'M0 70H372', 'fill': 'none', 'stroke-width': 1, 'stroke-dasharray': d.b ? '0' : '2 4',
                       'vector-effect': 'non-scaling-stroke', 'style': {'stroke': 'var(--pulse-sys-on-surface-subtle)', 'opacity': '0.45'}}),
            geo && h('path', {'d': geo.areaA, 'fill': 'url(#detA)'}),
            geo && h('path', {'d': geo.lineA, 'fill': 'none', 'stroke-width': 2, 'stroke-linecap': 'round', 'stroke-linejoin': 'round',
                              'vector-effect': 'non-scaling-stroke', 'style': {'stroke': A.c}}),
            geo && geo.lineB && h('path', {'d': geo.areaB, 'fill': 'url(#detB)'}),
            geo && geo.lineB && h('path', {'d': geo.lineB, 'fill': 'none', 'stroke-width': 2, 'stroke-linecap': 'round',
                                           'stroke-linejoin': 'round', 'vector-effect': 'non-scaling-stroke', 'style': {'stroke': B.c}})),
          scrub),
        h('div', {'class': 'axis', 'dir': 'ltr'},
          h('span', null, lc.t('ago10')), h('span', null, lc.t('ago5')), h('span', null, lc.t('now')))),
      h('div', {'class': 'tiles'}, tiles));
  }

  /**
   * Settings > Language: a native <select> with "Match system" and every
   * locale in the catalog, each under its own name. A new locale file adds
   * an option with no code change.
   */
  private renderLanguageRow_(lc: Locale): VNode {
    const list = this.registry_!.list();
    const option = (value: string, label: string, code?: string, dir?: string) =>
        h('option', {'value': value, 'selected': this.langPref_ === value,
                     'lang': code || null, 'dir': dir || null}, label);
    return h('div', {'class': 'set-row'},
      h('div', {'class': 'set-text'},
        h('label', {'class': 'lbl ell', 'id': 'lang-label', 'for': 'lang-select'}, lc.t('language')),
        h('div', {'class': 'set-sub ink2 ell'}, lc.t('languageSub'))),
      h('div', {'class': 'select-wrap'},
        h('select', {
          'class': 'lang-select', 'id': 'lang-select',
          'on': {change: (e: Event) => this.chooseLanguage_((e.target as HTMLSelectElement).value)},
        },
          option('', lc.t('langSystem')),
          list.map(x => option(x.code, x.name, x.code, x.dir))),
        icon('down', 10)));
  }

  private renderSettings_(lc: Locale, F: Formatter): VNode {
    const sw = (key: 'hog'|'charging'|'live', label: string, sub: string, disabled = false) => {
      const on = this.sim_[key];
      const id = 'sw-' + key;
      return h('div', {'class': 'set-row'},
        h('div', {'class': 'set-text'},
          h('div', {'class': 'lbl ell', 'id': id}, label),
          h('div', {'class': 'set-sub ink2 ell num-mix'}, sub)),
        h('button', {
          'class': 'switch', 'role': 'switch', 'aria-checked': String(on), 'aria-labelledby': id,
          'disabled': disabled ? '' : null,
          'on': {click: () => this.setSim_(key, !on)},
        }, h('span', {'class': 'knob'})));
    };
    const unit = (label: string, f: boolean) => h('button', {
      'dir': 'ltr', 'aria-pressed': String(this.fahrenheit_ === f),
      'on': {click: () => {
        this.fahrenheit_ = f;
        save('fahrenheit', f);
        this.requestRender_();
      }},
    }, label);
    const bat = this.real_ ? this.real_.battery : null;
    const adapterW = bat && bat.adapterWatts > 0 ? bat.adapterWatts : SIM_ADAPTER_FALLBACK_W;
    const adapter = F.num(Math.round(adapterW)) + ' W';
    const ended = this.ended_.size;
    return h('main', {'class': 'view detail', 'key': 'settings'},
      h('div', {'class': 'detail-bar'},
        h('button', {'class': 'btn back', 'on': {click: () => this.open_(null)}}, icon('back'), lc.t('back')),
        h('span', {'class': 'settings-title'}, lc.t('aSettings'))),
      h('div', {'class': 'card settings'},
        sw('hog', lc.t('simHog'), lc.t('simHogSub', {pct: F.pct(50)})),
        sw('charging', lc.t('simCharging'), lc.t('simChargingSub', {adapter}), !bat),
        sw('live', lc.t('simLive'), lc.t('simLiveSub', {secs: lc.t('seconds', {n: F.num(1.5, 1)})})),
        this.renderLanguageRow_(lc),
        h('div', {'class': 'set-row'},
          h('span', {'class': 'lbl ell'}, lc.t('tempUnit')),
          h('div', {'class': 'unit'}, unit('°C', false), unit('°F', true))),
        h('div', {'class': 'set-row'},
          h('div', {'class': 'set-text'},
            h('div', {'class': 'lbl ell'}, lc.t('restore')),
            h('div', {'class': 'set-sub ink2 ell'}, lc.plural('ended', ended))),
          h('button', {
            'class': 'btn tall', 'disabled': ended ? null : '',
            'on': {click: () => this.restoreEnded_()},
          }, lc.t('restoreBtn')))));
  }
}

customElements.define('pulse-app', PulseAppElement);

declare global {
  interface HTMLElementTagNameMap {
    'pulse-app': PulseAppElement;
  }
}
