TEMPLATE = app
!macx:TARGET = ../fnc
macx:TARGET = "FNC"
INCLUDEPATH += . ../..
QT += widgets serialport network
CONFIG += c++11

COMMON_PATH = ../../common
SPIFFS_PATH = $${COMMON_PATH}/spiffs
UTIL_PATH = $${COMMON_PATH}/util

QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.9

# Input
HEADERS += \
  cc3200.h \
  cli.h \
  config.h \
  dialog.h \
  esp8266.h \
  esp8266_fw_loader.h \
  esp_flasher_client.h \
  esp_rom_client.h \
  flasher.h \
  fs.h \
  fw_loader.h \
  log.h \
  prompter.h \
  serial.h \
  settings.h \
  sigsource.h \
  slip.h \
  status_qt.h

SOURCES += \
  cc3200.cc \
  cli.cc \
  config.cc \
  dialog.cc \
  esp8266.cc \
  esp8266_fw_loader.cc \
  esp_flasher_client.cc \
  esp_rom_client.cc \
  flasher.cc \
  fs.cc \
  fw_loader.cc \
  log.cc \
  main.cc \
  serial.cc \
  settings.cc \
  slip.cc \
  status_qt.cc

DEFINES += VERSION=\\\"$$VERSION\\\"
DEFINES += APP_NAME=\\\"$$TARGET\\\"

INCLUDEPATH += $${UTIL_PATH}
SOURCES += \
  $${UTIL_PATH}/error_codes.cc \
  $${UTIL_PATH}/logging.cc \
  $${UTIL_PATH}/status.cc

INCLUDEPATH += $${SPIFFS_PATH}
SOURCES += \
  $${SPIFFS_PATH}/spiffs_cache.c \
  $${SPIFFS_PATH}/spiffs_gc.c \
  $${SPIFFS_PATH}/spiffs_nucleus.c \
  $${SPIFFS_PATH}/spiffs_check.c \
  $${SPIFFS_PATH}/spiffs_hydrogen.c
DEFINES += SPIFFS_TEST_VISUALISATION=1 SPIFFS_HAL_CALLBACK_EXTRA=1

unix {
  SOURCES += sigsource_unix.cc
} else {
  SOURCES += sigsource_dummy.cc
}

RESOURCES = blobs.qrc images.qrc
FORMS = main.ui about.ui settings.ui

# libftdi stuff.
macx {
  # Works for libftdi installed with Homebrew: brew install libftdi
  INCLUDEPATH += /usr/local/include/libftdi1
  LIBS += -L/usr/local/lib -lftdi1
} else:unix {
  # Works on recent Ubuntu: apt-get install libftdi-dev
  INCLUDEPATH += /usr/include
  LIBS += -L/usr/lib/x86_64-linux-gnu -lftdi
}
win32 {
 DEFINES += NO_LIBFTDI
}

macx {
  QMAKE_INFO_PLIST = Info.plist.in
  ICON = smartjs.icns
}

win32 {
  QMAKE_TARGET_COMPANY = "Cesanta"
  RC_ICONS = smartjs.ico
}
