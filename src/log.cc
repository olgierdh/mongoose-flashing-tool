#include "log.h"

#include <iostream>
#include <memory>

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QMutexLocker>

namespace {

const int kMaxBufferedLines = 10000;

using std::endl;

class LocalLogSource : public Log::EntrySource {
 public:
  virtual ~LocalLogSource() {
  }
  void addLogEntry(const Log::Entry &e) {
    emit newLogEntry(e);
  }
};

QMutex mtx;  // guards verbosity, logfile and lines.
int verbosity = 0;
std::ostream *logfile = nullptr;
std::unique_ptr<std::ostream> logfile_owner;
QList<Log::Entry> lines;
std::unique_ptr<LocalLogSource> logSource;

void outputHandler(QtMsgType type, const QMessageLogContext &context,
                   const QString &msg) {
  QMutexLocker lock(&mtx);
  const Log::Entry e{type, context.file, context.line, msg};
  lines.push_back(e);
  if (lines.length() > kMaxBufferedLines) lines.pop_front();
  logSource->addLogEntry(e);
  if (logfile == nullptr) {
    return;
  }
  QByteArray localMsg = msg.toLocal8Bit();
  const char *ll = nullptr;
  bool die = false;
  switch (type) {
    case QtDebugMsg:
      if (verbosity >= 4) ll = "DEBUG";
      break;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 5, 0))
    case QtInfoMsg:
      if (verbosity >= 3) ll = "INFO";
      break;
#endif
    case QtWarningMsg:
      if (verbosity >= 2) ll = "WARNING";
      break;
    case QtCriticalMsg:
      if (verbosity >= 1) ll = "CRITICAL";
      break;
      if (verbosity >= 1) {
        *logfile << "CRITICAL: ";
        if (context.file != NULL) {
          *logfile << context.file << ":" << context.line << " ";
        }
        *logfile << localMsg.constData() << endl;
      }
      break;
    case QtFatalMsg:
      ll = "FATAL";
      die = true;
      break;
  }
  if (ll != nullptr) {
    *logfile << ll << ": ";
    if (context.file != NULL) {
      *logfile << context.file << ":" << context.line << " ";
    }
    *logfile << localMsg.constData() << endl;
  }
  if (die) abort();
}

}  // namespace

namespace Log {

void init() {
  qRegisterMetaType<Log::Entry>("Log::Entry");
  logSource.reset(new LocalLogSource);
  qInstallMessageHandler(outputHandler);
}

void setVerbosity(int v) {
  QMutexLocker lock(&mtx);
  verbosity = v;
}

void setFile(std::ostream *file) {
  QMutexLocker lock(&mtx);
  logfile = file;
  if (file != &std::cout && file != &std::cerr && file != &std::clog) {
    logfile_owner.reset(file);
  }
}

QList<Entry> getBufferedLines() {
  QMutexLocker l(&mtx);
  return lines;
}

EntrySource *entrySource() {
  return logSource.get();
}

}  // namespace Log
