#include "dialog.h"

#include <iostream>
#include <fstream>

#include <QApplication>
#include <QBoxLayout>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QGuiApplication>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QScrollBar>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QStringList>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "cc3200.h"
#include "config.h"
#include "esp8266.h"
#include "flasher.h"
#include "log.h"
#include "log_viewer.h"
#include "serial.h"
#include "status_qt.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 5, 0))
#define qInfo qWarning
#endif

// From build_info.cc (auto-generated).
extern const char *build_id;

namespace {

const int kInputHistoryLength = 1000;
const char kPromptEnd[] = "$ ";

const int kDefaultConsoleBaudRate = 115200;

}  // namespace

// static
void MainDialog::addOptions(Config *config) {
  QList<QCommandLineOption> opts;
  opts.append(QCommandLineOption(
      "console-line-count",
      "Maximum number of lines to keep in console window.", "count", "4096"));
  opts.append(QCommandLineOption(
      "console-log",
      "If set, bytes read from a serial port in console mode will be "
      "appended to the given file.",
      "file"));
  config->addOptions(opts);
}

MainDialog::MainDialog(Config *config, QWidget *parent)
    : QMainWindow(parent),
      config_(config),
      prompter_(this),
      settingsDlg_(config->options(), this) {
  ui_.setupUi(this);

  input_history_ = settings_.value("terminal/history").toStringList();
  restoreGeometry(settings_.value("window/geometry").toByteArray());
  restoreState(settings_.value("window/state").toByteArray());
  skip_detect_warning_ = settings_.value("skipDetectWarning", false).toBool();

#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
  connect(this, &MainDialog::updatePlatformSelector, ui_.platformSelector,
          &QComboBox::setCurrentIndex, Qt::QueuedConnection);
#endif

  QString p = settings_.value("selectedPlatform", "ESP8266").toString();
  for (int i = 0; i < ui_.platformSelector->count(); i++) {
    if (p == ui_.platformSelector->itemText(i)) {
#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
      emit updatePlatformSelector(i);
#else
      QTimer::singleShot(
          0, [this, i]() { ui_.platformSelector->setCurrentIndex(i); });
#endif
      break;
    }
  }

  net_mgr_.updateConfigurations();
  platformChanged();
  ui_.progressBar->hide();
  ui_.statusMessage->hide();

  const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  ui_.terminal->setFont(fixedFont);
  ui_.terminalInput->installEventFilter(this);

  action_enabled_in_state_.insert(ui_.actionConfigure_Wi_Fi, Terminal);
  action_enabled_in_state_.insert(ui_.actionUpload_a_file, Terminal);
  enabled_in_state_.insert(ui_.connectBtn, Connected);
  enabled_in_state_.insert(ui_.connectBtn, NotConnected);
  enabled_in_state_.insert(ui_.connectBtn, Terminal);
  enabled_in_state_.insert(ui_.flashBtn, Connected);
  enabled_in_state_.insert(ui_.flashBtn, NotConnected);
  enabled_in_state_.insert(ui_.flashBtn, Terminal);
  enabled_in_state_.insert(ui_.platformSelector, NoPortSelected);
  enabled_in_state_.insert(ui_.platformSelector, NotConnected);
  enabled_in_state_.insert(ui_.portSelector, NotConnected);
  enabled_in_state_.insert(ui_.rebootBtn, Connected);
  enabled_in_state_.insert(ui_.rebootBtn, Terminal);
  enabled_in_state_.insert(ui_.terminalInput, Terminal);
  enabled_in_state_.insert(ui_.uploadBtn, Terminal);

  enableControlsForCurrentState();

#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
  QTimer::singleShot(0, this, SLOT(updatePortList()));
#else
  QTimer::singleShot(0, this, &MainDialog::updatePortList);
#endif
  refresh_timer_ = new QTimer(this);
  refresh_timer_->start(500);
  connect(refresh_timer_, &QTimer::timeout, this, &MainDialog::updatePortList);

  connect(this, &MainDialog::gotPrompt, this, &MainDialog::sendQueuedCommand);

  connect(ui_.portSelector, static_cast<void (QComboBox::*) (int) >(
                                &QComboBox::currentIndexChanged),
          [this](int index) {
            switch (state_) {
              case NoPortSelected:
                if (index >= 0) {
                  setState(NotConnected);
                }
                break;
              case NotConnected:
                if (index < 0) {
                  setState(NoPortSelected);
                }
                break;
              default:
                // no-op
                break;
            }
          });

  connect(ui_.platformSelector, &QComboBox::currentTextChanged, this,
          &MainDialog::platformChanged);
  connect(ui_.platformSelector, &QComboBox::currentTextChanged,
          [this](QString platform) {
            settings_.setValue("selectedPlatform", platform);
          });

  connect(ui_.browseBtn, &QPushButton::clicked, this,
          &MainDialog::selectFirmwareFile);

  connect(ui_.flashBtn, &QPushButton::clicked, this, &MainDialog::flashClicked);

  connect(ui_.connectBtn, &QPushButton::clicked, this,
          &MainDialog::connectDisconnectTerminal);
  connect(ui_.rebootBtn, &QPushButton::clicked, this, &MainDialog::reboot);

  connect(ui_.actionConfigure_Wi_Fi, &QAction::triggered, this,
          &MainDialog::configureWiFi);
  connect(ui_.actionUpload_a_file, &QAction::triggered, this,
          &MainDialog::uploadFile);
  connect(ui_.actionTruncate_log_file, &QAction::triggered, this,
          &MainDialog::truncateConsoleLog);

  connect(ui_.uploadBtn, &QPushButton::clicked, this, &MainDialog::uploadFile);

  connect(ui_.terminalInput, &QLineEdit::returnPressed, this,
          &MainDialog::writeSerial);

  connect(ui_.actionOpenWebsite, &QAction::triggered, [this]() {
    const QString url = "https://www.cesanta.com/products/mongoose-iot";
    if (!QDesktopServices::openUrl(QUrl(url))) {
      QMessageBox::warning(this, tr("Error"), tr("Failed to open %1").arg(url));
    }
  });
  connect(ui_.actionOpenDashboard, &QAction::triggered, [this]() {
    const QString url = "https://dashboard.cesanta.com/";
    if (!QDesktopServices::openUrl(QUrl(url))) {
      QMessageBox::warning(this, tr("Error"), tr("Failed to open %1").arg(url));
    }
  });
  connect(ui_.actionSend_feedback, &QAction::triggered, [this]() {
    const QString url = "https://www.cesanta.com/contact";
    if (!QDesktopServices::openUrl(QUrl(url))) {
      QMessageBox::warning(this, tr("Error"), tr("Failed to open %1").arg(url));
    }
  });
  connect(ui_.actionHelp, &QAction::triggered, [this]() {
    const QString url = "https://github.com/cesanta/mft/blob/master/README.md";
    if (!QDesktopServices::openUrl(QUrl(url))) {
      QMessageBox::warning(this, tr("Error"), tr("Failed to open %1").arg(url));
    }
  });

  connect(ui_.actionLog, &QAction::triggered, this, &MainDialog::showLogViewer);

  connect(ui_.actionAbout_Qt, &QAction::triggered, qApp,
          &QApplication::aboutQt);
  connect(ui_.actionAbout, &QAction::triggered, this,
          &MainDialog::showAboutBox);

  connect(ui_.actionSettings, &QAction::triggered, this,
          &MainDialog::showSettings);
  connect(&settingsDlg_, &SettingsDialog::knobUpdated, this,
          &MainDialog::updateConfig);

  for (const auto &opt : config_->options()) {
    updateConfig(opt.names()[0]);
  }

  connect(&prompter_, &GUIPrompter::showPrompt, this, &MainDialog::showPrompt);
  connect(this, &MainDialog::showPromptResult, &prompter_,
          &GUIPrompter::showPromptResult);

  ui_.versionLabel->setText(
      tr("Build: %1 %2").arg(qApp->applicationVersion()).arg(build_id));

  openConsoleLogFile(false /* truncate */);
}

