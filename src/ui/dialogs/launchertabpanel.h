#ifndef LAUNCHERTABPANEL_H
#define LAUNCHERTABPANEL_H

#include "collectionutils.h"
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class LauncherTabPanel;
}
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
QT_END_NAMESPACE

/// Standalone panel widget for the per-collection "Launcher" tab in
/// SettingsDialog. Owns the executable group (path + core + parameters +
/// display name), the archive-handling group (extract toggle + extension),
/// and the multiple-launchers group (list + add/edit/remove buttons +
/// default-launcher combo).
///
/// Hybrid load/save: panel handles the simple data fields directly; the
/// host dialog continues to manage the additional-launchers list state
/// (m_workingAdditionalLaunchers) and the default-launcher combo
/// repopulation through accessor methods, since both touch dialog state
/// (the working launcher list + the launcher-presets list on
/// GeneralSettings).
class LauncherTabPanel : public QWidget {
  Q_OBJECT
public:
  explicit LauncherTabPanel(QWidget *parent = nullptr);
  ~LauncherTabPanel() override;

  /// Hydrate launcher path / core / parameters / name + extract flag +
  /// extracted extension from @p config. The additional-launchers list +
  /// default combo are populated separately by the host dialog.
  void load(const CollectionConfig &config);
  void clear();
  /// Persist launcher path / core / parameters / name + extract flag +
  /// extracted extension into @p config. Additional launchers + default
  /// index are written by the host dialog.
  void save(CollectionConfig &config) const;
  [[nodiscard]] bool hasChanges(const CollectionConfig &original) const;

  /// Toggles visibility of the launch-extension field (only meaningful
  /// when extract-archives is on). Idempotent.
  void updateExtractedExtensionVisibility();

  // Cross-cutting widget accessors:
  [[nodiscard]] QLineEdit *launcherLineEdit() const;
  [[nodiscard]] QLineEdit *coreLineEdit() const;
  [[nodiscard]] QLineEdit *launchParamsLineEdit() const;
  [[nodiscard]] QLineEdit *launcherNameLineEdit() const;
  [[nodiscard]] QPushButton *browseLauncherButton() const;
  [[nodiscard]] QPushButton *browseCoreButton() const;
  [[nodiscard]] QCheckBox *extractArchivesCheckBox() const;
  [[nodiscard]] QLineEdit *extractedExtensionLineEdit() const;
  [[nodiscard]] QGroupBox *launcherArchiveGroupBox() const;
  [[nodiscard]] QListWidget *additionalLaunchersList() const;
  [[nodiscard]] QPushButton *addAdditionalLauncherButton() const;
  [[nodiscard]] QPushButton *editAdditionalLauncherButton() const;
  [[nodiscard]] QPushButton *removeAdditionalLauncherButton() const;
  [[nodiscard]] QComboBox *defaultLauncherComboBox() const;
  [[nodiscard]] QLabel *labelCore() const;
  [[nodiscard]] QLabel *labelExtractedExtension() const;

signals:
  void changed();

private:
  Ui::LauncherTabPanel *ui;
};

#endif // LAUNCHERTABPANEL_H
