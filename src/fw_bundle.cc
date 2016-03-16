#include "fw_bundle.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include "status_qt.h"

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

util::StatusOr<QByteArray> FirmwareBundle::getPartSource(
    const QString &partName) const {
  if (!parts_.contains(partName)) {
    return QS(util::error::INVALID_ARGUMENT,
              QObject::tr("No %1 in fw bundle").arg(partName));
  }
  const Part &p = parts_[partName];
  const QString src = p.attrs["src"];
  if (src == "") {
    return QS(util::error::INVALID_ARGUMENT,
              QObject::tr("part %1: no source specified").arg(p.name));
  }
  if (!blobs_.contains(src)) {
    return QS(
        util::error::INVALID_ARGUMENT,
        QObject::tr("part %1: source %2 does not exist").arg(p.name).arg(src));
  }
  const QByteArray &data = blobs_[src];
  const QString &expected_digest = p.attrs["cs_sha1"].toLower();
  if (expected_digest == "") {
    return QS(util::error::INVALID_ARGUMENT,
              QObject::tr("part %1: missing SHA1 digest").arg(p.name));
  }
  const QString &digest =
      QCryptographicHash::hash(data, QCryptographicHash::Sha1)
          .toHex()
          .toLower();
  if (digest != expected_digest) {
    return QS(util::error::INVALID_ARGUMENT,
              QObject::tr("part %1: invalid digest - expected %2, got %3")
                  .arg(p.name)
                  .arg(expected_digest)
                  .arg(digest));
  }
  return data;
}