void MainDialog::setState(State newState) {
  State old = state_;
  state_ = newState;
  qInfo() << "MainDialog state changed from" << old << "to" << newState;
  enableControlsForCurrentState();
  // TODO(imax): find a better place for this.
  switch (state_) {
    case NoPortSelected:
    case NotConnected:
    case Downloading:
    case Flashing:
    case PortGoneWhileFlashing:
      ui_.connectBtn->setText(tr("&Connect"));
      break;
    case Connected:
    case Terminal:
      ui_.connectBtn->setText(tr("Dis&connect"));
      break;
  }
}

void MainDialog::enableControlsForCurrentState() {
  for (QWidget *w : enabled_in_state_.keys()) {
    w->setEnabled(enabled_in_state_.find(w, state_) != enabled_in_state_.end());
  }
  for (QAction *a : action_enabled_in_state_.keys()) {
    a->setEnabled(action_enabled_in_state_.find(a, state_) !=
                  action_enabled_in_state_.end());
  }
}

void MainDialog::platformChanged() {
  hal_.reset();
  {
    const QString selectedPlatform = ui_.platformSelector->currentText();
    const QString fwForPlatform =
        settings_.value(QString("selectedFirmware_%1").arg(selectedPlatform),
                        "").toString();
    ui_.firmwareFileName->setText(fwForPlatform);
  }
}

