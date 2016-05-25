/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_CC3200_H_
#define CS_FNC_SRC_CC3200_H_

#include <memory>

#include <QSerialPort>

#include "hal.h"

class Config;

namespace CC3200 {

std::unique_ptr<HAL> HAL(QSerialPort *port);

void addOptions(Config *config);

extern const char kFormatFailFS[];

}  // namespace CC3200

#endif /* CS_FNC_SRC_CC3200_H_ */
