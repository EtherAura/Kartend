#ifndef ATTRACTPANEL_H
#define ATTRACTPANEL_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class AttractPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the global "Attract Mode" sub-tab in
/// SettingsDialog. Owns the seven attract-mode fields (enable / idle timeout
/// / auto-scroll / scroll speed / advance-selection / advance interval /
/// random order). Observes a SettingsModel* installed by the host dialog.
/// Deferred-save semantics — host wires changed() to checkForChanges so the
/// Save button reflects unsaved edits; persistence happens on Save like the
/// rest of the per-collection deferred fields.
class AttractPanel : public QWidget {
  Q_OBJECT
public:
  explicit AttractPanel(QWidget *parent = nullptr);
  ~AttractPanel() override;

  void setModel(SettingsModel *model);
  void refresh();

signals:
  void changed();

private:
  void writeBack();

  Ui::AttractPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // ATTRACTPANEL_H
