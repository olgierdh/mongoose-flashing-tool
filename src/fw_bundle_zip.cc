#include "fw_bundle.h"

#include <cstring>

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include "status_qt.h"

#include "common/miniz.c"

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

namespace {
const char kManifestFileName[] = "manifest.json";
}  // namespace

class ZipFWBundle : public FirmwareBundle {
 public:
  ZipFWBundle() {
    std::memset(&zip_, 0, sizeof(zip_));
  }
  virtual ~ZipFWBundle() {
    mz_zip_reader_end(&zip_);
  }

  // FirmwareBundle interface.
  QString getAttr(const QString &key) const override {
    return manifest_[key].toString();
  }

  util::Status loadFile(const QString &zipFileName);

 private:
  util::Status loadContents();
  util::Status readManifest();

  mz_zip_archive zip_;
  QJsonObject manifest_;
};

util::Status ZipFWBundle::loadFile(const QString &zipFileName) {
  qInfo() << "Loading" << zipFileName;
  QByteArray fn = zipFileName.toUtf8().append('\0');  // NUL-terminate.
  mz_bool status = mz_zip_reader_init_file(&zip_, fn.data(), 0);
  if (!status)
    return QS(util::error::UNAVAILABLE, "mz_zip_reader_init_file failed");
  qInfo() << mz_zip_reader_get_num_files(&zip_) << "files";
  auto st = loadContents();
  if (!st.ok()) return QSP("failed to load archive contents", st);
  st = readManifest();
  if (!st.ok()) return QSP("failed to read manifest", st);
  return util::Status::OK;
}

util::Status ZipFWBundle::loadContents() {
  for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip_); i++) {
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&zip_, i, &stat)) {
      return QS(util::error::INVALID_ARGUMENT,
                QObject::tr("failed to stat file #%1").arg(i));
    }
    QString name(stat.m_filename);
    QString base_name = name.split("/").back();
    size_t uncomp_size;
    char *data =
        (char *) mz_zip_reader_extract_to_heap(&zip_, i, &uncomp_size, 0);
    if (data == NULL) {
      return QS(util::error::INVALID_ARGUMENT,
                QObject::tr("failed to extract %1").arg(name));
    }
    QByteArray data_array(data, uncomp_size);
    qDebug() << "Blob" << base_name << data_array.length();
    mz_free(data);
    blobs_[base_name] = data_array;
  }
  return util::Status::OK;
}

util::Status ZipFWBundle::readManifest() {
  if (!blobs_.contains(kManifestFileName)) {
    return QS(util::error::INVALID_ARGUMENT,
              QObject::tr("No %1 in archive").arg(kManifestFileName));
  }
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(blobs_[kManifestFileName], &err);
  if (err.error != QJsonParseError::NoError) {
    return QS(util::error::INVALID_ARGUMENT,
              QObject::tr("Failed to parse JSON: %1").arg(err.errorString()));
  }
  if (!doc.isObject()) {
    return QS(util::error::INVALID_ARGUMENT, QObject::tr("not an object"));
  }
  manifest_ = doc.object();
  // TODO(rojer): More validation here.
  if (manifest_.contains("parts")) {
    for (const QString &partName : manifest_["parts"].toObject().keys()) {
      const auto &v = manifest_["parts"].toObject()[partName];
      if (!v.isObject()) {
        return QS(util::error::INVALID_ARGUMENT,
                  QObject::tr("part %1 is not an object").arg(partName));
      }
      const QJsonObject &jsonPart = v.toObject();
      Part p;
      p.name = partName;
      for (const QString &attr : jsonPart.keys()) {
        p.attrs[attr] = jsonPart[attr].toString();
      }
      parts_[partName] = p;
    }
  }
  return util::Status::OK;
}

util::StatusOr<std::unique_ptr<FirmwareBundle>> NewZipFWBundle(
    const QString &zipFileName) {
  std::unique_ptr<ZipFWBundle> zfb(new ZipFWBundle());
  auto st = zfb->loadFile(zipFileName);
  if (!st.ok()) return st;
  return std::unique_ptr<FirmwareBundle>(
      static_cast<FirmwareBundle *>(zfb.release()));
}
