// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Pulse locale engine. A TypeScript port of pulse-i18n.js:
 * CLDR plural rules read from each locale file, exact "=N" forms first,
 * {placeholder} fill with FSI/PDI isolation in RTL locales, English fallback
 * then the key, and "ar-EG" resolving to "ar". UI code asks for keys and
 * never branches on a language code.
 */

export const CATEGORIES =
    ['zero', 'one', 'two', 'few', 'many', 'other'] as const;
export type PluralCategory = typeof CATEGORIES[number];

const FSI = '⁨';
const PDI = '⁩';
const RTL_CHARS = /[֐-ࣿיִ-﷿ﹰ-﻿]/;

export interface LocaleFonts {
  ui?: string;
  hero?: string;
  platforms?: Record<string, {ui?: string, hero?: string}>;
}

export interface LocaleData {
  code: string;
  label?: string;
  name?: string;
  dir?: 'ltr'|'rtl';
  order?: number;
  fallback?: string;
  numberingSystem?: string;
  fonts?: LocaleFonts;
  pluralRules?: Partial<Record<PluralCategory, string>>;
  plurals?: Record<string, Record<string, string>>;
  strings?: Record<string, string>;
  hardware?: Record<string, string>;
  apps?: Record<string, string>;
}

export interface Catalog {
  base: string;
  locales: LocaleData[];
}

export type Params = Record<string, string|number>;

// ---------------------------------------------------------------------------
// CLDR plural operands (UTS #35, "Operands").
// ---------------------------------------------------------------------------

export interface Operands {
  n: number;
  i: number;
  v: number;
  w: number;
  f: number;
  t: number;
  c: number;
  e: number;
}

function shiftDecimal(s: string, e: number): string {
  const dot = s.indexOf('.');
  let digits = s.replace('.', '');
  const pos = (dot < 0 ? s.length : dot) + e;
  while (digits.length < pos) {
    digits += '0';
  }
  return pos >= digits.length ? digits :
                                digits.slice(0, pos) + '.' + digits.slice(pos);
}

/**
 * `value` may be a number or a decimal string. `decimals` fixes the visible
 * fraction digits ("1.0" is not "1" in most languages).
 */
export function operands(value: number|string, decimals?: number): Operands {
  let s: string;
  if (typeof value === 'string') {
    s = value.trim();
  } else if (decimals != null) {
    s = Math.abs(value).toFixed(decimals);
  } else {
    s = String(Math.abs(value));
  }
  s = s.replace(/^[-+]/, '');
  let e = 0;
  const m = /^(.*?)[ce](\d+)$/i.exec(s);
  if (m) {
    e = parseInt(m[2]!, 10);
    s = shiftDecimal(m[1]!, e);
  }
  const dot = s.indexOf('.');
  const intPart = dot < 0 ? s : s.slice(0, dot);
  const frac = dot < 0 ? '' : s.slice(dot + 1);
  const trimmed = frac.replace(/0+$/, '');
  return {
    n: Math.abs(parseFloat(s)),
    i: parseInt(intPart || '0', 10),
    v: frac.length,
    w: trimmed.length,
    f: frac ? parseInt(frac, 10) : 0,
    t: trimmed ? parseInt(trimmed, 10) : 0,
    c: e,
    e: e,
  };
}

// ---------------------------------------------------------------------------
// CLDR plural rule compiler
//   condition     = and_condition ('or' and_condition)*
//   and_condition = relation ('and' relation)*
//   relation      = expr ('=' | '!=') range_list
//                 | expr 'is' 'not'? value
//                 | expr 'not'? ('in' | 'within') range_list
//   expr          = operand ('%' | 'mod' value)?
//   range_list    = (value | value '..' value) (',' range_list)*
// Sample lists ("@integer ...", "@decimal ...") are ignored.
// ---------------------------------------------------------------------------

type Test = (o: Operands) => boolean;

