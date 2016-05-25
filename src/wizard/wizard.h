/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_WIZARD_WIZARD_H_
#define CS_FNC_SRC_WIZARD_WIZARD_H_

#include <QDebug>
#include <QJsonArray>
#include <QMainWindow>
#include <QSerialPort>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "file_downloader.h"
#include "gui_prompter.h"
#include "hal.h"
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

  void startFirmwareDownload();
  void downloadProgress(qint64 recd, qint64 total);
  void downloadFinished();

  void showPrompt(QString text,
                  QList<QPair<QString, Prompter::ButtonRole>> buttons);

  void flashFirmware(const QString &fileName);
  void flashingProgress(int bytesWritten);
  void flashingDone(QString msg, bool success);

signals:
  void showPromptResult(int clicked_button);

 private:
  /* These correspond to widget indices in the stack. */
  enum class Step {
    Connect = 0,
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
  std::unique_ptr<HAL> hal_;
  std::unique_ptr<QSerialPort> port_;
  std::unique_ptr<QThread> worker_;
  int bytesToFlash_;

  QJsonArray releases_;
  std::unique_ptr<FileDownloader> fd_;

  QString selectedPlatform_;
  QUrl selectedFirmwareURL_;

  GUIPrompter prompter_;
  Ui::WizardWindow ui_;

  friend QDebug &operator<<(QDebug &d, const Step s);
};

#endif /* CS_FNC_SRC_WIZARD_WIZARD_H_ */
