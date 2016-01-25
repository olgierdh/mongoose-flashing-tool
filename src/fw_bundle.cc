#include "fw_bundle.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

QString FirmwareBundle::name() const {
  return getAttr("name");
}

QString FirmwareBundle::platform() const {
  return getAttr("platform");
}

QString FirmwareBundle::version() const {
  return getAttr("version");
}

QString FirmwareBundle::description() const {
  return getAttr("description");
}

qint64 FirmwareBundle::buildTimestamp() const {
  return getAttr("build_timestamp").toLong();
}

QString FirmwareBundle::buildId() const {
  return getAttr("build_id");
}

QMap<QString, FirmwareBundle::Part> FirmwareBundle::parts() const {
  return parts_;
}

QMap<QString, QByteArray> FirmwareBundle::blobs() const {
  return blobs_;
}
