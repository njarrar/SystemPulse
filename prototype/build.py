#!/usr/bin/env python3
"""Builds the single-file Pulse prototype.

Inlines the i18n engine and every locales/*.json file into the page, so a
new language only needs a new locale file and a rebuild:

    python3 prototype/build.py            # writes build/prototype/Pulse_System_Monitor.html
    python3 prototype/build.py --check    # validates locales only
    python3 prototype/build.py --with-fixtures --out /tmp/pulse-test.html
                                          # adds i18n/test/fixtures/*.json (pseudo-locale)
"""
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(HERE, 'src')
OUT = os.path.join(ROOT, 'build', 'prototype', 'Pulse_System_Monitor.html')
RUNTIME_TAG = '<script src="6fb80ad1-f0f4-46b4-842d-b864fdbcc5a0"></script>'


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def script_safe(text):
    return text.replace('</', '<\\/')


def load_locales(with_fixtures=False):
    locales = []
    paths = sorted(glob.glob(os.path.join(ROOT, 'locales', '*.json')))
    if with_fixtures:
        paths += sorted(glob.glob(os.path.join(ROOT, 'i18n', 'test', 'fixtures', '*.json')))
    for path in paths:
        if os.path.basename(path) == 'schema.json':
            continue
        data = json.loads(read(path))
        data.pop('$schema', None)
        locales.append(data)
    return locales


def validate():
    """Runs the engine's validator through node; fails the build on errors."""
    res = subprocess.run(['node', os.path.join(ROOT, 'i18n', 'validate.js')])
    if res.returncode:
        sys.exit(res.returncode)


def build(out=OUT, with_fixtures=False):
    engine = read(os.path.join(ROOT, 'i18n', 'pulse-i18n.js'))
    blocks = ['<script>' + script_safe(engine) + '</script>']
    for loc in load_locales(with_fixtures):
        body = json.dumps(loc, ensure_ascii=False, separators=(',', ':'))
        blocks.append('<script type="application/json" data-pulse-locale="%s">%s</script>' % (loc['code'], script_safe(body)))

    markup = read(os.path.join(SRC, 'pulse.markup.html'))
    if RUNTIME_TAG not in markup:
        sys.exit('runtime script tag not found in pulse.markup.html')
    markup = markup.replace(RUNTIME_TAG, '\n'.join(blocks) + '\n' + RUNTIME_TAG, 1)
    template = markup + read(os.path.join(SRC, 'pulse.component.js')) + read(os.path.join(SRC, 'pulse.tail.html'))

    def island(kind, payload):
        return '  <script type="__bundler/%s">\n%s\n  </script>\n' % (kind, payload)

    page = (read(os.path.join(SRC, 'loader.html'))
            + island('manifest', read(os.path.join(SRC, 'assets.json')))
            + '\n' + island('ext_resources', read(os.path.join(SRC, 'ext_resources.json')))
            + '\n' + island('page_order', '[]')
            + '\n' + island('template', json.dumps(template).replace('</', '<\\u002F'))
            + '</body>\n</html>\n')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'w', encoding='utf-8') as f:
        f.write(page)
    print('wrote %s (%d KB)' % (os.path.relpath(out), len(page.encode('utf-8')) // 1024))


if __name__ == '__main__':
    validate()
    args = sys.argv[1:]
    if '--check' not in args:
        out = args[args.index('--out') + 1] if '--out' in args else OUT
        build(out, '--with-fixtures' in args)
