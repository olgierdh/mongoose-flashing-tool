/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_SIGSOURCE_H_
#define CS_FNC_SRC_SIGSOURCE_H_

#include <QObject>

class SigSource : public QObject {
  Q_OBJECT
 public:
  SigSource(QObject *parent) : QObject(parent) {
  }
  virtual ~SigSource() {
  }

signals:
  void flash();
  void connectDisconnect();
};

SigSource *initSignalSource(QObject *parent);

#endif /* CS_FNC_SRC_SIGSOURCE_H_ */
