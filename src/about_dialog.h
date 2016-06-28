/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_ABOUT_H_
#define CS_MFT_SRC_ABOUT_H_

#include <QWidget>

#include "ui_about.h"

class AboutDialog : public QWidget {
  Q_OBJECT

 public:
  AboutDialog(QWidget *parent);
  virtual ~AboutDialog();

signals:
  void closed();

 private:
  void closeEvent(QCloseEvent *event);

  Ui::About ui_;
};

#endif /* CS_MFT_SRC_ABOUT_H_ */
