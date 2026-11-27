# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sailplay application SDK v1, provider side only (see README.md): the
# files are copied unchanged from Sailplay, so Sailcast builds without
# Sailplay being installed on the build host.

QT += dbus
INCLUDEPATH += $$PWD

HEADERS += $$PWD/sailplayprovider.h \
    $$PWD/sailplaycontent.h
SOURCES += $$PWD/sailplayprovider.cpp \
    $$PWD/sailplaycontent.cpp

# The provider reads its D-Bus contracts from :/sailplay-sdk/interfaces;
# it loads the resource by this file name (Q_INIT_RESOURCE)
RESOURCES += $$PWD/qmake_sailplay_contracts.qrc

# As in Sailplay's own build: the navigation checks need exact float math
QMAKE_CXXFLAGS += -fno-fast-math