function tokenize(src: string): string[] {
  const out: string[] = [];
  const re = /\s*(\.\.|!=|=|%|,|[a-z]+|\d+(?:\.\d+)?)\s*/y;
  src = src.replace(/@.*$/, '').trim();
  while (re.lastIndex < src.length) {
    const m = re.exec(src);
    if (!m) {
      throw new Error('Bad plural rule near: ' + src.slice(re.lastIndex));
    }
    out.push(m[1]!);
  }
  return out;
}

export function compileRule(src: string): Test {
  const tk = tokenize(src);
  let p = 0;
  if (!tk.length) {
    return () => true;
  }
  const peek = () => tk[p];
  const next = () => tk[p++];
  const num = (): number => {
    const x = next();
    if (!/^\d/.test(x || '')) {
      throw new Error('Expected number in plural rule: ' + src);
    }
    return parseFloat(x!);
  };
  const expr = (): (o: Operands) => number => {
    const op = next();
    if (!/^[nivwftce]$/.test(op || '')) {
      throw new Error(`Unknown operand "${op}" in plural rule: ${src}`);
    }
    let mod: number|null = null;
    if (peek() === '%' || peek() === 'mod') {
      next();
      mod = num();
    }
    const key = op as keyof Operands;
    return (o) => mod == null ? o[key] : o[key] % mod;
  };
  const rangeList = (): Array<[number, number]> => {
    const ranges: Array<[number, number]> = [];
    do {
      const lo = num();
      let hi = lo;
      if (peek() === '..') {
        next();
        hi = num();
      }
      ranges.push([lo, hi]);
    } while (peek() === ',' && next());
    return ranges;
  };
  const relation = (): Test => {
    const e = expr();
    let neg = false;
    let integerOnly = true;
    let ranges: Array<[number, number]>;
    let op = next();
    if (op === 'is') {
      if (peek() === 'not') {
        next();
        neg = true;
      }
      const v = num();
      ranges = [[v, v]];
    } else if (op === '=' || op === '!=') {
      neg = op === '!=';
      ranges = rangeList();
    } else {
      if (op === 'not') {
        neg = true;
        op = next();
      }
      if (op === 'within') {
        integerOnly = false;
      } else if (op !== 'in') {
        throw new Error(`Unknown relation "${op}" in plural rule: ${src}`);
      }
      ranges = rangeList();
    }
    return (o) => {
      const x = e(o);
      const hit = ranges.some(
          ([lo, hi]) =>
              x >= lo && x <= hi && (!integerOnly || x === Math.floor(x)));
      return neg ? !hit : hit;
    };
  };
  const andCond = (): Test => {
    const rs = [relation()];
    while (peek() === 'and') {
      next();
      rs.push(relation());
    }
    return (o) => rs.every(r => r(o));
  };
  const ors = [andCond()];
  while (peek() === 'or') {
    next();
    ors.push(andCond());
  }
  if (p !== tk.length) {
    throw new Error(`Unexpected "${tk[p]}" in plural rule: ${src}`);
  }
  return (o) => ors.some(r => r(o));
}

export type PluralSelect = (value: number|string, decimals?: number) =>
    PluralCategory;

/**
 * Uses the locale file's CLDR rules; falls back to Intl.PluralRules, then to
 * one/other.
 */
export function pluralSelector(
    code: string, rules?: Partial<Record<PluralCategory, string>>):
    PluralSelect {
  if (rules && Object.keys(rules).length) {
    const compiled: Array<[PluralCategory, Test]> =
        CATEGORIES.filter(c => c !== 'other' && rules[c])
            .map(c => [c, compileRule(rules[c]!)]);
    return (value, decimals) => {
      const o = operands(value, decimals);
      for (const [cat, test] of compiled) {
        if (test(o)) {
          return cat;
        }
      }
      return 'other';
    };
  }
  if (typeof Intl !== 'undefined' && Intl.PluralRules) {
    const pr: Record<number, Intl.PluralRules> = {};
    return (value, decimals) => {
      const d = decimals == null ? 0 : decimals;
      pr[d] = pr[d] ||
          new Intl.PluralRules(
                  code, {minimumFractionDigits: d, maximumFractionDigits: d});
      return pr[d]!.select(
                 typeof value === 'string' ? parseFloat(value) : value) as
          PluralCategory;
    };
  }
  return (value) => Number(value) === 1 ? 'one' : 'other';
}

