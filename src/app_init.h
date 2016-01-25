#ifndef APP_INIT_H
#define APP_INIT_H

#include <QCommandLineParser>

#include <common/util/status.h>

#include "config.h"

// Initialization bits common to GUI and CLI-only builds.
util::Status initApp(int argc, char *argv[], Config *config,
                     QCommandLineParser *parser);

#endif
