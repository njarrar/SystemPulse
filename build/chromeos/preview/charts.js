// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/** Catmull-Rom spline through `p`, as an SVG path. */
export function smooth(p) {
    if (!p.length) {
        return '';
    }
    const f = (x) => x.toFixed(1);
    let d = `M${f(p[0][0])},${f(p[0][1])}`;
    for (let i = 0; i < p.length - 1; i++) {
        const p0 = p[i - 1] || p[i];
        const p1 = p[i];
        const p2 = p[i + 1];
        const p3 = p[i + 2] || p2;
        d += ` C${f(p1[0] + (p2[0] - p0[0]) / 6)},${f(p1[1] + (p2[1] - p0[1]) / 6)} ${f(p2[0] - (p3[0] - p1[0]) / 6)},${f(p2[1] - (p3[1] - p1[1]) / 6)} ${f(p2[0])},${f(p2[1])}`;
    }
    return d;
}
/** Card sparkline in a 172 x 28 box, auto-scaled with some headroom. */
export function spark(vals, w = 172, h = 28) {
    if (vals.length < 2) {
        return { line: '', area: '' };
    }
    const n = vals.length;
    const mn = Math.min(...vals);
    const mx = Math.max(...vals);
    const rg = Math.max(mx - mn, 0.5);
    const lo = mn - rg * 0.25;
    const hi = mx + rg * 0.35;
    const pts = vals.map((v, i) => [i / (n - 1) * w, h - 2 - (v - lo) / (hi - lo) * (h - 4)]);
    const line = smooth(pts);
    return { line, area: `${line} L${w},${h} L0,${h} Z` };
}
export const CHART_W = 372;
export const CHART_H = 140;
/**
 * One series fills from the bottom. Two series (read/write, down/up) mirror
 * around the middle line: the first above, the second below.
 */
export function chart(a, b, zero) {
    const W = CHART_W;
    const H = CHART_H;
    const n = Math.max(a.length, 2);
    if (!b) {
        const mx = Math.max(...a, 0.001);
        const mn = Math.min(...a);
        const lo = zero ? 0 : Math.max(0, mn - (mx - mn) * 0.8);
        const hi = mx + (mx - lo) * 0.2 || 1;
        const pa = a.map((v, i) => [i / (n - 1) * W, H - 6 - (v - lo) / (hi - lo) * (H - 34)]);
        const lineA = smooth(pa);
        return {
            lineA,
            areaA: lineA ? `${lineA} L${W},${H} L0,${H} Z` : '',
            lineB: '',
            areaB: '',
            pa,
            pb: null,
        };
    }
    const c = H / 2;
    const ma = Math.max(...a, 0.001) * 1.12;
    const mb = Math.max(...b, 0.001) * 1.12;
    const pa = a.map((v, i) => [i / (n - 1) * W, c - 1 - (v / ma) * (c - 22)]);
    const pb = b.map((v, i) => [i / (n - 1) * W, c + 1 + (v / mb) * (c - 22)]);
    const lineA = smooth(pa);
    const lineB = smooth(pb);
    return {
        lineA,
        areaA: `${lineA} L${W},${c} L0,${c} Z`,
        lineB,
        areaB: `${lineB} L${W},${c} L0,${c} Z`,
        pa,
        pb,
    };
}
/** Resamples `vals` to `count` points by linear interpolation. */
export function resample(vals, count) {
    if (vals.length === 0) {
        return [];
    }
    if (vals.length === 1) {
        return new Array(count).fill(vals[0]);
    }
    const out = [];
    for (let i = 0; i < count; i++) {
        const x = i / (count - 1) * (vals.length - 1);
        const lo = Math.floor(x);
        const hi = Math.min(vals.length - 1, lo + 1);
        out.push(vals[lo] + (vals[hi] - vals[lo]) * (x - lo));
    }
    return out;
}
