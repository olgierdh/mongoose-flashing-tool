#include "cc3200.h"

#include <map>
#include <memory>
#include <vector>
#ifdef Q_OS_OSX
#include <sys/ioctl.h>
#include <IOKit/serial/ioss.h>
#endif

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QThread>
#ifndef NO_LIBFTDI
#include <ftdi.h>
#endif

#include <common/util/status.h>
#include <common/util/statusor.h>

#include "config.h"
#include "fs.h"
#include "serial.h"
#include "status_qt.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

namespace CC3200 {

const char kFormatFailFS[] = "cc3200-format-sflash";

namespace {

const int kSerialSpeed = 921600;
const int kVendorID = 0x0451;
const int kProductID = 0xC32A;
const int kDefaultTimeoutMs = 1000;

const int kStorageID = 0;
const char kFWFilename[] = "/sys/mcuimg.bin";
const char kFWBundleFWPartNameOld[] = "sys_mcuimg.bin";  // Backward compat.
const char kFWBundleFSPartName[] = "fs.img";
const char kFS0Filename[] = "0.fs";
const char kFS1Filename[] = "1.fs";
const int kBlockSizes[] = {0x100, 0x400, 0x1000, 0x4000, 0x10000};
const int kFileUploadBlockSize = 4096;
const int kSPIFFSMetadataSize = 64;

const int kFileOpenModeCreateIfNotExist = 0x3000;
const int kFileOpenModeSecure = 0x20000;
const int kFileSignatureLength = 256;

const char kOpcodeStartUpload = 0x21;
const char kOpcodeFinishUpload = 0x22;

const char kOpcodeFileChunk = 0x24;

const char kOpcodeFormatFlash = 0x28;

const char kOpcodeGetFileInfo = 0x2A;
const char kOpcodeReadFileChunk = 0x2B;

const char kOpcodeStorageWrite = 0x2D;
const char kOpcodeFileErase = 0x2E;
const char kOpcodeGetVersionInfo = 0x2F;

const char kOpcodeEraseBlocks = 0x30;
const char kOpcodeGetStorageInfo = 0x31;
const char kOpcodeExecFromRAM = 0x32;
const char kOpcodeSwitchUART2Apps = 0x33;

const qint32 kFlashBlockSize = 4096;

struct VersionInfo {
  quint8 byte1;
  quint8 byte16;
};

struct StorageInfo {
  quint16 blockSize;
  quint16 blockCount;
};

struct FileInfo {
  bool exists;
  quint32 size;
};

// Functions for handling the framing protocol. Each frame must be ACKed
// (2 bytes, 00 CC). Frame contains 3 fields:
//  - 2 bytes: big-endian number, length of payload + 2
//  - 1 byte: payload checksum (a sum of all bytes modulo 256)
//  - N bytes: payload
// First byte of each frame sent to the device is an opcode, rest are the
// arguments.

quint8 checksum(const QByteArray &bytes) {
  quint8 r = 0;
  for (const unsigned char c : bytes) {
    r = (r + c) % 0x100;
  }
  return r;
}

util::StatusOr<QByteArray> readBytes(QSerialPort *s, int n,
                                     int timeout = kDefaultTimeoutMs) {
  QByteArray r;
  int i = 0;
  char c = 0;
  while (i < n) {
    if (s->bytesAvailable() == 0 && !s->waitForReadyRead(timeout)) {
      qDebug() << "Read bytes:" << r.toHex();
      return util::Status(
          util::error::DEADLINE_EXCEEDED,
          QString("Timeout on reading byte %1").arg(i).toStdString());
    }
    if (!s->getChar(&c)) {
      qDebug() << "Read bytes:" << r.toHex();
      return util::Status(util::error::UNKNOWN,
                          QString("Error reading byte %1: %2")
                              .arg(i)
                              .arg(s->errorString())
                              .toStdString());
    }
    r.append(c);
    i++;
  }
  qDebug() << "Read bytes:" << r.toHex();
  return r;
}

util::Status writeBytes(QSerialPort *s, const QByteArray &bytes,
                        int timeout = kDefaultTimeoutMs) {
  // qDebug() << "Writing bytes:" << bytes.toHex();
  if (!s->write(bytes)) {
    return util::Status(
        util::error::UNKNOWN,
        QString("Write failed: %1").arg(s->errorString()).toStdString());
  }
  if (!s->waitForBytesWritten(timeout)) {
    return util::Status(
        util::error::DEADLINE_EXCEEDED,
        QString("Write timed out: %1").arg(s->errorString()).toStdString());
  }
  return util::Status::OK;
}

util::Status recvAck(QSerialPort *s, int timeout = kDefaultTimeoutMs) {
  auto r = readBytes(s, 2, timeout);
  if (!r.ok()) {
    return r.status();
  }
  if (r.ValueOrDie()[0] != 0 || (unsigned char) (r.ValueOrDie()[1]) != 0xCC) {
    return util::Status(util::error::UNKNOWN,
                        QString("Expected ACK(\\x00\\xCC), got %1")
                            .arg(QString::fromUtf8(r.ValueOrDie().toHex()))
                            .toStdString());
  }
  return util::Status::OK;
}

util::Status sendAck(QSerialPort *s, int timeout = kDefaultTimeoutMs) {
  return writeBytes(s, QByteArray("\x00\xCC", 2), timeout);
}

util::Status doBreak(QSerialPort *s, int timeout = kDefaultTimeoutMs) {
  qInfo() << "Sending break...";
  s->clear();
  if (!s->setBreakEnabled(true)) {
    return util::Status(util::error::UNKNOWN,
                        QString("setBreakEnabled(true) failed: %1")
                            .arg(s->errorString())
                            .toStdString());
  }
  QThread::msleep(500);
  if (!s->setBreakEnabled(false)) {
    return util::Status(util::error::UNKNOWN,
                        QString("setBreakEnabled(false) failed: %1")
                            .arg(s->errorString())
                            .toStdString());
  }
  return recvAck(s, timeout);
}

util::StatusOr<QByteArray> recvPacket(QSerialPort *s,
                                      int timeout = kDefaultTimeoutMs) {
  auto r = readBytes(s, 3, timeout);
  if (!r.ok()) {
    return r.status();
  }
  QDataStream hs(r.ValueOrDie());
  hs.setByteOrder(QDataStream::BigEndian);
  quint16 len;
  quint8 csum;
  hs >> len >> csum;
  auto payload = readBytes(s, len - 2, timeout);
  if (!payload.ok()) {
    return payload.status();
  }
  quint8 pcsum = checksum(payload.ValueOrDie());
  if (csum != pcsum) {
    return util::Status(util::error::UNKNOWN,
                        QString("Invalid checksum: %1, expected %2")
                            .arg(pcsum)
                            .arg(csum)
                            .toStdString());
  }
  sendAck(s, timeout);  // return value ignored
  return payload.ValueOrDie();
}

util::Status sendPacket(QSerialPort *s, const QByteArray &bytes,
                        int timeout = kDefaultTimeoutMs) {
  QByteArray header;
  QDataStream hs(&header, QIODevice::WriteOnly);
  hs.setByteOrder(QDataStream::BigEndian);
  hs << quint16(bytes.length() + 2) << checksum(bytes);
  util::Status st = writeBytes(s, header, timeout);
  if (!st.ok()) {
    return st;
  }
  st = writeBytes(s, bytes, timeout);
  if (!st.ok()) {
    return st;
  }
  return recvAck(s, timeout);
}

#ifndef NO_LIBFTDI
util::StatusOr<ftdi_context *> openFTDI() {
  std::unique_ptr<ftdi_context, void (*) (ftdi_context *) > ctx(ftdi_new(),
                                                                ftdi_free);
  if (ftdi_set_interface(ctx.get(), INTERFACE_A) != 0) {
    return util::Status(util::error::UNKNOWN, "ftdi_set_interface failed");
  }
  if (ftdi_usb_open(ctx.get(), kVendorID, kProductID) != 0) {
    return util::Status(util::error::UNKNOWN, "ftdi_usb_open failed");
  }
  if (ftdi_write_data_set_chunksize(ctx.get(), 1) != 0) {
    return util::Status(util::error::UNKNOWN,
                        "ftdi_write_data_set_chunksize failed");
  }
  if (ftdi_set_bitmode(ctx.get(), 0x61, BITMODE_BITBANG) != 0) {
    return util::Status(util::error::UNKNOWN, "ftdi_set_bitmode failed");
  }
  return ctx.release();
}

util::Status doReset(ftdi_context *ctx) {
  unsigned char c = 1;
  if (ftdi_write_data(ctx, &c, 1) < 0) {
    return util::Status(util::error::UNKNOWN, "ftdi_write_data failed");
  }
  QThread::msleep(5);
  c |= 0x20;
  if (ftdi_write_data(ctx, &c, 1) < 0) {
    return util::Status(util::error::UNKNOWN, "ftdi_write_data failed");
  }
  QThread::msleep(1000);
  return util::Status::OK;
}

util::Status boot(ftdi_context *ctx) {
  util::Status st;
  const std::vector<unsigned char> seq = {0, 0x20};
  for (unsigned char b : seq) {
    if (ftdi_write_data(ctx, &b, 1) < 0) {
      return util::Status(util::error::UNKNOWN, "ftdi_write_data failed");
    }
    QThread::msleep(100);
  }
  return util::Status::OK;
}
#endif

#ifndef NO_LIBFTDI
util::Status connectToBootLoader(QSerialPort *port, ftdi_context *ctx) {
#else
util::Status connectToBootLoader(QSerialPort *port) {
#endif
  util::Status st = setSpeed(port, kSerialSpeed);
  if (!st.ok()) return st;
  int i = 1;
  do {
#ifndef NO_LIBFTDI
    if (ctx != nullptr) doReset(ctx);
#endif
    st = doBreak(port);
  } while (!st.ok() && i++ < 3);
  if (!st.ok()) {
    st = QS(util::error::UNAVAILABLE,
            QObject::tr(
                "Unable to communicate with the boot loader. "
                "Please make sure SOP2 is high and reset the device. "
                "If you are using a LAUNCHXL board, the SOP2 jumper should "
                "be closed"
#ifndef NO_LIBFTDI
                " or a jumper wire installed as described "
                "<a href=\"http://energia.nu/cc3200guide/\">here</a>."
#else
                "."
#endif
                ));
  }
  return st;
}
class FlasherImpl : public Flasher {
  Q_OBJECT
 public:
#ifndef NO_LIBFTDI
  FlasherImpl(QSerialPort *port, ftdi_context *ftdiCtx, Prompter *prompter)
      : port_(port), ftdiCtx_(ftdiCtx), prompter_(prompter) {
  }
#else
  FlasherImpl(QSerialPort *port, Prompter *prompter)
      : port_(port), prompter_(prompter) {
  }
#endif

  util::Status setFirmware(FirmwareBundle *fw) override {
    auto code = fw->getPartSource(kFWFilename);
    if (!code.ok()) {
      code = fw->getPartSource(kFWBundleFWPartNameOld);
    }
    if (code.ok()) {
      const int kMaxSize =
          kBlockSizes[sizeof(kBlockSizes) / sizeof(kBlockSizes[0]) - 1] * 255;
      if (code.ValueOrDie().length() > kMaxSize) {
        return util::Status(util::error::INVALID_ARGUMENT,
                            tr("Code image is too big. Maximum size is %2")
                                .arg(kMaxSize)
                                .toStdString());
      }
    }
    spiffs_image_.clear();
    const auto fs = fw->getPartSource(kFWBundleFSPartName);
    if (fs.ok()) {
      spiffs_image_ = fs.ValueOrDie();
    }
    for (const auto &p : fw->parts()) {
      QString fileName = p.name;
      if (fileName == kFWBundleFSPartName) continue;
      if (fileName == kFWBundleFWPartNameOld) fileName = kFWFilename;
      const QString &type = p.attrs["type"].toString();
      if (p.attrs["type"].isValid()) {
        if (type != "slfile" && type != "boot" && type != "boot_cfg" &&
            type != "app" && type != "fs") {
          continue;
        }
      }
      SLFSFileInfo fi;
      fi.name = fileName;
      if (p.attrs["src"].type() == QVariant::String) {
        const auto data = fw->getPartSource(p.name);
        if (!data.ok()) return data.status();
        fi.data = data.ValueOrDie();
        const QString signPart = p.attrs["sign"].toString();
        if (signPart != "") {
          const auto sdr = fw->getPartSource(signPart);
          if (!sdr.ok()) {
            return QSP(tr("Unable to get signature data for part %1 (part %2)")
                           .arg(p.name)
                           .arg(signPart),
                       sdr.status());
          }
          const QByteArray signData = sdr.ValueOrDie();
          if (signData.length() != kFileSignatureLength) {
            return QS(
                util::error::INVALID_ARGUMENT,
                tr("Wrong signature length for part %1: expected %2, got %3")
                    .arg(p.name)
                    .arg(kFileSignatureLength)
                    .arg(signData.length()));
          }
          fi.signature = signData;
        }
      }
      if (p.attrs["falloc"].canConvert<int>()) {
        fi.allocSize = p.attrs["falloc"].toInt();
      }
      files_[fileName] = fi;
      qDebug() << "File:" << fi.toString();
    }
    qInfo() << fw->buildId();
    return util::Status::OK;
  }

  int totalBytes() const override {
    QMutexLocker lock(&lock_);
    int r = 0;
    if (spiffs_image_.length() > 0) {
      r += spiffs_image_.length() + kSPIFFSMetadataSize;
    }
    for (const QString &f : files_.keys()) {
      r += files_[f].data.length();
    }
    return r;
  }

  void run() override {
    QMutexLocker lock(&lock_);

    util::Status st = runLocked();
    if (!st.ok()) {
      emit done(QString::fromStdString(st.error_message()), false);
      return;
    }
    emit done(tr("All done!"), true);
  }

  util::Status setOption(const QString &name, const QVariant &value) override {
    if (name == kMergeFSOption) {
      if (value.type() != QVariant::Bool) {
        return util::Status(util::error::INVALID_ARGUMENT,
                            "value must be boolean");
      }
      merge_spiffs_ = value.toBool();
      return util::Status::OK;
    } else if (name == kFormatFailFS) {
      if (value.type() != QVariant::String) {
        return util::Status(util::error::INVALID_ARGUMENT,
                            "value must be string");
      }
      const std::map<std::string, int> size = {
          {"512K", 512 * 1024},
          {"1M", 1 * 1024 * 1024},
          {"2M", 2 * 1024 * 1024},
          {"4M", 4 * 1024 * 1024},
          {"8M", 8 * 1024 * 1024},
          {"16M", 16 * 1024 * 1024},
      };
      if (size.find(value.toString().toStdString()) == size.end()) {
        return util::Status(util::error::INVALID_ARGUMENT, "invalid size");
      }
      failfs_size_ = size.find(value.toString().toStdString())->second;
      return util::Status::OK;
    }
    return util::Status(util::error::INVALID_ARGUMENT, "Unknown option");
  }

  util::Status setOptionsFromConfig(const Config &config) override {
    util::Status r;

    QStringList boolOpts({kMergeFSOption});
    QStringList stringOpts({kFormatFailFS});

    for (const auto &opt : boolOpts) {
      auto s = setOption(opt, config.isSet(opt));
      if (!s.ok()) {
        r = util::Status(
            s.error_code(),
            (opt + ": " + s.error_message().c_str()).toStdString());
      }
    }
    for (const auto &opt : stringOpts) {
      // XXX: currently there's no way to "unset" a string option.
      if (config.isSet(opt)) {
        auto s = setOption(opt, config.value(opt));
        if (!s.ok()) {
          r = util::Status(
              s.error_code(),
              (opt + ": " + s.error_message().c_str()).toStdString());
        }
      }
    }
    return r;
  }

 private:
  struct SLFSFileInfo {
    QString name;
    QByteArray data;
    QByteArray signature;
    int allocSize = 0;

    QString toString() const;
  };

  util::Status runLocked() {
    util::Status st = util::Status::UNKNOWN;
    progress_ = 0;
    emit progress(progress_);

    // Optimization - device may already be in the boot loader mode,
    // such as if a successful probe() was performed previously.
    // Since there is no guarantee that we can reset the device at will,
    // we'll take a hint that the port is at the correct speed already
    // and try to communicate with the loader directly.
    if (port_->baudRate() == kSerialSpeed) {
      st = util::Status::OK;
    }

    do {
      while (!st.ok()) {
#ifndef NO_LIBFTDI
        st = connectToBootLoader(port_, ftdiCtx_);
#else
        st = connectToBootLoader(port_);
#endif
        if (!st.ok()) {
          qCritical() << st;
          QString msg = QString::fromUtf8(st.ToString().c_str());
          int answer = prompter_->Prompt(
              msg, {{tr("Retry"), Prompter::ButtonRole::No},
                    {tr("Cancel"), Prompter::ButtonRole::Yes}});
          if (answer == 1) return st;
        }
      }
      emit statusMessage(tr("Updating bootloader..."), true);
      st = switchToNWPBootloader();
    } while (!st.ok());

    if (failfs_size_ > 0) {
      st = formatFailFS(failfs_size_);
      if (!st.ok()) {
        return st;
      }
    }

    for (const QString &f : files_.keys()) {
      st = uploadFile(files_[f]);
      if (!st.ok()) return st;
    }

    if (spiffs_image_.length() > 0) {
      emit statusMessage(tr("Updating file system image..."), true);
      st = updateSPIFFS();
      if (!st.ok()) {
        return st;
      }
    }
#ifndef NO_LIBFTDI
    if (ftdiCtx_ != nullptr) {
      emit statusMessage(tr("Rebooting into firmware..."), true);
      st = boot(ftdiCtx_);
      if (!st.ok()) return st;
    } else
#endif
      prompter_->Prompt(tr("Please remove the SOP2 jumper and reboot"),
                        {{tr("Ok"), Prompter::ButtonRole::Yes}});
    return util::Status::OK;
  }

  static int getBlockSize(int len) {
    for (unsigned int i = 0; i < sizeof(kBlockSizes) / sizeof(kBlockSizes[0]);
         i++) {
      if (len <= kBlockSizes[i] * 255) {
        return kBlockSizes[i];
      }
    }
    return -1;
  }

  util::StatusOr<VersionInfo> getVersion() {
    emit statusMessage(tr("Getting device version info..."), true);
    util::Status st = sendPacket(port_, QByteArray(&kOpcodeGetVersionInfo, 1));
    if (!st.ok()) {
      return st;
    }
    auto data = recvPacket(port_);
    if (!data.ok()) {
      return data.status();
    }
    if (data.ValueOrDie().length() != 28) {
      return util::Status(util::error::UNKNOWN,
                          QString("Expected 28 bytes, got %1")
                              .arg(data.ValueOrDie().length())
                              .toStdString());
    }
    return VersionInfo(
        {quint8(data.ValueOrDie()[1]), quint8(data.ValueOrDie()[16])});
  }

  util::StatusOr<StorageInfo> getStorageInfo() {
    emit statusMessage(tr("Getting storage info..."), true);
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);
    ps << quint8(kOpcodeGetStorageInfo) << quint32(kStorageID);
    auto st = sendPacket(port_, payload);
    if (!st.ok()) {
      return st;
    }
    auto resp = recvPacket(port_);
    if (!resp.ok()) {
      return resp.status();
    }
    if (resp.ValueOrDie().length() < 4) {
      return util::Status(util::error::UNKNOWN,
                          QString("Expected at least 4 bytes, got %1")
                              .arg(resp.ValueOrDie().length())
                              .toStdString());
    }
    StorageInfo r;
    QDataStream rs(resp.ValueOrDie());
    rs.setByteOrder(QDataStream::BigEndian);
    rs >> r.blockSize >> r.blockCount;
    return r;
  }

  util::Status eraseBlocks(int start, int count) {
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);
    ps << quint8(kOpcodeEraseBlocks) << quint32(kStorageID) << quint32(start)
       << quint32(count);
    return sendPacket(port_, payload);
  }

