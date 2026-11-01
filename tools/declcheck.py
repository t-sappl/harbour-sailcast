#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Declarations lost in QML (142): compares the QML files with those of a
# previous delivery and lists names (property, function, signal, id) that
# were declared there, are gone now and are still used in the file.
# qmlcheck only checks the syntax; an undeclared name fails at run time
# (142: SearchPage lost searchText and others in a rewrite).
# Usage: python3 tools/declcheck.py <project dir> <previous zip>
import glob, os, re, sys, zipfile

PATTERN = r'(?:property\s+\w+(?:<\w+>)?\s+|function\s+|signal\s+)(\w+)|\bid:\s*(\w+)'

def names(text):
    return {a or b for a, b in re.findall(PATTERN, text)}

root, previous = sys.argv[1], sys.argv[2]
old = zipfile.ZipFile(previous)
members = set(old.namelist())
problems = 0
for path in sorted(glob.glob(os.path.join(root, 'qml', '**', '*.qml'), recursive=True)):
    rel = os.path.relpath(path, root)
    if rel not in members:
        continue
    text = open(path, encoding='utf-8').read()
    lost = names(old.read(rel).decode('utf-8')) - names(text)
    used = sorted(n for n in lost if re.search(r'(?<![\w.])' + n + r'\b', text))
    for name in used:
        print('%s: "%s" no longer declared but still used' % (rel, name))
        problems += 1
print('%d lost declarations' % problems)
sys.exit(1 if problems else 0)
