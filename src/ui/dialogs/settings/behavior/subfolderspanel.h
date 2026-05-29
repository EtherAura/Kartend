#ifndef SUBFOLDERSPANEL_H
#define SUBFOLDERSPANEL_H

#include "collectiontypes.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SubfoldersPanel;
}
QT_END_NAMESPACE

struct SettingsModel;

/// Standalone panel widget for the per-collection "Subfolders" tab in
/// SettingsDialog. Owns the content-subfolder include + show-all + hide-
/// titles + show-hidden checkboxes plus the artwork-subfolder include
/// checkbox. Per-collection load/save shape matching SidebarPanel; the
/// internal subfolderOptionsWidget visibility tracks the include-content
/// toggle so the dependent options collapse when content-subfolders are
/// off.
class SubfoldersPanel : public QWidget {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(SubfoldersPanel)
public:
  explicit SubfoldersPanel(QWidget *parent = nullptr);
  ~SubfoldersPanel() override;

  void setModel(SettingsModel *model);
  void load();
  void clear();
  void save() const;
  [[nodiscard]] bool hasChanges() const;

  /// Public accessor used by the host dialog's pre-save rescan-detection
  /// logic — toggling content-subfolders is one of the database-affecting
  /// fields that triggers a rescan on Save.
  [[nodiscard]] bool isContentSubfoldersIncluded() const;

signals:
  void changed();

private:
  void updateOptionsVisibility();

  Ui::SubfoldersPanel *ui;
  SettingsModel *m_model = nullptr;
};

#endif // SUBFOLDERSPANEL_H
