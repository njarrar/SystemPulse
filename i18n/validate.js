#!/usr/bin/env node
// Checks every locales/*.json against the base locale (en.json).
// Exit code 1 on errors; warnings are printed but do not fail.
'use strict';
const fs = require('fs');
const path = require('path');
const I18n = require('./pulse-i18n.js');

const dir = path.join(__dirname, '..', 'locales');
const files = fs.readdirSync(dir).filter(f => f.endsWith('.json') && f !== 'schema.json').sort();
const load = f => JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));
const base = load('en.json');
let failed = false;

for (const f of files) {
  const data = load(f);
  if (data.code + '.json' !== f) { console.log(`${f}: "code" is "${data.code}", file name must match`); failed = true; }
  const { errors, warnings } = I18n.validate(data, data.code === 'en' ? null : base);
  for (const e of errors) console.log(`${f}: error: ${e}`);
  for (const w of warnings) console.log(`${f}: warning: ${w}`);
  if (errors.length) failed = true;
  console.log(`${f}: ${errors.length} errors, ${warnings.length} warnings`);
}
process.exit(failed ? 1 : 0);
