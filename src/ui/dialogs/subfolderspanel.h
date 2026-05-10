#ifndef SUBFOLDERSPANEL_H
#define SUBFOLDERSPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class SubfoldersPanel;
}
QT_END_NAMESPACE

/// Standalone panel widget for the per-collection "Subfolders" tab in
/// SettingsDialog. Owns the content-subfolder include + show-all + hide-
/// titles + show-hidden checkboxes plus the artwork-subfolder include
/// checkbox. Per-collection load/save shape matching SidebarPanel; the
/// internal subfolderOptionsWidget visibility tracks the include-content
/// toggle so the dependent options collapse when content-subfolders are
/// off.
class SubfoldersPanel : public QWidget {
  Q_OBJECT
public:
  explicit SubfoldersPanel(QWidget *parent = nullptr);
  ~SubfoldersPanel() override;

  void load(const CollectionConfig &config);
  void clear();
  void save(CollectionConfig &config) const;
  [[nodiscard]] bool hasChanges(const CollectionConfig &original) const;

  /// Public accessor used by the host dialog's pre-save rescan-detection
  /// logic — toggling content-subfolders is one of the database-affecting
  /// fields that triggers a rescan on Save.
  [[nodiscard]] bool isContentSubfoldersIncluded() const;

signals:
  void changed();

private:
  void updateOptionsVisibility();

  Ui::SubfoldersPanel *ui;
};

#endif // SUBFOLDERSPANEL_H