void MainDialog::showPrompt(
    QString text, QList<QPair<QString, Prompter::ButtonRole>> buttons) {
  emit showPromptResult(prompter_.doShowPrompt(text, buttons));
}

util::Status MainDialog::openSerial() {
  if (state_ != NotConnected) {
    return util::Status::OK;
  }
  QString portName = ui_.portSelector->currentData().toString();
  if (portName == "") {
    return util::Status(util::error::INVALID_ARGUMENT,
                        tr("No port selected").toStdString());
  }

  qDebug() << "Opening" << portName;
  util::StatusOr<QSerialPort *> r = connectSerial(QSerialPortInfo(portName));
  if (!r.ok()) {
    qCritical() << "connectSerial:" << r.status().ToString().c_str();
    return r.status();
  }
  serial_port_.reset(r.ValueOrDie());
  connect(serial_port_.get(),
          static_cast<void (QSerialPort::*) (QSerialPort::SerialPortError)>(
              &QSerialPort::error),
          [this](QSerialPort::SerialPortError err) {
            if (err == QSerialPort::ResourceError) {
#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
              QTimer::singleShot(0, this, SLOT(closeSerial()));
#else
              QTimer::singleShot(0, this, &MainDialog::closeSerial);
#endif
            }
          });

  setState(Connected);
  return util::Status::OK;
}

util::Status MainDialog::closeSerial() {
  switch (state_) {
    case NotConnected:
      return util::Status(util::error::FAILED_PRECONDITION,
                          tr("Port is not connected").toStdString());
    case Connected:
      break;
    case Terminal:
      disconnectTerminal();
      readSerial();  // read the remainder of the buffer before closing the port
      break;
    case Flashing:
      setState(PortGoneWhileFlashing);
      return util::Status::OK;
    default:
      return util::Status(util::error::FAILED_PRECONDITION,
                          tr("Port is in use").toStdString());
  }
  setState(NotConnected);
  serial_port_->close();
  hal_.reset();
  serial_port_.reset();
  return util::Status::OK;
}

void MainDialog::connectDisconnectTerminal() {
  int speed;
  util::Status err;
  switch (state_) {
    case NoPortSelected:
      QMessageBox::critical(this, tr("Error"), tr("No port selected"));
      break;
    case NotConnected:
      err = openSerial();
      if (!err.ok()) {
        QMessageBox::critical(this, tr("Error"), err.error_message().c_str());
        return;
      }

      if (state_ != Connected) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Failed to connect to serial port."));
        return;
      }
    // fallthrough
    case Connected:
      openConsoleLogFile(false /* truncate */);
      connect(serial_port_.get(), &QIODevice::readyRead, this,
              &MainDialog::readSerial);

      speed = kDefaultConsoleBaudRate;
      if (config_->isSet("console-baud-rate")) {
        speed = config_->value("console-baud-rate").toInt();
        if (speed == 0) {
          qDebug() << "Invalid --console-baud-rate value:"
                   << config_->value("console-baud-rate");
          speed = kDefaultConsoleBaudRate;
        }
      }
      setSpeed(serial_port_.get(), speed);

      // Write a newline to get a prompt back.
      serial_port_->write(QByteArray("\r\n"));
      setState(Terminal);
      ui_.terminalInput->setFocus();
      ui_.terminal->appendPlainText(tr("--- connected"));
      ui_.terminal->appendPlainText("");  // readSerial will append stuff here.
      break;
    case Terminal:
      disconnectTerminal();
      closeSerial();
    case Downloading:
    case Flashing:
    case PortGoneWhileFlashing:
      break;
  }
}

