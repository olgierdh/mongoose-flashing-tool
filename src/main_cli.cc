#include <unistd.h>

#include <QCommandLineParser>
#include <QCoreApplication>

#include "app_init.h"
#include "cli.h"
#include "config.h"

int main(int argc, char *argv[]) {
  Config config;
  QCommandLineParser parser;
  if (!initApp(argc, argv, &config, &parser).ok()) return 1;

  QCoreApplication app(argc, argv);
  parser.process(app);
  config.fromCommandLine(parser);
  CLI cli(&config, &parser);
  _exit(app.exec());
  return 1;
}
