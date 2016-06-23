/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_PROMPTER_H_
#define CS_MFT_SRC_PROMPTER_H_

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

#endif /* CS_MFT_SRC_PROMPTER_H_ */
