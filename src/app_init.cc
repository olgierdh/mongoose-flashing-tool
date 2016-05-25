#include "app_init.h"

#include <string.h>
#include <iostream>
#include <fstream>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QObject>

#include "cc3200.h"
#include "cli.h"
#include "esp8266.h"
#include "flasher.h"
#include "log.h"
#include "sigsource.h"
#include "status_qt.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

// From build_info.cc (auto-generated).
extern const char *build_id;
extern const char *build_version;

using std::cerr;
using std::endl;

util::Status initApp(int *argc, char *argv[], Config *config,
                     QCommandLineParser *parser) {
  QCoreApplication::setOrganizationName("Cesanta");
  QCoreApplication::setOrganizationDomain("cesanta.com");
  QCoreApplication::setApplicationName(APP_NAME);
  QCoreApplication::setApplicationVersion(build_version);

  // QCommandLineOption supports C++11-style initialization only since Qt 5.4.
  QList<QCommandLineOption> commonOpts;
  commonOpts.append(QCommandLineOption(
      "console-baud-rate", "Baud rate to use with the console serial port.",
      "number", "115200"));
  commonOpts.append(QCommandLineOption(
      Flasher::kFlashBaudRateOption,
      "Baud rate to use with the serial port used for flashing.", "number",
      "0"));
  commonOpts.append(QCommandLineOption(
      Flasher::kMergeFSOption,
      "If set, merge the device FS data with the factory image"));
  commonOpts.append(QCommandLineOption(
      Flasher::kDumpFSOption,
      "Dump file system image to a given file before merging.", "filename"));
  commonOpts.append(QCommandLineOption(
      "console-log",
      "If set, bytes read from a serial port in console mode will be "
      "appended to the given file.",
      "file"));
  commonOpts.append(QCommandLineOption(
      {"verbose", "V"},
      "Verbosity level. 0 – normal output, 1 - also print critical (but not "
      "fatal) errors, 2 - also print warnings, 3 - print info messages, 4 - "
      "print debug output.",
      "level", "1"));
  commonOpts.append(
      QCommandLineOption("log", "Redirect logging into a file.", "filename"));
  commonOpts.append(QCommandLineOption(
      "console-line-count",
      "Maximum number of lines to keep in console window.", "count", "4096"));
  config->addOptions(commonOpts);
  ESP8266::addOptions(config);
  CC3200::addOptions(config);

  parser->setApplicationDescription("Mongoose IoT flashing tool");
  parser->addHelpOption();
  parser->addVersionOption();
  QList<QCommandLineOption> cliOpts;
  cliOpts.append(QCommandLineOption("gui", "Run in GUI mode."));
  cliOpts.append(QCommandLineOption("wizard", "Run in Wizard mode."));
  cliOpts.append(QCommandLineOption(
      {"c", "console"},
      "Console mode, stdin and stdout are forwarded to UART"));
  cliOpts.append(QCommandLineOption(
      {"p", "platform"},
      "Target device platform. Required. Valid values: esp8266, cc3200.",
      "platform"));
  cliOpts.append(QCommandLineOption("port", "Serial port to use.", "port"));
  cliOpts.append(
      QCommandLineOption("probe", "Check device presence on a given port."));
  cliOpts.append(QCommandLineOption(
      "flash", "Flash firmware from the given file.", "file"));
  cliOpts.append(QCommandLineOption(
      {"debug", "d"}, "Enable debug output. Equivalent to --V=4"));
#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
  for (const auto &opt : cliOpts) {
    parser->addOption(opt);
  }
#else
  parser->addOptions(cliOpts);
#endif
  config->addOptionsToParser(parser);

#ifdef Q_OS_MAC
  // Finder adds "-psn_*" argument whenever it shows the Gatekeeper prompt.
  // We can't just add it to the list of options since numbers in it are not
  // stable, so we just won't let QCommandLineParser know about that argument.
  for (int i = 1; i < *argc; i++) {
    if (strncmp(argv[i], "-psn_", 5) == 0) {
      for (int j = i + 1; j < *argc; j++) {
        argv[j - 1] = argv[j];
      }
      (*argc)--;
    }
  }
#endif

  QStringList commandline;
  for (int i = 0; i < *argc; i++) {
    commandline << QString(argv[i]);
  }
  // We ignore the return value here, since there might be some options handled
  // by QApplication class. For now the most important thing we need to check
  // for is presence of "--gui" option. Later, once we have an application
  // object, we invoke parser->process(), which does the parsing again, handles
  // --help/--version and exits with error if there are still some unknown
  // options.
  parser->parse(commandline);

  Log::init();
  if (parser->isSet("log")) {
    auto *logfile = new std::ofstream(parser->value("log").toStdString(),
                                      std::ios_base::app);
    if (logfile->fail()) {
      cerr << "Failed to open log file." << endl;
      return QS(util::error::UNAVAILABLE, "Failed to open log file");
    }
    *logfile << "\n";
    Log::setFile(logfile);
  } else {
    Log::setFile(&cerr);
  }
  if (parser->isSet("debug")) {
    Log::setVerbosity(4);
  } else if (parser->isSet("V")) {
    bool ok;
    Log::setVerbosity(parser->value("V").toInt(&ok, 10));
    if (!ok) {
      cerr << parser->value("V").toStdString() << " is not a number" << endl
           << endl;
      return QS(util::error::INVALID_ARGUMENT,
                QObject::tr("'%1' is not a number").arg(parser->value("V")));
    }
  }
  qInfo() << "---------- Log started on "
          << QDateTime::currentDateTime()
                 .toString(Qt::ISODate)
                 .toStdString()
                 .c_str();
  qInfo() << APP_NAME << "Version" << build_version << "Build" << build_id;

  return util::Status::OK;
}
