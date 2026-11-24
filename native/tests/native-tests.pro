QT += core gui qml quick quickcontrols2 testlib sql
CONFIG += c++17 testcase
TEMPLATE = app
TARGET = jot-native-tests
SOURCES += native_tests.cpp ../desktop/backend.cpp ../desktop/hotkey.cpp ../desktop/placement.cpp
HEADERS += ../desktop/backend.h ../desktop/hotkey.h ../desktop/placement.h ../core/jot_core.h
RESOURCES += ../desktop/resources.qrc
LIBS += $$PWD/../core/target/debug/libjot_core.a -ldl -lpthread -lm
