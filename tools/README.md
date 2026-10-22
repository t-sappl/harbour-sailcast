# Developer tools (not part of the app)

Used by Claude to check deliveries without Qt libraries; they also run on a normal Linux PC.

## Translations – `gen_ts.py`

Regenerates `translations/harbour-sailcast-de.ts` from all `qsTr()` strings in `qml/` and `tr()` in `src/appcore.cpp` (context = QML file name or `AppCore`, numerus forms when a count argument is given). Existing translations are kept (matched by source text); new strings are listed as MISSING and the file is not written until they are supplied:

```
python3 tools/gen_ts.py .                      # check / write
python3 tools/gen_ts.py . extra.json           # extra.json: {"English": "Deutsch", "%n x": ["%n Ding", "%n Dinge"]}
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
