#ifndef LOG_VIEWER_H
#define LOG_VIEWER_H

#include <memory>

#include <QTextCursor>
#include <QWidget>

#include "log.h"
#include "ui_log_viewer.h"

class LogViewer : public QWidget {
  Q_OBJECT

 public:
  LogViewer(QWidget *parent);
  virtual ~LogViewer();

 private slots:
  void newLogEntry(const Log::Entry e);
  void clearView();

signals:
  void closed();

 private:
  void closeEvent(QCloseEvent *event);

  Ui::LogViewer ui_;
  std::unique_ptr<QTextCursor> cursor_;
  bool first_ = true;
};

#endif  // LOG_VIEWER_H
