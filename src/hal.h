/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_HAL_H_
#define CS_FNC_SRC_HAL_H_

#include <memory>

#include <common/util/status.h>

#include "flasher.h"
#include "prompter.h"

class QSerialPort;
class QSerialPortInfo;

class HAL {
 public:
  virtual ~HAL(){};
  virtual util::Status probe() const = 0;
  virtual std::unique_ptr<Flasher> flasher(Prompter *prompter) const = 0;
  virtual std::string name() const = 0;
  virtual util::Status reboot(QSerialPort *) const = 0;
};

#endif /* CS_FNC_SRC_HAL_H_ */
