QT += widgets uiplugin
CONFIG += c++11 plugin
TEMPLATE = lib

TARGET = $$qtLibraryTarget("progress_widget")
target.path = $$[QT_INSTALL_PLUGINS]/designer
INSTALLS += target

HEADERS = ../progress_widget.h progress_widget_plugin.h
SOURCES = ../progress_widget.cc progress_widget_plugin.cc
RESOURCES = progress_widget.qrc
OTHER_FILES = progress_widget.json
INCLUDEPATH += ..
