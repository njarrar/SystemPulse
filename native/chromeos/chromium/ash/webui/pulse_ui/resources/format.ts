// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Number, unit and duration formatting for Pulse. Digits go
 * through Locale.num() so a locale with its own numbering system gets its
 * own digits. Unit symbols (GB, MB/s, W, °C) stay as written in every
 * language.
 */

import type {Locale} from './i18n.js';

const KB = 1024;
const MB = KB * 1024;
const GB = MB * 1024;

export class Formatter {
  constructor(readonly lc: Locale, readonly fahrenheit: boolean) {}

  num(x: number, decimals = 0): string {
    return this.lc.num(x, decimals);
  }

  pct(x: number, decimals = 0): string {
    return this.num(decimals ? x : Math.round(x), decimals) + '%';
  }

  temp(celsius: number, decimals = 0): string {
    return this.fahrenheit ? this.num(celsius * 9 / 5 + 32, decimals) + '°F' :
                             this.num(celsius, decimals) + '°C';
  }

  watts(w: number): string {
    return this.num(w, 1) + ' W';
  }

  /** "14.8W": the compact form used in the status pill. */
  wattsShort(w: number): string {
    return this.num(w, 1) + 'W';
  }

  /** Signed draw: "−14.8 W" on battery, "+48 W" while charging. */
  flow(w: number): string {
    return w >= 0 ? '+' + this.num(Math.round(w)) + ' W' :
                    '−' + this.watts(-w);
  }

  gb(bytes: number, decimals = 1): string {
    return this.num(bytes / GB, decimals) + ' GB';
  }

  /** Process memory: MB below a quarter gigabyte, else GB with 2 decimals. */
  memory(bytes: number): string {
    if (bytes < GB / 4) {
      const mb = bytes / MB;
      return this.num(mb, mb < 10 ? 1 : 0) + ' MB';
    }
    return this.num(bytes / GB, 2) + ' GB';
  }

  /** Totals: "212 MB", "1.4 GB". */
  size(bytes: number): string {
    if (bytes >= GB) {
      return this.num(bytes / GB, 1) + ' GB';
    }
    return this.num(Math.round(bytes / MB)) + ' MB';
  }

  /** Throughput: "1.9 MB/s" from one megabyte per second, else "140 KB/s". */
  rate(bytesPerSecond: number): string {
    if (bytesPerSecond >= MB) {
      return this.num(bytesPerSecond / MB, 1) + ' MB/s';
    }
    return this.num(Math.round(bytesPerSecond / KB)) + ' KB/s';
  }

  /** "2h 40m" or "52m", from the locale's durHM / durM strings. */
  duration(minutes: number): string {
    const h = Math.floor(minutes / 60);
    const m = Math.round(minutes % 60);
    return h ? this.lc.t('durHM', {h, m}) : this.lc.t('durM', {m});
  }
}

export function toNumber(x: bigint|number|null|undefined): number {
  return x == null ? 0 : Number(x);
}
