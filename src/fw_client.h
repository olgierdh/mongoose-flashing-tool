/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_FW_CLIENT_H_
#define CS_MFT_SRC_FW_CLIENT_H_

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <common/util/status.h>

class FWClient : public QObject {
  Q_OBJECT

 public:
  FWClient(QSerialPort *port);
  ~FWClient() override;

  void doConnect();
  void doGetConfig();
  void doWifiScan();
  void doWifiSetup(const QString &ssid, const QString &password);
  void testClubbyConfig(const QJsonObject &cfg);
  void setConfValue(const QString &k, const QJsonValue &v);
  void doSaveConfig();

  // This is sj_wifi_status, reproduced here to avoid dependency.
  enum class WifiStatus {
    Disconnected = 0,
    Connected = 1,
    IP_Acquired = 2,
  };

signals:
  void connectResult(util::Status result);
  void getConfigResult(QJsonObject config);
  void wifiScanResult(QStringList networks);
  void wifiStatusChanged(WifiStatus ws);
  void clubbyStatus(int status);

 private slots:
  void portReadyRead();

 private:
  void doConnectAttempt();
  void sendCommand();
  void parseMessage(const QByteArray &msg);

  const QString beginMarker_;
  const QString endMarker_;
  QSerialPort *port_;

  int clubbyTestId_ = 0;

  QTimer connectTimer_;
  bool connected_ = false;
  bool sending_ = false;
  bool scanning_ = false;
  int connectAttempt_ = 0;
  QByteArray buf_;
  QStringList cmdQueue_;
};

#endif /* CS_MFT_SRC_FW_CLIENT_H_ */
