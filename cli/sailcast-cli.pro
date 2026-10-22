# SPDX-License-Identifier: GPL-3.0-or-later
#
# sailcast-cli: command line tool to test the Sailcast core on the desktop
# (CachyOS/Arch: needs qt5-base). Not part of the Sailfish package.
#
#   cd cli && qmake-qt5 && make -j$(nproc)
#   ./sailcast-cli --help

TEMPLATE = app
TARGET = sailcast-cli
QT -= gui
CONFIG += console
CONFIG -= app_bundle

include(../core/core.pri)

SOURCES += main.cpp
