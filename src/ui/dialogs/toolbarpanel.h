#ifndef TOOLBARPANEL_H
#define TOOLBARPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class ToolbarPanel;
}
QT_END_NAMESPACE

struct GeneralSettings;

/// Standalone panel widget for the global "Toolbar Buttons" sub-tab. Owns
/// the 9 visibility checkboxes and 6 text-override edits for the items-page
/// toolbar widgets. Observes GeneralSettings* and emits changed() on every
/// mutation; deferred-save semantics — host wires changed→checkForChanges.
class ToolbarPanel : public QWidget {
  Q_OBJECT
public:
  explicit ToolbarPanel(QWidget *parent = nullptr);
  ~ToolbarPanel() override;

  void setSettings(GeneralSettings *settings);
  void refresh();

signals:
  void changed();

private:
  void writeBack();

  Ui::ToolbarPanel *ui;
  GeneralSettings *m_settings = nullptr;
};

#endif // TOOLBARPANEL_H
