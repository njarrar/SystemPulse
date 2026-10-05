// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
const KB = 1024;
const MB = KB * 1024;
const GB = MB * 1024;
export class Formatter {
    lc;
    fahrenheit;
    constructor(lc, fahrenheit) {
        this.lc = lc;
        this.fahrenheit = fahrenheit;
    }
    num(x, decimals = 0) {
        return this.lc.num(x, decimals);
    }
    pct(x, decimals = 0) {
        return this.num(decimals ? x : Math.round(x), decimals) + '%';
    }
    temp(celsius, decimals = 0) {
        return this.fahrenheit ? this.num(celsius * 9 / 5 + 32, decimals) + '°F' :
            this.num(celsius, decimals) + '°C';
    }
    watts(w) {
        return this.num(w, 1) + ' W';
    }
    /** "14.8W": the compact form used in the status pill. */
    wattsShort(w) {
        return this.num(w, 1) + 'W';
    }
    /** Signed draw: "−14.8 W" on battery, "+48 W" while charging. */
    flow(w) {
        return w >= 0 ? '+' + this.num(Math.round(w)) + ' W' :
            '−' + this.watts(-w);
    }
    gb(bytes, decimals = 1) {
        return this.num(bytes / GB, decimals) + ' GB';
    }
    /** Process memory: MB below a quarter gigabyte, else GB with 2 decimals. */
    memory(bytes) {
        if (bytes < GB / 4) {
            const mb = bytes / MB;
            return this.num(mb, mb < 10 ? 1 : 0) + ' MB';
        }
        return this.num(bytes / GB, 2) + ' GB';
    }
    /** Totals: "212 MB", "1.4 GB". */
    size(bytes) {
        if (bytes >= GB) {
            return this.num(bytes / GB, 1) + ' GB';
        }
        return this.num(Math.round(bytes / MB)) + ' MB';
    }
    /** Throughput: "1.9 MB/s" from one megabyte per second, else "140 KB/s". */
    rate(bytesPerSecond) {
        if (bytesPerSecond >= MB) {
            return this.num(bytesPerSecond / MB, 1) + ' MB/s';
        }
        return this.num(Math.round(bytesPerSecond / KB)) + ' KB/s';
    }
    /** "2h 40m" or "52m", from the locale's durHM / durM strings. */
    duration(minutes) {
        const h = Math.floor(minutes / 60);
        const m = Math.round(minutes % 60);
        return h ? this.lc.t('durHM', { h, m }) : this.lc.t('durM', { m });
    }
}
export function toNumber(x) {
    return x == null ? 0 : Number(x);
}
