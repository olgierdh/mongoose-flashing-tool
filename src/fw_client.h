/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_FW_CLIENT_H_
#define CS_FNC_SRC_FW_CLIENT_H_

#include <QByteArray>
#include <QStringList>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QTimer>

#include <common/util/status.h>

class FWClient : public QObject {
  Q_OBJECT

 public:
  FWClient(QSerialPort *port);
  ~FWClient() override;

  void doConnect();
  void doWifiScan();
  void doWifiSetup(const QString &ssid, const QString &password);

  // This is sj_wifi_status, reproduced here to avoid dependency.
  enum class WifiStatus {
    Disconnected = 0,
    Connected = 1,
    IP_Acquired = 2,
  };

signals:
  void connectResult(util::Status result);
  void wifiScanResult(QStringList networks);
  void wifiStatusChanged(WifiStatus ws);

 private slots:
  void portReadyRead();

 private:
  void doConnectAttempt();
  void parseMessage(const QByteArray &msg);

  const QString beginMarker_;
  const QString endMarker_;
  QSerialPort *port_;

  QTimer connectTimer_;
  bool connected_;
  int connectAttempt_;
  QByteArray buf_;
};

#endif /* CS_FNC_SRC_FW_CLIENT_H_ */
