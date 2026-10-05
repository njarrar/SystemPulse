// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
/**
 * @fileoverview A small virtual DOM for the Pulse page. Views return trees
 * of VNodes; patch() updates the real DOM in place, so focus, hover and
 * running transitions survive the 1.5 s refresh. It only uses
 * createElement, setAttribute and textContent, so it works under the
 * Trusted Types policy WebUI enforces (no innerHTML anywhere).
 */
const SVG_NS = 'http://www.w3.org/2000/svg';
const SVG_TAGS = new Set([
    'svg', 'path', 'g', 'line', 'rect', 'circle', 'polyline', 'defs',
    'linearGradient', 'stop', 'pattern', 'clipPath',
]);
const TEXT = '#text';
function normalize(children) {
    const out = [];
    for (const c of children.flat(Infinity)) {
        if (c === null || c === undefined || c === false) {
            continue;
        }
        if (typeof c === 'string' || typeof c === 'number') {
            out.push({ tag: TEXT, props: {}, children: [], text: String(c) });
        }
        else {
            out.push(c);
        }
    }
    return out;
}
/** Builds a VNode. `h('div', {class: 'x'}, 'text', h('span'))`. */
export function h(tag, props, ...children) {
    return { tag, props: props || {}, children: normalize(children) };
}
function create(v, svg) {
    if (v.tag === TEXT) {
        return document.createTextNode(v.text || '');
    }
    const isSvg = svg || SVG_TAGS.has(v.tag);
    const el = isSvg ? document.createElementNS(SVG_NS, v.tag) :
        document.createElement(v.tag);
    update(el, null, v, isSvg);
    return el;
}
function setProp(el, name, value) {
    if (name === 'key') {
        return;
    }
    if (name === 'value' && el instanceof HTMLInputElement) {
        el.value = String(value ?? '');
        return;
    }
    if (value === false || value === null || value === undefined) {
        el.removeAttribute(name);
    }
    else {
        el.setAttribute(name, value === true ? '' : String(value));
    }
}
function update(el, old, v, svg) {
    const b = el;
    const oldProps = old ? old.props : {};
    for (const name of Object.keys(oldProps)) {
        if (name !== 'style' && name !== 'on' && !(name in v.props)) {
            el.removeAttribute(name);
        }
    }
    for (const [name, value] of Object.entries(v.props)) {
        if (name === 'style' || name === 'on') {
            continue;
        }
        if (oldProps[name] !== value) {
            setProp(el, name, value);
        }
    }
    const style = el.style;
    const os = oldProps.style || {};
    const ns = v.props.style || {};
    for (const k of Object.keys(os)) {
        if (!(k in ns)) {
            style.removeProperty(k);
        }
    }
    for (const [k, val] of Object.entries(ns)) {
        if (os[k] !== val) {
            if (val === null || val === undefined) {
                style.removeProperty(k);
            }
            else {
                style.setProperty(k, String(val));
            }
        }
    }
    // Listeners go through one stable wrapper per event type, so a new closure
    // on each render costs nothing.
    b.__on = v.props.on || {};
    b.__listeners = b.__listeners || {};
    for (const type of Object.keys(b.__on)) {
        if (!b.__listeners[type]) {
            const listener = (e) => {
                const fn = b.__on && b.__on[type];
                if (fn) {
                    fn(e);
                }
            };
            b.__listeners[type] = listener;
            el.addEventListener(type, listener);
        }
    }
    patchChildren(el, old ? old.children : [], v.children, svg && v.tag !== 'foreignObject');
    b.__vnode = v;
}
function sameKind(a, b) {
    return a.tag === b.tag && a.props.key === b.props.key;
}
function patchChildren(parent, oldKids, newKids, svg) {
    const nodes = Array.from(parent.childNodes);
    for (let i = 0; i < newKids.length; i++) {
        const v = newKids[i];
        const node = nodes[i];
        const prev = node && node.__vnode ? node.__vnode :
            node ? oldKids[i] : undefined;
        if (node && prev && sameKind(prev, v)) {
            if (v.tag === TEXT) {
                if (node.textContent !== v.text) {
                    node.textContent = v.text || '';
                }
                node.__vnode = v;
            }
            else {
                update(node, prev, v, svg);
            }
            continue;
        }
        const fresh = create(v, svg);
        if (v.tag === TEXT) {
            fresh.__vnode = v;
        }
        if (node) {
            parent.replaceChild(fresh, node);
        }
        else {
            parent.appendChild(fresh);
        }
    }
    for (let i = nodes.length - 1; i >= newKids.length; i--) {
        parent.removeChild(nodes[i]);
    }
}
/** Renders `tree` into `root`, reusing the DOM from the previous call. */
export function patch(root, tree) {
    const b = root;
    const old = b.__vnode ? b.__vnode.children : [];
    const kids = normalize(tree);
    patchChildren(root, old, kids, false);
    b.__vnode = { tag: '#root', props: {}, children: kids };
}
