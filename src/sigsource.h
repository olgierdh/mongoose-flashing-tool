/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef SIGSOURCE_H
#define SIGSOURCE_H

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

#endif  // SIGSOURCE_H
