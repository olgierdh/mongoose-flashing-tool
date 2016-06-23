/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_APP_INIT_H_
#define CS_MFT_SRC_APP_INIT_H_

#include <QCommandLineParser>

#include <common/util/status.h>

#include "config.h"

// Initialization bits common to GUI and CLI-only builds.
util::Status initApp(int *argc, char *argv[], Config *config,
                     QCommandLineParser *parser);

#endif /* CS_MFT_SRC_APP_INIT_H_ */
