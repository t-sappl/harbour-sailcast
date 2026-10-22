#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Checks that every core.<name> used in QML exists in src/appcore.h
# (Q_INVOKABLE method, Q_PROPERTY or signal). Usage: python3 tools/refcheck.py [PROJECT]
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
print("%d unknown references" % missing)
sys.exit(1 if missing else 0)
