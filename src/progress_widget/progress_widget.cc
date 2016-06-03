#include "progress_widget.h"

#include <QDebug>
#include <QtWidgets>

ProgressWidget::ProgressWidget(QWidget *parent) : QWidget(parent) {
}

void ProgressWidget::setProgress(double progress, double total) {
  progress_ = progress;
  total_ = total;
  repaint();
}

void ProgressWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  int ringWidth = width() * (10.0 / 90.0);
  QRectF outer(1, 1, width() - 1, height() - 1);
  QRectF inner(ringWidth, ringWidth, width() - 2 * ringWidth,
               height() - 2 * ringWidth);
  const QColor green(46, 198, 86, 255);
  p.setPen(QPen(Qt::white, 1, Qt::SolidLine));
  p.drawEllipse(outer);
  p.setBrush(QBrush(green, Qt::SolidPattern));
  const double progressFraction = qMin(1.0, progress_ / total_);
  if (progressFraction > 0) {
    p.drawPie(outer, 90 * 16, -progressFraction * 360 * 16);
  }
  p.setBrush(p.background());
  p.drawEllipse(inner);

  QFont f = p.font();
  f.setPointSize(28);
  p.setFont(f);

  QRectF textRect(ringWidth, height() / 2 - 25, width() - 2 * ringWidth, 50);
  //  p.setBrush(QBrush(Qt::red, Qt::SolidPattern));
  //  p.drawRect(textRect);
  const int progressPct = progressFraction * 100;
  p.drawText(textRect, Qt::AlignCenter, QString("%1%").arg(progressPct));
}
