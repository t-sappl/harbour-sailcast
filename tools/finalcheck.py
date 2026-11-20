#!/usr/bin/env python3
# (315) QML properties that shadow a property of Item (FINAL in Qt 5.6:
# "Cannot override FINAL property" - the whole page fails to load; happened
# with "top" on the statistics page in 309 and 315) and duplicate signal
# handlers in the same object at the top level of a file (312).
import re, sys, os, collections
root = sys.argv[1] if len(sys.argv) > 1 else "."
names = ("top bottom left right x y z width height parent data children resources state states "
         "visible enabled opacity scale rotation clip focus anchors baseline horizontalCenter "
         "verticalCenter implicitWidth implicitHeight objectName layer smooth antialiasing transform "
         "transitions activeFocus transformOrigin childrenRect").split()
decl = re.compile(r"^\s*(readonly\s+)?property\s+\w+(<\w+>)?\s+(" + "|".join(names) + r")\s*[:\n]")
errors = 0
for dirpath, _, files in os.walk(os.path.join(root, "qml")):
    for f in files:
        if not f.endswith(".qml"):
            continue
        path = os.path.join(dirpath, f)
        lines = open(path, encoding="utf-8").read().split("\n")
        for i, line in enumerate(lines, 1):
            if decl.match(line + "\n"):
                print("%s:%d: property shadows an Item property: %s" % (path, i, line.strip()))
                errors += 1
        top = collections.Counter(re.findall(r"^    (on[A-Z]\w*)\s*:", "\n".join(lines), re.M))
        for name, n in top.items():
            if n > 1:
                print("%s: handler %s %d times at the top level" % (path, name, n))
                errors += 1
# (356) No thread_local in the app: the booster loads it with dlopen, and
# thread-local storage then crashed in other threads (351: the log handler,
# segfault when switching to the app overview, Lipstick went down with it)
for sub in ("src", "core"):
    for dirpath, _, files in os.walk(os.path.join(root, sub)):
        for f in files:
            if not f.endswith((".cpp", ".h")):
                continue
            path = os.path.join(dirpath, f)
            for i, line in enumerate(open(path, encoding="utf-8").read().split("\n"), 1):
                code = line.split("//")[0]
                if re.search(r"\bthread_local\b|__thread\b", code):
                    print("%s:%d: thread_local in the app (booster/dlopen, see 356)" % (path, i))
                    errors += 1
print("%d problems" % errors)
sys.exit(1 if errors else 0)
