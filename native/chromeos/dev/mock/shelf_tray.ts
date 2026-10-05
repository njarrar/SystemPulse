// Harness stand-in for Tier 1, the shelf readout. On a device this is
// ash/system/pulse/pulse_tray.cc (views), fed by PulseTrayBridge. This
// copy draws the same parts from the same snapshots so the layout can be
// checked here: pulsing dot, six-bar CPU wave, CPU % and W, bottom-end.

import {PulseBrowserProxyImpl} from './browser_proxy.js';
import {LocaleRegistry} from './i18n.js';
import type {Catalog} from './i18n.js';
import {HistoryMetric} from './pulse_ui.mojom-webui.js';
import type {Snapshot} from './pulse_ui.mojom-webui.js';

const BARS = 6;

function el(tag: string, cls: string, text?: string): HTMLElement {
  const e = document.createElement(tag);
  e.className = cls;
  if (text !== undefined) {
    e.textContent = text;
  }
  return e;
}

export async function startShelf(catalog: Catalog, win: HTMLElement) {
  const shelf = document.querySelector<HTMLElement>('.shelf')!;
  const registry = LocaleRegistry.fromCatalog(catalog);
  const proxy = PulseBrowserProxyImpl.getInstance();
  const {samples} = await proxy.handler.getHistory(HistoryMetric.kCpu);
  let wave = samples.slice(-(BARS - 1));

  const launcher = el('span', 'launcher');
  const tray = el('button', 'pulse-tray');
  const dot = el('span', 'dot');
  const bars = el('span', 'wave');
  const cpu = el('span', 'cpu');
  const watts = el('span', 'watts');
  cpu.dir = 'ltr';
  watts.dir = 'ltr';
  tray.append(dot, bars, cpu, watts);
  const status = el('span', 'status-area');
  const time = el('span', 'time');
  status.append(el('span', 'wifi'), el('span', 'batt'), time);
  const end = el('span', 'shelf-end');
  end.append(tray, status);
  shelf.append(launcher, end);
  tray.addEventListener('click', () => {
    win.hidden = !win.hidden;
    tray.classList.toggle('active', !win.hidden);
  });
  tray.classList.add('active');

  let last: Snapshot|null = null;
  const draw = (s: Snapshot) => {
    last = s;
    const lc = registry.get(document.documentElement.lang || 'en');
    shelf.dir = lc.dir;
    bars.replaceChildren(...wave.map(v => {
      const b = el('i', '');
      b.style.blockSize = Math.max(3, Math.min(14, 3 + v / 70 * 11)).toFixed(1) + 'px';
      return b;
    }));
    const w = s.battery ? Math.abs(s.battery.powerWatts) : 0;
    cpu.textContent = lc.num(Math.round(s.cpu.usagePercent)) + '%';
    watts.textContent = lc.num(w, 1) + 'W';
    tray.classList.toggle('alert', !!s.hog);
    time.textContent = lc.t('chromeTime');
    tray.setAttribute('aria-label', lc.join(['Pulse', `${lc.t('barCpu')} ${cpu.textContent}`, lc.num(w, 1) + ' W']));
  };
  const push = (s: Snapshot) => {
    wave = [...wave, s.cpu.usagePercent].slice(-BARS);
    draw(s);
  };
  proxy.callbackRouter.onSnapshot.addListener(push);
  push((await proxy.handler.getSnapshot()).snapshot);
  // Follow live language switches in the window.
  new MutationObserver(() => last && draw(last))
      .observe(document.documentElement, {attributes: true, attributeFilter: ['lang']});
}
