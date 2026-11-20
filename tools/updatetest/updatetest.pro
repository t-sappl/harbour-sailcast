# SPDX-License-Identifier: GPL-3.0-or-later
# Unit tests of UpdateChecker's pure functions (desktop Qt 5, no Sailfish)
TEMPLATE = app
TARGET = updatetest
CONFIG += console c++11
CONFIG -= app_bundle
QT = core network
INCLUDEPATH += ../../core
SOURCES += main.cpp ../../core/updatechecker.cpp ../../core/releasenotes.cpp
HEADERS += ../../core/updatechecker.h ../../core/releasenotes.h ../../core/netutil.h
