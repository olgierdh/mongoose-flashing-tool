/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_MFT_SRC_DIALOG_H_
#define CS_MFT_SRC_DIALOG_H_

#include <memory>

#include <QDir>
#include <QFile>
#include <QList>
#include <QMainWindow>
#include <QMessageBox>
#include <QMultiMap>
#include <QNetworkConfigurationManager>
#include <QPair>
#include <QSerialPort>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QThread>

#include <common/util/statusor.h>

#include "file_downloader.h"
#include "fw_bundle.h"
#include "gui_prompter.h"
#include "hal.h"
#include "log_viewer.h"
#include "prompter.h"
#include "settings.h"
#include "ui_main.h"

class Config;
class QAction;
class QEvent;
class QSerialPort;

class Ui_MainWindow;

class MainDialog : public QMainWindow {
  Q_OBJECT

 public:
  static void addOptions(Config *config);
  MainDialog(Config *config, QWidget *parent = 0);

 protected:
  bool eventFilter(QObject *, QEvent *);  // used for input history navigation
  void closeEvent(QCloseEvent *event);

 private:
  enum State {
    NoPortSelected,
    NotConnected,
    Connected,
    Downloading,
    Flashing,
    PortGoneWhileFlashing,
    Terminal,
  };

 public slots:
  void flashClicked();
  void connectDisconnectTerminal();

 private slots:
  void updatePortList();
  void selectFirmwareFile();
  void flashingDone(QString msg, bool success);
  util::Status disconnectTerminal();
  void readSerial();
  void writeSerial();
  void reboot();
  void configureWiFi();
  void uploadFile();
  void platformChanged();

  util::Status openSerial();
  util::Status closeSerial();
  void sendQueuedCommand();

  void setState(State);
  void enableControlsForCurrentState();
  void showLogViewer();
  void showAboutBox();

  void logViewerClosed();

  void showPrompt(QString text,
                  QList<QPair<QString, Prompter::ButtonRole>> buttons);

  void showSettings();
  void updateConfig(const QString &name);
  void truncateConsoleLog();

  void downloadProgress(qint64 recd, qint64 total);
  void downloadFinished();

signals:
  void gotPrompt();
  void showPromptResult(int clicked_button);
#if (QT_VERSION < QT_VERSION_CHECK(5, 4, 0))
  void updatePlatformSelector(int index);
#endif

 private:
  enum class MsgType { OK, INFO, ERROR };
  void setStatusMessage(MsgType level, const QString &msg);

  void createHAL();
  void downloadAndFlashFirmware(const QString &url);
  void flashFirmware(const QString &file);
  util::Status loadFirmwareBundle(const QString &fileName);
  void openConsoleLogFile(bool truncate);

  Config *config_ = nullptr;
  bool skip_detect_warning_ = false;
  std::unique_ptr<QThread> worker_;
  std::unique_ptr<FirmwareBundle> fw_;
  std::unique_ptr<QSerialPort> serial_port_;
  QMultiMap<QWidget *, State> enabled_in_state_;
  QMultiMap<QAction *, State> action_enabled_in_state_;
  QTimer *refresh_timer_;
  QStringList input_history_;
  QString incomplete_input_;
  int history_cursor_ = -1;
  QSettings settings_;
  QStringList command_queue_;
  std::unique_ptr<HAL> hal_;
  bool scroll_after_flashing_ = false;
  std::unique_ptr<QFile> console_log_;
  GUIPrompter prompter_;
  SettingsDialog settingsDlg_;
  std::unique_ptr<LogViewer> log_viewer_;

  QNetworkConfigurationManager net_mgr_;

  std::unique_ptr<FileDownloader> fd_;
  State prevState_;

  State state_ = NoPortSelected;

  Ui::MainWindow ui_;
};

#endif /* CS_MFT_SRC_DIALOG_H_ */
