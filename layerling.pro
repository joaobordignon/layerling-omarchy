QT += core gui qml quick quickcontrols2 dbus network webenginequick

CONFIG += c++17 release
TARGET = layerling

# One version for the app and the layerling release it ships: the PKGBUILD's.
LAYERLING_VERSION = $$system(sed -n 's/^pkgver=//p' $$PWD/pkgbuild/PKGBUILD)
DEFINES += LAYERLING_APP_VERSION=\\\"$$LAYERLING_VERSION\\\"
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/files.h \
    src/instance.h \
    src/paths.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/files.cpp \
    src/instance.cpp \
    src/paths.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
