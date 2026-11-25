QT += core gui qml quick quickcontrols2 testlib sql
CONFIG += c++17 testcase
TEMPLATE = app
TARGET = jot-native-tests
SOURCES += native_tests.cpp ../desktop/backend.cpp ../desktop/cloud_worker.cpp ../desktop/hotkey.cpp ../desktop/hyprland_rule.cpp ../desktop/placement.cpp
HEADERS += ../desktop/backend.h ../desktop/cloud_worker.h ../desktop/hotkey.h ../desktop/hyprland_rule.h ../desktop/placement.h ../core/jot_core.h
RESOURCES += ../desktop/resources.qrc
LIBS += $$PWD/../core/target/debug/libjot_core.a -ldl -lpthread -lm
