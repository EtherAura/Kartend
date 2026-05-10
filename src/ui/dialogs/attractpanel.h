#ifndef ATTRACTPANEL_H
#define ATTRACTPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AttractPanel;
}
QT_END_NAMESPACE

struct GeneralSettings;

/// Standalone panel widget for the global "Attract Mode" sub-tab in
/// SettingsDialog. Owns the seven attract-mode fields (enable / idle timeout
/// / auto-scroll / scroll speed / advance-selection / advance interval /
/// random order). Observes a GeneralSettings* installed by the host dialog.
/// Deferred-save semantics — host wires changed() to checkForChanges so the
/// Save button reflects unsaved edits; persistence happens on Save like the
/// rest of the per-collection deferred fields.
class AttractPanel : public QWidget {
  Q_OBJECT
public:
  explicit AttractPanel(QWidget *parent = nullptr);
  ~AttractPanel() override;

  void setSettings(GeneralSettings *settings);
  void refresh();

signals:
  void changed();

private:
  void writeBack();

  Ui::AttractPanel *ui;
  GeneralSettings *m_settings = nullptr;
};

#endif // ATTRACTPANEL_H
