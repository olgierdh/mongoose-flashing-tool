#include "cli.h"

#include <fcntl.h>
#include <stdio.h>

#include <iostream>
#include <memory>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>
#include <QSerialPort>
#include <QSerialPortInfo>

#include <common/util/error_codes.h>

#include "cc3200.h"
#include "config.h"
#include "esp8266.h"
#include "prompter.h"
#include "serial.h"
#include "status_qt.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

using std::cout;
using std::cerr;
using std::endl;

class CLIPrompterImpl : public Prompter {
 public:
  CLIPrompterImpl(QObject *parent) : Prompter(parent) {
  }
  virtual ~CLIPrompterImpl() {
  }

  int Prompt(QString text, QList<QPair<QString, ButtonRole>> buttons) override {
    cout << "Prompt: " << text.toStdString() << endl;
    cout << "CLI prompting not implemented, returning default ("
         << buttons[0].first.toStdString() << ")" << endl;
    return 0;
  }
};

CLI::CLI(Config *config, QCommandLineParser *parser, QObject *parent)
    : QObject(parent),
      config_(config),
      parser_(parser),
      prompter_(new CLIPrompterImpl(this)) {
#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
  QTimer::singleShot(0, this, SLOT(run()));
#else
  QTimer::singleShot(0, this, &CLI::run);
#endif
}

void CLI::run() {
  const QString platform = parser_->value("platform");
  if (platform == "esp8266") {
    hal_ = ESP8266::HAL();
  } else if (platform == "cc3200") {
    hal_ = CC3200::HAL();
  } else if (platform == "") {
    cerr << "Flag --platform is required." << endl;
    qApp->exit(1);
    return;
  } else {
    cerr << "Unknown platform: " << platform.toStdString() << endl
         << endl;
    parser_->showHelp(1);
  }

  int exit_code = 0;

  QString port;
  if (parser_->isSet("port")) {
    port = parser_->value("port");
#ifdef __unix__
    // Resolve symlinks if any.
    QFileInfo qf(port);
    if (!qf.canonicalFilePath().isEmpty() && qf.canonicalFilePath() != port) {
      qInfo() << port << "->" << qf.canonicalFilePath();
      port = qf.canonicalFilePath();
    }
#endif
    const auto qspi = findSerial(port);
    if (!qspi.ok()) {
      cerr << qspi.status() << endl;
      qApp->exit(1);
    }
  }

  util::Status r;
  bool exit = true;
  if (parser_->isSet("probe-ports")) {
    r = listPorts();
  } else if (parser_->isSet("probe")) {
    r = probePort(port);
  } else if (parser_->isSet("flash")) {
    r = flash(port, parser_->value("flash"));
    if (r.ok() && parser_->isSet("console")) {
      r = console(port);
      if (r.ok()) exit = false;
    }
  } else if (parser_->isSet("console")) {
    r = console(port);
    if (r.ok()) exit = false;
  } else {
    cerr << "No action specified. " << endl
         << endl;
    parser_->showHelp(1);
  }
  if (exit) {
    if (r.ok()) {
      exit_code = 0;
    } else {
      cerr << r << endl;
      exit_code = 1;
    }
    qApp->exit(exit_code);
  }
}

util::Status CLI::listPorts() {
  if (hal_ == nullptr) {
    return util::Status(util::error::INVALID_ARGUMENT, "No platform selected");
  }
  const auto &ports = QSerialPortInfo::availablePorts();
  for (const auto &port : ports) {
    util::Status s = hal_->probe(port);
    cout << "Port: " << port.systemLocation().toStdString() << "\t";
    if (s.ok()) {
      cout << "ok";
    } else {
      cout << s;
    }
    cout << endl;
  }
  return util::Status::OK;
}

util::Status CLI::probePort(const QString &portName) {
  if (hal_ == nullptr) {
    return util::Status(util::error::INVALID_ARGUMENT, "No platform selected");
  }
  if (portName == "") {
    return util::Status(util::error::INVALID_ARGUMENT, "Port not specified");
  }
  const auto &ports = QSerialPortInfo::availablePorts();
  for (const auto &port : ports) {
    if (port.systemLocation() != portName) {
      continue;
    }
    return hal_->probe(port);
  }
  return util::Status(util::error::INVALID_ARGUMENT, "No such port");
}