  util::Status sendChunk(int offset, const QByteArray &bytes) {
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);
    ps << quint8(kOpcodeStorageWrite) << quint32(kStorageID) << quint32(offset)
       << quint32(bytes.length());
    payload.append(bytes);
    return sendPacket(port_, payload);
  }

  util::Status rawWrite(quint32 offset, const QByteArray &bytes) {
    auto si = getStorageInfo();
    if (!si.ok()) {
      return si.status();
    }
    if (si.ValueOrDie().blockSize > 0) {
      quint16 bs = si.ValueOrDie().blockSize;
      int start = offset / bs;
      int count = bytes.length() / bs;
      if ((bytes.length() % bs) > 0) {
        count++;
      }
      util::Status st = eraseBlocks(start, count);
      if (!st.ok()) {
        return st;
      }
    }
    const int kChunkSize = 4080;
    int sent = 0;
    while (sent < bytes.length()) {
      util::Status st = sendChunk(offset + sent, bytes.mid(sent, kChunkSize));
      if (!st.ok()) {
        return st;
      }
      sent += kChunkSize;
    }
    return util::Status::OK;
  }

  util::Status execFromRAM() {
    return sendPacket(port_, QByteArray(&kOpcodeExecFromRAM, 1));
  }

  util::Status switchUART2Apps() {
    const quint32 magic = 0x0196E6AB;
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);
    ps << quint8(kOpcodeSwitchUART2Apps) << quint32(magic);
    return sendPacket(port_, payload);
  }

  util::Status switchToNWPBootloader() {
    emit statusMessage(tr("Switching to NWP bootloader..."));
    auto ver = getVersion();
    if (!ver.ok()) {
      return ver.status();
    }
    if ((ver.ValueOrDie().byte16 & 0x10) == 0) {
      return util::Status::OK;
    }
    quint8 bl_ver = ver.ValueOrDie().byte1;
    if (bl_ver < 3) {
      return util::Status(util::error::FAILED_PRECONDITION,
                          "Unsupported device");
    } else if (bl_ver == 3) {
      emit statusMessage(tr("Uploading rbtl3101_132.dll..."));
      QFile f(":/cc3200/rbtl3101_132.dll");
      if (!f.open(QIODevice::ReadOnly)) {
        return util::Status(util::error::INTERNAL,
                            "Failed to open embedded file");
      }
      util::Status st = rawWrite(0x4000, f.readAll());
      if (!st.ok()) {
        return st;
      }
      st = execFromRAM();
      if (!st.ok()) {
        return st;
      }
    } else /* if (bl_ver > 3) */ {
      util::Status st = switchUART2Apps();
      if (!st.ok()) {
        return st;
      }
    }
    util::Status st;
    for (int attempt = 0; attempt < 3; attempt++) {
      QThread::sleep(1);
      qInfo() << "Checking if the device is back online...";
      st = doBreak(port_);
      if (st.ok()) {
        break;
      }
    }
    if (!st.ok()) {
      return st;
    }
    QByteArray blob;
    if (bl_ver == 3) {
      emit statusMessage(tr("Uploading rbtl3100.dll..."));
      QFile f(":/cc3200/rbtl3100.dll");
      if (!f.open(QIODevice::ReadOnly)) {
        return util::Status(util::error::INTERNAL,
                            "Failed to open embedded file");
      }
      blob = f.readAll();
    } else /* if (bl_ver > 3) */ {
      emit statusMessage(tr("Uploading rbtl3100s.dll..."));
      QFile f(":/cc3200/rbtl3100s.dll");
      if (!f.open(QIODevice::ReadOnly)) {
        return util::Status(util::error::INTERNAL,
                            "Failed to open embedded file");
      }
      blob = f.readAll();
    }
    st = rawWrite(0, blob);
    if (!st.ok()) {
      return st;
    }
    st = execFromRAM();
    if (!st.ok()) {
      return st;
    }
    return recvAck(port_);
  }

  util::Status eraseFile(const QString &name) {
    emit statusMessage(tr("Erasing %1...").arg(name));
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);
    ps << quint8(kOpcodeFileErase) << quint32(0);
    payload.append(name.toUtf8());
    payload.append('\0');
    return sendPacket(port_, payload);
  }

  util::Status openFileForWrite(const SLFSFileInfo &fi) {
    int allocSize = fi.data.length();
    if (fi.allocSize > allocSize) allocSize = fi.allocSize;
    emit statusMessage(tr("Uploading %1...").arg(fi.toString()), true);
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);
    quint32 flags = kFileOpenModeCreateIfNotExist;
    if (!fi.signature.isEmpty()) flags |= kFileOpenModeSecure;
    const int num_sizes = sizeof(kBlockSizes) / sizeof(kBlockSizes[0]);
    int block_size_index = 0;
    for (; block_size_index < num_sizes; block_size_index++) {
      if (kBlockSizes[block_size_index] * 255 >= allocSize) {
        break;
      }
    }
    if (block_size_index == num_sizes) {
      return util::Status(util::error::FAILED_PRECONDITION, "File is too big");
    }
    flags |= (block_size_index & 0xf) << 8;
    int blocks = allocSize / kBlockSizes[block_size_index];
    if (allocSize % kBlockSizes[block_size_index] > 0) {
      blocks++;
    }
    flags |= blocks & 0xff;
    ps << quint8(kOpcodeStartUpload) << quint32(flags) << quint32(0);
    payload.append(fi.name.toUtf8());
    payload.append('\0');
    payload.append('\0');
    util::Status st = sendPacket(port_, payload, 10000);
    if (!st.ok()) {
      return st;
    }
    auto token = readBytes(port_, 4);
    if (!token.ok()) {
      return token.status();
    }
    return util::Status::OK;
  }

  util::Status openFileForRead(const QString &filename) {
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);
    ps << quint8(kOpcodeStartUpload) << quint32(0) << quint32(0);
    payload.append(filename.toUtf8());
    payload.append('\0');
    payload.append('\0');
    util::Status st = sendPacket(port_, payload, 10000);
    if (!st.ok()) {
      return st;
    }
    auto token = readBytes(port_, 4);
    if (!token.ok()) {
      return token.status();
    }
    return util::Status::OK;
  }

  util::Status closeFile(const QByteArray &signature) {
    QByteArray payload(&kOpcodeFinishUpload, 1);
    payload.append(QByteArray("\0", 1).repeated(63));
    if (!signature.isEmpty()) {
      payload.append(signature);
    } else {
      payload.append(QByteArray("\x46", 1).repeated(256));
    }
    payload.append("\0", 1);
    return sendPacket(port_, payload);
  }

  util::Status uploadFile(const SLFSFileInfo &fi) {
    auto info = getFileInfo(fi.name);
    if (!info.ok()) {
      return info.status();
    }
    util::Status st;
    if (info.ValueOrDie().exists) {
      st = eraseFile(fi.name);
      if (!st.ok()) {
        return st;
      }
    }
    st = openFileForWrite(fi);
    if (!st.ok()) {
      return st;
    }
    int start = 0;
    while (start < fi.data.length()) {
      emit statusMessage(tr("Writing @ 0x%1...").arg(start, 0, 16));
      QByteArray payload;
      QDataStream ps(&payload, QIODevice::WriteOnly);
      ps.setByteOrder(QDataStream::BigEndian);
      ps << quint8(kOpcodeFileChunk) << quint32(start);
      payload.append(fi.data.mid(start, kFileUploadBlockSize));

      st = sendPacket(port_, payload);
      if (!st.ok()) {
        return st;
      }
      start += kFileUploadBlockSize;
      progress_ += kFileUploadBlockSize;
      emit progress(progress_);
    }
    emit statusMessage(tr("Upload finished."), true);
    return closeFile(fi.signature);
  }

  util::StatusOr<FileInfo> getFileInfo(const QString &filename) {
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);
    ps << quint8(kOpcodeGetFileInfo) << quint32(filename.length());
    payload.append(filename.toUtf8());
    util::Status st = sendPacket(port_, payload);
    if (!st.ok()) {
      return st;
    }
    auto resp = recvPacket(port_);
    if (!resp.ok()) {
      return resp.status();
    }
    QByteArray b = resp.ValueOrDie();
    FileInfo r;
    r.exists = b[0] == char(1);
    QDataStream ss(b.mid(4, 4));
    ss.setByteOrder(QDataStream::BigEndian);
    ss >> r.size;
    return r;
  }

  util::StatusOr<QByteArray> getFile(const QString &filename) {
    auto info = getFileInfo(filename);
    if (!info.ok()) {
      return info.status();
    }
    if (!info.ValueOrDie().exists) {
      return util::Status(util::error::FAILED_PRECONDITION,
                          "File does not exist");
    }
    util::Status st = openFileForRead(filename);
    if (!st.ok()) {
      return st;
    }
    int size = info.ValueOrDie().size;
    QByteArray r;
    while (r.length() < size) {
      int n = kFileUploadBlockSize;
      if (n > size - r.length()) {
        n = size - r.length();
      }

      QByteArray payload;
      QDataStream ps(&payload, QIODevice::WriteOnly);
      ps << quint8(kOpcodeReadFileChunk) << quint32(r.length()) << quint32(n);
      st = sendPacket(port_, payload);
      if (!st.ok()) {
        qCritical() << "getChunk failed at " << r.length() << ": "
                    << st.ToString().c_str();
        return st;
      }
      auto resp = recvPacket(port_);
      if (!resp.ok()) {
        qCritical() << "Failed to read chunk at " << r.length() << ": "
                    << resp.status().ToString().c_str();
        return resp.status();
      }
      r.append(resp.ValueOrDie());
    }

    st = closeFile("");
    if (!st.ok()) {
      return st;
    }
    return r;
  }

  util::StatusOr<QByteArray> readSPIFFS(const QString &filename, quint64 *seq,
                                        quint32 *block_size,
                                        quint32 *page_size) {
    auto info = getFileInfo(filename);
    if (!info.ok()) {
      return info.status();
    }
    if (!info.ValueOrDie().exists) {
      return QByteArray();
    }
    auto data = getFile(filename);
    if (!data.ok()) {
      return data.status();
    }
    QByteArray bytes = data.ValueOrDie();
    if (bytes.length() < kSPIFFSMetadataSize) {
      return util::Status(util::error::FAILED_PRECONDITION,
                          "Image is too short");
    }
    QDataStream meta(bytes.mid(bytes.length() - kSPIFFSMetadataSize));
    meta.setByteOrder(QDataStream::LittleEndian);
    quint32 fs_size;
    // See struct fs_info in platforms/cc3200/cc3200_fs_spiffs_container.c
    meta >> *seq >> fs_size >> *block_size >> *page_size;
    return bytes.mid(0, bytes.length() - kSPIFFSMetadataSize);
  }

  util::Status updateSPIFFS() {
    quint64 seq[2] = {~(0ULL), ~(0ULL)};
    quint32 page_size[2] = {0, 0}, block_size[2] = {0, 0};
    auto fs0 = readSPIFFS(kFS0Filename, &seq[0], &block_size[0], &page_size[0]);
    if (!fs0.ok()) {
      return fs0.status();
    }
    auto fs1 = readSPIFFS(kFS1Filename, &seq[1], &block_size[1], &page_size[1]);
    if (!fs1.ok()) {
      return fs1.status();
    }
    qInfo() << "Sequence nubmer of 0.fs:" << seq[0];
    qInfo() << "Sequence nubmer of 1.fs:" << seq[1];
    QByteArray meta;
    int min_seq = 0;
    QDataStream ms(&meta, QIODevice::WriteOnly);
    ms.setByteOrder(QDataStream::LittleEndian);
    quint64 new_seq;
    if (seq[0] < seq[1]) {
      new_seq = seq[0] - 1;
      min_seq = 0;
    } else {
      new_seq = seq[1] - 1;
      min_seq = 1;
    }
    qInfo() << "FS meta:" << new_seq << spiffs_image_.length()
            << quint32(FLASH_BLOCK_SIZE) << quint32(LOG_PAGE_SIZE);
    ms << new_seq << quint32(spiffs_image_.length());
    // TODO(imax): make mkspiffs write page size and block size into a separate
    // file and use it here instead of hardcoded values.
    ms << quint32(FLASH_BLOCK_SIZE) << quint32(LOG_PAGE_SIZE);
    meta.append(
        QByteArray("\xFF", 1).repeated(kSPIFFSMetadataSize - meta.length()));
    QByteArray image = spiffs_image_;
    if ((fs0.ValueOrDie().length() > 0 || fs1.ValueOrDie().length() > 0) &&
        merge_spiffs_) {
      QByteArray dev = (min_seq == 0 ? fs0.ValueOrDie() : fs1.ValueOrDie());

      auto merged = mergeFilesystems(dev, spiffs_image_);
      if (!merged.ok()) {
        return merged.status();
      }
      if (!extra_spiffs_files_.empty()) {
        merged = mergeFiles(merged.ValueOrDie(), extra_spiffs_files_);
        if (!merged.ok()) {
          return merged.status();
        }
      }
      image = merged.ValueOrDie();
    }
    image.append(meta);
    QString fname = min_seq == 0 ? kFS1Filename : kFS0Filename;
    qInfo() << "Overwriting" << fname;
    SLFSFileInfo fi;
    fi.name = fname;
    fi.data = image;
    return uploadFile(fi);
  }

  util::Status formatFailFS(int size) {
    emit statusMessage(tr("Formatting SFLASH file system (%1)...").arg(size),
                       true);
    QByteArray payload;
    QDataStream ps(&payload, QIODevice::WriteOnly);
    ps.setByteOrder(QDataStream::BigEndian);  // NB
    ps << quint8(kOpcodeFormatFlash) << quint32(2)
       << quint32(size / kFlashBlockSize) << quint32(0) << quint32(0)
       << quint32(2);
    return sendPacket(port_, payload, 10000);
  }

  mutable QMutex lock_;

  QSerialPort *port_;
