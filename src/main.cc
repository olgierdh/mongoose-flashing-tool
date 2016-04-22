#include <unistd.h>

#include <QApplication>

#include "app_init.h"
#include "cli.h"
#include "dialog.h"
#include "sigsource.h"

int main(int argc, char *argv[]) {
  Config config;
  QCommandLineParser parser;
  if (!initApp(&argc, argv, &config, &parser).ok()) return 1;

  if (argc == 1 || parser.isSet("gui")) {
    // Run in GUI mode.
    QApplication app(argc, argv);
    parser.process(app);
    config.fromCommandLine(parser);
    app.setApplicationDisplayName("Mongoose IoT flashing tool");
    MainDialog w(&config);
    w.show();
    SigSource *ss = initSignalSource(&w);
    QObject::connect(ss, &SigSource::flash, &w, &MainDialog::flashClicked);
    QObject::connect(ss, &SigSource::connectDisconnect, &w,
                     &MainDialog::connectDisconnectTerminal);
    _exit(app.exec());
  }

  // Run in CLI mode.
  QCoreApplication app(argc, argv);
  parser.process(app);
  config.fromCommandLine(parser);
  CLI cli(&config, &parser);
  _exit(app.exec());
  return 1;
}
