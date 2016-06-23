/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_GUI_PROMPTER_H_
#define CS_MFT_SRC_GUI_PROMPTER_H_

#include "prompter.h"

#include <QMutex>
#include <QWaitCondition>

class GUIPrompter : public Prompter {
  Q_OBJECT

 public:
  GUIPrompter(QObject *parent);
  virtual ~GUIPrompter();

  int Prompt(QString text, QList<QPair<QString, ButtonRole>> buttons) override;

  int doShowPrompt(QString text, QList<QPair<QString, ButtonRole>> buttons);

signals:
  void showPrompt(QString text,
                  QList<QPair<QString, Prompter::ButtonRole>> buttons);

 public slots:
  void showPromptResult(int clicked_button);

 private:
  QMutex lock_;
  QWaitCondition wc_;
  int clicked_button_;
};

#endif /* CS_MFT_SRC_GUI_PROMPTER_H_ */
