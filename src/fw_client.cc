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

namespace {
const char kPromptEnd[] = "] $ ";
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
  port_->write(
      R"(Wifi.scan(function (a) {)" BEGIN_MARKER_JS
      R"(print(JSON.stringify({t:"wsr", r:a}));)" END_MARKER_JS "});\n");
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
  buf_ += port_->readAll();
  if (!connected_) {
    if (buf_.endsWith(kPromptEnd)) {
      connected_ = true;
      qInfo() << "Connected to FW";
      emit connectResult(util::Status::OK);
    }
    return;
  }
  while (true) {
    const int beginIndex = buf_.indexOf(beginMarker_);
    const int endIndex = buf_.indexOf(endMarker_);
    qDebug() << beginIndex << endIndex;
    if (beginIndex < 0 || endIndex < beginIndex) return;
    const QByteArray content =
        buf_.mid(beginIndex + beginMarker_.length(),
                 endIndex - beginIndex - beginMarker_.length());
    qDebug() << content;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(content, &err);
    if (err.error == QJsonParseError::NoError) {
      if (doc.isObject() && doc.object()["t"].isString()) {
        const QString type = doc.object()["t"].toString();
        if (type == "wsr") {
          QStringList networks;
          for (const auto &v : doc.object()["r"].toArray()) {
            networks.push_back(v.toString());
          }
          emit wifiScanResult(networks);
        }
      } else {
        qCritical() << "Invalid JSON:" << content;
      }
    } else {
      const QString msg(
          tr("Failed to parse JSON: %1").arg(content.toStdString().c_str()));
      qCritical() << msg;
    }
    buf_ = buf_.mid(endIndex + endMarker_.length());
  }
}
