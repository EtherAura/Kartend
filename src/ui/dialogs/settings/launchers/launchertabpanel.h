#ifndef LAUNCHERTABPANEL_H
#define LAUNCHERTABPANEL_H

#include "collection/generalsettings.h"
#include "isettingspanel.h"
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

struct SettingsModel;

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
class LauncherTabPanel : public QWidget, public ISettingsPanel {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(LauncherTabPanel)
public:
  explicit LauncherTabPanel(QWidget *parent = nullptr);
  ~LauncherTabPanel() override;

  void setModel(SettingsModel *model);

  /// Hydrate launcher path / core / parameters / name + extract flag +
  /// extracted extension from the model's current working collection. The
  /// additional-launchers list + default combo are populated separately by
  /// the host dialog.
  void load() override;
  void clear() override;
  /// Persist launcher path / core / parameters / name + extract flag +
  /// extracted extension into the model's current working collection.
  /// Additional launchers + default index are written by the host dialog.
  void save() override;

  /// Toggles visibility of the launch-extension field (only meaningful
  /// when extract-archives is on). Idempotent.
  void updateExtractedExtensionVisibility();

  /// Fill the Core combo with libretro cores discovered in the
  /// RetroArch install (honouring the General-settings override).
  void populateCoreCombo();

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
  SettingsModel *m_model = nullptr;
};

#endif // LAUNCHERTABPANEL_H
