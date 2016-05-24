#include "wizard.h"

#include <QDebug>
#include <QSerialPortInfo>

namespace {

/* These correspond to widget indices in the stack. */
enum class Step {
  Begin = 0,
  FirmwareSelection = 1,
  Flashing = 2,
  WiFiConfig = 3,
  WiFiConnect = 4,
  CloudRegistration = 5,
  CloudCredentials = 6,
  CloudConnect = 7,
  Finish = 8,

  Invalid = 99,
};

}  // namespace

WizardDialog::WizardDialog(Config *config, QWidget *parent)
    : QMainWindow(parent), config_(config) {
  ui_.setupUi(this);
  restoreGeometry(settings_.value("wizard/geometry").toByteArray());

  ui_.steps->setCurrentIndex(static_cast<int>(Step::Begin));
  connect(ui_.prevBtn, &QPushButton::clicked, this, &WizardDialog::prevStep);
  connect(ui_.nextBtn, &QPushButton::clicked, this, &WizardDialog::nextStep);

  port_refresh_timer_.start(1);
  connect(&port_refresh_timer_, &QTimer::timeout, this,
          &WizardDialog::updatePortList);
}

void WizardDialog::nextStep() {
  const Step ci = static_cast<Step>(ui_.steps->currentIndex());
  Step ni = Step::Invalid;
  switch (ci) {
    case Step::Begin: {
      ni = Step::FirmwareSelection;
      break;
    }
    case Step::FirmwareSelection: {
      ni = Step::Flashing;
      break;
    }
    case Step::Flashing: {
      ni = Step::WiFiConfig;
      break;
    }
    case Step::WiFiConfig: {
      ni = Step::WiFiConnect;
      break;
    }
    case Step::WiFiConnect: {
      ni = Step::CloudRegistration;
      break;
    }
    case Step::CloudRegistration: {
      if (ui_.s4_newID->isChecked()) {
        ni = Step::CloudConnect;
      } else if (ui_.s4_existingID->isChecked()) {
        ni = Step::CloudCredentials;
      } else {
        ni = Step::Finish;
      }
      break;
    }
    case Step::CloudCredentials: {
      ni = Step::CloudConnect;
      break;
    }
    case Step::CloudConnect: {
      ni = Step::Finish;
      break;
    }
    case Step::Finish: {
    }
    case Step::Invalid: {
      break;
    }
  }
  qDebug() << static_cast<int>(ci) << "->" << static_cast<int>(ni);

  if (ni != Step::Invalid) ui_.steps->setCurrentIndex(static_cast<int>(ni));
}

void WizardDialog::prevStep() {
  const Step ci = static_cast<Step>(ui_.steps->currentIndex());
  Step ni = Step::Invalid;
  switch (ci) {
    case Step::Begin: {
      break;
    }
    case Step::FirmwareSelection: {
      ni = Step::Begin;
      break;
    }
    case Step::Flashing: {
      ni = Step::FirmwareSelection;
      break;
    }
    case Step::WiFiConfig: {
      ni = Step::Flashing;
      break;
    }
    case Step::WiFiConnect: {
      ni = Step::WiFiConfig;
      break;
    }
    case Step::CloudRegistration: {
      ni = Step::WiFiConnect;
      break;
    }
    case Step::CloudCredentials: {
      ni = Step::CloudRegistration;
      break;
    }
    case Step::CloudConnect: {
      if (ui_.s4_existingID->isChecked()) {
        ni = Step::CloudCredentials;
      } else {
        ni = Step::CloudRegistration;
      }
      break;
    }
    case Step::Finish: {
      ni = Step::CloudRegistration;
    }
    case Step::Invalid: {
      break;
    }
  }
  qDebug() << static_cast<int>(ci) << "->" << static_cast<int>(ni);

  if (ni != Step::Invalid) ui_.steps->setCurrentIndex(static_cast<int>(ni));
}

void WizardDialog::updatePortList() {
  QSet<QString> ports;
  for (const auto &info : QSerialPortInfo::availablePorts()) {
#ifdef Q_OS_MAC
    if (info.portName().contains("Bluetooth")) continue;
#endif
    ports.insert(info.portName());
  }

  for (int i = 0; i < ui_.portSelector->count(); i++) {
    if (ui_.portSelector->itemData(i).type() != QVariant::String) continue;
    const QString &portName = ui_.portSelector->itemData(i).toString();
    if (!ports.contains(portName)) {
      ui_.portSelector->removeItem(i);
      qDebug() << "Removing port" << portName;
      i--;
    } else {
      ports.remove(portName);
    }
  }

  for (const auto &portName : ports) {
    qDebug() << "Adding port" << portName;
    ui_.portSelector->addItem(portName, portName);
  }

  port_refresh_timer_.start(500);
}

void WizardDialog::closeEvent(QCloseEvent *event) {
  settings_.setValue("wizard/geometry", saveGeometry());
  QMainWindow::closeEvent(event);
}
