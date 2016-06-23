/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_CLI_H_
#define CS_MFT_SRC_CLI_H_

#include <memory>

#include <QObject>
#include <QSerialPort>
#include <QString>

#include <common/util/status.h>

#include "hal.h"
#include "prompter.h"

class Config;
class QCommandLineParser;

class CLI : public QObject {
  Q_OBJECT

 public:
  CLI(Config *config, QCommandLineParser *parser, QObject *parent = 0);

 private:
  util::Status flash(const QString &path);
  util::Status console();
  util::Status generateID(const QString &filename, const QString &domain);
  void run();

  Config *config_;
  QCommandLineParser *parser_;
  std::unique_ptr<HAL> hal_;
  std::unique_ptr<QSerialPort> port_;
  Prompter *prompter_;
};

#endif /* CS_MFT_SRC_CLI_H_ */