util::Status MainDialog::disconnectTerminal() {
  if (state_ != Terminal) {
    qDebug() << "Attempt to disconnect signals in non-Terminal mode.";
    return util::Status(util::error::FAILED_PRECONDITION,
                        tr("not in terminal mode").toStdString());
  }

  disconnect(serial_port_.get(), &QIODevice::readyRead, this,
             &MainDialog::readSerial);

  setState(Connected);
  ui_.terminal->appendPlainText(tr("--- disconnected"));
  return util::Status::OK;
}

QString trimRight(QString s) {
  for (int i = s.length() - 1; i >= 0; i--) {
    if (s[i] == '\r' || s[i] == '\n') {
      s.remove(i, 1);
    } else {
      break;
    }
  }
  return s;
}

void MainDialog::readSerial() {
  if (serial_port_ == nullptr) {
    qDebug() << "readSerial called with NULL port";
    return;
  }
  QByteArray data = serial_port_->readAll();
  if (data.length() >= 2 && data.right(2) == kPromptEnd) {
    emit gotPrompt();
  }
  if (console_log_) {
    console_log_->write(data);
    console_log_->flush();
  }
  auto *scroll = ui_.terminal->verticalScrollBar();
  bool autoscroll = scroll->value() == scroll->maximum();
  // Appending a bunch of text the hard way, because
  // QPlainTextEdit::appendPlainText creates a new paragraph on each call,
  // making it look like extra newlines.
  const QStringList parts = QString(data).split('\n');
  QTextCursor cursor = QTextCursor(ui_.terminal->document());
  cursor.movePosition(QTextCursor::End);
  for (int i = 0; i < parts.length() - 1; i++) {
    cursor.insertText(trimRight(parts[i]));
    cursor.insertBlock();
  }
  cursor.insertText(trimRight(parts.last()));

  if (autoscroll) {
    scroll->setValue(scroll->maximum());
  }
}

void MainDialog::writeSerial() {
  if (serial_port_ == nullptr) {
    return;
  }
  const QString &text = ui_.terminalInput->text();
  if (text.contains("\n")) {
    serial_port_->write(":here\r\n");
    serial_port_->write(text.toUtf8());
    serial_port_->write("\r\nEOF\r\n");
  } else {
    serial_port_->write(text.toUtf8());
    serial_port_->write("\r\n");
  }
  if (!ui_.terminalInput->text().isEmpty() &&
      (input_history_.length() == 0 ||
       input_history_.last() != ui_.terminalInput->text())) {
    input_history_ << ui_.terminalInput->text();
  }
  while (input_history_.length() > kInputHistoryLength) {
    input_history_.removeAt(0);
  }
  settings_.setValue("terminal/history", input_history_);
  history_cursor_ = -1;
  ui_.terminalInput->clear();
  incomplete_input_ = "";
  // Relying on remote echo.
}

void MainDialog::reboot() {
  if (serial_port_ == nullptr) {
    qDebug() << "Attempt to reboot without an open port!";
    return;
  }
  if (hal_ == nullptr) createHAL();
  disconnectTerminal();
  util::Status st = hal_->reboot();
  connectDisconnectTerminal();
  if (!st.ok()) {
    qCritical() << "Rebooting failed:" << st.ToString().c_str();
    QMessageBox::critical(this, tr("Error"),
                          QString::fromStdString(st.ToString()));
  }
}

