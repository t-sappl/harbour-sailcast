#!/usr/bin/env python3
# (338) Player bar per page: evaluates PlayerBar.isHiddenPage with node for
# every page and dialog in qml/pages (objectName, hidesPlayerBar) and checks
# it against the decision of 16 Nov (VISIBLE below). Run from the project
# folder: python3 tools/playerbarcheck.py
import os, re, subprocess, sys, json

VISIBLE = {"homePage", "podcastsPage", "episodesPage", "playlistPage", "downloadsPage",
           "searchPage", "localSearchPage", "addPodcastDialog", "bookmarksPage",
           "continueListeningPage", "statisticsPage", "CategoriesPage", "CategoryPage", "SimilarPage"}

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
bar = open(os.path.join(root, "qml", "PlayerBar.qml"), encoding="utf-8").read()
i = bar.index("return page !== null")
j = bar.index("\n    }", i)
expr = re.sub(r"//[^\n]*", "", bar[i + len("return "):j])

pages = []
pdir = os.path.join(root, "qml", "pages")
for name in sorted(os.listdir(pdir)):
    if not name.endswith(".qml"):
        continue
    text = open(os.path.join(pdir, name), encoding="utf-8").read()
    top = re.search(r"^(Page|Dialog) \{", text, re.M)
    if not top:
        continue          # components, menus, items
    m = re.search(r'objectName: "([^"]*)"', text)
    pages.append({"file": name[:-4], "objectName": m.group(1) if m else "",
                  "hidesPlayerBar": "readonly property bool hidesPlayerBar: true" in text})

js = "function hidden(page){ return %s }\nvar pages=%s;\nconsole.log(JSON.stringify(pages.map(function(p){return hidden(p)})))" % (expr, json.dumps(pages))
out = subprocess.run(["node", "-e", js], capture_output=True, text=True)
if out.returncode != 0:
    print(out.stderr); sys.exit(2)
result = json.loads(out.stdout)
problems = 0
for p, h in zip(pages, result):
    key = p["objectName"] or p["file"]
    want_visible = key in VISIBLE
    ok = (not h) == want_visible
    problems += 0 if ok else 1
    print("%s %-8s %-28s %s" % ("ok  " if ok else "FAIL", "visible" if not h else "hidden", key, "" if ok else "(expected %s)" % ("visible" if want_visible else "hidden")))
print("%d problems" % problems)
sys.exit(1 if problems else 0)
