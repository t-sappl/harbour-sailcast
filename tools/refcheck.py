#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Checks that every core.<name> used in QML exists in src/appcore.h
# (Q_INVOKABLE method, Q_PROPERTY or signal), and (349) that every file in
# qml/pages and qml/cover that uses one of the app's own components (qml/*.qml)
# imports "../" - without it the whole page fails to load ("Page could not
# be loaded", MobileDataDialog since 294). Usage: python3 tools/refcheck.py [PROJECT]
import glob, os, re, sys

root = sys.argv[1] if len(sys.argv) > 1 else "."
header = open(os.path.join(root, "src/appcore.h")).read()
names = set(re.findall(r"Q_INVOKABLE\s+[\w<>:, ]+?\s+(\w+)\(", header))
names |= set(re.findall(r"Q_PROPERTY\(\s*[\w<>]+\s+(\w+)", header))
signals = header.split("signals:")[1].split("private:")[0]
names |= set(re.findall(r"void (\w+)\(", signals))

missing = 0
for path in sorted(glob.glob(os.path.join(root, "qml/**/*.qml"), recursive=True)):
    for name in sorted(set(re.findall(r"\bcore\.(\w+)", open(path).read()))):
        if name not in names:
            print("MISSING core.%s in %s" % (name, os.path.relpath(path, root)))
            missing += 1
own = {os.path.basename(f)[:-4] for f in glob.glob(os.path.join(root, "qml/*.qml"))}
for sub in ("qml/pages", "qml/cover"):
    for path in sorted(glob.glob(os.path.join(root, sub, "*.qml"))):
        text = open(path).read()
        if 'import "../"' in text:
            continue
        used = sorted({t for t in re.findall(r"\b([A-Z][A-Za-z0-9]+)\s*\{", text) if t in own})
        if used:
            print("NO IMPORT \"../\" in %s, uses %s" % (os.path.relpath(path, root), ", ".join(used)))
            missing += 1
print("%d unknown references" % missing)
sys.exit(1 if missing else 0)
