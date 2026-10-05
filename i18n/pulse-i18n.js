/*
 * Pulse i18n engine
 *
 * Loads declarative locale files (locales/<code>.json), resolves CLDR plural
 * categories, interpolates {placeholders} with Unicode bidi isolation, and
 * formats numbers in the locale's numbering system. UI code asks for keys;
 * it never branches on a language code. Adding a language means adding one
 * locale file.
 *
 * Works as a browser global (window.PulseI18n) and as a CommonJS module.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.PulseI18n = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  var CATEGORIES = ['zero', 'one', 'two', 'few', 'many', 'other'];
  var FSI = '⁨', PDI = '⁩';
  var RTL_CHARS = /[֐-ࣿיִ-﷿ﹰ-﻿]/;

  // ---------------------------------------------------------------------------
  // CLDR plural operands (https://unicode.org/reports/tr35/tr35-numbers.html#Operands)
  // ---------------------------------------------------------------------------

  // `value` may be a number or a decimal string. `decimals` fixes the visible
  // fraction digits ("1.0" is not "1" in most languages).
  function operands(value, decimals) {
    var s;
    if (typeof value === 'string') s = value.trim();
    else if (decimals != null) s = Math.abs(value).toFixed(decimals);
    else s = String(Math.abs(value));
    s = s.replace(/^[-+]/, '');
    var e = 0, m = /^(.*?)[ce](\d+)$/i.exec(s);
    if (m) { e = parseInt(m[2], 10); s = shiftDecimal(m[1], e); }
    var dot = s.indexOf('.');
    var intPart = dot < 0 ? s : s.slice(0, dot);
    var frac = dot < 0 ? '' : s.slice(dot + 1);
    var trimmed = frac.replace(/0+$/, '');
    return {
      n: Math.abs(parseFloat(s)),
      i: parseInt(intPart || '0', 10),
      v: frac.length,
      w: trimmed.length,
      f: frac ? parseInt(frac, 10) : 0,
      t: trimmed ? parseInt(trimmed, 10) : 0,
      c: e,
      e: e
    };
  }

  function shiftDecimal(s, e) {
    var dot = s.indexOf('.'), digits = s.replace('.', ''), pos = (dot < 0 ? s.length : dot) + e;
    while (digits.length < pos) digits += '0';
    return pos >= digits.length ? digits : digits.slice(0, pos) + '.' + digits.slice(pos);
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

  function tokenize(src) {
    var out = [], re = /\s*(\.\.|!=|=|%|,|[a-z]+|\d+(?:\.\d+)?)\s*/gy, m;
    src = src.replace(/@.*$/, '').trim();
    re.lastIndex = 0;
    while (re.lastIndex < src.length) {
      m = re.exec(src);
      if (!m) throw new Error('Bad plural rule near: ' + src.slice(re.lastIndex));
      out.push(m[1]);
    }
    return out;
  }

  function compileRule(src) {
    var tk = tokenize(src), p = 0;
    if (!tk.length) return function () { return true; };
    function peek() { return tk[p]; }
    function next() { return tk[p++]; }
    function expect(x) { if (next() !== x) throw new Error('Expected "' + x + '" in plural rule: ' + src); }
    function num() {
      var x = next();
      if (!/^\d/.test(x || '')) throw new Error('Expected number in plural rule: ' + src);
      return parseFloat(x);
    }
    function expr() {
      var op = next();
      if (!/^[nivwftce]$/.test(op || '')) throw new Error('Unknown operand "' + op + '" in plural rule: ' + src);
      var mod = null;
      if (peek() === '%' || peek() === 'mod') { next(); mod = num(); }
      return function (o) { var x = o[op]; return mod == null ? x : x % mod; };
    }
    function rangeList() {
      var ranges = [];
      do {
        var lo = num(), hi = lo;
        if (peek() === '..') { next(); hi = num(); }
        ranges.push([lo, hi]);
      } while (peek() === ',' && next());
      return ranges;
    }
    function relation() {
      var e = expr(), neg = false, ranges, integerOnly = true, op = next();
      if (op === 'is') {
        if (peek() === 'not') { next(); neg = true; }
        ranges = [[num(), 0]]; ranges[0][1] = ranges[0][0];
      } else if (op === '=' || op === '!=') {
        neg = op === '!='; ranges = rangeList();
      } else {
        if (op === 'not') { neg = true; op = next(); }
        if (op === 'within') integerOnly = false;
        else if (op !== 'in') throw new Error('Unknown relation "' + op + '" in plural rule: ' + src);
        ranges = rangeList();
      }
      return function (o) {
        var x = e(o), hit = false;
        for (var k = 0; k < ranges.length && !hit; k++) {
          var r = ranges[k];
          hit = x >= r[0] && x <= r[1] && (!integerOnly || x === Math.floor(x));
        }
        return neg ? !hit : hit;
      };
    }
    function andCond() {
      var rs = [relation()];
      while (peek() === 'and') { next(); rs.push(relation()); }
      return function (o) { for (var k = 0; k < rs.length; k++) if (!rs[k](o)) return false; return true; };
    }
    var ors = [andCond()];
    while (peek() === 'or') { next(); ors.push(andCond()); }
    if (p !== tk.length) throw new Error('Unexpected "' + tk[p] + '" in plural rule: ' + src);
    return function (o) { for (var k = 0; k < ors.length; k++) if (ors[k](o)) return true; return false; };
  }

  // Returns select(value, decimals) -> category. Uses the locale file's
  // declared CLDR rules; falls back to Intl.PluralRules, then to one/other.
  function pluralSelector(code, rules) {
    if (rules && Object.keys(rules).length) {
      var compiled = CATEGORIES.filter(function (c) { return c !== 'other' && rules[c]; })
        .map(function (c) { return [c, compileRule(rules[c])]; });
      return function (value, decimals) {
        var o = operands(value, decimals);
        for (var k = 0; k < compiled.length; k++) if (compiled[k][1](o)) return compiled[k][0];
        return 'other';
      };
    }
    if (typeof Intl !== 'undefined' && Intl.PluralRules) {
      var pr = {};
      return function (value, decimals) {
        var d = decimals == null ? 0 : decimals;
        pr[d] = pr[d] || new Intl.PluralRules(code, { minimumFractionDigits: d, maximumFractionDigits: d });
        return pr[d].select(typeof value === 'string' ? parseFloat(value) : value);
      };
    }
    return function (value) { return Number(value) === 1 ? 'one' : 'other'; };
  }

  // ---------------------------------------------------------------------------
  // Locale
  // ---------------------------------------------------------------------------

  function isolate(s) { return FSI + s + PDI; }

  function Locale(data, fallback) {
    if (!data || !data.code) throw new Error('Locale file needs a "code"');
    this.data = data;
    this.fallback = fallback && fallback !== this ? fallback : null;
    this.code = data.code;
    this.label = data.label || data.code.toUpperCase();
    this.name = data.name || data.code;
    this.dir = data.dir === 'rtl' ? 'rtl' : 'ltr';
    this.rtl = this.dir === 'rtl';
    this.numberingSystem = data.numberingSystem || 'latn';
    this.select = pluralSelector(data.code, data.pluralRules);
    this._nf = {};
    this._warned = {};
  }

  Locale.prototype._lookup = function (section, key) {
    var sec = this.data[section];
    if (sec && Object.prototype.hasOwnProperty.call(sec, key)) return sec[key];
    if (this.fallback) return this.fallback._lookup(section, key);
    return undefined;
  };

  Locale.prototype._miss = function (section, key) {
    var id = section + '.' + key;
    if (!this._warned[id] && typeof console !== 'undefined') console.warn('[i18n] missing ' + id + ' in ' + this.code);
    this._warned[id] = true;
    return key;
  };

  // Formats a number in this locale's numbering system with fixed decimals.
  Locale.prototype.num = function (x, decimals) {
    var d = decimals || 0;
    if (typeof Intl === 'undefined' || !Intl.NumberFormat) return Number(x).toFixed(d);
    var nf = this._nf[d] || (this._nf[d] = new Intl.NumberFormat(this.code + '-u-nu-' + this.numberingSystem, {
      minimumFractionDigits: d, maximumFractionDigits: d, useGrouping: false
    }));
    return nf.format(x);
  };

  // Replaces {name} with params.name. Inserted values are wrapped in bidi
  // isolates whenever their direction could clash with the surrounding text,
  // so numbers with units and Latin names never get reordered.
  Locale.prototype.format = function (template, params) {
    if (template == null) return '';
    var self = this;
    return String(template).replace(/\{(\w+)\}/g, function (all, name) {
      if (!params || !(name in params)) return all;
      var v = params[name];
      v = typeof v === 'number' ? self.num(v) : String(v);
      return self.rtl || RTL_CHARS.test(v) ? isolate(v) : v;
    });
  };

  Locale.prototype.t = function (key, params) {
    var s = this._lookup('strings', key);
    return this.format(s === undefined ? this._miss('strings', key) : s, params);
  };

  Locale.prototype.hw = function (key, params) {
    var s = this._lookup('hardware', key);
    return this.format(s === undefined ? this._miss('hardware', key) : s, params);
  };

  // Picks the plural form for `n`. Exact matches ("=0", "=1") win over CLDR
  // categories, the way ICU MessageFormat works. Missing forms fall back to
  // "other", then to the fallback locale.
  Locale.prototype.plural = function (key, n, params, decimals) {
    var forms = this.data.plurals && this.data.plurals[key];
    if (!forms) {
      if (this.fallback) return this.fallback.plural(key, n, params, decimals);
      return this._miss('plurals', key);
    }
    var cat = this.select(n, decimals);
    var tpl = forms['=' + n] != null ? forms['=' + n] : forms[cat] != null ? forms[cat] : forms.other;
    var p = { n: decimals == null ? this.num(n) : this.num(n, decimals) };
    if (params) for (var k in params) p[k] = params[k];
    return this.format(tpl, p);
  };

  // Joins items with the locale's "listSep" string. In RTL locales each item
  // is isolated so a Latin or numeric item never swaps places with its neighbour.
  Locale.prototype.join = function (items) {
    var sep = this._lookup('strings', 'listSep'), self = this;
    if (sep == null) sep = ' · ';
    return items.filter(function (x) { return x != null && x !== ''; })
      .map(function (x) { return self.rtl ? isolate(String(x)) : String(x); }).join(sep);
  };

  Locale.prototype.app = function (name) {
    var a = this.data.apps;
    return a && a[name] ? a[name] : name;
  };

  // Font stacks for a platform. A locale may set "fonts.ui"/"fonts.hero" and
  // per-platform overrides under "fonts.platforms.<platform>". Returns null
  // for a stack the locale leaves to the platform.
  Locale.prototype.fonts = function (platform) {
    var f = this.data.fonts || {}, o = (f.platforms && f.platforms[platform]) || {};
    return { ui: o.ui || f.ui || null, hero: o.hero || f.hero || null };
  };

  // Every string, resolved with fallback, for templates that bind t.<key>.
  Locale.prototype.strings = function () {
    var out = {}, k;
    if (this.fallback) { var fb = this.fallback.strings(); for (k in fb) out[k] = fb[k]; }
    var s = this.data.strings || {};
    for (k in s) if (!/\{\w+\}/.test(s[k])) out[k] = s[k];
    return out;
  };

  // ---------------------------------------------------------------------------
  // Registry
  // ---------------------------------------------------------------------------

  var registry = {}, baseCode = 'en';

  function register(data) {
    registry[data.code] = { data: data, locale: null };
    for (var c in registry) registry[c].locale = null; // rebuild fallback chains lazily
    return data.code;
  }

  function get(code) {
    var entry = registry[resolve(code)];
    if (!entry) throw new Error('No locale registered for ' + code);
    if (!entry.locale) {
      var fbCode = entry.data.fallback || (entry.data.code === baseCode ? null : baseCode);
      var fb = fbCode && registry[fbCode] && fbCode !== entry.data.code ? get(fbCode) : null;
      entry.locale = new Locale(entry.data, fb);
    }
    return entry.locale;
  }

  // "ar-EG" -> "ar" when only "ar" exists; unknown -> base locale.
  function resolve(code) {
    if (code && registry[code]) return code;
    var parts = String(code || '').split(/[-_]/);
    while (parts.length > 1) { parts.pop(); if (registry[parts.join('-')]) return parts.join('-'); }
    return registry[baseCode] ? baseCode : Object.keys(registry)[0];
  }

  function list() {
    return Object.keys(registry).map(function (c) { return registry[c].data; }).sort(function (a, b) {
      var oa = a.order != null ? a.order : a.code === baseCode ? -1 : 0;
      var ob = b.order != null ? b.order : b.code === baseCode ? -1 : 0;
      return oa - ob || (a.code < b.code ? -1 : a.code > b.code ? 1 : 0);
    }).map(function (d) { return { code: d.code, label: d.label || d.code.toUpperCase(), name: d.name || d.code, dir: d.dir === 'rtl' ? 'rtl' : 'ltr' }; });
  }

  // Registers every <script type="application/json" data-pulse-locale> block.
  function loadFromDocument(doc) {
    doc = doc || (typeof document !== 'undefined' ? document : null);
    if (!doc) return [];
    var els = doc.querySelectorAll('script[data-pulse-locale]'), codes = [];
    for (var k = 0; k < els.length; k++) codes.push(register(JSON.parse(els[k].textContent)));
    return codes;
  }

  // ---------------------------------------------------------------------------
  // Validation: checks a locale against the base locale.
  // ---------------------------------------------------------------------------

  function placeholders(s) {
    var out = {}, m, re = /\{(\w+)\}/g;
    while ((m = re.exec(String(s)))) out[m[1]] = true;
    return out;
  }

  function validate(data, base) {
    var errors = [], warnings = [];
    if (!data.code) errors.push('missing "code"');
    if (data.dir && data.dir !== 'ltr' && data.dir !== 'rtl') errors.push('"dir" must be "ltr" or "rtl"');
    if (data.pluralRules) {
      Object.keys(data.pluralRules).forEach(function (c) {
        if (CATEGORIES.indexOf(c) < 0 || c === 'other') errors.push('pluralRules: bad category "' + c + '"');
        else { try { compileRule(data.pluralRules[c]); } catch (e) { errors.push('pluralRules.' + c + ': ' + e.message); } }
      });
    }
    var cats = ['other'].concat(Object.keys(data.pluralRules || {}));
    Object.keys(data.plurals || {}).forEach(function (key) {
      var forms = data.plurals[key];
      if (forms.other == null) errors.push('plurals.' + key + ': needs an "other" form');
      Object.keys(forms).forEach(function (f) {
        if (!/^=\d+$/.test(f) && CATEGORIES.indexOf(f) < 0) errors.push('plurals.' + key + ': unknown form "' + f + '"');
        else if (data.pluralRules && CATEGORIES.indexOf(f) >= 0 && cats.indexOf(f) < 0) warnings.push('plurals.' + key + '.' + f + ': this language never selects "' + f + '"');
      });
      if (data.pluralRules) cats.forEach(function (c) { if (forms[c] == null && c !== 'other') warnings.push('plurals.' + key + ': no "' + c + '" form, "other" is used'); });
    });
    if (base) {
      ['strings', 'hardware', 'plurals'].forEach(function (sec) {
        var b = base[sec] || {}, d = data[sec] || {};
        Object.keys(b).forEach(function (k) {
          if (d[k] == null) { warnings.push(sec + '.' + k + ': missing, falls back to ' + base.code); return; }
          if (sec === 'plurals') return;
          var pb = placeholders(b[k]), pd = placeholders(d[k]);
          Object.keys(pb).forEach(function (p) { if (!pd[p]) warnings.push(sec + '.' + k + ': drops {' + p + '}'); });
          Object.keys(pd).forEach(function (p) { if (!pb[p]) errors.push(sec + '.' + k + ': unknown placeholder {' + p + '}'); });
        });
        Object.keys(d).forEach(function (k) { if (b[k] == null) warnings.push(sec + '.' + k + ': not in ' + base.code + ', never used'); });
      });
    }
    return { errors: errors, warnings: warnings };
  }

  return {
    CATEGORIES: CATEGORIES,
    operands: operands,
    compileRule: compileRule,
    pluralSelector: pluralSelector,
    isolate: isolate,
    Locale: Locale,
    register: register,
    get: get,
    resolve: resolve,
    list: list,
    loadFromDocument: loadFromDocument,
    validate: validate
  };
});
