#!/usr/bin/env python3
# (364) CHANGELOG.md and CHANGELOG.de.md must match: same versions with the
# same dates, and per version the same number of headings and bullets
# (the app shows the German text per version, falling back to English).
import re, sys, os
root = sys.argv[1] if len(sys.argv) > 1 else '.'
def parse(name):
    out, cur = {}, None
    for line in open(os.path.join(root, name), encoding='utf-8'):
        m = re.match(r'^## (\S+) \((\d{4}-\d{2}-\d{2})\)', line)
        if m:
            cur = m.group(1); out[cur] = {'date': m.group(2), 'h': 0, 'item': 0, 'sub': 0, 'order': len(out)}
            continue
        if cur is None: continue
        if line.startswith('### '): out[cur]['h'] += 1
        elif line.startswith('- '): out[cur]['item'] += 1
        elif re.match(r'^ {2,}- ', line): out[cur]['sub'] += 1
    return out
en, de = parse('CHANGELOG.md'), parse('CHANGELOG.de.md')
problems = 0
for v, e in en.items():
    d = de.get(v)
    if not d:
        print('missing in German: %s' % v); problems += 1; continue
    for k in ('date', 'h', 'item', 'sub', 'order'):
        if e[k] != d[k]:
            print('%s: %s differs (en %s, de %s)' % (v, k, e[k], d[k])); problems += 1
for v in de:
    if v not in en:
        print('only in German: %s' % v); problems += 1
print('%d problems' % problems)
sys.exit(1 if problems else 0)