#ifndef NO_LIBFTDI
  ftdi_context *ftdiCtx_;
#endif
  Prompter *prompter_;

  QByteArray spiffs_image_;
  QMap<QString, QByteArray> extra_spiffs_files_;
  QMap<QString, SLFSFileInfo> files_;
  bool merge_spiffs_ = false;
  int failfs_size_ = -1;
  int progress_ = 0;
};

QString FlasherImpl::SLFSFileInfo::toString() const {
  QString result = tr("%1: size %2").arg(name).arg(data.length());
  if (allocSize > data.length()) {
    result += tr(", alloc %1").arg(allocSize);
  }
  if (signature.length() > 0) {
    result += tr(", signed");
  }
  return result;
}

#ifndef NO_LIBFTDI
class CC3200HAL : public HAL {
 public:
  CC3200HAL(QSerialPort *port) : port_(port), ftdiCtx_(ftdi_new(), ftdi_free) {
    auto ftdi = openFTDI();
    if (ftdi.ok()) {
      ftdiCtx_.reset(ftdi.MoveValueOrDie());
    } else {
      // This may be ok if the device being used is not a Launchpad.
      qWarning() << "Unable to open FTDI context";
      ftdiCtx_.reset();
    }
  }

  util::Status probe() const override {
    return connectToBootLoader(port_, ftdiCtx_.get());
  }

