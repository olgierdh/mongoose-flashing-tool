#include "progress_widget_plugin.h"
#include "progress_widget.h"

#include <QtPlugin>

ProgressWidgetPlugin::ProgressWidgetPlugin(QObject *parent) : QObject(parent) {
}

void ProgressWidgetPlugin::initialize(QDesignerFormEditorInterface *core) {
  if (initialized_) return;
  initialized_ = true;
  (void) core;
}

bool ProgressWidgetPlugin::isInitialized() const {
  return initialized_;
}

QWidget *ProgressWidgetPlugin::createWidget(QWidget *parent) {
  return new ProgressWidget(parent);
}

QString ProgressWidgetPlugin::name() const {
  return "ProgressWidget";
}

QString ProgressWidgetPlugin::group() const {
  return "Custom Widgets";
}

QIcon ProgressWidgetPlugin::icon() const {
  return QIcon(":/progress_widget.png");
}

QString ProgressWidgetPlugin::toolTip() const {
  return "";
}

QString ProgressWidgetPlugin::whatsThis() const {
  return "";
}

bool ProgressWidgetPlugin::isContainer() const {
  return false;
}

QString ProgressWidgetPlugin::domXml() const {
  return R"(
<ui language="c++">
<widget class="ProgressWidget" name="progressWidget">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>180</width>
    <height>180</height>
   </rect>
  </property>
  <property name="toolTip">
   <string>Fancy progress widget</string>
  </property>
  <property name="whatsThis">
   <string>Circular progress widget.</string>
  </property>
 </widget>
</ui>)";
}

QString ProgressWidgetPlugin::includeFile() const {
  return "progress_widget.h";
}