export function isolate(s: string): string {
  return FSI + s + PDI;
}

// ---------------------------------------------------------------------------
// Locale
// ---------------------------------------------------------------------------

type Section = 'strings'|'hardware';

export class Locale {
  readonly data: LocaleData;
  readonly fallback: Locale|null;
  readonly code: string;
  readonly label: string;
  readonly name: string;
  readonly dir: 'ltr'|'rtl';
  readonly rtl: boolean;
  readonly numberingSystem: string;
  private readonly select_: PluralSelect;
  private readonly nf_ = new Map<number, Intl.NumberFormat>();
  private readonly warned_ = new Set<string>();

  constructor(data: LocaleData, fallback: Locale|null) {
    if (!data || !data.code) {
      throw new Error('Locale file needs a "code"');
    }
    this.data = data;
    this.fallback = fallback;
    this.code = data.code;
    this.label = data.label || data.code.toUpperCase();
    this.name = data.name || data.code;
    this.dir = data.dir === 'rtl' ? 'rtl' : 'ltr';
    this.rtl = this.dir === 'rtl';
    this.numberingSystem = data.numberingSystem || 'latn';
    this.select_ = pluralSelector(data.code, data.pluralRules);
  }

  private lookup_(section: Section, key: string): string|undefined {
    const sec = this.data[section];
    if (sec && Object.prototype.hasOwnProperty.call(sec, key)) {
      return sec[key];
    }
    return this.fallback ? this.fallback.lookup_(section, key) : undefined;
  }

  private miss_(section: string, key: string): string {
    const id = section + '.' + key;
    if (!this.warned_.has(id)) {
      console.warn(`[i18n] missing ${id} in ${this.code}`);
      this.warned_.add(id);
    }
    return key;
  }

  pluralCategory(value: number|string, decimals?: number): PluralCategory {
    return this.select_(value, decimals);
  }

  /** Formats a number in this locale's numbering system, fixed decimals. */
  num(x: number, decimals = 0): string {
    let nf = this.nf_.get(decimals);
    if (!nf) {
      nf = new Intl.NumberFormat(`${this.code}-u-nu-${this.numberingSystem}`, {
        minimumFractionDigits: decimals,
        maximumFractionDigits: decimals,
        useGrouping: false,
      });
      this.nf_.set(decimals, nf);
    }
    return nf.format(x);
  }

  /**
   * Replaces {name} with params.name. Inserted values are isolated whenever
   * their direction could clash with the surrounding text.
   */
  format(template: string|undefined, params?: Params): string {
    if (template == null) {
      return '';
    }
    return String(template).replace(/\{(\w+)\}/g, (all, name: string) => {
      if (!params || !(name in params)) {
        return all;
      }
      const raw = params[name]!;
      const v = typeof raw === 'number' ? this.num(raw) : String(raw);
      return this.rtl || RTL_CHARS.test(v) ? isolate(v) : v;
    });
  }

  t(key: string, params?: Params): string {
    const s = this.lookup_('strings', key);
    return this.format(s === undefined ? this.miss_('strings', key) : s, params);
  }

  hw(key: string, params?: Params): string {
    const s = this.lookup_('hardware', key);
    return this.format(
        s === undefined ? this.miss_('hardware', key) : s, params);
  }

