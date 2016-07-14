/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_WIZARD_WIZARD_H_
#define CS_MFT_SRC_WIZARD_WIZARD_H_

#include <memory>

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMainWindow>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSerialPort>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "about_dialog.h"
#include "file_downloader.h"
#include "fw_client.h"
#include "gui_prompter.h"
#include "hal.h"
#include "config.h"
#include "log_viewer.h"
#include "ui_wizard.h"

class WizardDialog : public QMainWindow {
  Q_OBJECT

 public:
  static void addOptions(Config *config);
  WizardDialog(Config *config, QWidget *parent = nullptr);

 private slots:
  void nextStep();
  void prevStep();
  void currentStepChanged();

  void updatePortList();

  void updateReleaseInfo();
  void updateFirmwareSelector();

  void startFirmwareDownload(const QUrl &url);
  void downloadProgress(qint64 recd, qint64 total);
  void downloadFinished();

  void showPrompt(QString text,
                  QList<QPair<QString, Prompter::ButtonRole>> buttons);

  void flashFirmware(const QString &fileName);
  void flasherStatusMessage(QString msg, bool important);
  void flashingProgress(int bytesWritten);
  void flashingDone(QString msg, bool success);

  void fwConnectResult(util::Status st);
  void updateSysConfig(QJsonObject config);
  void doWifiScan();
  void updateWiFiNetworks(QStringList networks);
  void wifiNameChanged();
  void updateWiFiStatus(FWClient::WifiStatus ws);

  void registerDevice();
  void registerDeviceRequestFinished();

  void testCloudConnection(const QString &cloudId, const QString &cloudKey);
  void clubbyStatus(int status);

  void claimBtnClicked();

  void showAboutBox();
  void aboutBoxClosed();

  void showLogViewer();
  void logViewerClosed();

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
    ClaimDevice = 8,

    Invalid = 99,
  };

  Step currentStep() const;

  util::Status doConnect();

  QJsonValue getDevConfKey(const QString &key);
  QJsonValue getDevVar(const QString &var);

  void closeEvent(QCloseEvent *event);

  Config *config_ = nullptr;
  QSettings settings_;

  QTimer portRefreshTimer_;
  std::unique_ptr<HAL> hal_;
  std::unique_ptr<QSerialPort> port_;
  std::unique_ptr<QThread> worker_;
  int bytesToFlash_ = 0;

  QJsonArray releases_;
  std::unique_ptr<FileDownloader> fd_;
  std::unique_ptr<FWClient> fwc_;
  QMap<QString, int> scanResults_;
  bool gotNetworks_ = false;
  QJsonObject devConfig_;
  FWClient::WifiStatus wifiStatus_ = FWClient::WifiStatus::Disconnected;

  QNetworkReply *registerDeviceReply_;

  QString selectedPlatform_;
  QString selectedPort_;
  QUrl selectedFirmwareURL_;
  QString wifiName_;
  QString wifiPass_;
  QString cloudId_;
  QString cloudKey_;

  std::unique_ptr<AboutDialog> aboutBox_;
  std::unique_ptr<LogViewer> logViewer_;
  QNetworkAccessManager nam_;
  GUIPrompter prompter_;
  Ui::WizardWindow ui_;

  const QString skipFlashingText_;

  friend QDebug &operator<<(QDebug &d, const Step s);
};

#endif /* CS_MFT_SRC_WIZARD_WIZARD_H_ */