util::Status CLI::flash(const QString &portName, const QString &path) {
  if (hal_ == nullptr) {
    return util::Status(util::error::INVALID_ARGUMENT, "No platform selected");
  }
  if (portName == "") {
    return util::Status(util::error::INVALID_ARGUMENT, "Port not specified");
  }
  const auto &ports = QSerialPortInfo::availablePorts();
  QSerialPortInfo info;
  bool found = false;
  for (const auto &port : ports) {
    if (port.systemLocation() != portName) {
      continue;
    }
    info = port;
    found = true;
    break;
  }
  if (!found) {
    return util::Status(util::error::INVALID_ARGUMENT, "No such port");
  }

  std::unique_ptr<Flasher> f(hal_->flasher(prompter_));
  util::Status config_status = f->setOptionsFromConfig(*config_);
  if (!config_status.ok()) {
    return config_status;
  }
  util::Status st;

  auto fwbs = NewZipFWBundle(path);
  if (!fwbs.ok()) {
    return QSP("failed to load firmware bundle", fwbs.status());
  }

  FirmwareBundle *fwb = fwbs.ValueOrDie().get();
  util::Status err = f->setFirmware(fwb);
  if (!err.ok()) {
    return err;
  }

  qInfo() << "Flashing" << fwb->name() << fwb->platform().toUpper()
          << fwb->buildId();

  util::StatusOr<QSerialPort *> r = connectSerial(info, 115200);
  if (!r.ok()) {
    return r.status();
  }
  std::unique_ptr<QSerialPort> s(r.ValueOrDie());
  st = f->setPort(s.get());
  if (!st.ok()) {
    return st;
  }
  bool success = false;
  connect(f.get(), &Flasher::done, [&success](QString msg, bool ok) {
    cout << endl
         << msg.toStdString();
    success = ok;
    if (!ok) {
      cout << endl
           << "Try -V=2 or -V=3 if you want to see more details about the "
              "error." << endl;
    }
  });
  connect(f.get(), &Flasher::statusMessage, [](QString s, bool important) {
    static bool prev = true;
    if (important) {
      if (!prev) cout << endl;
      cout << s.toStdString() << endl
           << std::flush;
    } else {
      cout << "\r" << s.toStdString() << std::flush;
    }
    prev = important;
  });

  f->run();  // connected slots should be called inline, so we don't need to
             // unblock the event loop for to print progress on terminal.

  cout << endl;

  if (!success) {
    return util::Status(util::error::ABORTED, "Flashing failed.");
  }
  return util::Status::OK;
}

#ifndef _WIN32
util::Status CLI::console(const QString &portName) {
  util::StatusOr<QSerialPort *> ps =
      connectSerial(portName, config_->value("console-baud-rate").toInt());
  if (!ps.ok()) {
    qCritical() << "connectSerial:" << ps.status();
    return QSP(tr("unable to open %1").arg(portName), ps.status());
  }
  QSerialPort *port = ps.ValueOrDie();
  QFile *cin = new QFile();
  cin->open(fileno(stdin), QIODevice::ReadOnly);
  QFile *cout = new QFile();
  cout->open(fileno(stdout), QIODevice::WriteOnly);
  QSocketNotifier *qsn =
      new QSocketNotifier(fileno(stdin), QSocketNotifier::Read, this);
  fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);
  connect(port, &QIODevice::readyRead, [port, cout]() {
    cout->write(port->readAll());
    cout->flush();
  });
  connect(qsn, &QSocketNotifier::activated,
          [port, cin]() { port->write(cin->readAll()); });
  return util::Status::OK;
}
#else
// TODO(rojer): Implement console on Windows.
util::Status CLI::console(const QString &portName) {
  (void) portName;
  return QS(util::error::UNIMPLEMENTED, "No console on Windows, sorry.");
}
#endif

util::Status CLI::generateID(const QString &filename, const QString &domain) {
  QByteArray bytes = ESP8266::makeIDBlock(domain);
  QFile f(filename);
  if (!f.open(QIODevice::WriteOnly)) {
    return util::Status(
        util::error::ABORTED,
        "failed to open the file: " + f.errorString().toStdString());
  }
  qint32 written, start = 0;
  while (start < bytes.length()) {
    written = f.write(bytes.mid(start));
    if (written < 0) {
      return util::Status(util::error::ABORTED,
                          "write failed: " + f.errorString().toStdString());
    }
    start += written;
  }
  return util::Status::OK;
}
