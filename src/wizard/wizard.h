/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_WIZARD_WIZARD_H_
#define CS_FNC_SRC_WIZARD_WIZARD_H_

#include <QMainWindow>
#include <QSettings>

#include "config.h"
#include "ui_wizard.h"

class WizardDialog : public QMainWindow {
  Q_OBJECT

 public:
  WizardDialog(Config *config, QWidget *parent = nullptr);

 private slots:
   void nextStep();
   void prevStep();

 private:
  void closeEvent(QCloseEvent *event);

  Config *config_ = nullptr;
  QSettings settings_;
  Ui::WizardWindow ui_;
};

#endif /* CS_FNC_SRC_WIZARD_WIZARD_H_ */
