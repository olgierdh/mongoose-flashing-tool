/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_ESP_FLASHER_CLIENT_H_
#define CS_FNC_SRC_ESP_FLASHER_CLIENT_H_

#include <QObject>
#include <QSerialPort>

#include "esp_rom_client.h"

#include <common/platforms/esp8266/stubs/stub_flasher.h>

class ESPFlasherClient : public QObject {
  Q_OBJECT
 public:
  ESPFlasherClient(ESPROMClient *rom);
  virtual ~ESPFlasherClient();

  const quint32 kFlashSectorSize = 4096;

  // Load the flasher stub.
  util::Status connect(qint32 baudRate);

  // Disconnect from the flasher stub. The stub stays running.
  util::Status disconnect();

  // Erase a region of SPI flash.
  // Address and size must be aligned to flash sector size.
  util::Status erase(quint32 addr, quint32 size);

  // Write a region of SPI flash. Performs erase before writing.
  // Address and size must be aligned to flash sector size.
  util::Status write(quint32 addr, QByteArray data, bool erase);

  // Read a region of SPI flash.
  // No special alignment requirements.
  util::StatusOr<QByteArray> read(quint32 addr, quint32 size);

  // Compute MD5 digest of SPI flash contents.
  // No special alignment requirements.
  typedef struct {
    QByteArray digest;
    QVector<QByteArray> blockDigests;
  } DigestResult;
  util::StatusOr<DigestResult> digest(quint32 addr, quint32 size,
                                      quint32 digestBlockSize);

  util::StatusOr<quint32> getFlashChipID();

  util::Status eraseChip();

  util::Status bootFirmware();

  util::Status reboot();

signals:
  void progress(quint32 bytes);

 private:
  util::Status simpleCmd(enum stub_cmd cmd, const QString &name, int timeoutMs);

  ESPROMClient *rom_;  // Not owned.
  qint32 oldBaudRate_ = 0;
};

#endif /* CS_FNC_SRC_ESP_FLASHER_CLIENT_H_ */
