# SPDX-License-Identifier: GPL-3.0-or-later
#
# libsailcast: the app's core (database, feed parsing and updating,
# OPML, importers, sync, downloads, archive). Only QtCore, QtNetwork and
# QtSql - no QML, no Silica - so the same sources build into the Sailfish
# app (harbour-sailcast.pro) and into the desktop test tool (cli/).
#
# Must stay compatible with Qt 5.6 (Sailfish OS): no APIs newer than 5.6,
# even when building the CLI against a newer Qt 5 on the desktop.

QT += network sql
CONFIG += c++11
INCLUDEPATH += $$PWD
DEPENDPATH += $$PWD

HEADERS += \
    $$PWD/archiver.h \
    $$PWD/chaptersloader.h \
    $$PWD/database.h \
    $$PWD/gpodderdevices.h \
    $$PWD/nextcloudlogin.h \
    $$PWD/urlmask.h \
    $$PWD/downloadmanager.h \
    $$PWD/episodeaction.h \
    $$PWD/feedparser.h \
    $$PWD/feedprobe.h \
    $$PWD/feedtypes.h \
    $$PWD/feedupdater.h \
    $$PWD/filenames.h \
    $$PWD/gpodderimport.h \
    $$PWD/netutil.h \
    $$PWD/opml.h \
    $$PWD/podcastindex.h \
    $$PWD/sqltext.h \
    $$PWD/syncengine.h \
    $$PWD/timeparse.h \
    $$PWD/webdavbackup.h

SOURCES += \
    $$PWD/archiver.cpp \
    $$PWD/chaptersloader.cpp \
    $$PWD/database.cpp \
    $$PWD/gpodderdevices.cpp \
    $$PWD/nextcloudlogin.cpp \
    $$PWD/downloadmanager.cpp \
    $$PWD/feedparser.cpp \
    $$PWD/feedprobe.cpp \
    $$PWD/feedupdater.cpp \
    $$PWD/filenames.cpp \
    $$PWD/gpodderimport.cpp \
    $$PWD/opml.cpp \
    $$PWD/podcastindex.cpp \
    $$PWD/syncengine.cpp \
    $$PWD/timeparse.cpp \
    $$PWD/webdavbackup.cpp

# Podcast Index API key (private, not in the repository) - see
# podcastindex.pri.example in the project folder
exists($$PWD/../podcastindex.pri) {
    include($$PWD/../podcastindex.pri)
}
