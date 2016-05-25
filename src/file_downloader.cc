#include "file_downloader.h"

#include <QNetworkReply>
#include <QNetworkRequest>

#include "status_qt.h"

FileDownloader::FileDownloader(const QUrl &url) : url_(url) {
}

QUrl FileDownloader::url() const {
  return url_;
}

util::Status FileDownloader::status() const {
  return status_;
}

QString FileDownloader::fileName() const {
  if (tempFile_ == nullptr) return "";
  return tempFile_->fileName();
}

void FileDownloader::start() {
  startURL(url_);
}

void FileDownloader::startURL(const QUrl &url) {
  QNetworkRequest req(url);
  if (!etag_.isEmpty()) {
    req.setRawHeader(QByteArray("If-None-Match"), etag_);
  }
  reply_ = nam_.get(req);
  connect(reply_, &QNetworkReply::downloadProgress, this,
          &FileDownloader::networkRequestProgress);
  connect(reply_, &QNetworkReply::finished, this,
          &FileDownloader::networkRequestFinished);
}

void FileDownloader::networkRequestProgress(qint64 recd, qint64 total) {
  qDebug() << "networkRequestProgress" << recd << total;
  if (total < 5000) return;
  emit progress(recd, total);
}

void FileDownloader::networkRequestFinished() {
  if (reply_ == nullptr) return;
  int code =
      reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  qDebug() << "networkRequestFinished" << reply_ << code;
  if (reply_->error()) {
    status_ = QS(util::error::UNAVAILABLE, reply_->errorString());
    emit finished();
  } else {
    QVariant redir =
        reply_->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (!redir.isNull()) {
      reply_->deleteLater();
      QUrl newURL = url_.resolved(redir.toUrl());
      qDebug() << "Redirected to" << newURL;
      startURL(newURL);
      return;
    } else {
      if (code == 200) {
        qDebug() << "Download finished," << reply_->bytesAvailable() << "bytes";
        tempFile_.reset(new QTemporaryFile());
        if (tempFile_->open()) {
          QByteArray data = reply_->readAll();
          if (tempFile_->write(data) == data.length() && tempFile_->flush()) {
            status_ = util::Status::OK;
            etag_ = reply_->rawHeader("ETag");
            if (etag_.startsWith("W/")) etag_.clear();
            qDebug() << "Wrote" << data.length() << "bytes to"
                     << tempFile_->fileName() << "ETag" << etag_;
          } else {
            status_ = QS(
                util::error::UNAVAILABLE,
                tr("Failed to write data: %1").arg(tempFile_->errorString()));
          }
        } else {
          status_ =
              QS(util::error::UNAVAILABLE, tr("Failed to create temp file: %1")
                                               .arg(tempFile_->errorString()));
        }
      } else if (code == 304) {
        qDebug() << "Not modified";
        status_ = util::Status::OK;
      } else {
        status_ = QS(util::error::INTERNAL, tr("Bad error code %1").arg(code));
      }
    }
  }
  reply_->deleteLater();
  reply_ = nullptr;
  emit finished();
}

void FileDownloader::abort() {
  if (reply_ != nullptr) reply_->abort();
}
