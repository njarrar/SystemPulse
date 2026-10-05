#!/usr/bin/env node
// Unit tests for the Pulse i18n engine. Run: node i18n/test/test.js
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const I = require('../pulse-i18n.js');

const load = f => JSON.parse(fs.readFileSync(path.join(__dirname, '..', '..', 'locales', f), 'utf8'));
const en = load('en.json'), ar = load('ar.json');
I.register(en); I.register(ar);
const FSI = '⁨', PDI = '⁩';
const strip = s => s.replace(/[⁦-⁩]/g, '');
let passed = 0;
function test(name, fn) { fn(); passed++; console.log('ok - ' + name); }

// CLDR rules for languages Pulse may add later, copied from CLDR plurals.xml.
const RULES = {
  en: { one: 'i = 1 and v = 0' },
  ar: { zero: 'n = 0', one: 'n = 1', two: 'n = 2', few: 'n % 100 = 3..10', many: 'n % 100 = 11..99' },
  he: { one: 'i = 1 and v = 0 or i = 0 and v != 0', two: 'i = 2 and v = 0' },
  ru: { one: 'v = 0 and i % 10 = 1 and i % 100 != 11', few: 'v = 0 and i % 10 = 2..4 and i % 100 != 12..14', many: 'v = 0 and i % 10 = 0 or v = 0 and i % 10 = 5..9 or v = 0 and i % 100 = 11..14' },
  pl: { one: 'i = 1 and v = 0', few: 'v = 0 and i % 10 = 2..4 and i % 100 != 12..14', many: 'v = 0 and i != 1 and i % 10 = 0..1 or v = 0 and i % 10 = 5..9 or v = 0 and i % 100 = 12..14' },
  cy: { zero: 'n = 0', one: 'n = 1', two: 'n = 2', few: 'n = 3', many: 'n = 6' },
  fr: { one: 'i = 0,1', many: 'e = 0 and i != 0 and i % 1000000 = 0 and v = 0 or e != 0..5' },
  lv: { zero: 'n % 10 = 0 or n % 100 = 11..19 or v = 2 and f % 100 = 11..19', one: 'n % 10 = 1 and n % 100 != 11 or v = 2 and f % 10 = 1 and f % 100 != 11 or v != 2 and f % 10 = 1' },
  fa: { one: 'i = 0 or n = 1' },
  ja: {}
};

test('plural resolver matches Intl.PluralRules (0..1200, and 0.00..30.00)', () => {
  for (const [code, rules] of Object.entries(RULES)) {
    const sel = I.pluralSelector(code, rules);
    for (let d = 0; d <= 2; d++) {
      const max = d ? 30 : 1200, step = d ? 0.01 : 1;
      for (let k = 0; k * step <= max; k++) {
        const x = +(k * step).toFixed(d);
        const want = new Intl.PluralRules(code, { minimumFractionDigits: d, maximumFractionDigits: d }).select(x);
        assert.strictEqual(sel(x, d), want, `${code} ${x.toFixed(d)}`);
      }
    }
  }
});

test('operands follow CLDR (n i v w f t)', () => {
  assert.deepStrictEqual(I.operands('1.50'), { n: 1.5, i: 1, v: 2, w: 1, f: 50, t: 5, c: 0, e: 0 });
  assert.deepStrictEqual(I.operands(7), { n: 7, i: 7, v: 0, w: 0, f: 0, t: 0, c: 0, e: 0 });
  assert.strictEqual(I.operands('1.2c3').i, 1200);
});

test('Arabic picks all six forms', () => {
  const L = I.get('ar');
  const want = { 0: 'zero', 1: 'one', 2: 'two', 3: 'few', 10: 'few', 11: 'many', 99: 'many', 100: 'other', 101: 'other', 102: 'other', 103: 'few', 111: 'many', 1000: 'other' };
  for (const [n, cat] of Object.entries(want)) assert.strictEqual(L.select(+n), cat, 'ar ' + n);
  assert.strictEqual(strip(L.plural('process', 1)), 'عملية واحدة');
  assert.strictEqual(strip(L.plural('process', 2)), 'عمليتان');
  assert.strictEqual(strip(L.plural('process', 9)), '9 عمليات');
  assert.strictEqual(strip(L.plural('thread', 84)), '84 خيطاً');
  assert.strictEqual(strip(L.plural('thread', 103)), '103 خيوط');
  assert.strictEqual(strip(L.plural('ended', 0)), 'لم يتم إنهاء أي تطبيق بعد');
});

