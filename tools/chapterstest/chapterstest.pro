# SPDX-License-Identifier: GPL-3.0-or-later
# (344) Tests of the show-notes chapter parser: qmake-qt5 && make && ./chapterstest
TEMPLATE = app
TARGET = chapterstest
QT -= gui
CONFIG += console c++11
CONFIG -= app_bundle
INCLUDEPATH += ../../core
SOURCES += main.cpp ../../core/shownoteschapters.cpp
HEADERS += ../../core/shownoteschapters.h ../../core/feedtypes.h
