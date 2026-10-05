#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Turns Pulse locale files (locales/*.json) into Chromium catalogs.

Outputs, all written to --out-dir:

  pulse_catalog.json
      Every locale, normalized. The WebUI loads it so the in-app language
      switcher can flip between locales without a restart. The TypeScript
      engine (resources/i18n.ts) reads plural rules, forms and strings from it.

  pulse_strings.grd
  translations/pulse_strings_<lang>.xtb
      A GRIT file with one <message> per key (strings, hardware names, every
      plural form and the per-locale metadata) plus one XTB per non-base
      locale. Chrome's string pipeline packs these into the locale .pak files,
      and C++ reads the window title from them.

  pulse_strings_ids.inc
      {"<loadTimeData key>", IDS_...} rows for WebUIDataSource.

Adding a locale file needs no code change: run this script again (the GN
action does it on every build when a locale file changes).

Usage:
  gen_pulse_catalog.py --locales-dir DIR --out-dir DIR [--pak-locales en,ar,...]
  gen_pulse_catalog.py --locales-dir DIR --list
"""

import argparse
import hashlib
import json
import os
import re
import sys
from xml.sax.saxutils import escape as xml_escape
from xml.sax.saxutils import quoteattr

BASE = 'en'
CATEGORIES = ['zero', 'one', 'two', 'few', 'many', 'other']
SECTIONS = (('strings', 'S'), ('hardware', 'HW'))
PLACEHOLDER = re.compile(r'\{(\w+)\}')

# ---------------------------------------------------------------------------
# GRIT message ids. Same algorithm as grit/extern/FP.py and
# grit/extern/tclib.py: the first 64 bits of the MD5 of the presentable text
# (placeholders replaced by their names), folded with the meaning.
# ---------------------------------------------------------------------------


def _fingerprint(text):
  fp = int(hashlib.md5(text.encode('utf-8')).hexdigest()[:16], 16)
  if fp & 0x8000000000000000:
    fp = -((~fp & 0xFFFFFFFFFFFFFFFF) + 1)
  return fp


def message_id(presentable, meaning=''):
  fp = _fingerprint(presentable)
  if meaning:
    fp2 = _fingerprint(meaning)
    fp = fp2 + (fp << 1) + (1 if fp < 0 else 0)
  return str(fp & 0x7FFFFFFFFFFFFFFF)


# ---------------------------------------------------------------------------
# Locale loading
# ---------------------------------------------------------------------------


def load_locales(locales_dir):
  out = {}
  for name in sorted(os.listdir(locales_dir)):
    if not name.endswith('.json') or name == 'schema.json':
      continue
    with open(os.path.join(locales_dir, name), encoding='utf-8') as f:
      data = json.load(f)
    if not isinstance(data, dict) or 'code' not in data:
      raise SystemExit('%s: not a locale file (no "code")' % name)
    data.pop('$schema', None)
    out[data['code']] = data
  if BASE not in out:
    raise SystemExit('%s: needs %s.json' % (locales_dir, BASE))
  return out


def ordered_codes(locales):
  def key(code):
    d = locales[code]
    order = d.get('order', -1 if code == BASE else 0)
    return (order, code)
  return sorted(locales, key=key)


# Chrome pak locale codes use "-" (en-GB, pt-BR). Pulse codes already do.
def pak_lang(code):
  return code


def ident(key):
  """camelCase or snake key -> IDS-safe upper snake (memOf -> MEM_OF)."""
  s = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', key)
  s = re.sub(r'[^A-Za-z0-9]+', '_', s)
  return s.upper().strip('_')


def form_ident(form):
  return 'EQ' + form[1:] if form.startswith('=') else form.upper()


# ---------------------------------------------------------------------------
# Message table: one row per (id name, meaning, english text, per-locale text)
# ---------------------------------------------------------------------------


def build_messages(locales):
  base = locales[BASE]
  rows = []  # (ids_name, ltd_key, meaning, desc, {code: text})

  def add(ids_name, ltd_key, desc, texts):
    rows.append((ids_name, ltd_key, 'pulse:' + ltd_key, desc, texts))

  for section, prefix in SECTIONS:
    for key in base.get(section, {}):
      texts = {c: d[section][key] for c, d in locales.items()
               if key in d.get(section, {})}
      add('IDS_PULSE_%s_%s' % (prefix, ident(key)), '%s.%s' % (section, key),
          'Pulse %s "%s".' % (section, key), texts)

  # Plural forms: the union over every locale, so the source file carries a
  # slot for each form any translation uses. English fills slots it never
  # selects with its "other" form.
  plural_keys = []
  for code in ordered_codes(locales):
    for k in locales[code].get('plurals', {}):
      if k not in plural_keys:
        plural_keys.append(k)
  for key in plural_keys:
    forms = []
    for code in ordered_codes(locales):
      for f in locales[code].get('plurals', {}).get(key, {}):
        if f not in forms:
          forms.append(f)
    forms.sort(key=lambda f: (0, f) if f.startswith('=') else
               (1, CATEGORIES.index(f)))
    base_forms = base.get('plurals', {}).get(key, {})
    for form in forms:
      texts = {}
      for code, d in locales.items():
        f = d.get('plurals', {}).get(key, {})
        if form in f:
          texts[code] = f[form]
      texts.setdefault(BASE, base_forms.get(form, base_forms.get('other', '')))
      add('IDS_PULSE_PL_%s_%s' % (ident(key), form_ident(form)),
          'plurals.%s.%s' % (key, form),
          'Pulse plural "%s", form "%s". {n} is the count.' % (key, form),
          texts)

  for key in sorted({k for d in locales.values() for k in d.get('apps', {})}):
    texts = {c: d['apps'][key] for c, d in locales.items()
             if key in d.get('apps', {})}
    texts.setdefault(BASE, key)
    add('IDS_PULSE_APP_%s' % ident(key), 'apps.' + key,
        'Display name of the app or process group "%s".' % key, texts)

  # Locale metadata travels as translatable text so each pak knows its own
  # plural rules, direction and switcher label.
  meta = {
      'label': lambda d: d.get('label', d['code'].upper()),
      'name': lambda d: d.get('name', d['code']),
      'dir': lambda d: 'rtl' if d.get('dir') == 'rtl' else 'ltr',
      'numberingSystem': lambda d: d.get('numberingSystem', 'latn'),
      'pluralRules': lambda d: json.dumps(d.get('pluralRules', {}),
                                          ensure_ascii=False, sort_keys=True),
  }
  for key, fn in meta.items():
    add('IDS_PULSE_META_%s' % ident(key), 'meta.' + key,
        'Locale metadata "%s". Not shown to users.' % key,
        {c: fn(d) for c, d in locales.items()})
  return rows


# ---------------------------------------------------------------------------
# GRD / XTB writers
# ---------------------------------------------------------------------------


def _quote_ws(xml_text, raw):
  if raw != raw.strip():
    return "'''" + xml_text + "'''"
  return xml_text


def grd_body(text):
  parts, last = [], 0
  for m in PLACEHOLDER.finditer(text):
    parts.append(xml_escape(text[last:m.start()]))
    name = ident(m.group(1))
    parts.append('<ph name="%s">%s<ex>%s</ex></ph>' %
                 (name, xml_escape(m.group(0)), xml_escape(m.group(1))))
    last = m.end()
  parts.append(xml_escape(text[last:]))
  return _quote_ws(''.join(parts), text)


def xtb_body(text):
  parts, last = [], 0
  for m in PLACEHOLDER.finditer(text):
    parts.append(xml_escape(text[last:m.start()]))
    parts.append('<ph name="%s"/>' % ident(m.group(1)))
    last = m.end()
  parts.append(xml_escape(text[last:]))
  return ''.join(parts)


def presentable(text):
  stripped = text.strip()
  return PLACEHOLDER.sub(lambda m: ident(m.group(1)), stripped)


def xtb_plan(locales, pak_locales):
  """Maps each translation file language to the Pulse locale that fills it.

  With --pak-locales (the GN build), every Chrome pak locale but English
  gets a file, so the GN action has a fixed list of outputs. A pak locale
  takes the Pulse locale with the same code, else the one with the same
  language (es-419 takes es), else none: an empty bundle, and GRIT falls
  back to English. Without --pak-locales, one file per Pulse locale.
  """
  if not pak_locales:
    return [(pak_lang(c), c) for c in ordered_codes(locales) if c != BASE]
  plan = []
  for lang in pak_locales:
    if lang == BASE:
      continue
    src = lang if lang in locales else None
    if src is None:
      base_lang = lang.split('-')[0]
      src = base_lang if base_lang in locales and base_lang != BASE else None
    plan.append((lang, src))
  return plan


def write_grd(path, rows, plan, pak_locales):
  out = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<!-- Generated by gen_pulse_catalog.py from locales/*.json. '
         'Do not edit. -->',
         '<grit base_dir="." latest_public_release="0" current_release="1" '
         'output_all_resource_defines="false" source_lang_id="en">',
         '  <outputs>',
         '    <output filename="grit/pulse_strings.h" type="rc_header">',
         '      <emit emit_type="prepend"></emit>',
         '    </output>']
  for lang in pak_locales:
    out.append('    <output filename="pulse_strings_%s.pak" '
               'type="data_package" lang="%s" />' % (lang, lang))
  out.append('  </outputs>')
  out.append('  <translations>')
  for lang, _ in plan:
    out.append('    <file path="translations/pulse_strings_%s.xtb" lang="%s" />'
               % (lang, lang))
  out.append('  </translations>')
  out.append('  <release seq="1" allow_pseudo="false">')
  out.append('    <messages fallback_to_english="true">')
  out.append('      <message name="IDS_PULSE_APP_NAME" translateable="false" '
             'desc="Product name.">Pulse</message>')
  for ids_name, _, meaning, desc, texts in rows:
    out.append('      <message name="%s" meaning=%s desc=%s>%s</message>' %
               (ids_name, quoteattr(meaning), quoteattr(desc),
                grd_body(texts[BASE])))
  out += ['    </messages>', '  </release>', '</grit>', '']
  _write(path, '\n'.join(out))


def write_xtb(path, lang, code, rows):
  out = ['<?xml version="1.0" ?>', '<!DOCTYPE translationbundle>',
         '<translationbundle lang="%s">' % lang]
  for _, _, meaning, _, texts in rows:
    if code is None or code not in texts:
      continue  # GRIT falls back to the English source.
    out.append('<translation id="%s">%s</translation>' %
               (message_id(presentable(texts[BASE]), meaning),
                xtb_body(texts[code])))
  out += ['</translationbundle>', '']
  _write(path, '\n'.join(out))


def write_ids_inc(path, rows):
  out = ['// Generated by gen_pulse_catalog.py. Do not edit.']
  for ids_name, ltd_key, _, _, _ in rows:
    out.append('{"%s", %s},' % (ltd_key, ids_name))
  _write(path, '\n'.join(out) + '\n')


def write_catalog(path, locales):
  catalog = {'base': BASE,
             'locales': [locales[c] for c in ordered_codes(locales)]}
  _write(path, json.dumps(catalog, ensure_ascii=False, indent=1) + '\n')


def _write(path, text):
  os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
  # Only touch the file when it changes, so ninja does not rebuild for nothing.
  if os.path.exists(path):
    with open(path, encoding='utf-8') as f:
      if f.read() == text:
        return
  with open(path, 'w', encoding='utf-8') as f:
    f.write(text)


def main(argv):
  p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
  p.add_argument('--locales-dir', required=True)
  p.add_argument('--out-dir')
  p.add_argument('--pak-locales', default='',
                 help='Comma list of Chrome pak locales (GN '
                 'platform_pak_locales). Defaults to the Pulse locales.')
  p.add_argument('--stamp')
  p.add_argument('--depfile',
                 help='Write a Ninja depfile naming every locale file and '
                 'the locales directory, so adding a file reruns this.')
  p.add_argument('--list', action='store_true',
                 help='Print the locale codes and exit.')
  args = p.parse_args(argv)

  locales = load_locales(args.locales_dir)
  if args.list:
    print('\n'.join(ordered_codes(locales)))
    return 0
  if not args.out_dir:
    p.error('--out-dir is required')

  given = [x for x in args.pak_locales.split(',') if x]
  pak_locales = given or [pak_lang(c) for c in ordered_codes(locales)]
  plan = xtb_plan(locales, given)
  rows = build_messages(locales)
  catalog_path = os.path.join(args.out_dir, 'pulse_catalog.json')
  write_catalog(catalog_path, locales)
  write_grd(os.path.join(args.out_dir, 'pulse_strings.grd'), rows, plan,
            pak_locales)
  for lang, code in plan:
    write_xtb(os.path.join(args.out_dir, 'translations',
                           'pulse_strings_%s.xtb' % lang), lang, code, rows)
  write_ids_inc(os.path.join(args.out_dir, 'pulse_strings_ids.inc'), rows)
  if args.depfile:
    inputs = [args.locales_dir] + [
        os.path.join(args.locales_dir, n)
        for n in sorted(os.listdir(args.locales_dir)) if n.endswith('.json')]
    target = args.stamp or catalog_path
    _write(args.depfile, '%s: %s\n' % (target.replace(' ', '\\ '), ' '.join(
        x.replace(' ', '\\ ') for x in inputs)))
  if args.stamp:
    _write(args.stamp, '')
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
