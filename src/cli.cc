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
  int exit_code = 0;

  std::unique_ptr<QSerialPort> port;
  if (parser_->isSet("port")) {
    QString portName = parser_->value("port");
#ifdef __unix__
    // Resolve symlinks if any.
    QFileInfo qf(portName);
    if (!qf.canonicalFilePath().isEmpty() &&
        qf.canonicalFilePath() != portName) {
      qInfo() << portName << "->" << qf.canonicalFilePath();
      portName = qf.canonicalFilePath();
    }
#endif
    const auto qspi = findSerial(portName);
    if (!qspi.ok()) {
      qCritical() << qspi.status();
      qApp->exit(1);
      return;
    }
    auto sp = connectSerial(qspi.ValueOrDie(), 115200);
    if (!sp.ok()) {
      qCritical() << "Error opening " << portName << ": " << sp.status();
      qApp->exit(1);
      return;
    }
    port_.reset(sp.ValueOrDie());
  }

  const QString platform = parser_->value("platform");
  if (platform == "esp8266") {
    hal_ = ESP8266::HAL(port_.get());
  } else if (platform == "cc3200") {
    hal_ = CC3200::HAL(port_.get());
  } else if (platform == "") {
    qCritical() << "Flag --platform is required.";
    qApp->exit(1);
    return;
  } else {
    qCritical() << "Unknown platform: " << platform;
    parser_->showHelp(1);
  }

  util::Status r;
  bool exit = true;
  if (parser_->isSet("probe")) {
    r = hal_->probe();
  } else if (parser_->isSet("flash")) {
    r = flash(parser_->value("flash"));
    if (r.ok() && parser_->isSet("console")) {
      r = console();
      if (r.ok()) exit = false;
    }
  } else if (parser_->isSet("console")) {
    r = console();
    if (r.ok()) exit = false;
  } else {
    qCritical() << "No action specified.";
    parser_->showHelp(1);
  }
  if (exit) {
    if (r.ok()) {
      exit_code = 0;
    } else {
      qCritical() << r;
      exit_code = 1;
    }
    qApp->exit(exit_code);
  }
}

util::Status CLI::flash(const QString &path) {
  if (hal_ == nullptr) {
    return util::Status(util::error::INVALID_ARGUMENT, "No platform selected");
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
util::Status CLI::console() {
  QSerialPort *port = port_.get();
  util::Status st = setSpeed(port, config_->value("console-baud-rate").toInt());
  if (!st.ok()) return st;

  QFile *cin = new QFile();
  cin->open(fileno(stdin), QIODevice::ReadOnly);
  QFile *cout = new QFile();
  cout->open(fileno(stdout), QIODevice::WriteOnly);

  QFile *console_log = nullptr;
  if (config_->isSet("console-log")) {
    QString fn = config_->value("console-log");
    console_log = new QFile(fn, this);
    if (!console_log->open(QIODevice::ReadWrite | QIODevice::Append)) {
      return QS(util::error::UNAVAILABLE, tr("Error opening %1").arg(fn));
    }
  }

  QSocketNotifier *qsn =
      new QSocketNotifier(fileno(stdin), QSocketNotifier::Read, this);
  fcntl(0, F_SETFL, fcntl(0, F_GETFL, 0) | O_NONBLOCK);
  connect(port_.get(), &QIODevice::readyRead, [port, cout, console_log]() {
    QByteArray data = port->readAll();
    if (console_log != nullptr) {
      console_log->write(data);
      console_log->flush();
    }
    cout->write(data);
    cout->flush();
  });
  connect(qsn, &QSocketNotifier::activated,
          [port, cin]() { port->write(cin->readAll()); });
  return util::Status::OK;
}
#else
// TODO(rojer): Implement console on Windows.
util::Status CLI::console() {
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
