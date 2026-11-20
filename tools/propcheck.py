#!/usr/bin/env python3
# Every property set on an instance of one of our own QML components must be
# declared in that component (or its own base component). Base types from Qt /
# Silica are unknown here, so a name is only reported when it is declared in
# none of our component files and is not in the list of common Qt/Silica names.
# (368: PodcastTile got "started:" – the whole FolderSection became unavailable.)
# Usage: python3 tools/propcheck.py [project dir]
import re, os, sys, glob
root = sys.argv[1] if len(sys.argv) > 1 else '.'
files = glob.glob(os.path.join(root, 'qml/**/*.qml'), recursive=True)
comp = {os.path.splitext(os.path.basename(f))[0]: f for f in files}
decl, base = {}, {}
for name, f in comp.items():
    t = open(f, encoding='utf-8').read()
    t = re.sub(r'//[^\n]*', '', t)
    m = re.search(r'^\s*([A-Z]\w*)\s*\{', t, re.M)
    base[name] = m.group(1) if m else None
    d = set(re.findall(r'\bproperty\s+(?:alias|var|int|real|bool|string|url|color|date|list<\w+>|variant|double|\w+)\s+(\w+)', t))
    d |= set(re.findall(r'\bsignal\s+(\w+)', t))
    d |= {'on' + s[0].upper() + s[1:] for s in re.findall(r'\bsignal\s+(\w+)', t)}
    d |= {'on' + p[0].upper() + p[1:] + 'Changed' for p in list(d)}
    decl[name] = d
def declared(name, seen=()):
    out = set(decl.get(name, ()))
    b = base.get(name)
    if b in comp and b not in seen:
        out |= declared(b, seen + (name,))
    return out
# names that Qt Quick / Silica base types have (BackgroundItem, ListItem,
# ValueButton, TextSwitch, Label, MouseArea …); not reported
COMMON = set('''width height x y z anchors visible enabled opacity clip color text label
description value checked highlighted contentHeight contentWidth menu down pressed
font horizontalAlignment verticalAlignment wrapMode elide source fillMode icon
spacing padding leftMargin rightMargin topMargin bottomMargin radius border
automaticCheck busy currentIndex hint placeholderText inputMethodHints focus
scale rotation states transitions layer objectName parent children data
implicitWidth implicitHeight baselineOffset activeFocusOnTab sourceSize
truncationMode maximumLineCount lineHeight textFormat linkColor
'''.split())
problems = 0
for f in sorted(files):
    lines = open(f, encoding='utf-8').read().split('\n')
    stack = []
    for i, line in enumerate(lines, 1):
        code = re.sub(r'//.*', '', line)
        m = re.match(r'\s*(?:[\w.]+\s*:\s*)?([A-Z]\w*)\s*\{', code)
        opened = code.count('{') - code.count('}')
        if m:
            stack.append([m.group(1), 0])
            stack[-1][1] += opened
        elif stack:
            if stack[-1][1] == 1 and stack[-1][0] in comp:
                pm = re.match(r'\s*([a-z]\w*)\s*:(?!:)', code)
                if pm and not re.match(r'\s*\w+\s*:\s*[A-Z]\w*\s*\{', code):
                    p = pm.group(1)
                    if re.match(r'on[A-Z]', p) or p in COMMON:
                        stack[-1][1] += opened; continue
                    c = stack[-1][0]
                    ok = declared(c)
                    if p not in ok and base.get(c) not in comp and p in set().union(*decl.values()) - ok:
                        # declared in another of our components but not here: likely a mistake
                        print('%s:%d: %s has no property "%s"' % (os.path.relpath(f, root), i, c, p)); problems += 1
            stack[-1][1] += opened
        while stack and stack[-1][1] <= 0:
            stack.pop()
print(problems, 'problems')
