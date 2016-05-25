/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_FLASHER_H_
#define CS_FNC_SRC_FLASHER_H_

#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QVariant>

#include <common/util/statusor.h>

#include "fw_bundle.h"

class Config;
class QByteArray;
class QSerialPortInfo;
class QVariant;

// Flasher overwrites the firmware on the device with a new image.
// Same object can be re-used, just call load() again to load a new image
// or setPort() to change the serial port before calling run() again.
class Flasher : public QObject {
  Q_OBJECT

 public:
  virtual ~Flasher(){};
  // Sets the firmware bundle to be flashed. Implementation should perform any
  // platform-specific validation necessary and return OK if the fw is good.
  virtual util::Status setFirmware(FirmwareBundle *fw) = 0;
  // totalBytes should return the number of bytes in the loaded firmware.
  // It is used to track the progress of flashing.
  virtual int totalBytes() const = 0;
  // run should actually do the flashing. It needs to be started in a separate
  // thread as it does not return until the flashing is done (or failed).
  virtual void run() = 0;
  // setOption should set a named option to a given value, returning non-OK
  // status on error or if option is not known.
  virtual util::Status setOption(const QString &name,
                                 const QVariant &value) = 0;
  // setOptionsFromConfig should extract known options from parser, returning
  // non-OK status if there were any errors.
  virtual util::Status setOptionsFromConfig(const Config &config) = 0;

  static const char kMergeFSOption[];
  static const char kFlashBaudRateOption[];
  static const char kDumpFSOption[];

signals:
  void progress(int blocksWritten);
  void statusMessage(QString message, bool important = false);
  void done(QString message, bool success);
};

QByteArray randomDeviceID(const QString &domain);
util::StatusOr<quint32> parseSize(const QVariant &value);

#endif /* CS_FNC_SRC_FLASHER_H_ */
