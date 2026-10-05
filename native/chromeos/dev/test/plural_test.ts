// Checks the TypeScript locale engine against the shared vectors in
// pulse/i18n/test/fixtures/vectors.json and the real locale files.
// Run by dev/build.sh after compiling: node <out>/test/plural_test.js

import {readFileSync} from 'node:fs';
import {dirname, join} from 'node:path';

import {type Catalog, Locale, LocaleRegistry, isolate} from '../app/i18n.js';

interface Vectors {
  [code: string]: {
    pluralRules: Record<string, string>,
    categories: Array<[string, string]>,
    samples: Record<string, string>,
  };
}

const pulseRoot = process.env['PULSE_ROOT'] || '/mnt/project-files/pulse';
const catalogPath = process.argv[2] ||
    join(dirname(process.argv[1]!), '..', 'app', 'pulse_catalog.json');
const vectors = JSON.parse(readFileSync(
                    join(pulseRoot, 'i18n/test/fixtures/vectors.json'),
                    'utf8')) as Vectors;
const catalog = JSON.parse(readFileSync(catalogPath, 'utf8')) as Catalog;
const registry = LocaleRegistry.fromCatalog(catalog);

let pass = 0;
const fails: string[] = [];
function check(name: string, got: unknown, want: unknown) {
  if (got === want) {
    pass++;
  } else {
    fails.push(`${name}: got ${JSON.stringify(got)}, want ${
        JSON.stringify(want)}`);
  }
}

// Renders a vector sample key. "thread:3" is a plural; "tempLine:cpu=52°C,..."
// is a string with parameters.
function render(lc: Locale, key: string): string {
  const [name, arg] = key.split(/:(.*)/s) as [string, string];
  if (arg !== undefined && /^-?\d+(\.\d+)?$/.test(arg)) {
    return lc.plural(name, Number(arg));
  }
  const params: Record<string, string> = {};
  for (const pair of (arg || '').split(',')) {
    const [k, v] = pair.split('=');
    if (k) {
      params[k] = v ?? '';
    }
  }
  return lc.t(name, params);
}

for (const [code, vec] of Object.entries(vectors)) {
  const lc = registry.get(code);
  // The catalog must carry the same rules the vectors were cut from.
  check(
      `${code} pluralRules`, JSON.stringify(lc.data.pluralRules),
      JSON.stringify(vec.pluralRules));
  for (const [value, want] of vec.categories) {
    check(`${code} category(${value})`, lc.pluralCategory(value), want);
  }
  for (const [key, want] of Object.entries(vec.samples)) {
    check(`${code} sample ${key}`, render(lc, key), want);
  }
}

// Engine rules from BRIEF.md beyond the vectors.
const en = registry.get('en');
const ar = registry.get('ar');
check('ar-EG resolves to ar', registry.resolve('ar-EG'), 'ar');
check('unknown resolves to en', registry.resolve('xx-YY'), 'en');
check('exact =0 wins', en.plural('ended', 0), 'No apps ended yet');
const partial = LocaleRegistry.fromCatalog(catalog);
partial.register({code: 'xx', dir: 'rtl', strings: {calm: 'XX calm'}});
check('own string', partial.get('xx').t('calm'), 'XX calm');
check('en fallback for missing key', partial.get('xx').t('quit'), 'Quit');
check(
    'fallback text still isolated in rtl', partial.get('xx').t('memOf', {
      used: '9.3 GB',
      total: '16 GB',
    }),
    `${isolate('9.3 GB')} of ${isolate('16 GB')}`);
check('3 locales listed', partial.list().length, 3);
check('key when missing everywhere', en.t('noSuchKey'), 'noSuchKey');
check('ar join isolates', ar.join(['a', 'b']).startsWith(isolate('a')), true);
check('en join plain', en.join(['a', 'b']), 'a · b');
check('ar 103 is few', ar.pluralCategory(103), 'few');
check('ar 101 is other', ar.pluralCategory(101), 'other');
check('ar 11 is many', ar.pluralCategory(11), 'many');
check('decimal 1.0 en other', en.pluralCategory(1, 1), 'other');
check('latn digits in ar', ar.num(58), '58');
check('switcher list', registry.list().map(x => x.code).join(','), 'en,ar');

if (fails.length) {
  console.error(fails.slice(0, 30).join('\n'));
  console.error(`plural_test: ${fails.length} failed, ${pass} passed`);
  process.exit(1);
}
console.log(`plural_test: ${pass} checks passed`);
