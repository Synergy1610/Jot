QT += core gui qml quick quickcontrols2 dbus
CONFIG += c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = jot-native-prototype

SOURCES += main.cpp backend.cpp cloud_worker.cpp hotkey.cpp instance.cpp placement.cpp tray.cpp
HEADERS += backend.h cloud_worker.h hotkey.h instance.h placement.h tray.h ../core/jot_core.h
RESOURCES += resources.qrc

isEmpty(JOT_CORE_LIB): JOT_CORE_LIB = $$PWD/../core/target/debug/libjot_core.a
LIBS += $$quote($$JOT_CORE_LIB) -ldl -lpthread -lm
