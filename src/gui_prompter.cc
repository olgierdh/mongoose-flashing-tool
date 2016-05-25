#include "gui_prompter.h"

#include <QMap>
#include <QMessageBox>
#include <QPushButton>

GUIPrompter::GUIPrompter(QObject *parent) : Prompter(parent) {
}

GUIPrompter::~GUIPrompter() {
}

int GUIPrompter::Prompt(QString text,
                        QList<QPair<QString, ButtonRole>> buttons) {
  QMutexLocker l(&lock_);
  emit showPrompt(text, buttons);
  wc_.wait(&lock_);
  return clicked_button_;
}

void GUIPrompter::showPromptResult(int clicked_button) {
  QMutexLocker l(&lock_);
  clicked_button_ = clicked_button;
  wc_.wakeOne();
}

int GUIPrompter::doShowPrompt(QString text,
                              QList<QPair<QString, ButtonRole>> buttons) {
  QMessageBox mb;
  mb.setText(text);
  QMap<QAbstractButton *, int> b2i;
  int i = 0;
  for (const auto &bd : buttons) {
    QMessageBox::ButtonRole role = QMessageBox::YesRole;
    switch (bd.second) {
      case Prompter::ButtonRole::Yes:
        role = QMessageBox::YesRole;
        break;
      case Prompter::ButtonRole::No:
        role = QMessageBox::NoRole;
        break;
      case Prompter::ButtonRole::Reject:
        role = QMessageBox::RejectRole;
        break;
    }
    QAbstractButton *b = mb.addButton(bd.first, role);
    b2i[b] = i++;
  }
  mb.exec();
  QAbstractButton *clicked = mb.clickedButton();
  return b2i.contains(clicked) ? b2i[clicked] : -1;
}