  /**
   * Picks the plural form for `n`. Exact forms ("=0") win over CLDR
   * categories; a missing form falls back to "other", then to the fallback
   * locale.
   */
  plural(key: string, n: number|string, params?: Params, decimals?: number):
      string {
    const forms = this.data.plurals && this.data.plurals[key];
    if (!forms) {
      if (this.fallback) {
        return this.fallback.plural(key, n, params, decimals);
      }
      return this.miss_('plurals', key);
    }
    const cat = this.select_(n, decimals);
    const exact = forms['=' + n];
    const tpl = exact != null ? exact : forms[cat] != null ? forms[cat] : forms['other'];
    const value = typeof n === 'string' ? parseFloat(n) : n;
    const p: Params = {n: this.num(value, decimals || 0)};
    if (params) {
      Object.assign(p, params);
    }
    return this.format(tpl, p);
  }

  /**
   * Joins items with the locale's "listSep". In RTL each item is isolated so
   * a Latin or numeric item never swaps places with its neighbour.
   */
  join(items: Array<string|null|undefined|false>): string {
    let sep = this.lookup_('strings', 'listSep');
    if (sep == null) {
      sep = ' · ';
    }
    return items.filter((x): x is string => !!x)
        .map(x => this.rtl ? isolate(x) : x)
        .join(sep);
  }

  app(name: string): string {
    const a = this.data.apps;
    return a && a[name] ? a[name]! : name;
  }

  fonts(platform: string): {ui: string|null, hero: string|null} {
    const f = this.data.fonts || {};
    const o = (f.platforms && f.platforms[platform]) || {};
    return {ui: o.ui || f.ui || null, hero: o.hero || f.hero || null};
  }
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

export interface LocaleEntry {
  code: string;
  label: string;
  name: string;
  dir: 'ltr'|'rtl';
}

export class LocaleRegistry {
  private readonly baseCode: string;
  private readonly data_ = new Map<string, LocaleData>();
  private readonly built_ = new Map<string, Locale>();

  constructor(baseCode = 'en') {
    this.baseCode = baseCode;
  }

  static fromCatalog(catalog: Catalog): LocaleRegistry {
    const r = new LocaleRegistry(catalog.base || 'en');
    for (const d of catalog.locales) {
      r.register(d);
    }
    return r;
  }

  register(data: LocaleData): string {
    this.data_.set(data.code, data);
    this.built_.clear();  // Rebuild fallback chains lazily.
    return data.code;
  }

  /** "ar-EG" -> "ar" when only "ar" exists; unknown -> base locale. */
  resolve(code: string|null|undefined): string {
    if (code && this.data_.has(code)) {
      return code;
    }
    const parts = String(code || '').split(/[-_]/);
    while (parts.length > 1) {
      parts.pop();
      const c = parts.join('-');
      if (this.data_.has(c)) {
        return c;
      }
    }
    return this.data_.has(this.baseCode) ? this.baseCode :
                                           [...this.data_.keys()][0]!;
  }

  get(code: string): Locale {
    const resolved = this.resolve(code);
    const cached = this.built_.get(resolved);
    if (cached) {
      return cached;
    }
    const data = this.data_.get(resolved);
    if (!data) {
      throw new Error('No locale registered for ' + code);
    }
    const fbCode = data.fallback ||
        (data.code === this.baseCode ? null : this.baseCode);
    const fb = fbCode && fbCode !== data.code && this.data_.has(fbCode) ?
        this.get(fbCode) :
        null;
    const locale = new Locale(data, fb);
    this.built_.set(resolved, locale);
    return locale;
  }

  list(): LocaleEntry[] {
    const order = (d: LocaleData) =>
        d.order != null ? d.order : d.code === this.baseCode ? -1 : 0;
    return [...this.data_.values()]
        .sort(
            (a, b) => order(a) - order(b) ||
                (a.code < b.code ? -1 : a.code > b.code ? 1 : 0))
        .map(d => ({
               code: d.code,
               label: d.label || d.code.toUpperCase(),
               name: d.name || d.code,
               dir: d.dir === 'rtl' ? 'rtl' : 'ltr',
             }));
  }
}