void MainDialog::updatePortList() {
  if (state_ != NotConnected && state_ != NoPortSelected &&
      QGuiApplication::applicationState() != Qt::ApplicationActive) {
    return;
  }

  QSet<QString> to_delete, to_add;

  for (int i = 0; i < ui_.portSelector->count(); i++) {
    if (ui_.portSelector->itemData(i).type() == QVariant::String) {
      to_delete.insert(ui_.portSelector->itemData(i).toString());
    }
  }

  auto ports = QSerialPortInfo::availablePorts();
  for (const auto &info : ports) {
#ifdef Q_OS_MAC
    if (info.portName().contains("Bluetooth")) {
      continue;
    }
#endif
    to_add.insert(info.portName());
  }

  QSet<QString> common = to_delete & to_add;
  to_delete -= common;
  to_add -= common;
  if (!to_delete.empty()) {
    qDebug() << "Removing ports:" << to_delete;
  }
  if (!to_add.empty()) {
    qDebug() << "Adding ports:" << to_add;
  }

  for (const auto &s : to_delete) {
    for (int i = 0; i < ui_.portSelector->count(); i++) {
      if (ui_.portSelector->itemData(i).type() == QVariant::String &&
          ui_.portSelector->itemData(i).toString() == s) {
        ui_.portSelector->removeItem(i);
        break;
      }
    }
  }

  for (const auto &s : to_add) {
    ui_.portSelector->addItem(s, s);
  }
}

void MainDialog::selectFirmwareFile() {
  QString curDir;
  const QString curFileName = ui_.firmwareFileName->text();
  if (curFileName != "") {
    QFileInfo curFileInfo(curFileName);
    curDir = curFileInfo.path();
  }
  QString fileName = QFileDialog::getOpenFileName(
      this, tr("Select firmware file"), curDir, tr("Firmware files (*.zip)"));
  if (fileName != "" && loadFirmwareBundle(fileName).ok()) {
    ui_.firmwareFileName->setText(fileName);
  }
}

void MainDialog::flashingDone(QString msg, bool success) {
  Q_UNUSED(msg);
  ui_.progressBar->hide();
  if (scroll_after_flashing_) {
    auto *scroll = ui_.terminal->verticalScrollBar();
    scroll->setValue(scroll->maximum());
  }
  setState(Connected);
  if (state_ == PortGoneWhileFlashing) {
    success = false;
    msg = "Port went away while flashing";
  }
  if (success) {
    msg = tr("Flashed %1 %2 %3")
              .arg(fw_->name())
              .arg(fw_->platform().toUpper())
              .arg(fw_->buildId());
    setStatusMessage(MsgType::OK, msg);
    ui_.terminal->appendPlainText(tr("--- %1").arg(msg));
    connectDisconnectTerminal();
  } else {
    setStatusMessage(MsgType::ERROR, msg);
    closeSerial();
  }
}

void MainDialog::flashClicked() {
  QString path = ui_.firmwareFileName->text();
  if (path.isEmpty()) {
    setStatusMessage(MsgType::ERROR, tr("No firmware selected"));
    return;
  }
  QString portName = ui_.portSelector->currentData().toString();
  if (portName == "") {
    setStatusMessage(MsgType::ERROR, tr("No port selected"));
    return;
  }
  if (path.startsWith("http://", Qt::CaseInsensitive) ||
      path.startsWith("https://", Qt::CaseInsensitive)) {
    downloadAndFlashFirmware(path);
  } else {
    flashFirmware(path);
  }
}

void MainDialog::setStatusMessage(MsgType level, const QString &msg) {
  emit ui_.statusMessage->setText(msg);
  switch (level) {
    case MsgType::OK:
      emit ui_.statusMessage->setStyleSheet("QLabel { color: green; }");
      qInfo() << msg.toUtf8().constData();
      break;
    case MsgType::INFO:
      if (ui_.statusMessage->styleSheet() != "") {
        emit ui_.statusMessage->setStyleSheet("");
      }
      qInfo() << msg.toUtf8().constData();
      break;
    case MsgType::ERROR:
      emit ui_.statusMessage->setStyleSheet("QLabel { color: red; }");
      qCritical() << msg.toUtf8().constData();
      break;
  }
  if (!ui_.statusMessage->isVisible()) emit ui_.statusMessage->show();
}

void MainDialog::createHAL() {
  const QString platform = ui_.platformSelector->currentText();
  if (platform == "ESP8266") {
    hal_ = ESP8266::HAL(serial_port_.get());
  } else if (platform == "CC3200") {
    hal_ = CC3200::HAL(serial_port_.get());
  } else {
    qFatal("Unknown platform: %s", platform.toStdString().c_str());
  }
}

