#include "about_dialog.h"

// From build_info.cc (auto-generated).
extern const char *build_id;

AboutDialog::AboutDialog(QWidget *parent) : QWidget(parent) {
  ui_.setupUi(this);
  ui_.versionLabel->setText(tr("Version: %1").arg(qApp->applicationVersion()));
  ui_.buildLabel->setText(build_id);
  ui_.buildLabel->setReadOnly(true);
}

AboutDialog::~AboutDialog() {
}

void AboutDialog::closeEvent(QCloseEvent *event) {
  QWidget::closeEvent(event);
  emit closed();
}
