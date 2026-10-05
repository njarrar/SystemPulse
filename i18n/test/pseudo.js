#!/usr/bin/env node
// Writes a pseudo-locale (en-XA) built from en.json: every string gets about
// 40% longer and its letters accented, placeholders untouched. Use it to check
// truncation and the 3+ locale menu:
//   node i18n/test/pseudo.js && python3 prototype/build.py --with-fixtures
'use strict';
const fs = require('fs');
const path = require('path');
const en = JSON.parse(fs.readFileSync(path.join(__dirname, '..', '..', 'locales', 'en.json'), 'utf8'));
const MAP = { a: 'á', e: 'é', i: 'í', o: 'ó', u: 'ú', A: 'Á', E: 'É', I: 'Í', O: 'Ó', U: 'Ú', c: 'ç', n: 'ñ', s: 'š', y: 'ý' };
const pad = ['', ' ~', ' ~~', ' ~~~'];
function pseudo(s) {
  const parts = String(s).split(/(\{\w+\})/);
  let len = 0;
  const out = parts.map(p => {
    if (/^\{\w+\}$/.test(p)) return p;
    len += p.length;
    return p.replace(/[a-zA-Z]/g, ch => MAP[ch] || ch);
  }).join('');
  const extra = Math.ceil(len * 0.4);
  return '[' + out + ' ' + 'ẋ'.repeat(Math.max(1, extra - 3)) + ']';
}
const mapObj = o => Object.fromEntries(Object.entries(o || {}).map(([k, v]) => [k, typeof v === 'object' ? mapObj(v) : pseudo(v)]));
const xa = {
  code: 'en-XA', label: 'XA', name: 'Pseudo (long)', dir: 'ltr', numberingSystem: 'latn', fallback: 'en',
  pluralRules: en.pluralRules,
  plurals: mapObj(en.plurals),
  strings: { ...mapObj(en.strings), listSep: ' · ' },
  hardware: mapObj(en.hardware),
  apps: {}
};
const out = path.join(__dirname, 'fixtures', 'en-XA.json');
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, JSON.stringify(xa, null, 2) + '\n');
console.log('wrote ' + path.relative(process.cwd(), out));
