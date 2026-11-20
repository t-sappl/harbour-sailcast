# Developer tools (not part of the app)

Used by Claude to check deliveries without Qt libraries; they also run on a normal Linux PC.

## Translations – `gen_ts.py`

Regenerates `translations/harbour-sailcast-de.ts` from all `qsTr()` strings in `qml/` and `tr()` in `src/appcore.cpp` (context = QML file name or `AppCore`, numerus forms when a count argument is given). Existing translations are kept (matched by source text); new strings are listed as MISSING and the file is not written until they are supplied:

```
python3 tools/gen_ts.py .                      # check / write
python3 tools/gen_ts.py . extra.json           # extra.json: {"English": "Deutsch", "%n x": ["%n Ding", "%n Dinge"]}
```

French (since 19.11.2026): `--lang fr` works on `translations/harbour-sailcast-fr.ts`; `--seed other.ts` also takes translations from another file (e.g. a contributor's TS from a pull request; `unfinished` entries are ignored); `--dump-missing` writes the missing sources to `/tmp/missing_fr.json` as a template. The English plural file is only written for German.

```
python3 tools/gen_ts.py . --lang fr --seed pr-fr.ts --dump-missing   # what is missing
python3 tools/gen_ts.py . fr-extra.json --lang fr --seed pr-fr.ts    # write
```

## SQL – `sqlcheck.py`

Builds the schema (all `SchemaV*` arrays) in an in-memory SQLite and runs `EXPLAIN` on every statement in `core/database.cpp`:

```
python3 tools/sqlcheck.py
```

## QML → AppCore references – `refcheck.py`

Every `core.<name>` in the QML files must be a `Q_INVOKABLE`, `Q_PROPERTY` or signal of `src/appcore.h`:

```
python3 tools/refcheck.py
```

## C++ syntax against the Qt headers

```
git clone --depth 1 -b 5.15 https://github.com/qt/qtbase.git
cd qtbase && perl bin/syncqt.pl -module QtCore -module QtNetwork -module QtSql -module QtGui -version 5.15.2 -outdir ../build
```

Then create the missing feature headers (`qtcore-config.h`, `qtnetwork-config.h`, `qtsql-config.h`, `qtgui-config.h`, `qconfig.h`) in `build/include/...`; `qt-allfeat.h` is the permissive set used so far (include it from each of them; set OpenGL/Vulkan features to -1). Check a file with:

```
B=build/include
g++ -std=gnu++11 -fsyntax-only -Wall -Wextra -DQT_NO_DEBUG -I$B -I$B/QtCore -I$B/QtNetwork -I$B/QtSql -I$B/QtGui \
    -Iqtbase/mkspecs/linux-g++ -Icore -Isrc core/database.cpp
```

`src/harbour-sailcast.cpp` (QtQuick, sailfishapp) and `src/secretstore.cpp` (Sailfish Secrets) cannot be checked this way. For `networkaccess.h`, a stub `QQmlNetworkAccessManagerFactory` header (abstract class with `create(QObject*)`) is enough.

## QML syntax – `qmlcheck.c`

```
git clone https://github.com/tree-sitter/tree-sitter
git clone https://github.com/yuja/tree-sitter-qmljs
gcc -O2 -Itree-sitter/lib/include -Itree-sitter/lib/src tools/qmlcheck.c tree-sitter/lib/src/lib.c \
    tree-sitter-qmljs/src/parser.c tree-sitter-qmljs/src/scanner.c -Itree-sitter-qmljs/src -o qmlcheck
./qmlcheck qml/*.qml qml/pages/*.qml qml/cover/*.qml
```

Only syntax: wrong property names or Qt 5.7+ features (e.g. `padding` on Text, `Qt.callLater`) are not detected – keep to QtQuick 2.6.

## Show-notes chapters – `chapterstest/` (344)

Tests of `core/shownoteschapters.cpp` (cases 1–30 of DESIGN-INTERNAL "Plan: Shownotes-Kapitel-Parser"):

```
cd tools/chapterstest && qmake-qt5 && make && ./chapterstest
```

Without qmake, against the system QtCore (as in Claude's sandbox): `g++ -std=gnu++11 -fPIC -I<qt headers> -I../../core main.cpp ../../core/shownoteschapters.cpp -lQt5Core`. On real data: CLI `shownotes-chapters all` on a copy of the database.

## Update check – `updatetest/`

Unit tests of the pure functions of `core/updatechecker` (version compare, architecture, choice of the release asset, plain release notes, clean-up rules). With qmake on a desktop Qt 5: `cd tools/updatetest && qmake-qt5 && make && ./updatetest`. Claude (no moc) compiles a copy without the network part against the system QtCore.

## Release notes – `release_notes.py`

`python3 tools/release_notes.py 1.5` writes `release-notes-1.5.md` from the section "## 1.5" of `CHANGELOG.md`, with the install instructions and the credit line (RELEASE-CHECKLIST E). `updatetest/` also tests the app's side (`core/releasenotes`).

- `dupcheck.py [dir]` (352): findet Eigenschaften/Handler, die im selben QML-Objekt zweimal gesetzt sind (z. B. zwei `Component.onCompleted`) – QML lehnt dann die ganze Datei ab („Could not load page“); qmlcheck sieht das nicht.

- `changelogcheck.py [dir]` (364): CHANGELOG.md und CHANGELOG.de.md müssen zusammenpassen – gleiche Versionen und Daten, pro Version gleich viele Überschriften, Punkte und Unterpunkte (die App zeigt auf deutschen Telefonen den deutschen Text pro Version).

## Properties of own components – `propcheck.py`

A property set on an instance of one of our own QML components (`PodcastTile { started: … }`) must be declared in that component. Qt would otherwise drop the whole surrounding component ("Type FolderSection unavailable", 367). Names that Qt/Silica base types have and `on…` handlers are not checked:

```
python3 tools/propcheck.py .
```
