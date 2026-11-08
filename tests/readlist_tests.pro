QT += core gui qml quick quickcontrols2 network testlib
CONFIG += c++17 testcase
TARGET = readlist_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/metadata.h ../src/paperlist.h ../src/theme.h ../src/translator.h
SOURCES += readlist_tests.cpp ../src/backend.cpp ../src/metadata.cpp ../src/paperlist.cpp ../src/theme.cpp ../src/translator.cpp
