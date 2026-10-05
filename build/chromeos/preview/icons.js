// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/**
 * @fileoverview Line icons on a 24 px grid. Icons that point along the
 * reading direction (chevrons, back, external link) carry the "flip" class
 * and mirror in RTL. The rest never mirror.
 */
import { h } from './vdom.js';
const ICONS = {
    cpu: {
        rects: [[5, 5, 14, 14, 3], [9.5, 9.5, 5, 5, 1]],
        paths: ['M9 2v3M15 2v3M9 19v3M15 19v3M2 9h3M2 15h3M19 9h3M19 15h3'],
    },
    mem: {
        rects: [[3, 7, 18, 10, 2]],
        paths: ['M7.5 10.5v3M12 10.5v3M16.5 10.5v3M6 17v3M18 17v3'],
    },
    nrg: { paths: ['M13 2 4.5 14H11l-1 8 8.5-12H12l1-8z'] },
    thm: { paths: ['M14 14.76V4a2 2 0 1 0-4 0v10.76a4 4 0 1 0 4 0z'] },
    gpu: {
        rects: [[2, 6, 20, 12, 2]],
        circles: [[9, 12, 3]],
        paths: ['M15.5 10h3M15.5 14h3'],
        width: 2.2,
    },
    ssd: { rects: [[3, 13, 18, 7, 2]], paths: ['M5 13l2.5-8h9L19 13'], width: 2.2 },
    wifi: {
        paths: ['M2 8.8a15 15 0 0 1 20 0M5 12.5a10 10 0 0 1 14 0M8.5 16a5 5 0 0 1 7 0'],
        circles: [[12, 19.5, 1]],
        width: 2.4,
    },
    ethernet: {
        rects: [[4, 9, 16, 10, 2]],
        paths: ['M8 9V5h8v4M8 13v2M12 13v2M16 13v2'],
        width: 2.2,
    },
    chevron: { paths: ['m9 6 6 6-6 6'], flip: true },
    back: { paths: ['m15 6-6 6 6 6'], flip: true, width: 2.2 },
    down: { paths: ['m6 9 6 6 6-6'], width: 2.6 },
    external: {
        paths: [
            'M14 4h6v6M20 4l-9 9',
            'M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5',
        ],
        flip: true,
        width: 2.2,
    },
    lock: { rects: [[5, 11, 14, 10, 2]], paths: ['M8 11V8a4 4 0 0 1 8 0v3'], width: 2.2 },
    moon: { paths: ['M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z'] },
    sun: {
        circles: [[12, 12, 4]],
        paths: ['M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4'],
    },
    tune: {
        paths: ['M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1'],
        circles: [[15, 6, 2], [9, 12, 2], [17, 18, 2]],
    },
    warn: { paths: ['M12 3.5 2.5 20h19L12 3.5z', 'M12 10v4.5M12 17.2v.3'] },
    close: { paths: ['M6 6l12 12M18 6 6 18'], width: 2.4 },
    power: { paths: ['M12 3v9M6.3 6.3a8 8 0 1 0 11.4 0'], width: 2.2 },
    check: { paths: ['m5 12 5 5 9-10'], width: 3 },
};
export function icon(name, size = 15, extraClass = '') {
    const spec = ICONS[name];
    if (!spec) {
        throw new Error('Unknown icon ' + name);
    }
    const cls = ['icon', spec.flip ? 'flip' : '', extraClass].filter(Boolean);
    return h('svg', {
        'class': cls.join(' '),
        'width': size,
        'height': size,
        'viewBox': '0 0 24 24',
        'fill': 'none',
        'stroke': 'currentColor',
        'stroke-width': spec.width || 2,
        'stroke-linecap': 'round',
        'stroke-linejoin': 'round',
        'aria-hidden': 'true',
        'focusable': 'false',
    }, (spec.rects || [])
        .map(([x, y, w, ht, r]) => h('rect', { x, y, width: w, height: ht, rx: r })), (spec.circles || []).map(([cx, cy, r]) => h('circle', { cx, cy, r })), spec.paths.map(d => h('path', { d })));
}
