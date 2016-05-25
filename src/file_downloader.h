/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_FILE_DOWNLOADER_H_
#define CS_FNC_SRC_FILE_DOWNLOADER_H_

#include <memory>

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTemporaryFile>
#include <QUrl>

#include <common/util/status.h>

class FileDownloader : public QObject {
  Q_OBJECT

 public:
  FileDownloader(const QUrl &url);
  QUrl url() const;
  util::Status status() const;
  QString fileName() const;

  void start();
  void abort();

signals:
  void progress(qint64 recd, qint64 total);
  void finished();

 private slots:
  void networkRequestProgress(qint64 recd, qint64 total);
  void networkRequestFinished();

 private:
  void startURL(const QUrl &url);

  const QUrl url_;
  QNetworkAccessManager nam_;
  std::unique_ptr<QTemporaryFile> tempFile_;
  QByteArray etag_;
  QNetworkReply *reply_;
  util::Status status_;
};

#endif /* CS_FNC_SRC_FILE_DOWNLOADER_H_ */
