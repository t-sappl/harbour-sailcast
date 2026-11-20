#!/usr/bin/env python3
# (352) A property or handler set twice in the same QML object (e.g. two
# Component.onCompleted) makes QML refuse the whole file - Silica then shows
# "Could not load page". qmlcheck does not see it. Usage: dupcheck.py [dir]
import re, glob, sys, os
root = sys.argv[1] if len(sys.argv) > 1 else '.'
n = 0
for f in sorted(glob.glob(os.path.join(root, 'qml/**/*.qml'), recursive=True)):
    stack = [{}]
    for i, l in enumerate(open(f, encoding='utf-8').read().split('\n'), 1):
        code = re.sub(r'"(?:[^"\\]|\\.)*"', '""', l)
        code = re.sub(r'//.*', '', code)
        # (review 353) also "property type name" / "readonly property ..."
        p = re.match(r'\s*(?:readonly\s+|default\s+)?property\s+[\w.<>]+\s+([A-Za-z_]\w*)', code)
        m = re.match(r'\s*([A-Za-z_][\w.]*)\s*:(?!:)', code)
        if p:
            m = p
        if m and not re.match(r'\s*(case|default)\b', code) or p:
            name = m.group(1)
            if name in stack[-1]:
                print("%s:%d %s set twice (first line %d)" % (os.path.relpath(f, root), i, name, stack[-1][name]))
                n += 1
            stack[-1].setdefault(name, i)
        for ch in code:
            if ch == '{':
                stack.append({})
            elif ch == '}' and len(stack) > 1:
                stack.pop()
print("%d duplicates" % n)
sys.exit(1 if n else 0)