test('English exact "=0" form wins over the category', () => {
  const L = I.get('en');
  assert.strictEqual(L.plural('ended', 0), 'No apps ended yet');
  assert.strictEqual(L.plural('ended', 1), '1 app ended this session');
  assert.strictEqual(L.plural('ended', 3), '3 apps ended this session');
  assert.strictEqual(L.plural('thread', 1), '1 thread');
});

test('RTL interpolation isolates every inserted value', () => {
  const L = I.get('ar');
  assert.strictEqual(L.t('hogTitle', { app: 'Xcode', pct: '52%' }), `${FSI}Xcode${PDI} يستهلك ${FSI}52%${PDI} من المعالج`);
  assert.strictEqual(L.t('tempLine', { cpu: '52°C', gpu: '31°C' }), `المعالج ${FSI}52°C${PDI} · الرسوميات ${FSI}31°C${PDI}`);
});

test('LTR interpolation isolates only right-to-left values', () => {
  const L = I.get('en');
  assert.strictEqual(L.t('hogTitle', { app: 'Xcode', pct: '52%' }), 'Xcode is using 52% of total CPU');
  assert.strictEqual(L.t('endNamed', { app: 'خادم النوافذ' }), `End ${FSI}خادم النوافذ${PDI}`);
});

test('missing keys fall back to English, then to the key', () => {
  I.register({ code: 'xx', dir: 'ltr', strings: { cpu: 'XPU' } });
  const L = I.get('xx');
  assert.strictEqual(L.t('cpu'), 'XPU');
  assert.strictEqual(L.t('quit'), 'Quit');
  assert.strictEqual(L.plural('process', 3), '3 processes');
  const warn = console.warn; console.warn = () => {};
  assert.strictEqual(L.t('noSuchKey'), 'noSuchKey');
  console.warn = warn;
});

test('locale codes resolve by prefix', () => {
  assert.strictEqual(I.resolve('ar-EG'), 'ar');
  assert.strictEqual(I.resolve('en_GB'), 'en');
  assert.strictEqual(I.resolve('zz'), 'en');
});

test('numbers follow the locale numbering system', () => {
  I.register({ code: 'fa', dir: 'rtl', numberingSystem: 'arabext', strings: {} });
  assert.strictEqual(I.get('fa').num(54.25, 1), '۵۴٫۳');
  assert.strictEqual(I.get('ar').num(54.25, 1), '54.3');
});

test('join isolates items in RTL locales', () => {
  assert.strictEqual(I.get('ar').join(['a', 'b']), `${FSI}a${PDI} · ${FSI}b${PDI}`);
  assert.strictEqual(I.get('en').join(['a', '', 'b']), 'a · b');
});

test('validator catches bad files', () => {
  const r = I.validate({ code: 'zz', dir: 'up', pluralRules: { one: 'n = 1', lots: 'n > 3' }, plurals: { process: { one: 'x' } }, strings: { calm: '{oops}' } }, en);
  const text = r.errors.join('\n');
  assert.ok(/"dir"/.test(text));
  assert.ok(/bad category "lots"/.test(text));
  assert.ok(/needs an "other" form/.test(text));
  assert.ok(/unknown placeholder \{oops\}/.test(text));
});

test('shipped locales are clean', () => {
  assert.deepStrictEqual(I.validate(en, null).errors, []);
  const r = I.validate(ar, en);
  assert.deepStrictEqual(r.errors, []);
  assert.deepStrictEqual(r.warnings, []);
});

console.log(`\n${passed} tests passed`);
