#include "fw_client.h"

#include <cctype>

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

}  // namespace

FWClient::FWClient(QSerialPort *port)
    : beginMarker_(BEGIN_MARKER), endMarker_(END_MARKER), port_(port) {
  connect(port_, &QSerialPort::readyRead, this, &FWClient::portReadyRead);
}

FWClient::~FWClient() {
  connectTimer_.stop();
  disconnect(port_, &QSerialPort::readyRead, this, &FWClient::portReadyRead);
}

void FWClient::doConnect() {
  connected_ = false;
  scanning_ = false;
  connectAttempt_ = 0;
  doConnectAttempt();
}

void FWClient::doWifiScan() {
  if (!connected_ || scanning_) return;
  qInfo() << "doWifiScan";
  cmdQueue_.push_back(
      R"(Wifi.scan(function (a) {)" BEGIN_MARKER_JS
      R"(print(JSON.stringify({t:')" WIFI_SCAN_RESULT_TYPE
      R"(', r:a}));)" END_MARKER_JS "});");
  sendCommand();
}

void FWClient::doGetConfig() {
  if (!connected_) return;
  qInfo() << "doGetConfig";
  cmdQueue_.push_back(BEGIN_MARKER_JS
                      "print(JSON.stringify({t:'" SYS_CONFIG_TYPE
                      "', "
                      "sys:{conf:{wifi:{sta:{ssid:Sys.conf.wifi.sta.ssid,"
                      "pass:Sys.conf.wifi.sta.pass}},"
                      "clubby:{server_address:Sys.conf.clubby.server_address,"
                      "device_id:Sys.conf.clubby.device_id,"
                      "device_psk:Sys.conf.clubby.device_psk}},"
                      "ro_vars:Sys.ro_vars}}));)" END_MARKER_JS);
  sendCommand();
}

void FWClient::doWifiSetup(const QString &ssid, const QString &password) {
  if (!connected_) return;
  qInfo() << "doWifiSetup" << ssid << (password.isEmpty() ? "" : "(password)");
  cmdQueue_.push_back(
      R"(Wifi.changed(function (s) {)" BEGIN_MARKER_JS
      R"(print(JSON.stringify({t:')" WIFI_STATUS_TYPE
      R"(', ws:s}));)" END_MARKER_JS "});");
  cmdQueue_.push_back(QString("Wifi.setup(%1, %2);")
                          .arg(jsEscapeString(ssid))
                          .arg(jsEscapeString(password))
                          .toUtf8());
  sendCommand();
}

void FWClient::testClubbyConfig(const QJsonObject &cfg) {
  if (!connected_) return;
  QJsonObject cf(cfg);
  clubbyTestId_++;
  cf["connect"] = false;
  cf["reconnect_timeout_max"] = 0;
  qInfo() << "testClubbyConfig" << clubbyTestId_ << cf;
  QJsonDocument doc(cf);
  cmdQueue_.push_back(QString("c = new Clubby(%1);")
                          .arg(QString(doc.toJson(QJsonDocument::Compact)))
                          .toUtf8());
  cmdQueue_.push_back(QString(R"(c.onopen(function (s) {)" BEGIN_MARKER_JS
                              R"(print(JSON.stringify({t:')" CLUBBY_STATUS_TYPE
                              R"(', id:%1, cs:1}));)" END_MARKER_JS
                              "});").arg(clubbyTestId_));
  cmdQueue_.push_back(QString(R"(c.onclose(function (s) {)" BEGIN_MARKER_JS
                              R"(print(JSON.stringify({t:')" CLUBBY_STATUS_TYPE
                              R"(', id:%1, cs:0}));)" END_MARKER_JS
                              "});").arg(clubbyTestId_));
  cmdQueue_.push_back("c.connect();");
  sendCommand();
}

void FWClient::setConfValue(const QString &k, const QJsonValue &v) {
  if (!connected_) return;
  QString vs;
  switch (v.type()) {
    case QJsonValue::Null: {
      vs = "null";
      break;
    }
    case QJsonValue::Bool: {
      vs = (v.toBool() ? "true" : "false");
      break;
    }
    case QJsonValue::Double: {
      if (v.toDouble() == v.toInt()) {
        vs = QString("%1").arg(v.toInt());
      } else {
        vs = QString("%1").arg(v.toDouble());
      }
      break;
    }
    case QJsonValue::String: {
      vs = jsEscapeString(v.toString());
      break;
    }
    default:
      // Unsupported value.
      return;
  }
  const QString cmd = QString("Sys.conf.%1 = %2;").arg(k).arg(vs);
  qInfo() << cmd;
  cmdQueue_.push_back(cmd);
  sendCommand();
}

void FWClient::doSaveConfig() {
  if (!connected_) return;
  qInfo() << "doSaveConfig";
  cmdQueue_.push_back("Sys.conf.save();");
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
    qDebug() << "Got" << buf.length() << "bytes, total" << buf_.length();
    qDebug() << buf;
    buf_ += buf;
  }
  // Consume all the responses in the buffer.
  int beginIndex, endIndex;
  while (true) {
    beginIndex = buf_.indexOf(beginMarker_);
    endIndex = buf_.indexOf(endMarker_);
    if (beginIndex < 0 || endIndex < beginIndex) break;
    qDebug() << "Found message @" << beginIndex << "-" << endIndex;
    const QByteArray content =
        buf_.mid(beginIndex + beginMarker_.length(),
                 endIndex - beginIndex - beginMarker_.length());
    qDebug() << content;
    parseMessage(content);
    buf_ = buf_.left(beginIndex) + buf_.mid(endIndex + endMarker_.length());
    qDebug() << buf_;
    while (buf_.length() > 0 && !buf_.endsWith(kPromptEnd) &&
           std::isspace(buf_[buf_.length() - 1])) {
      buf_.chop(1);
    }
    beginIndex = -1;
  }
  // Sync with the device by waiting for prompt to appear.
  // If we are receiving a message, don't mess with the buffer.
  qDebug() << beginIndex << buf_;
  if (beginIndex < 0 && buf_.endsWith(kPromptEnd)) {
    buf_.clear();
    sending_ = false;
    if (!connected_) {
      connected_ = true;
      qInfo() << "Connected to FW";
      emit connectResult(util::Status::OK);
    }
  }
  qDebug() << buf_.length() << "bytes left in the buffer;" << cmdQueue_.length()
           << "commands pending; sending?" << sending_;
  if (!cmdQueue_.isEmpty()) sendCommand();
}

void FWClient::sendCommand() {
  if (sending_) return;
  const QByteArray cmd = (cmdQueue_.front() + "\n").toUtf8();
  cmdQueue_.pop_front();
  qDebug() << "Cmd:" << cmd;
  port_->write(cmd);
  sending_ = true;
}

void FWClient::parseMessage(const QByteArray &msg) {
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(msg, &err);
  if (err.error != QJsonParseError::NoError) {
    const QString emsg(
        tr("Failed to parse JSON: %1").arg(msg.toStdString().c_str()));
    qCritical() << emsg;
    return;
  }
  if (!doc.isObject() || !doc.object()["t"].isString()) {
    qCritical() << "Invalid message format:" << msg;
    return;
  }
  const QJsonObject &o = doc.object();
  const QString type = o["t"].toString();
  if (type == WIFI_SCAN_RESULT_TYPE) {
    scanning_ = false;
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
    if (o["id"].toInt() == clubbyTestId_) {
      clubbyTestId_++;
      emit clubbyStatus(o["cs"].toInt());
    } else {
      // Old test, ignore.
    }
  } else {
    qCritical() << "Unknown message type:" << type << msg;
  }
}
