/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef FW_BUNDLE_H
#define FW_BUNDLE_H

#include <memory>

#include <QByteArray>
#include <QMap>
#include <QString>

#include <common/util/statusor.h>

class FirmwareBundle {
 public:
  FirmwareBundle() {
  }
  virtual ~FirmwareBundle() {
  }

  virtual QString name() const;
  virtual QString version() const;
  virtual QString platform() const;
  virtual QString description() const;
  virtual qint64 buildTimestamp() const;
  virtual QString buildId() const;

  virtual QString getAttr(const QString &key) const = 0;

  struct Part {
    QString name;
    QMap<QString, QString> attrs;
  };

  QMap<QString, Part> parts() const;
  QMap<QString, QByteArray> blobs() const;

 protected:
  QMap<QString, QByteArray> blobs_;
  QMap<QString, Part> parts_;

 private:
  FirmwareBundle(const FirmwareBundle &other) = delete;
};

util::StatusOr<std::unique_ptr<FirmwareBundle>> NewZipFWBundle(
    const QString &zipFileName);

#endif  // FW_BUNDLE_H
