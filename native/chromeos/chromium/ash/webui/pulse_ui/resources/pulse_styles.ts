// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Shadow DOM styles for <pulse-app>. Colors, radii and
 * families come from pulse_tokens.css. Every offset uses logical
 * properties, so dir="rtl" on the host mirrors the layout.
 */

export const PULSE_CSS = `
:host {
  --flip: 1;
  display: block;
  box-sizing: border-box;
  inline-size: 100%;
  max-inline-size: 420px;
  min-block-size: 100%;
  padding: 10px;
  background: var(--pulse-sys-surface);
  color: var(--pulse-sys-on-surface);
  font: 400 12px/1.3 var(--pulse-font-text);
  -webkit-font-smoothing: antialiased;
}
:host([dir=rtl]) { --flip: -1; }
* { box-sizing: border-box; }
button { font: inherit; color: inherit; }
:focus-visible { outline: 2px solid var(--pulse-focus); outline-offset: 2px; }
.flip { transform: scaleX(var(--flip)); }
.icon { flex: none; display: block; }
.num {
  font-variant-numeric: tabular-nums;
  direction: ltr;
  unicode-bidi: isolate;
}
.ell { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; min-inline-size: 0; }
.ink2 { color: var(--pulse-sys-on-surface-variant); }
.ink3 { color: var(--pulse-sys-on-surface-subtle); }
.col { display: flex; flex-direction: column; gap: 6px; }
@keyframes pulseDot {
  0% { box-shadow: 0 0 0 0 currentColor; }
  80% { box-shadow: 0 0 0 5px transparent; }
  100% { box-shadow: 0 0 0 0 transparent; }
}
@media (prefers-reduced-motion: reduce) {
  .dot, .dot-live { animation: none !important; }
  .card { transition: none !important; }
}

/* Header ---------------------------------------------------------------- */
.head { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin-block-end: 6px; }
.head-start { display: flex; align-items: center; gap: 8px; min-inline-size: 0; flex: 1 1 auto; }
.head-end { display: flex; align-items: center; gap: 6px; flex: none; }
.logo {
  position: relative; inline-size: 32px; block-size: 32px; flex: none; overflow: hidden;
  border-radius: var(--pulse-shape-logo); background: var(--pulse-sys-logo);
  border: 1px solid var(--pulse-sys-outline-variant);
}
.logo span { position: absolute; inset-block-start: 8px; inline-size: 12px; block-size: 12px; border-radius: 50%; }
.logo .a { left: 5px; border: 2px solid var(--pulse-cpu); box-shadow: 0 0 8px var(--pulse-cpu-glow); }
.logo .b { left: 11px; border: 2px solid var(--pulse-mem); box-shadow: 0 0 8px var(--pulse-mem-glow); }
.brand { font: 800 16px/1 var(--pulse-font-hero); letter-spacing: -0.01em; flex: none; }
.status {
  display: inline-flex; align-items: center; gap: 6px; block-size: 22px; padding: 0 9px; min-inline-size: 0;
  border-radius: var(--pulse-shape-pill); font-size: 11px; font-weight: 600; white-space: nowrap;
  transition: background .3s, color .3s;
}
.status.calm { background: var(--pulse-cpu-tint); color: var(--pulse-cpu-ink); }
.status.hog { background: var(--pulse-warn-tint); color: var(--pulse-warn-ink); }
.dot { inline-size: 6px; block-size: 6px; border-radius: 50%; flex: none; background: currentColor; animation: pulseDot 2s ease-out infinite; }
.status.calm .dot { color: var(--pulse-cpu); }
.status.hog .dot { color: var(--pulse-warn); }
.icon-btn {
  inline-size: 28px; block-size: 28px; display: grid; place-items: center; padding: 0; cursor: pointer;
  border-radius: var(--pulse-shape-button); border: 1px solid var(--pulse-sys-outline-variant);
  background: var(--pulse-sys-surface-container); color: var(--pulse-sys-on-surface-variant);
}
.icon-btn:hover { color: var(--pulse-sys-on-surface); }
.icon-btn[aria-pressed=true] { background: var(--pulse-sys-surface-container-high); border-color: var(--pulse-mem-line); color: var(--pulse-mem-ink); }
.lang-pill {
  position: relative; block-size: 28px; display: grid; grid-auto-flow: column; grid-auto-columns: minmax(0, 1fr);
  padding: 2px; border-radius: var(--pulse-shape-pill); border: 1px solid var(--pulse-sys-outline-variant);
  background: var(--pulse-sys-track); cursor: pointer;
}
.lang-pill .thumb {
  position: absolute; inset-block: 2px; border-radius: var(--pulse-shape-pill);
  background: var(--pulse-sys-segment); box-shadow: var(--pulse-sys-segment-shadow);
  transition: inset-inline-start .28s cubic-bezier(.2,.8,.2,1);
}
.lang-pill .opt { position: relative; display: grid; place-items: center; font-weight: 700; font-size: 12px; line-height: 1; }
.lang-menu-wrap { position: relative; }
.lang-trigger {
  block-size: 28px; display: inline-flex; align-items: center; gap: 4px; padding-inline: 9px 7px; cursor: pointer;
  border-radius: var(--pulse-shape-button); border: 1px solid var(--pulse-sys-outline-variant);
  background: var(--pulse-sys-surface-container); font-weight: 700; font-size: 12px; line-height: 1;
}
.lang-menu {
  position: absolute; inset-block-start: calc(100% + 4px); inset-inline-end: 0; z-index: 8; min-inline-size: 168px;
  max-block-size: 264px; overflow-y: auto; padding: 4px; display: flex; flex-direction: column; gap: 1px;
  border-radius: var(--pulse-shape-tip); background: var(--pulse-sys-tooltip);
  border: 1px solid var(--pulse-sys-tooltip-outline); box-shadow: var(--pulse-sys-tooltip-shadow);
  backdrop-filter: blur(20px) saturate(180%);
}
.lang-item {
  inline-size: 100%; block-size: 30px; display: flex; align-items: center; gap: 8px; padding: 0 8px; border: 0;
  border-radius: var(--pulse-shape-row); background: transparent; cursor: pointer; text-align: start; font-size: 12.5px;
}
.lang-item:hover, .lang-item[aria-checked=true] { background: var(--pulse-sys-hover); }
.lang-item .code { min-inline-size: 24px; font-weight: 700; }
.lang-item .icon { color: var(--pulse-cpu-ink); }
.lang-item[aria-checked=false] .icon { visibility: hidden; }

/* Banners --------------------------------------------------------------- */
.banner { display: flex; align-items: center; gap: 10px; padding: 6px 8px; margin-block-end: 6px; border-radius: var(--pulse-shape-card); }
.banner.hog {
  background: linear-gradient(calc(90deg * var(--flip)), color-mix(in srgb, var(--pulse-warn) 13%, transparent), color-mix(in srgb, var(--pulse-crit) 8%, transparent));
  border: 1px solid var(--pulse-warn-line); box-shadow: var(--pulse-sys-elevation-1);
}
.badge { inline-size: 28px; block-size: 28px; border-radius: var(--pulse-shape-badge); display: grid; place-items: center; flex: none; }
.banner.hog .badge { background: var(--pulse-warn-tint); color: var(--pulse-warn-ink); }
.banner .text { flex: 1; min-inline-size: 0; }
.banner .title { font-size: 12.5px; font-weight: 700; text-align: start; }
.banner .sub { font-size: 11px; margin-block-start: 1px; text-align: start; }
.btn {
  block-size: 28px; padding: 0 11px; border-radius: var(--pulse-shape-button); cursor: pointer; white-space: nowrap; flex: none;
  font-size: 12px; font-weight: 700; border: 1px solid var(--pulse-sys-outline-variant); background: var(--pulse-sys-surface-container);
}
.btn:hover { background: var(--pulse-sys-surface-container-high); }
.btn.warn { border-color: var(--pulse-warn-line); background: transparent; color: var(--pulse-warn-ink); }
.btn.warn:hover { background: var(--pulse-warn-tint); }
.btn.danger-outline { border-color: var(--pulse-thm-line); background: transparent; color: var(--pulse-thm-ink); }
.btn.danger-outline:hover { background: var(--pulse-thm-tint); }
.btn.danger { border: 0; background: var(--pulse-sys-danger); color: var(--pulse-sys-on-danger); }
.btn.tall { block-size: 30px; padding: 0 14px; }
.ghost { inline-size: 28px; block-size: 28px; display: grid; place-items: center; border: 0; padding: 0; background: transparent; cursor: pointer; border-radius: var(--pulse-shape-button); flex: none; color: var(--pulse-sys-on-surface-variant); }
.ghost:hover { color: var(--pulse-sys-on-surface); background: var(--pulse-sys-hover); }
.dialog {
  display: flex; flex-direction: column; gap: 10px; padding: 12px; margin-block-end: 6px; border-radius: var(--pulse-shape-card);
  background: var(--pulse-sys-surface-container); border: 1px solid var(--pulse-crit-line); box-shadow: var(--pulse-sys-elevation-2);
}
.dialog .row { display: flex; align-items: flex-start; gap: 10px; }
.dialog .badge { background: var(--pulse-thm-tint); color: var(--pulse-thm-ink); }
.dialog .title { font-size: 13px; font-weight: 700; text-align: start; }
.dialog .sub { font-size: 11.5px; margin-block-start: 3px; }
.dialog .actions { display: flex; justify-content: flex-end; gap: 8px; }
.toast {
  display: flex; align-items: center; gap: 9px; padding: 9px 12px; margin-block-end: 6px; border-radius: var(--pulse-shape-card);
  background: var(--pulse-sys-surface-container); border: 1px solid var(--pulse-cpu-line); box-shadow: var(--pulse-sys-elevation-1);
}
.toast .tick { inline-size: 22px; block-size: 22px; border-radius: 50%; display: grid; place-items: center; flex: none; background: var(--pulse-cpu-tint); color: var(--pulse-cpu-ink); }
.toast .msg { font-size: 12px; font-weight: 600; flex: 1; min-inline-size: 0; text-align: start; }

/* Cards ----------------------------------------------------------------- */
.grid2 { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 6px; }
.card {
  background: var(--pulse-sys-surface-container); border: 1px solid var(--pulse-sys-outline-variant);
  box-shadow: var(--pulse-sys-elevation-1); border-radius: var(--pulse-shape-card); min-inline-size: 0;
  transition: background .2s, border-color .2s, transform .2s, box-shadow .2s;
}
.card.tap { cursor: pointer; }
.card.tap:hover { background: var(--pulse-sys-surface-container-high); transform: translateY(-1px); box-shadow: var(--pulse-sys-elevation-2); }
.card.tap:focus-visible { outline: 2px solid var(--pulse-focus); outline-offset: 2px; }
.metric { padding: 9px 11px 6px; display: flex; flex-direction: column; gap: 6px; }
.metric.cpu:hover { border-color: var(--pulse-cpu-line); }
.metric.mem:hover { border-color: var(--pulse-mem-line); }
.metric.nrg:hover { border-color: var(--pulse-nrg-line); }
.metric.high { border-color: var(--pulse-warn-line); }
.card-head { display: flex; align-items: center; gap: 8px; min-inline-size: 0; }
.card-head .label { font-size: 12.5px; font-weight: 600; color: var(--pulse-sys-on-surface-variant); flex: 1; }
.card-head .chev { color: var(--pulse-sys-on-surface-subtle); }
.chip-icon { inline-size: 26px; block-size: 26px; border-radius: var(--pulse-shape-badge); display: grid; place-items: center; flex: none; }
.chip-icon.sm { inline-size: 20px; block-size: 20px; }
.t-cpu { background: var(--pulse-cpu-tint); color: var(--pulse-cpu-ink); }
.t-mem { background: var(--pulse-mem-tint); color: var(--pulse-mem-ink); }
.t-nrg { background: var(--pulse-nrg-tint); color: var(--pulse-nrg-ink); }
.t-thm { background: var(--pulse-thm-tint); color: var(--pulse-thm-ink); }
.t-gpu { background: var(--pulse-gpu-tint); color: var(--pulse-gpu-ink); }
.t-warn { background: var(--pulse-warn-tint); color: var(--pulse-warn-ink); }
.t-crit { background: var(--pulse-crit-tint); color: var(--pulse-crit-ink); }
.high-chip { block-size: 20px; display: inline-flex; align-items: center; padding: 0 7px; border-radius: var(--pulse-shape-pill); font-size: 11px; font-weight: 700; white-space: nowrap; background: var(--pulse-warn-tint); color: var(--pulse-warn-ink); }
.hero { font: 700 28px/1 var(--pulse-font-hero); letter-spacing: -0.02em; }
.hero.warn { color: var(--pulse-warn-ink); }
.sub { font-size: 11px; color: var(--pulse-sys-on-surface-variant); margin-block-start: 5px; }
.bars { display: flex; flex-direction: column; gap: 3px; }
.bar-row { display: grid; grid-template-columns: 68px minmax(0, 1fr) 34px; align-items: center; gap: 6px; font-size: 11px; }
.bar-row .v { text-align: end; font-weight: 600; }
.track { block-size: 4px; border-radius: 99px; background: var(--pulse-sys-track); overflow: hidden; display: block; }
.track > span { display: block; block-size: 100%; border-radius: 99px; transition: inline-size .5s ease; }
.pressure { display: inline-flex; align-items: center; gap: 5px; block-size: 20px; min-inline-size: 0; padding: 0 8px; border-radius: var(--pulse-shape-pill); font-size: 11px; font-weight: 700; }
.pressure.ok { background: var(--pulse-cpu-tint); color: var(--pulse-cpu-ink); }
.pressure.ok .pdot { background: var(--pulse-cpu); }
.pressure.bad { background: var(--pulse-warn-tint); color: var(--pulse-warn-ink); }
.pressure.bad .pdot { background: var(--pulse-warn); }
.pressure.crit { background: var(--pulse-crit-tint); color: var(--pulse-crit-ink); }
.pressure.crit .pdot { background: var(--pulse-crit); }
.hero.nobat { font-size: 22px; line-height: 34px; }
.toast.error { border-color: var(--pulse-warn-line); }
.toast.error .tick { background: var(--pulse-warn-tint); color: var(--pulse-warn-ink); }
.select-wrap { position: relative; flex: none; display: flex; align-items: center; }
.lang-select {
  appearance: none; block-size: 30px; min-inline-size: 132px; max-inline-size: 170px; padding-block: 0; padding-inline: 12px 28px;
  border-radius: var(--pulse-shape-pill); border: 1px solid var(--pulse-sys-outline-variant); background: var(--pulse-sys-surface-container);
  color: var(--pulse-sys-on-surface); font: 600 12px var(--pulse-font-text); cursor: pointer; text-overflow: ellipsis;
}
.lang-select:focus-visible { outline: 2px solid var(--pulse-mem); outline-offset: 2px; }
.select-wrap .icon { position: absolute; inset-inline-end: 11px; pointer-events: none; color: var(--pulse-sys-on-surface-variant); }
.lang-select option { color: initial; background: initial; }
.pdot { inline-size: 6px; block-size: 6px; border-radius: 50%; flex: none; }
.stack { display: flex; block-size: 8px; border-radius: var(--pulse-shape-pill); overflow: hidden; background: var(--pulse-sys-track); gap: 2px; }
.stack span { transition: inline-size .5s ease; }
.seg-app { background: var(--pulse-mem); }
.seg-sys { background: var(--pulse-mem-soft); }
.seg-zram { background: repeating-linear-gradient(135deg, var(--pulse-mem) 0 2px, var(--pulse-mem-tint2) 2px 4px); }
.seg-free { background: var(--pulse-sys-track-strong); }
.legend { display: flex; flex-wrap: wrap; gap: 1px 10px; font-size: 11px; line-height: 1.25; color: var(--pulse-sys-on-surface-variant); }
.legend > span { display: inline-flex; align-items: center; gap: 4px; white-space: nowrap; }
.legend i { inline-size: 7px; block-size: 7px; border-radius: 2px; display: inline-block; flex: none; }
.flow { display: flex; align-items: center; gap: 7px; flex-wrap: wrap; }
.flow-chip {
  display: inline-flex; align-items: center; gap: 5px; block-size: 24px; padding: 0 9px; border-radius: var(--pulse-shape-pill);
  background: var(--pulse-nrg-tint); color: var(--pulse-nrg-ink); border: 1px solid var(--pulse-nrg-line);
  font: 700 12px var(--pulse-font-hero); white-space: nowrap;
}
.flow-chip .dot-live { inline-size: 6px; block-size: 6px; border-radius: 50%; color: var(--pulse-nrg); background: currentColor; animation: pulseDot 1.6s ease-out infinite; }
.hero.stage { font-weight: 700; }
.meter { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 3px; }
.meter span { block-size: 6px; border-radius: var(--pulse-shape-pill); background: var(--pulse-sys-track); transition: background .4s; }
.meter span.on { background: var(--stage); }
.stages { display: flex; justify-content: space-between; gap: 4px; font-size: 11px; min-inline-size: 0; }
.stages span { color: var(--pulse-sys-on-surface-subtle); font-weight: 500; flex: 0 1 auto; }
.stages span.on { color: var(--stage-ink); font-weight: 700; }
.note { font-size: 11px; color: var(--pulse-sys-on-surface-subtle); }
.note.crit { color: var(--pulse-crit-ink); }
.spark { position: relative; margin-block-start: auto; }
.spark .tag {
  position: absolute; inset-block-start: -2px; inset-inline-start: -3px; z-index: 1; padding: 0 4px; border-radius: 3px;
  background: var(--pulse-sys-surface); font-size: 10.5px; color: var(--pulse-sys-on-surface-subtle); line-height: 1.2; white-space: nowrap;
}
.spark svg { inline-size: 100%; block-size: 18px; display: block; overflow: visible; }

/* GPU strip, storage, network ------------------------------------------ */
.strip { padding: 6px 10px; display: grid; grid-template-columns: auto minmax(0, 1fr) minmax(48px, 84px) auto auto; align-items: center; gap: 8px; margin-block: 6px; }
.strip:hover { border-color: var(--pulse-gpu-line); }
.strip .names { min-inline-size: 0; display: flex; align-items: baseline; gap: 6px; white-space: nowrap; overflow: hidden; }
.strip .label { font-size: 11.5px; font-weight: 600; color: var(--pulse-sys-on-surface-variant); flex: none; }
.strip .model { font-size: 11px; color: var(--pulse-sys-on-surface-subtle); text-align: start; }
.strip .track { block-size: 5px; }
.strip .val { min-inline-size: 32px; text-align: end; font: 700 13px/1 var(--pulse-font-hero); white-space: nowrap; }
.temp-chip { display: inline-flex; align-items: center; block-size: 18px; padding: 0 6px; border-radius: var(--pulse-shape-pill); font: 700 11px var(--pulse-font-hero); white-space: nowrap; }
.mini { padding: 8px 10px; display: flex; flex-direction: column; gap: 4px; }
.mini .top { display: flex; align-items: center; gap: 6px; min-inline-size: 0; }
.mini .name { font-size: 11.5px; font-weight: 600; color: var(--pulse-sys-on-surface-variant); flex: 1; text-align: start; }
.mini .line { display: flex; align-items: baseline; justify-content: space-between; gap: 8px; min-inline-size: 0; }
.mini .strong { font: 700 12.5px/1.1 var(--pulse-font-hero); white-space: nowrap; flex: none; }
.mini .light { font-size: 11px; color: var(--pulse-sys-on-surface-subtle); }
.mini .track { block-size: 5px; }
.ssd-fill { background: linear-gradient(calc(90deg * var(--flip)), var(--pulse-cpu), var(--pulse-nrg)); }
.rates { display: flex; align-items: baseline; gap: 4px 10px; flex-wrap: wrap; min-inline-size: 0; }
.rate-down { font: 700 13px/1.1 var(--pulse-font-hero); color: var(--pulse-mem-ink); white-space: nowrap; }
.rate-up { font: 700 12px/1.1 var(--pulse-font-hero); color: var(--pulse-cpu-ink); white-space: nowrap; }

/* Top apps -------------------------------------------------------------- */
.apps { padding: 8px 6px 4px; position: relative; margin-block-start: 6px; }
.apps-head { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 6px 8px; padding: 0 6px 4px; }
.apps-head .title { font-size: 12.5px; font-weight: 700; }
.seg {
  position: relative; display: grid; grid-auto-flow: column; grid-auto-columns: 1fr; min-inline-size: 168px; flex: none;
  block-size: 28px; padding: 2px; border-radius: var(--pulse-shape-pill); background: var(--pulse-sys-track);
}
.seg .thumb {
  position: absolute; inset-block: 2px; border-radius: var(--pulse-shape-pill); background: var(--pulse-sys-segment);
  box-shadow: var(--pulse-sys-segment-shadow); transition: inset-inline-start .28s cubic-bezier(.2,.8,.2,1);
}
.seg button { position: relative; border: 0; background: transparent; padding: 0 8px; white-space: nowrap; font-size: 11px; font-weight: 700; cursor: pointer; color: var(--pulse-sys-on-surface-variant); }
.seg button[aria-selected=true], .seg button[aria-pressed=true] { color: var(--pulse-sys-on-surface); }
.rows { position: relative; }
.row {
  position: relative; block-size: 34px; display: flex; align-items: center; gap: 8px; padding: 0 6px;
  border-radius: var(--pulse-shape-row); cursor: pointer;
}
.row:hover, .row.hover { background: var(--pulse-sys-hover); }
.row:focus-visible { outline-offset: -2px; }
.mono { inline-size: 22px; block-size: 22px; border-radius: var(--pulse-shape-mono); display: grid; place-items: center; flex: none; font: 700 11px var(--pulse-font-hero); }
.row .who { flex: 1; min-inline-size: 0; display: flex; align-items: center; gap: 6px; }
.row .name { font-size: 12.5px; font-weight: 500; }
.procs { flex: none; font: 600 11px var(--pulse-font-hero); color: var(--pulse-sys-on-surface-variant); background: var(--pulse-sys-track); border-radius: 5px; padding: 1px 5px; }
.row .track { inline-size: 56px; flex: none; }
.row .val { inline-size: 58px; flex: none; text-align: end; font: 600 12px var(--pulse-font-hero); white-space: nowrap; transition: opacity .15s; }
.row .val.hog { color: var(--pulse-warn-ink); }
.row.hover .val.can-end { opacity: 0; }
.end-btn {
  position: absolute; inset-inline-end: 6px; inset-block-start: 4px; block-size: 26px; min-inline-size: 56px; padding: 0 10px;
  border-radius: var(--pulse-shape-pill); border: 1px solid var(--pulse-thm-line); background: var(--pulse-sys-surface-container);
  color: var(--pulse-thm-ink); font-size: 11.5px; font-weight: 700; cursor: pointer; opacity: 0; pointer-events: none; transition: opacity .15s;
}
.row.hover .end-btn { opacity: 1; pointer-events: auto; }
.end-btn:hover { background: var(--pulse-thm-tint-solid); }
.tip {
  position: absolute; inset-inline-end: 10px; inline-size: 212px; padding: 10px 12px; border-radius: var(--pulse-shape-tip);
  background: var(--pulse-sys-tooltip); border: 1px solid var(--pulse-sys-tooltip-outline); box-shadow: var(--pulse-sys-tooltip-shadow);
  backdrop-filter: blur(20px) saturate(180%); z-index: 6; pointer-events: none; font-size: 11.5px;
}
.tip .tname { font-weight: 700; font-size: 12px; margin-block-end: 7px; text-align: start; }
.tip .trow { display: flex; justify-content: space-between; gap: 8px; white-space: nowrap; margin-block-start: 5px; }
.tip .tv { font: 600 11.5px var(--pulse-font-hero); }
.tip .tfoot { margin-block-start: 8px; padding-block-start: 7px; border-block-start: 1px solid var(--pulse-sys-outline); color: var(--pulse-cpu-ink); font-weight: 600; font-size: 11px; }

/* Footer ---------------------------------------------------------------- */
.foot { display: flex; align-items: center; justify-content: space-between; gap: 6px 8px; min-inline-size: 0; margin-block-start: 8px; }
.foot .open { display: inline-flex; align-items: center; gap: 6px; min-inline-size: 0; max-inline-size: 52%; flex: 0 1 auto; block-size: 30px; padding: 0 12px; font-weight: 600; }
.foot .open .icon { color: var(--pulse-sys-on-surface-variant); }
.privacy { display: inline-flex; align-items: center; gap: 5px; min-inline-size: 0; flex: 0 1 auto; font-size: 11px; color: var(--pulse-sys-on-surface-subtle); white-space: nowrap; }
.quit { block-size: 30px; padding: 0 8px; border: 0; background: transparent; border-radius: var(--pulse-shape-button); font-size: 12px; font-weight: 600; color: var(--pulse-sys-on-surface-variant); cursor: pointer; flex: none; }
.quit:hover { background: var(--pulse-sys-hover); color: var(--pulse-sys-on-surface); }

/* Detail ---------------------------------------------------------------- */
.detail { display: flex; flex-direction: column; gap: 8px; }
.detail-bar { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.back { display: inline-flex; align-items: center; gap: 3px; block-size: 30px; padding-inline: 6px 11px; font-weight: 600; }
.title-chip {
  display: inline-flex; align-items: center; gap: 6px; block-size: 30px; padding-inline: 5px 11px; border-radius: var(--pulse-shape-pill);
  background: var(--acc-tint); color: var(--acc-ink); font-size: 12px; font-weight: 700; flex: 0 1 auto; overflow: hidden; max-inline-size: 240px;
}
.title-chip .ring { inline-size: 20px; block-size: 20px; border-radius: 50%; background: var(--acc-tint); display: grid; place-items: center; flex: none; }
.title-chip .ring span { inline-size: 7px; block-size: 7px; border-radius: 50%; color: var(--acc); background: currentColor; animation: pulseDot 1.8s ease-out infinite; }
.detail-bar .end { margin-inline-start: auto; block-size: 30px; }
.hero-line { display: flex; align-items: center; gap: 4px 10px; padding: 2px 2px 0; }
.hero-line .big { flex: none; font: 700 30px/1 var(--pulse-font-hero); letter-spacing: -0.02em; white-space: nowrap; }
.hero-line .desc { flex: 1 1 140px; min-inline-size: 0; font-size: 12px; line-height: 1.3; color: var(--pulse-sys-on-surface-variant); }
.chart-card { padding: 12px; }
.chart-head { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 4px 8px; margin-block-end: 10px; }
.chart-head .h { font-size: 12px; font-weight: 600; color: var(--pulse-sys-on-surface-variant); white-space: nowrap; }
.chart-head .stat { font-size: 11px; color: var(--pulse-sys-on-surface-subtle); white-space: nowrap; }
.chart-legend { flex-basis: 100%; display: flex; gap: 12px; font-size: 11px; font-weight: 600; }
.chart-legend span { display: inline-flex; align-items: center; gap: 5px; }
.chart-legend i { inline-size: 8px; block-size: 8px; border-radius: 2px; display: inline-block; }
.chart { position: relative; block-size: 140px; cursor: crosshair; }
.chart svg { position: absolute; inset: 0; inline-size: 100%; block-size: 140px; display: block; overflow: visible; }
.scrub-line { position: absolute; inset-block: 0; inline-size: 0; border-left: 1px dashed var(--pulse-sys-on-surface-subtle); pointer-events: none; }
.scrub-dot { position: absolute; inline-size: 9px; block-size: 9px; margin: -5px 0 0 -5px; border-radius: 50%; box-shadow: 0 0 0 3px var(--pulse-sys-surface); pointer-events: none; }
.scrub-pill {
  position: absolute; inset-block-start: 2px; max-inline-size: 100%; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  padding: 5px 10px; border-radius: var(--pulse-shape-pill); background: var(--pulse-sys-tooltip); border: 1px solid var(--pulse-sys-tooltip-outline);
  box-shadow: var(--pulse-sys-tooltip-shadow); font-size: 11px; font-weight: 600; pointer-events: none;
}
.axis { display: flex; justify-content: space-between; margin-block-start: 8px; font-size: 11px; color: var(--pulse-sys-on-surface-subtle); white-space: nowrap; }
.tiles { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px; }
.tile { padding: 10px 12px; display: flex; flex-direction: column; gap: 4px; }
.tile .k { font-size: 11px; color: var(--pulse-sys-on-surface-variant); font-weight: 500; }
.tile .v { font: 700 15.5px/1.2 var(--pulse-font-hero); text-align: start; }
.tile.wide { grid-column: 1 / -1; }

/* Settings -------------------------------------------------------------- */
.settings-title { font: 700 15px var(--pulse-font-hero); }
.settings { padding: 4px 12px; }
.set-row { display: flex; align-items: center; gap: 12px; padding: 10px 0; }
.set-row + .set-row { border-block-start: 1px solid var(--pulse-sys-outline); }
.set-row .lbl { flex: 1; min-inline-size: 0; font-size: 12.5px; font-weight: 600; }
.set-text { flex: 1; min-inline-size: 0; display: flex; flex-direction: column; gap: 2px; }
.set-text .lbl { flex: none; }
.set-sub { font-size: 11px; }
.switch { position: relative; flex: none; inline-size: 38px; block-size: 22px; padding: 0; border: 0; border-radius: var(--pulse-shape-pill); background: var(--pulse-sys-track-strong); cursor: pointer; transition: background .18s; }
.switch .knob { position: absolute; inset-block-start: 3px; inset-inline-start: 3px; inline-size: 16px; block-size: 16px; border-radius: 50%; background: #fff; box-shadow: 0 1px 3px rgba(0,0,0,.25); transition: inset-inline-start .18s; }
.switch[aria-checked=true] { background: var(--pulse-cpu); }
.switch[aria-checked=true] .knob { inset-inline-start: 19px; }
.switch:disabled, .btn:disabled { opacity: .45; cursor: default; }
.btn:disabled:hover { background: var(--pulse-sys-surface-container); }
.unit { display: flex; gap: 2px; padding: 2px; border-radius: var(--pulse-shape-pill); background: var(--pulse-sys-track); }
.unit button { block-size: 26px; min-inline-size: 40px; padding: 0 10px; border: 0; border-radius: var(--pulse-shape-pill); background: transparent; font: 700 12px var(--pulse-font-hero); cursor: pointer; color: var(--pulse-sys-on-surface-variant); }
.unit button[aria-pressed=true] { background: var(--pulse-sys-segment); box-shadow: var(--pulse-sys-segment-shadow); color: var(--pulse-sys-on-surface); }
.view { animation: viewIn .28s cubic-bezier(.2,.8,.2,1); }
@keyframes viewIn { from { opacity: 0; transform: translateX(calc(16px * var(--flip))); } to { opacity: 1; transform: none; } }
`;
