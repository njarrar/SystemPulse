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

export type Child = VNode|string|number|null|undefined|false;
export type Handler = (e: Event) => void;

export interface Props {
  [name: string]: unknown;
  // Applied with style.setProperty, so CSP style-src never applies.
  style?: Record<string, string|number|null|undefined>;
  // Event listeners, keyed by event type ("click", "keydown").
  on?: Record<string, Handler>;
  // Child identity across renders when the list reorders.
  key?: string;
}

export interface VNode {
  tag: string;
  props: Props;
  children: VNode[];
  text?: string;
}

const TEXT = '#text';

function normalize(children: Child[]): VNode[] {
  const out: VNode[] = [];
  for (const c of children.flat(Infinity as 1) as Child[]) {
    if (c === null || c === undefined || c === false) {
      continue;
    }
    if (typeof c === 'string' || typeof c === 'number') {
      out.push({tag: TEXT, props: {}, children: [], text: String(c)});
    } else {
      out.push(c);
    }
  }
  return out;
}

/** Builds a VNode. `h('div', {class: 'x'}, 'text', h('span'))`. */
export function h(
    tag: string, props?: Props|null, ...children: Array<Child|Child[]>):
    VNode {
  return {tag, props: props || {}, children: normalize(children as Child[])};
}

interface Bound extends Node {
  __vnode?: VNode;
  __on?: Record<string, Handler>;
  __listeners?: Record<string, EventListener>;
}

function create(v: VNode, svg: boolean): Node {
  if (v.tag === TEXT) {
    return document.createTextNode(v.text || '');
  }
  const isSvg = svg || SVG_TAGS.has(v.tag);
  const el = isSvg ? document.createElementNS(SVG_NS, v.tag) :
                     document.createElement(v.tag);
  update(el, null, v, isSvg);
  return el;
}

function setProp(el: Element, name: string, value: unknown) {
  if (name === 'key') {
    return;
  }
  if (name === 'value' && el instanceof HTMLInputElement) {
    el.value = String(value ?? '');
    return;
  }
  if (value === false || value === null || value === undefined) {
    el.removeAttribute(name);
  } else {
    el.setAttribute(name, value === true ? '' : String(value));
  }
}

function update(el: Element, old: VNode|null, v: VNode, svg: boolean) {
  const b = el as unknown as Bound;
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
  const style = (el as HTMLElement | SVGElement).style;
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
      } else {
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
      const listener = (e: Event) => {
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

function sameKind(a: VNode, b: VNode): boolean {
  return a.tag === b.tag && a.props.key === b.props.key;
}

function patchChildren(
    parent: Node, oldKids: VNode[], newKids: VNode[], svg: boolean) {
  const nodes = Array.from(parent.childNodes) as Bound[];
  for (let i = 0; i < newKids.length; i++) {
    const v = newKids[i]!;
    const node = nodes[i];
    const prev = node && node.__vnode ? node.__vnode :
        node ? oldKids[i] : undefined;
    if (node && prev && sameKind(prev, v)) {
      if (v.tag === TEXT) {
        if (node.textContent !== v.text) {
          node.textContent = v.text || '';
        }
        (node as Bound).__vnode = v;
      } else {
        update(node as unknown as Element, prev, v, svg);
      }
      continue;
    }
    const fresh = create(v, svg) as Bound;
    if (v.tag === TEXT) {
      fresh.__vnode = v;
    }
    if (node) {
      parent.replaceChild(fresh, node);
    } else {
      parent.appendChild(fresh);
    }
  }
  for (let i = nodes.length - 1; i >= newKids.length; i--) {
    parent.removeChild(nodes[i]!);
  }
}

/** Renders `tree` into `root`, reusing the DOM from the previous call. */
export function patch(root: Node, tree: Child[]) {
  const b = root as Bound;
  const old = b.__vnode ? b.__vnode.children : [];
  const kids = normalize(tree);
  patchChildren(root, old, kids, false);
  b.__vnode = {tag: '#root', props: {}, children: kids};
}
