/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FNC_SRC_PROGRESS_WIDGET_PROGRESS_WIDGET_PLUGIN_H_
#define CS_FNC_SRC_PROGRESS_WIDGET_PROGRESS_WIDGET_PLUGIN_H_

#include <QtUiPlugin/QDesignerCustomWidgetInterface>

class ProgressWidgetPlugin : public QObject,
                             public QDesignerCustomWidgetInterface {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QDesignerCustomWidgetInterface" FILE
                        "progress_widget.json")
  Q_INTERFACES(QDesignerCustomWidgetInterface)
 public:
  explicit ProgressWidgetPlugin(QObject *parent = 0);

  bool isContainer() const override;
  bool isInitialized() const override;
  QIcon icon() const override;
  QString domXml() const override;
  QString group() const override;
  QString includeFile() const override;
  QString name() const override;
  QString toolTip() const override;
  QString whatsThis() const override;
  QWidget *createWidget(QWidget *parent) override;
  void initialize(QDesignerFormEditorInterface *core) override;

 private:
  bool initialized_ = false;
};

#endif /* CS_FNC_SRC_PROGRESS_WIDGET_PROGRESS_WIDGET_PLUGIN_H_ */
