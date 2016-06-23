/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_PROGRESS_WIDGET_PROGRESS_WIDGET_H_
#define CS_MFT_SRC_PROGRESS_WIDGET_PROGRESS_WIDGET_H_

#include <QWidget>

class ProgressWidget : public QWidget {
  Q_OBJECT

 public:
  ProgressWidget(QWidget *parent = 0);

 public slots:
  void setProgress(double progress, double total);

 private:
  void paintEvent(QPaintEvent *event) override;

  double progress_ = 0;
  double total_ = 1;
};

#endif /* CS_MFT_SRC_PROGRESS_WIDGET_PROGRESS_WIDGET_H_ */
