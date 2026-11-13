QT += core gui quick quickcontrols2 testlib dbus network webenginequick
CONFIG += c++17 testcase
TARGET = layerling_tests
TEMPLATE = app

DEFINES += LAYERLING_APP_VERSION=\\\"test\\\"
INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/cli.h ../src/files.h ../src/instance.h ../src/paths.h ../src/theme.h ../src/translator.h
SOURCES += layerling_tests.cpp ../src/backend.cpp ../src/cli.cpp ../src/files.cpp ../src/instance.cpp ../src/paths.cpp \
    ../src/theme.cpp ../src/translator.cpp
