/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef PROMPTER_H
#define PROMPTER_H

#include <QList>
#include <QObject>
#include <QPair>

class Prompter : public QObject {
  Q_OBJECT
 public:
  enum class ButtonRole { Yes, No, Reject };

  Prompter(QObject *parent) : QObject(parent) {
    qRegisterMetaType<QList<QPair<QString, ButtonRole>>>(
        "QList<QPair<QString,Prompter::ButtonRole> >");
  }

  virtual int Prompt(QString text,
                     QList<QPair<QString, ButtonRole>> buttons) = 0;

  virtual ~Prompter() {
  }
};

#endif  // PROMPTER_H
