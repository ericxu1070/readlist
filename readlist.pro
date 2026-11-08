QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = readlist
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/metadata.h \
    src/paperlist.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/metadata.cpp \
    src/paperlist.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
