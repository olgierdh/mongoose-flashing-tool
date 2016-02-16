#include "flasher.h"

#include <QByteArray>
#include <QDataStream>
#include <QDateTime>

#include "status_qt.h"

const char Flasher::kMergeFSOption[] = "merge-flash-fs";
const char Flasher::kFlashBaudRateOption[] = "flash-baud-rate";
const char Flasher::kDumpFSOption[] = "dump-fs";

QByteArray randomDeviceID(const QString &domain) {
  qsrand(QDateTime::currentMSecsSinceEpoch() & 0xFFFFFFFF);
  QByteArray random;
  QDataStream s(&random, QIODevice::WriteOnly);
  for (int i = 0; i < 6; i++) {
    // Minimal value for RAND_MAX is 32767, so we are guaranteed to get at
    // least 15 bits of randomness. In that case highest bit of each word will
    // be 0, but whatever, we're not doing crypto here (although we should).
    s << qint16(qrand() & 0xFFFF);
    // TODO(imax): use a proper cryptographic PRNG at least for PSK. It must
    // be hard to guess PSK knowing the ID, which is not the case with
    // qrand(): there are at most 2^32 unique sequences.
  }
  return QString("{\"id\":\"//%1/d/%2\",\"key\":\"%3\"}")
      .arg(domain)
      .arg(QString::fromUtf8(random.mid(0, 5).toBase64(
          QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)))
      .arg(QString::fromUtf8(random.mid(5).toBase64(
          QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)))
      .toUtf8();
}

util::StatusOr<quint32> parseSize(const QVariant &value) {
  quint32 v = value.toUInt();
  if (v > 0) return v;  // in bytes.
  const QString vs = value.toString();
  if (!vs.isEmpty()) {
    v = vs.mid(0, vs.length() - 1).toUInt();
    if (v > 0) {
      quint32 multiplier = 0;
      switch (vs.at(vs.length() - 1).toLatin1()) {
        case 'K':
          multiplier = 1024;
          break;
        case 'M':
          multiplier = 1024 * 1024;
          break;
        case 'k':
          multiplier = 1024 / 8;
          break;
        case 'm':
          multiplier = (1024 * 1024) / 8;
          break;
      }
      if (multiplier > 0) {
        return v * multiplier;
      }
    }
  }
  return QS(util::error::INVALID_ARGUMENT,
            QObject::tr("Invalid size spec: %1").arg(vs));
}
