#ifndef SIDEBARPANEL_H
#define SIDEBARPANEL_H

#include "collectiontypes.h"
#include "isettingspanel.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SidebarPanel;
}
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the per-collection "Details Pane" tab in
/// SettingsDialog. Owns every widget that used to live under sidebarTab —
/// layout / background / typography / bubbles, plus the scrollbar checkboxes
/// physically grouped with the layout fields. Internalizes the color/font/file
/// picker dialogs and the position-driven width-vs-height visibility toggle.
class SidebarPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SidebarPanel)
public:
  explicit SidebarPanel(QWidget *parent = nullptr);
  ~SidebarPanel() override;

  void setModel(SettingsModel *model);

  /// Hydrate every field from the model's current working collection and
  /// refresh the active-collection label.
  void load() override;

  /// Reset to defaults and clear the active-collection label. Used when no
  /// collection is selected.
  void clear() override;

  /// Persist user-edited values back into the model's current working
  /// collection.
  void save() override;

  /// True when any field on the live UI differs from the corresponding field
  /// on the model's original-collection snapshot.
  [[nodiscard]] bool hasChanges() const;

signals:
  /// Emitted on any internal field change. Also fires during load() because
  /// programmatic setters trigger Qt's *Changed signals; the host dialog
  /// guards via its own is-loading flag.
  void changed();

private slots:
  void onPositionChanged();
  void onBackgroundTypeChanged(int index);
  void onBackgroundPick();
  void onFontPick();

private:
  void wireColorPicker(QPushButton *button, QLineEdit *edit, const QString &title);
  void wireAlphaColorPicker(QPushButton *button, QLineEdit *edit, const QString &title);
  void connectChangeSignals();

  Ui::SidebarPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // SIDEBARPANEL_H
