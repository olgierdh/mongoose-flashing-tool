/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef ESP8266_H
#define ESP8266_H

#include <memory>

#include <QString>

#include <common/util/status.h>
#include <common/util/statusor.h>

#include "hal.h"

class Config;

namespace ESP8266 {

util::StatusOr<int> flashParamsFromString(const QString &s);

void addOptions(Config *parser);

QByteArray makeIDBlock(const QString &domain);

std::unique_ptr<HAL> HAL();

}  // namespace ESP8266

#endif  // ESP8266_H
