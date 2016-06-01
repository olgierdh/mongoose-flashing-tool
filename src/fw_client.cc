#include "fw_client.h"

#include <QDebug>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "status_qt.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

#define BEGIN_MARKER "=== BEGIN ==="
#define BEGIN_MARKER_JS "print('=== ' + 'BEGIN' + ' ===');"
#define END_MARKER "=== END ==="
#define END_MARKER_JS "print('=== ' + 'END' + ' ===');"

#define SYS_CONFIG_TYPE "sys"
#define WIFI_SCAN_RESULT_TYPE "wsr"
#define WIFI_STATUS_TYPE "ws"
#define CLUBBY_STATUS_TYPE "cs"

namespace {
const char kPromptEnd[] = "] $ ";

QString jsEscapeString(const QString &s) {
  QString escaped(s);
  escaped = escaped.replace(R"(\)", R"(\\)");
  escaped = escaped.replace("'", R"(\')");
  return QString("'%1'").arg(escaped);
}
}

FWClient::FWClient(QSerialPort *port)
    : beginMarker_(BEGIN_MARKER),
      endMarker_(END_MARKER),
      port_(port),
      connected_(false),
      connectAttempt_(0) {
  connect(port_, &QSerialPort::readyRead, this, &FWClient::portReadyRead);
}

FWClient::~FWClient() {
  connectTimer_.stop();
  disconnect(port_, &QSerialPort::readyRead, this, &FWClient::portReadyRead);
}

void FWClient::doConnect() {
  connected_ = false;
  connectAttempt_ = 0;
  doConnectAttempt();
}

void FWClient::doWifiScan() {
  if (!connected_) return;
  qInfo() << "doWifiScan";
  cmdQueue_.push_back(
      R"(Wifi.scan(function (a) {)" BEGIN_MARKER_JS
      R"(print(JSON.stringify({t:')" WIFI_SCAN_RESULT_TYPE
      R"(', r:a}));)" END_MARKER_JS "});\n");
  sendCommand();
}

void FWClient::doGetConfig() {
  if (!connected_) return;
  qInfo() << "doGetConfig";
  cmdQueue_.push_back(BEGIN_MARKER_JS
                      R"(print(JSON.stringify({t:')" SYS_CONFIG_TYPE
                      R"(', sys:Sys}));)" END_MARKER_JS "\n");
  sendCommand();
}

void FWClient::doWifiSetup(const QString &ssid, const QString &password) {
  if (!connected_) return;
  qInfo() << "doWifiSetup" << ssid << (password.isEmpty() ? "" : "(password)");
  cmdQueue_.push_back(
      R"(Wifi.changed(function (s) {)" BEGIN_MARKER_JS
      R"(print(JSON.stringify({t:')" WIFI_STATUS_TYPE
      R"(', ws:s}));)" END_MARKER_JS "});\n");
  cmdQueue_.push_back(QString("Wifi.setup(%1, %2);\n")
                          .arg(jsEscapeString(ssid))
                          .arg(jsEscapeString(password))
                          .toUtf8());
  sendCommand();
}

void FWClient::testClubbyConfig(const QJsonObject &cfg) {
  if (!connected_) return;
  QJsonObject cf(cfg);
  cf["connect"] = false;
  cf["reconnect_timeout_max"] = 0;
  qInfo() << "testClubbyConfig" << cf;
  QJsonDocument doc(cf);
  cmdQueue_.push_back(QString("c = new Clubby(%1);\n")
                          .arg(QString(doc.toJson(QJsonDocument::Compact)))
                          .toUtf8());
  cmdQueue_.push_back(
      R"(c.onopen(function (s) {)" BEGIN_MARKER_JS
      R"(print(JSON.stringify({t:')" CLUBBY_STATUS_TYPE
      R"(', cs:1}));)" END_MARKER_JS "});\n");
  cmdQueue_.push_back(
      R"(c.onclose(function (s) {)" BEGIN_MARKER_JS
      R"(print(JSON.stringify({t:')" CLUBBY_STATUS_TYPE
      R"(', cs:0}));)" END_MARKER_JS "});\n");
  cmdQueue_.push_back("c.connect();\n");
  sendCommand();
}

void FWClient::doConnectAttempt() {
  if (connected_) return;
  if (connectAttempt_++ > 6) {
    emit connectResult(QS(util::error::UNAVAILABLE,
                          "Unable to communicate with the firmware."));
    return;
  }
  qInfo() << "Connecting to FW, attempt" << connectAttempt_;
  buf_.clear();
  port_->readAll();  // Discard everything in the buffer up till now.
  port_->write("\n");
  QTimer::singleShot(1000, this, &FWClient::doConnectAttempt);
}

void FWClient::portReadyRead() {
  {
    QByteArray buf = port_->readAll();
    qDebug() << buf;
    buf_ += buf;
  }
  if (buf_.endsWith(kPromptEnd)) {
    if (!connected_) {
      connected_ = true;
      qInfo() << "Connected to FW";
      emit connectResult(util::Status::OK);
    }
    if (!cmdQueue_.isEmpty()) sendCommand();
    buf_.clear();
  }
  while (true) {
    const int beginIndex = buf_.indexOf(beginMarker_);
    const int endIndex = buf_.indexOf(endMarker_);
    if (beginIndex < 0 || endIndex < beginIndex) return;
    qDebug() << beginIndex << endIndex;
    const QByteArray content =
        buf_.mid(beginIndex + beginMarker_.length(),
                 endIndex - beginIndex - beginMarker_.length());
    qDebug() << content;
    parseMessage(content);
    buf_ = buf_.mid(endIndex + endMarker_.length());
  }
}

void FWClient::sendCommand() {
  const QByteArray cmd = cmdQueue_.front().toUtf8();
  cmdQueue_.pop_front();
  qDebug() << "Cmd:" << cmd;
  port_->write(cmd);
}

void FWClient::parseMessage(const QByteArray &msg) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(msg, &err);
  if (err.error != QJsonParseError::NoError) {
    const QString msg(
        tr("Failed to parse JSON: %1").arg(msg.toStdString().c_str()));
    qCritical() << msg;
    return;
  }
  if (!doc.isObject() || !doc.object()["t"].isString()) {
    qCritical() << "Invalid message format:" << msg;
    return;
  }
  const QJsonObject &o = doc.object();
  const QString type = o["t"].toString();
  if (type == WIFI_SCAN_RESULT_TYPE) {
    QStringList networks;
    for (const auto &v : o["r"].toArray()) {
      networks.push_back(v.toString());
    }
    emit wifiScanResult(networks);
  } else if (type == WIFI_STATUS_TYPE) {
    WifiStatus ws;
    switch (o["ws"].toInt()) {
      case 0:
        ws = WifiStatus::Disconnected;
        break;
      case 1:
        ws = WifiStatus::Connected;
        break;
      case 2:
        ws = WifiStatus::IP_Acquired;
        break;
      default:
        qCritical() << "Invalid wifi status:" << o["ws"].toInt();
        return;
    }
    emit wifiStatusChanged(ws);
  } else if (type == SYS_CONFIG_TYPE) {
    emit getConfigResult(o["sys"].toObject());
  } else if (type == CLUBBY_STATUS_TYPE) {
    emit clubbyStatus(o["cs"].toInt());
  } else {
    qCritical() << "Unknown messgae type:" << type << msg;
  }
}
