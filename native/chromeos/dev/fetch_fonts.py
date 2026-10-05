#!/usr/bin/env python3
"""Downloads the harness fonts (Google Sans Flex, Noto Sans Arabic) once.

ChromeOS ships Google Sans and Noto Sans Arabic, so the real app never
loads web fonts. The dev harness runs on a plain Linux box, so it fetches
the open versions from Google Fonts into a cache and serves them locally.

Usage: fetch_fonts.py CACHE_DIR
"""
import os
import re
import sys
import urllib.request

CSS_URL = ('https://fonts.googleapis.com/css2?family=Google+Sans+Flex:'
           'wght@400..800&family=Noto+Sans+Arabic:wght@400..800&display=swap')
UA = ('Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/130.0 Safari/537.36')


def get(url):
  req = urllib.request.Request(url, headers={'User-Agent': UA})
  with urllib.request.urlopen(req, timeout=60) as r:
    return r.read()


def main(cache):
  os.makedirs(cache, exist_ok=True)
  out_css = os.path.join(cache, 'fonts.css')
  if os.path.exists(out_css):
    return 0
  css = get(CSS_URL).decode('utf-8')
  n = 0

  def local(m):
    nonlocal n
    n += 1
    name = 'f%02d.woff2' % n
    with open(os.path.join(cache, name), 'wb') as f:
      f.write(get(m.group(1)))
    return 'url(fonts/%s)' % name

  css = re.sub(r'url\((https://[^)]+)\)', local, css)
  with open(out_css, 'w', encoding='utf-8') as f:
    f.write(css)
  print('fetched %d font files' % n)
  return 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1]))
