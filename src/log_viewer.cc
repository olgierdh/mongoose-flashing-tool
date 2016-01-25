#include "log_viewer.h"

#include <iostream>

#include <QScrollBar>

namespace {
const int kMaxLineLength = 1000;
}

LogViewer::LogViewer(QWidget *parent) : QWidget(parent) {
  ui_.setupUi(this);
  cursor_.reset(new QTextCursor(ui_.logView->document()));
  cursor_->movePosition(QTextCursor::End);
  for (const auto &e : Log::getBufferedLines()) {
    newLogEntry(e);
  }
  connect(Log::entrySource(), &Log::EntrySource::newLogEntry, this,
          &LogViewer::newLogEntry);
  connect(ui_.clearButton, &QPushButton::clicked, this, &LogViewer::clearView);
}

LogViewer::~LogViewer() {
}

void LogViewer::newLogEntry(const Log::Entry e) {
  QScrollBar *scroll = ui_.logView->verticalScrollBar();
  const bool autoscroll = (scroll->value() == scroll->maximum());
  {
    QString line;
    if (e.file != "") {
      line =
          QString("%1 %2:%3 %4").arg(e.type).arg(e.file).arg(e.line).arg(e.msg);
    } else {
      line = QString("%1 %4").arg(e.type).arg(e.msg);
    }
    if (line.length() > kMaxLineLength) {
      line = QString("%1... (%2)")
                 .arg(line.left(kMaxLineLength))
                 .arg(line.length());
    }
    if (!first_) cursor_->insertBlock();
    cursor_->insertText(line);
  }
  if (autoscroll) {
    scroll->setValue(scroll->maximum());
  }
  first_ = false;
}

void LogViewer::clearView() {
  ui_.logView->clear();
  first_ = true;
}

void LogViewer::closeEvent(QCloseEvent *event) {
  QWidget::closeEvent(event);
  emit closed();
}