void MainDialog::downloadAndFlashFirmware(const QString &url) {
  prevState_ = state_;
  setState(Downloading);
  setStatusMessage(MsgType::INFO, "Downloading...");
  if (fd_ == nullptr || fd_->url() != url) {
    fd_.reset(new FileDownloader(url));
    connect(fd_.get(), &FileDownloader::progress, this,
            &MainDialog::downloadProgress);
    connect(fd_.get(), &FileDownloader::finished, this,
            &MainDialog::downloadFinished);
  }
  fd_->start();
}

void MainDialog::downloadProgress(qint64 recd, qint64 total) {
  qDebug() << "downloadProgress" << recd << "of" << total;
  // Only show progress when downloading something substantial.
  // In particular, do not react to error an redirect responses.
  if (total > 5000) {
    ui_.progressBar->show();
    ui_.progressBar->setRange(0, total);
    ui_.progressBar->setValue(recd);
  }
}

void MainDialog::downloadFinished() {
  qDebug() << "downloadFinished";
  ui_.progressBar->setValue(0);
  ui_.progressBar->hide();
  setState(prevState_);
  if (fd_->status().ok()) flashFirmware(fd_->fileName());
}

void MainDialog::flashFirmware(const QString &file) {
  if (!loadFirmwareBundle(file).ok()) {
    // Error already shown by loadFirmwareBundle.
    return;
  }
  if (state_ == Terminal) disconnectTerminal();
  util::Status s = openSerial();
  if (!s.ok()) {
    setStatusMessage(MsgType::ERROR, s.ToString().c_str());
    return;
  }
  if (state_ != Connected) {
    setStatusMessage(MsgType::ERROR, tr("port is not connected"));
    return;
  }
  setState(Flashing);
  // Check if the terminal is scrolled down to the bottom before showing
  // progress bar, so we can scroll it back again after we're done.
  auto *scroll = ui_.terminal->verticalScrollBar();
  scroll_after_flashing_ = scroll->value() == scroll->maximum();

  if (hal_ == nullptr) createHAL();
  std::unique_ptr<Flasher> f(hal_->flasher(&prompter_));
  s = f->setOptionsFromConfig(*config_);
  if (!s.ok()) {
    setStatusMessage(MsgType::ERROR, tr("Invalid command line flag setting: ") +
                                         s.ToString().c_str());
    return;
  }
  s = f->setFirmware(fw_.get());
  if (!s.ok()) {
    setStatusMessage(MsgType::ERROR, s.ToString().c_str());
    return;
  }
  ui_.progressBar->show();
  ui_.progressBar->setRange(0, f->totalBytes());
  connect(f.get(), &Flasher::progress, ui_.progressBar,
          &QProgressBar::setValue);
  connect(f.get(), &Flasher::done,
          [this]() { serial_port_->moveToThread(this->thread()); });
  connect(f.get(), &Flasher::statusMessage,
          [this](QString msg, bool important) {
            setStatusMessage(MsgType::INFO, msg);
            (void) important;
          });
  connect(f.get(), &Flasher::done, this, &MainDialog::flashingDone);

  worker_.reset(new QThread);  // TODO(imax): handle already running thread?
  connect(worker_.get(), &QThread::finished, f.get(), &QObject::deleteLater);
  connect(f.get(), &Flasher::done, worker_.get(), &QThread::quit);
  f->moveToThread(worker_.get());
  serial_port_->moveToThread(worker_.get());
  worker_->start();
#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
  QTimer::singleShot(0, f.release(), SLOT(run()));
#else
  QTimer::singleShot(0, f.release(), &Flasher::run);
#endif
}

void MainDialog::showAboutBox() {
  if (aboutBox_ == nullptr) {
    aboutBox_.reset(new AboutDialog(nullptr));
    aboutBox_->show();
    connect(aboutBox_.get(), &AboutDialog::closed, this,
            &MainDialog::aboutBoxClosed);
  } else {
    aboutBox_->raise();
    aboutBox_->activateWindow();
  }
}

void MainDialog::aboutBoxClosed() {
  aboutBox_.reset();
}

void MainDialog::showLogViewer() {
  if (logViewer_ == nullptr) {
    logViewer_.reset(new LogViewer(nullptr));
    logViewer_->show();
    connect(logViewer_.get(), &LogViewer::closed, this,
            &MainDialog::logViewerClosed);
  } else {
    logViewer_->raise();
    logViewer_->activateWindow();
  }
}

