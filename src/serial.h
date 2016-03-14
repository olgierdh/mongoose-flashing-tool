/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_SERIAL_H_
#define CS_FNC_SRC_SERIAL_H_

#include <QSerialPortInfo>

#include <common/util/statusor.h>

class QSerialPort;

util::StatusOr<QSerialPort *> connectSerial(const QSerialPortInfo &port,
                                            int speed = 115200);

util::Status setSpeed(QSerialPort *port, int speed);

#endif /* CS_FNC_SRC_SERIAL_H_ */
