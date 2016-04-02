/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_CLI_H_
#define CS_FNC_SRC_CLI_H_

#include <memory>

#include <QObject>
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
  util::Status listPorts();
  util::Status probePort(const QString &portName);
  util::Status flash(const QString &portName, const QString &path);
  util::Status console(const QString &portName);
  util::Status generateID(const QString &filename, const QString &domain);
  void run();

  Config *config_;
  QCommandLineParser *parser_;
  std::unique_ptr<HAL> hal_;
  Prompter *prompter_;
};

#endif /* CS_FNC_SRC_CLI_H_ */