void MainDialog::logViewerClosed() {
  logViewer_.reset();
}

bool MainDialog::eventFilter(QObject *obj, QEvent *e) {
  if (obj != ui_.terminalInput) {
    return QMainWindow::eventFilter(obj, e);
  }
  if (e->type() == QEvent::KeyPress) {
    QKeyEvent *key = static_cast<QKeyEvent *>(e);
    if (key->key() == Qt::Key_Up) {
      if (input_history_.length() == 0) {
        return true;
      }
      if (history_cursor_ < 0) {
        history_cursor_ = input_history_.length() - 1;
        incomplete_input_ = ui_.terminalInput->text();
      } else {
        history_cursor_ -= history_cursor_ > 0 ? 1 : 0;
      }
      ui_.terminalInput->setText(input_history_[history_cursor_]);
      return true;
    } else if (key->key() == Qt::Key_Down) {
      if (input_history_.length() == 0 || history_cursor_ < 0) {
        return true;
      }
      if (history_cursor_ < input_history_.length() - 1) {
        history_cursor_++;
        ui_.terminalInput->setText(input_history_[history_cursor_]);
      } else {
        history_cursor_ = -1;
        ui_.terminalInput->setText(incomplete_input_);
      }
      return true;
    }
  }
  return false;
}

void MainDialog::closeEvent(QCloseEvent *event) {
  settings_.setValue("window/geometry", saveGeometry());
  settings_.setValue("window/state", saveState());
  if (logViewer_ != nullptr) logViewer_->close();
  QMainWindow::closeEvent(event);
}

void MainDialog::configureWiFi() {
  QDialog dlg(this);
  QFormLayout *layout = new QFormLayout();
  QComboBox *ssid = new QComboBox;
  QLineEdit *password = new QLineEdit;
  layout->addRow(tr("SSID:"), ssid);
  layout->addRow(tr("Password:"), password);

  ssid->setEditable(true);
  ssid->setInsertPolicy(QComboBox::NoInsert);

  // net config update is async so this list might be empty
  // but usually there is enough time to receive the net list
  // from the OS and if not, blocking doesn't buy anything.
  for (const auto &net_conf :
       net_mgr_.allConfigurations(QNetworkConfiguration::Discovered)) {
    if (net_conf.bearerType() == QNetworkConfiguration::BearerWLAN) {
      ssid->addItem(net_conf.name());
    }
  }
  ssid->clearEditText();

  QPushButton *ok = new QPushButton(tr("&OK"));
  QPushButton *cancel = new QPushButton(tr("&Cancel"));
  QHBoxLayout *hlayout = new QHBoxLayout;
  hlayout->addWidget(ok);
  hlayout->addWidget(cancel);
  layout->addRow(hlayout);
  connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
  connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  ok->setDefault(true);

  // This is not the default Mac behaviour, but this makes resizing less ugly.
  layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  dlg.setWindowTitle(tr("Configure Wi-Fi"));
  dlg.setLayout(layout);
  dlg.setFixedHeight(layout->sizeHint().height());
  if (dlg.exec() == QDialog::Accepted) {
    // TODO(imax): escape strings.
    QString s = QString("Wifi.setup('%1', '%2')\r\n")
                    .arg(ssid->currentText())
                    .arg(password->text());
    serial_port_->write(s.toUtf8());
  }
}

void MainDialog::uploadFile() {
  QString name =
      QFileDialog::getOpenFileName(this, tr("Select file to upload"));
  if (name.isNull()) {
    return;
  }
  QFile f(name);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::critical(this, tr("Error"), tr("Failed to open the file."));
    return;
  }
  // TODO(imax): hide commands from the console.
  QByteArray bytes = f.readAll();
  QString basename = QFileInfo(name).fileName();
  command_queue_ << QString("var uf = File.open('%1','w')").arg(basename);
  const int batchSize = 32;
  for (int i = 0; i < bytes.length(); i += batchSize) {
    QString hex = bytes.mid(i, batchSize).toHex();
    QString cmd = "uf.write('";
    for (int j = 0; j < hex.length(); j += 2) {
      cmd.append("\\x");
      cmd.append(hex.mid(j, 2));
    }
    cmd.append("')");
    command_queue_ << cmd;
  }
  command_queue_ << "uf.close()";
  f.close();
  sendQueuedCommand();
}

