#include "wizard.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QSerialPortInfo>

#include "cc3200.h"
#include "esp8266.h"
#include "flasher.h"
#include "fw_bundle.h"
#include "serial.h"
#include "status_qt.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

namespace {

const char kReleaseInfoFile[] = ":/releases.json";
// In the future we may want to fetch release info from a server.
// const char kReleaseInfoURL[] = "https://backend.cesanta.com/...";

}  // namespace

WizardDialog::WizardDialog(Config *config, QWidget *parent)
    : QMainWindow(parent), config_(config), prompter_(this) {
  ui_.setupUi(this);
  restoreGeometry(settings_.value("wizard/geometry").toByteArray());

  connect(ui_.steps, &QStackedWidget::currentChanged, this,
          &WizardDialog::currentStepChanged);
  ui_.steps->setCurrentIndex(static_cast<int>(Step::Connect));
  connect(ui_.prevBtn, &QPushButton::clicked, this, &WizardDialog::prevStep);
  connect(ui_.nextBtn, &QPushButton::clicked, this, &WizardDialog::nextStep);

  portRefreshTimer_.start(1);
  connect(&portRefreshTimer_, &QTimer::timeout, this,
          &WizardDialog::updatePortList);

  connect(ui_.platformSelector, &QComboBox::currentTextChanged, this,
          &WizardDialog::updateFirmwareSelector);

  connect(&prompter_, &GUIPrompter::showPrompt, this,
          &WizardDialog::showPrompt);
  connect(this, &WizardDialog::showPromptResult, &prompter_,
          &GUIPrompter::showPromptResult);

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
    case Step::Connect: {
      selectedPlatform_ = ui_.platformSelector->currentText();
      const QString selectedPort = ui_.portSelector->currentText();
      if (port_ == nullptr || port_->portName() != selectedPort) {
        port_.reset();
        auto sp = connectSerial(QSerialPortInfo(selectedPort), 115200);
        if (!sp.ok()) {
          QMessageBox::critical(this, tr("Error"),
                                tr("Error opening %1: %2")
                                    .arg(selectedPort)
                                    .arg(sp.status().ToString().c_str()));
        } else {
          port_.reset(sp.ValueOrDie());
        }
      }
      if (port_ != nullptr) {
        qInfo() << "Probing" << selectedPlatform_ << "@" << selectedPort;
        if (selectedPlatform_ == "ESP8266") {
          hal_ = ESP8266::HAL(port_.get());
        } else if (selectedPlatform_ == "CC3200") {
          hal_ = CC3200::HAL(port_.get());
        } else {
          qFatal("Unknown platform: %s",
                 selectedPlatform_.toStdString().c_str());
        }
        util::Status st = hal_->probe();
        if (st.ok()) {
          ni = Step::FirmwareSelection;
          qInfo() << "Probe successful";
        } else {
          const QString msg = tr("Did not find %1 @ %2")
                                  .arg(selectedPlatform_)
                                  .arg(selectedPort);
          qCritical() << msg;
          QMessageBox::critical(this, tr("Error"), msg);
          hal_.reset();
        }
      }
      break;
    }
    case Step::FirmwareSelection: {
      selectedFirmwareURL_ = ui_.firmwareSelector->currentText();
      if (selectedFirmwareURL_.scheme() == "") {
        selectedFirmwareURL_ = ui_.firmwareSelector->currentData().toString();
      }
      qInfo() << "Selected platform:" << selectedPlatform_
              << "fw:" << selectedFirmwareURL_;
      if (selectedFirmwareURL_.scheme() != "") {
        ni = Step::Flashing;
      } else {
        QMessageBox::critical(this, tr("Error"), tr("Invalid firmware URL"));
      }
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
  if (ni != Step::Invalid) {
    qDebug() << "Step" << ci << "->" << ni;
    ui_.steps->setCurrentIndex(static_cast<int>(ni));
  }
}

void WizardDialog::currentStepChanged() {
  const Step ci = currentStep();
  qInfo() << "Step" << ci;
  if (ci == Step::Connect) {
    hal_.reset();
    ui_.prevBtn->hide();
    ui_.nextBtn->setEnabled(ui_.portSelector->currentText() != "");
  } else {
    ui_.prevBtn->show();
  }
  if (ci == Step::FirmwareSelection) {
    QTimer::singleShot(1, this, &WizardDialog::updateFirmwareSelector);
  }
  if (ci == Step::Flashing) {
    QTimer::singleShot(1, this, &WizardDialog::startFirmwareDownload);
    ui_.nextBtn->setEnabled(false);
  }
  if (ci == Step::Finish) {
    ui_.nextBtn->setText(tr("Finish"));
  } else {
    ui_.nextBtn->setText(tr("Next >"));
  }
}

void WizardDialog::prevStep() {
  const Step ci = currentStep();
  Step ni = Step::Invalid;
  switch (ci) {
    case Step::Connect: {
      break;
    }
    case Step::FirmwareSelection: {
      ni = Step::Connect;
      break;
    }
    case Step::Flashing: {
      if (fd_ != nullptr) fd_->abort();
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
  if (ni != Step::Invalid) {
    qDebug() << "Step" << ni << "<-" << ci;
    ui_.steps->setCurrentIndex(static_cast<int>(ni));
  }
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
  if (currentStep() == Step::Connect) {
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
    ui_.firmwareSelector->addItem(name, loc);
  }
  if (currentStep() == Step::FirmwareSelection) {
    ui_.nextBtn->setEnabled(ui_.firmwareSelector->currentText() != "");
  }
}

void WizardDialog::startFirmwareDownload() {
  ui_.s2_1_title->setText(tr("DOWNLOADING ..."));
  if (fd_ == nullptr || fd_->url() != selectedFirmwareURL_) {
    fd_.reset(new FileDownloader(selectedFirmwareURL_));
    connect(fd_.get(), &FileDownloader::progress, this,
            &WizardDialog::downloadProgress);
    connect(fd_.get(), &FileDownloader::finished, this,
            &WizardDialog::downloadFinished);
  }
  fd_->start();
}

void WizardDialog::downloadProgress(qint64 recd, qint64 total) {
  qDebug() << "downloadProgress" << recd << total;
  if (currentStep() != Step::Flashing) return;
  const qint64 progressPct = recd * 100 / total;
  ui_.s2_1_progress->setText(tr("%1%").arg(progressPct));
}

void WizardDialog::downloadFinished() {
  util::Status st = fd_->status();
  qDebug() << "downloadFinished" << st;
  if (!st.ok()) {
    QMessageBox::critical(this, tr("Error"), st.ToString().c_str());
    prevStep();
    return;
  }
  flashFirmware(fd_->fileName());
}

void WizardDialog::flashFirmware(const QString &fileName) {
  qInfo() << "Loading" << fileName;
  ui_.s2_1_title->setText(tr("LOADING ..."));
  ui_.s2_1_progress->setText("0%");

  auto fwbs = NewZipFWBundle(fileName);
  if (!fwbs.ok()) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to load %1: %2")
                              .arg(fileName)
                              .arg(fwbs.status().ToString().c_str()));
    return;
  }
  std::unique_ptr<FirmwareBundle> fwb = fwbs.MoveValueOrDie();
  if (fwb->platform().toUpper() != selectedPlatform_) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Platform mismatch: want %1, got %2")
                              .arg(selectedPlatform_)
                              .arg(fwb->platform()));
    return;
  }
  qInfo() << "Flashing" << fwb->name() << fwb->buildId();
  ui_.s2_1_title->setText(tr("FLASHING ..."));

  std::unique_ptr<Flasher> f(hal_->flasher(&prompter_));
  auto s = f->setOptionsFromConfig(*config_);
  if (!s.ok()) {
    QMessageBox::critical(
        this, tr("Error"),
        tr("Invalid command line flag setting: %1").arg(s.ToString().c_str()));
    return;
  }
  s = f->setFirmware(fwb.get());
  if (!s.ok()) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Invalid firmware: %1").arg(s.ToString().c_str()));
    return;
  }
  bytesToFlash_ = f->totalBytes();
  connect(f.get(), &Flasher::progress, this, &WizardDialog::flashingProgress);
  connect(f.get(), &Flasher::done,
          [this]() { port_->moveToThread(this->thread()); });
  /* TODO(rojer)
    connect(f.get(), &Flasher::statusMessage,
            [this](QString msg, bool important) {
              setStatusMessage(MsgType::INFO, msg);
              (void) important;
            });
  */
  connect(f.get(), &Flasher::done, this, &WizardDialog::flashingDone);

  // Can't go back while flashing thread is running.
  ui_.prevBtn->setEnabled(false);
  worker_.reset(new QThread);
  connect(worker_.get(), &QThread::finished, f.get(), &QObject::deleteLater);
  f->moveToThread(worker_.get());
  port_->moveToThread(worker_.get());
  worker_->start();
  QTimer::singleShot(0, f.release(), &Flasher::run);
}

void WizardDialog::flashingProgress(int bytesWritten) {
  qDebug() << "Flashed" << bytesWritten << "of" << bytesToFlash_;
  if (currentStep() != Step::Flashing) return;
  const int progressPct = bytesWritten * 100 / bytesToFlash_;
  ui_.s2_1_progress->setText(tr("%1%").arg(progressPct));
}

void WizardDialog::flashingDone(QString msg, bool success) {
  worker_->quit();
  ui_.prevBtn->setEnabled(true);
  if (success) {
    ui_.nextBtn->setEnabled(true);
  } else {
    QMessageBox::critical(this, tr("Error"), tr("Flashing error: %1").arg(msg));
    return;
  }
}

void WizardDialog::showPrompt(
    QString text, QList<QPair<QString, Prompter::ButtonRole>> buttons) {
  emit showPromptResult(prompter_.doShowPrompt(text, buttons));
}

void WizardDialog::closeEvent(QCloseEvent *event) {
  settings_.setValue("wizard/geometry", saveGeometry());
  QMainWindow::closeEvent(event);
}

QDebug &operator<<(QDebug &d, const WizardDialog::Step s) {
  return d << static_cast<int>(s);
}
