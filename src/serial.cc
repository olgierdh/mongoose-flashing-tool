#include "serial.h"

#include <memory>
#ifdef Q_OS_OSX
#include <sys/ioctl.h>
#include <IOKit/serial/ioss.h>
#endif

#include <QCoreApplication>
#include <QDebug>
#include <QSerialPort>

#include <common/util/error_codes.h>
#include <common/util/status.h>

#include "status_qt.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

util::StatusOr<QSerialPort *> connectSerial(const QSerialPortInfo &port,
                                            int speed) {
  std::unique_ptr<QSerialPort> s(new QSerialPort(port));
  if (!s->setParity(QSerialPort::NoParity)) {
    return util::Status(
        util::error::INTERNAL,
        QCoreApplication::translate("connectSerial", "Failed to disable parity")
            .toStdString());
  }
  if (!s->setFlowControl(QSerialPort::NoFlowControl)) {
    return util::Status(
        util::error::INTERNAL,
        QCoreApplication::translate(
            "connectSerial", "Failed to disable flow control").toStdString());
  }
  if (!s->open(QIODevice::ReadWrite)) {
    return QS(util::error::INTERNAL, QObject::tr("Failed to open %1: %2")
                                         .arg(port.portName())
                                         .arg(s->errorString()));
  }
  auto st = setSpeed(s.get(), speed);
  if (!st.ok()) {
    return st;
  }
  return s.release();
}

util::Status setSpeed(QSerialPort *port, int speed) {
  qInfo() << "Setting" << port->portName() << "speed to" << speed
          << "(real speed may be different)";
  if (!port->setBaudRate(speed)) {
    return util::Status(
        util::error::INTERNAL,
        QCoreApplication::translate("setSpeed", "Failed to set baud rate")
            .toStdString());
  }
#ifdef Q_OS_OSX
  if (ioctl(port->handle(), IOSSIOSPEED, &speed) < 0) {
    return util::Status(
        util::error::INTERNAL,
        QCoreApplication::translate(
            "setSpeed", "Failed to set baud rate with ioctl").toStdString());
  }
#endif
  return util::Status::OK;
}