void MainDialog::sendQueuedCommand() {
  if (serial_port_ == nullptr || command_queue_.length() == 0) {
    return;
  }
  QString cmd = command_queue_.takeFirst();
  serial_port_->write(cmd.toUtf8());
  serial_port_->write("\r\n");
}

void MainDialog::showSettings() {
  settingsDlg_.setModal(true);
  settingsDlg_.show();
}

util::Status MainDialog::loadFirmwareBundle(const QString &fileName) {
  auto fwbs = NewZipFWBundle(fileName);
  if (!fwbs.ok()) {
    setStatusMessage(MsgType::ERROR,
                     tr("Failed to load %1: %2")
                         .arg(fileName)
                         .arg(fwbs.status().ToString().c_str()));
    return QS(util::error::INVALID_ARGUMENT, "");
  }
  std::unique_ptr<FirmwareBundle> fwb = fwbs.MoveValueOrDie();
  if (fwb->platform().toUpper() !=
      ui_.platformSelector->currentText().toUpper()) {
    setStatusMessage(MsgType::ERROR,
                     tr("Platform mismatch: want %1, got %2")
                         .arg(ui_.platformSelector->currentText())
                         .arg(fwb->platform()));
    return QS(util::error::INVALID_ARGUMENT, "");
  }
  setStatusMessage(MsgType::INFO, tr("Loaded %1 %2 %3")
                                      .arg(fwb->name())
                                      .arg(fwb->platform().toUpper())
                                      .arg(fwb->buildId()));
  fw_ = std::move(fwb);
  settings_.setValue(
      QString("selectedFirmware_%1").arg(ui_.platformSelector->currentText()),
      ui_.firmwareFileName->text());
  return util::Status::OK;
}

void MainDialog::openConsoleLogFile(bool truncate) {
  if (truncate) console_log_.reset();
  if (config_->isSet("console-log")) {
    ui_.actionTruncate_log_file->setEnabled(true);
    if (console_log_ == nullptr ||
        console_log_->fileName() != config_->value("console-log")) {
      console_log_.reset(new QFile(config_->value("console-log")));
      if (!console_log_->open(
              QIODevice::ReadWrite |
              (truncate ? QIODevice::Truncate : QIODevice::Append))) {
        qCritical() << "Failed to open console log file:"
                    << console_log_->errorString();
        console_log_->reset();
      }
    }
  } else {
    ui_.actionTruncate_log_file->setEnabled(false);
    console_log_.reset();
  }
}

void MainDialog::truncateConsoleLog() {
  openConsoleLogFile(true /* truncate */);
}

void MainDialog::updateConfig(const QString &name) {
  if (settings_.value(SettingsDialog::isSetKey(name), false).toBool()) {
    config_->setValue(
        name, settings_.value(SettingsDialog::valueKey(name), "").toString());
  } else {
    config_->unset(name);
  }
  if (name == "verbose") {
    bool ok;
    int v;
    v = config_->value("verbose").toInt(&ok, 10);
    if (ok) {
      Log::setVerbosity(v);
    } else {
      qCritical() << "Failed to change verbosity level:"
                  << config_->value("verbose") << "is not a number";
    }
  } else if (name == "log") {
    if (config_->value("log").isEmpty()) {
      Log::setFile(&std::cerr);
    } else {
      auto *logfile = new std::ofstream(config_->value("log").toStdString(),
                                        std::ios_base::app);
      if (logfile->fail()) {
        std::cerr << "Failed to open log file." << std::endl;
        return;
      }
      *logfile
          << "\n---------- Log started on "
          << QDateTime::currentDateTime().toString(Qt::ISODate).toStdString()
          << std::endl;
      Log::setFile(logfile);
    }
  } else if (name == "console-line-count") {
    bool ok = false;
    int n = config_->value("console-line-count").toInt(&ok);
    if (!ok) {
      qInfo() << "Invalid value for --console-line-count:"
              << config_->value("console-line-count");
      n = 4096;
    }
    ui_.terminal->setMaximumBlockCount(n);
  }
}
