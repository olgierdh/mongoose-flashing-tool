#include "wizard.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSerialPortInfo>

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

namespace {

const char kReleaseInfoFile[] = ":/releases.json";
// In the future we may want to fetch release info from a server.
// const char kReleaseInfoURL[] = "https://backend.cesanta.com/...";

}  // namespace

WizardDialog::WizardDialog(Config *config, QWidget *parent)
    : QMainWindow(parent), config_(config) {
  ui_.setupUi(this);
  restoreGeometry(settings_.value("wizard/geometry").toByteArray());

  connect(ui_.steps, &QStackedWidget::currentChanged, this,
          &WizardDialog::currentStepChanged);
  ui_.steps->setCurrentIndex(static_cast<int>(Step::Begin));
  connect(ui_.prevBtn, &QPushButton::clicked, this, &WizardDialog::prevStep);
  connect(ui_.nextBtn, &QPushButton::clicked, this, &WizardDialog::nextStep);

  portRefreshTimer_.start(1);
  connect(&portRefreshTimer_, &QTimer::timeout, this,
          &WizardDialog::updatePortList);

  connect(ui_.platformSelector, &QComboBox::currentTextChanged, this,
          &WizardDialog::updateFirmwareSelector);

  QTimer::singleShot(10, this, &WizardDialog::currentStepChanged);
  QTimer::singleShot(10, this, &WizardDialog::updateReleaseInfo);
}

WizardDialog::Step WizardDialog::currentStep() const {
  return static_cast<Step>(ui_.steps->currentIndex());
}

void WizardDialog::nextStep() {
  const Step ci = currentStep();
  Step ni = Step::Invalid;
  switch (ci) {
    case Step::Begin: {
      ni = Step::FirmwareSelection;
      selectedPort_ = ui_.portSelector->currentText();
      qInfo() << "Selected port:" << selectedPort_;
      break;
    }
    case Step::FirmwareSelection: {
      ni = Step::Flashing;
      selectedPlatform_ = ui_.platformSelector->currentText();
      selectedFirmware_ = ui_.firmwareSelector->currentData().toString();
      qInfo() << "Selected platform:" << selectedPlatform_
              << "fw:" << selectedFirmware_;
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
  qDebug() << "Step" << ci << "->" << ni;

  if (ni != Step::Invalid) ui_.steps->setCurrentIndex(static_cast<int>(ni));
}

void WizardDialog::currentStepChanged() {
  const Step ci = currentStep();
  qInfo() << "Step" << ci;
  if (ci == Step::Begin) {
    ui_.prevBtn->hide();
    ui_.nextBtn->setEnabled(ui_.portSelector->currentText() != "");
  } else {
    ui_.prevBtn->show();
  }
  if (ci == Step::Finish) {
    ui_.nextBtn->setText(tr("Finish"));
  } else {
    ui_.nextBtn->setText(tr("Next >"));
  }
  if (ci == Step::FirmwareSelection) {
    QTimer::singleShot(1, this, &WizardDialog::updateFirmwareSelector);
  }
}

void WizardDialog::prevStep() {
  const Step ci = currentStep();
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
  qDebug() << "Step" << ni << "<-" << ci;

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

  portRefreshTimer_.start(500);
  if (currentStep() == Step::Begin) {
    ui_.nextBtn->setEnabled(ui_.portSelector->currentText() != "");
  }
}

void WizardDialog::updateReleaseInfo() {
  QFile f(kReleaseInfoFile);
  if (!f.open(QIODevice::ReadOnly)) {
    qFatal("Failed to open release info file");
  }
  QJsonParseError err;
  QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError) {
    qFatal("Failed to parse JSON");
  }
  if (!doc.isObject()) {
    qFatal("Release info is not an object");
  }
  if (!doc.object().contains("releases")) {
    qFatal("No release info in the object");
  }
  if (!doc.object()["releases"].isArray()) {
    qFatal("Release list is not an array");
  }
  releases_ = doc.object()["releases"].toArray();
}

void WizardDialog::updateFirmwareSelector() {
  const QString platform = ui_.platformSelector->currentText().toUpper();
  ui_.firmwareSelector->clear();
  for (const auto &item : releases_) {
    if (!item.isObject()) continue;
    const QJsonObject &r = item.toObject();
    if (!r["name"].isString() || !r["locs"].isObject()) continue;
    const QString &name = r["name"].toString();
    const QJsonObject &locs = r["locs"].toObject();
    if (!locs[platform].isString()) continue;
    const QString &loc = locs[platform].toString();
    qDebug() << platform << name << loc;
    ui_.firmwareSelector->addItem(name, loc);
  }
  if (currentStep() == Step::FirmwareSelection) {
    ui_.nextBtn->setEnabled(ui_.firmwareSelector->currentText() != "");
  }
}

void WizardDialog::closeEvent(QCloseEvent *event) {
  settings_.setValue("wizard/geometry", saveGeometry());
  QMainWindow::closeEvent(event);
}

QDebug &operator<<(QDebug &d, const WizardDialog::Step s) {
  return d << static_cast<int>(s);
}
