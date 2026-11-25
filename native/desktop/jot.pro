QT += core gui qml quick quickcontrols2 dbus
CONFIG += c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = jot
DEFINES += JOT_PRODUCTION

SOURCES += main.cpp backend.cpp cloud_worker.cpp hotkey.cpp hyprland_rule.cpp instance.cpp placement.cpp tray.cpp
HEADERS += backend.h cloud_worker.h hotkey.h hyprland_rule.h instance.h placement.h tray.h ../core/jot_core.h
RESOURCES += resources.qrc

# The installed build always links the optimized core.
isEmpty(JOT_CORE_LIB): JOT_CORE_LIB = $$PWD/../core/target/release/libjot_core.a
LIBS += $$quote($$JOT_CORE_LIB) -ldl -lpthread -lm
