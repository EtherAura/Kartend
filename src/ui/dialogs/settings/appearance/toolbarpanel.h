#ifndef TOOLBARPANEL_H
#define TOOLBARPANEL_H

#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ToolbarPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the global "Toolbar Buttons" sub-tab. Owns
/// the 9 visibility checkboxes and 6 text-override edits for the items-page
/// toolbar widgets. Observes SettingsModel* and emits changed() on every
/// mutation; deferred-save semantics — host wires changed→checkForChanges.
class ToolbarPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(ToolbarPanel)
public:
  explicit ToolbarPanel(QWidget *parent = nullptr);
  ~ToolbarPanel() override;

  void setModel(SettingsModel *model);
  // ISettingsPanel (Kartend-ny2ki). load() was refresh(); save() was the
  // private writeBack(); clear() is a no-op — this global panel is always
  // backed by the single live GeneralSettings model.
  void load() override;
  void save() override;
  void clear() override {}

signals:
  void changed();

private:
  Ui::ToolbarPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // TOOLBARPANEL_H
