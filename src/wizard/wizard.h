/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_WIZARD_WIZARD_H_
#define CS_FNC_SRC_WIZARD_WIZARD_H_

#include <QDebug>
#include <QJsonArray>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>

#include "config.h"
#include "ui_wizard.h"

class WizardDialog : public QMainWindow {
  Q_OBJECT

 public:
  WizardDialog(Config *config, QWidget *parent = nullptr);

 private slots:
  void nextStep();
  void prevStep();
  void currentStepChanged();

  void updatePortList();

  void updateReleaseInfo();
  void updateFirmwareSelector();

 private:
  /* These correspond to widget indices in the stack. */
  enum class Step {
    Begin = 0,
    FirmwareSelection = 1,
    Flashing = 2,
    WiFiConfig = 3,
    WiFiConnect = 4,
    CloudRegistration = 5,
    CloudCredentials = 6,
    CloudConnect = 7,
    Finish = 8,

    Invalid = 99,
  };

  Step currentStep() const;

  void closeEvent(QCloseEvent *event);

  Config *config_ = nullptr;
  QSettings settings_;

  QTimer portRefreshTimer_;
  QJsonArray releases_;

  QString selectedPort_;
  QString selectedPlatform_;
  QString selectedFirmware_;

  Ui::WizardWindow ui_;

  friend QDebug &operator<<(QDebug &d, const Step s);
};

#endif /* CS_FNC_SRC_WIZARD_WIZARD_H_ */
