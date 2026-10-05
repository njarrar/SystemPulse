#!/usr/bin/env python3
"""Flattens i18n/test/fixtures/vectors.json into a TSV the C tests can read.

Lines:  cat <TAB> code <TAB> value <TAB> category
        sample <TAB> code <TAB> spec <TAB> expected   (spec as in vectors.json)
"""
import json, sys
v = json.load(open(sys.argv[1], encoding='utf-8'))
out = open(sys.argv[2], 'w', encoding='utf-8')
for code, d in v.items():
    for value, cat in d['categories']:
        out.write('cat\t%s\t%s\t%s\n' % (code, value, cat))
    for spec, exp in d['samples'].items():
        out.write('sample\t%s\t%s\t%s\n' % (code, spec, exp))