  std::unique_ptr<Flasher> flasher(Prompter *prompter) const override {
    return std::move(std::unique_ptr<Flasher>(
        new FlasherImpl(port_, ftdiCtx_.get(), prompter)));
  }

  std::string name() const override {
    return "CC3200";
  }

  util::Status reboot() override {
    return boot(ftdiCtx_.get());
  }

 private:
  QSerialPort *port_;
  std::unique_ptr<ftdi_context, void (*)(ftdi_context *) > ftdiCtx_;
};
#else   // NO_LIBFTDI
class CC3200HAL : public HAL {
 public:
  CC3200HAL(QSerialPort *port) : port_(port) {
  }

  util::Status probe() const override {
    return connectToBootLoader(port_);
  }

  std::unique_ptr<Flasher> flasher(Prompter *prompter) const override {
    return std::move(
        std::unique_ptr<Flasher>(new FlasherImpl(port_, prompter)));
  }

  std::string name() const override {
    return "CC3200";
  }

  util::Status reboot() override {
    return util::Status(util::error::UNIMPLEMENTED,
                        "Rebooting CC3200 is not supported");
  }

 private:
  QSerialPort *port_;
};
#endif  // NO_LIBFTDI

}  // namespace

std::unique_ptr<::HAL> HAL(QSerialPort *port) {
  return std::move(std::unique_ptr<::HAL>(new CC3200HAL(port)));
}

void addOptions(Config *config) {
  // QCommandLineOption supports C++11-style initialization only since Qt 5.4.
  QList<QCommandLineOption> opts;
  opts.append(QCommandLineOption(kFormatFailFS,
                                 "Format SFLASH file system before flashing. "
                                 "Accepted sizes: 512K, 1M, 2M, 4M, 8M, 16M.",
                                 "size", "1M"));
  config->addOptions(opts);
}

}  // namespace CC3200

#include "cc3200.moc"
