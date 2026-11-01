#!/usr/bin/env python3
# Checks all SQL statements of core/database.cpp against the schema (v1..vN)
# with sqlite3 EXPLAIN - catches typos in table/column names.
# Usage: python3 tools/sqlcheck.py   (from the project root)
import re, sqlite3
src = open('core/database.cpp').read()
def lits(block): return re.findall(r'"((?:[^"\\]|\\.)*)"', block)
db = sqlite3.connect(':memory:')
for m in re.finditer(r'const char \*const (SchemaV\d+)\[\] = \{(.*?)\n\};', src, re.S):
    body = re.sub(r'^\s*//.*$', '', m.group(2), flags=re.M)
    cur = []
    for tok in re.findall(r'"(?:[^"\\]|\\.)*"|,', body):
        if tok == ',':
            if cur: db.execute(''.join(cur).encode().decode('unicode_escape')); cur = []
        else: cur.append(tok[1:-1])
    if cur: db.execute(''.join(cur).encode().decode('unicode_escape'))
cols = {}
for name in ('EpisodeColumns', 'PodcastColumns'):
    m = re.search(name + r'\s*=(.*?);\n', src, re.S); cols[name] = ''.join(lits(m.group(1)))
stmts = [cols['PodcastColumns'] + " GROUP BY p.id", cols['EpisodeColumns'] + " LIMIT 1"]
for p in re.findall(r'(?:prepare|exec)\(\s*QStringLiteral\(((?:\s*"(?:[^"\\]|\\.)*")+)\s*\)', src):
    stmts.append(''.join(lits(p)).encode().decode('unicode_escape'))
ok = 0; bad = []
for s in stmts:
    try: db.execute("EXPLAIN " + s, [None] * s.count('?')); ok += 1
    except Exception as e: bad.append((s[:90], str(e)))
print(ok, 'statements OK')
for b in bad: print('ERROR', b)
# The highest SchemaV* step must equal SchemaVersion (130: it stayed at 8
# after v9 was added, and a migrated database could not be opened again)
steps = [int(n) for n in re.findall(r'const char \*const SchemaV(\d+)\[\]', src)]
declared = int(re.search(r'const int SchemaVersion = (\d+);', src).group(1))
if steps and max(steps) != declared:
    print('ERROR SchemaVersion is', declared, 'but the highest step is SchemaV%d' % max(steps))
else:
    print('SchemaVersion', declared, 'OK')

