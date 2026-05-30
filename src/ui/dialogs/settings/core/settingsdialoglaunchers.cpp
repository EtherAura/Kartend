// Multi-launcher controls for SettingsDialog.
// Sibling translation unit hosting the load/save/edit logic for the
// Additional Launchers list and Default Launcher combo. Kept separate from
// settingsdialogform.cpp so the launcher feature lives in one place.
#include "launchereditordialog.h"
#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>

void SettingsDialog::loadAdditionalLaunchersToUI(const CollectionConfig &config) {
  m_workingAdditionalLaunchers = config.launcher.additionalLaunchers;

  if (ui->launcherPanel->additionalLaunchersList()) {
    QSignalBlocker blocker(ui->launcherPanel->additionalLaunchersList());
    ui->launcherPanel->additionalLaunchersList()->clear();
    for (const LauncherConfig &launcher : std::as_const(m_workingAdditionalLaunchers)) {
      const QString display = launcher.name.trimmed().isEmpty() ? launcher.launcherPath.trimmed()
                                                                : launcher.name.trimmed();
      ui->launcherPanel->additionalLaunchersList()->addItem(display.isEmpty() ? tr("(unnamed)")
                                                                              : display);
    }
  }

  rebuildDefaultLauncherCombo(config.launcher.defaultLauncherIndex);
  updateAdditionalLauncherButtonsState();
}

void SettingsDialog::clearAdditionalLaunchersUI() {
  m_workingAdditionalLaunchers.clear();
  if (ui->launcherPanel->additionalLaunchersList()) {
    QSignalBlocker blocker(ui->launcherPanel->additionalLaunchersList());
    ui->launcherPanel->additionalLaunchersList()->clear();
  }
  if (ui->launcherPanel->defaultLauncherComboBox()) {
    QSignalBlocker blocker(ui->launcherPanel->defaultLauncherComboBox());
    ui->launcherPanel->defaultLauncherComboBox()->clear();
  }
  updateAdditionalLauncherButtonsState();
}

void SettingsDialog::rebuildDefaultLauncherCombo(int preferredIndex) {
  if (!ui->launcherPanel->defaultLauncherComboBox()) {
    return;
  }
  QSignalBlocker blocker(ui->launcherPanel->defaultLauncherComboBox());
  ui->launcherPanel->defaultLauncherComboBox()->clear();

  // Build a synthetic launcher list reflecting the live UI state so the combo
  // names stay in sync as the user edits the primary fields and the
  // additional list. We don't read from m_workingCollections because the user
  // may have unsaved name/path edits in the line edits.
  const QString primaryName = ui->launcherPanel->launcherNameLineEdit()
                                  ? ui->launcherPanel->launcherNameLineEdit()->text().trimmed()
                                  : QString();
  const QString primaryPath = ui->launcherPanel->launcherLineEdit()
                                  ? ui->launcherPanel->launcherLineEdit()->text().trimmed()
                                  : QString();
  auto basenameOf = [](const QString &path) {
    int slash = path.lastIndexOf(QLatin1Char('/'));
    return (slash >= 0) ? path.mid(slash + 1) : path;
  };
  QString primaryLabel = !primaryName.isEmpty()
                             ? primaryName
                             : (!primaryPath.isEmpty() ? basenameOf(primaryPath) : tr("Primary"));
  ui->launcherPanel->defaultLauncherComboBox()->addItem(tr("1: %1").arg(primaryLabel));
  for (int i = 0; i < m_workingAdditionalLaunchers.size(); ++i) {
    const LauncherConfig &l = m_workingAdditionalLaunchers[i];
    QString label = !l.name.trimmed().isEmpty()           ? l.name.trimmed()
                    : !l.launcherPath.trimmed().isEmpty() ? basenameOf(l.launcherPath.trimmed())
                                                          : tr("Launcher %1").arg(i + 2);
    ui->launcherPanel->defaultLauncherComboBox()->addItem(tr("%1: %2").arg(i + 2).arg(label));
  }
  const int clamped =
      std::clamp(preferredIndex, 0, ui->launcherPanel->defaultLauncherComboBox()->count() - 1);
  ui->launcherPanel->defaultLauncherComboBox()->setCurrentIndex(clamped);
}

void SettingsDialog::updateAdditionalLauncherButtonsState() {
  const bool hasSelection = ui->launcherPanel->additionalLaunchersList() &&
                            ui->launcherPanel->additionalLaunchersList()->currentRow() >= 0;
  if (ui->launcherPanel->editAdditionalLauncherButton()) {
    ui->launcherPanel->editAdditionalLauncherButton()->setEnabled(hasSelection);
  }
  if (ui->launcherPanel->removeAdditionalLauncherButton()) {
    ui->launcherPanel->removeAdditionalLauncherButton()->setEnabled(hasSelection);
  }
}

void SettingsDialog::onAddAdditionalLauncher() {
  LauncherEditorDialog dialog(this, LauncherConfig{}, tr("Add Launcher"),
                              m_generalSettings.launchers.launcherPresets,
                              m_generalSettings.launchers.retroarchConfigPath);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  m_workingAdditionalLaunchers.append(dialog.launcher());
  // Preserve the existing default-launcher pick when extending the list — the
  // newly added entry should not silently take over.
  const int currentDefault = ui->launcherPanel->defaultLauncherComboBox()
                                 ? ui->launcherPanel->defaultLauncherComboBox()->currentIndex()
                                 : 0;
  CollectionConfig snapshot;
  snapshot.launcher.additionalLaunchers = m_workingAdditionalLaunchers;
  snapshot.launcher.defaultLauncherIndex = currentDefault;
  loadAdditionalLaunchersToUI(snapshot);
  if (ui->launcherPanel->additionalLaunchersList()) {
    ui->launcherPanel->additionalLaunchersList()->setCurrentRow(
        m_workingAdditionalLaunchers.size() - 1);
  }
  checkForChanges();
}

void SettingsDialog::onEditAdditionalLauncher() {
  if (!ui->launcherPanel->additionalLaunchersList()) {
    return;
  }
  const int row = ui->launcherPanel->additionalLaunchersList()->currentRow();
  if (row < 0 || row >= m_workingAdditionalLaunchers.size()) {
    return;
  }
  LauncherEditorDialog dialog(this, m_workingAdditionalLaunchers[row], tr("Edit Launcher"),
                              m_generalSettings.launchers.launcherPresets,
                              m_generalSettings.launchers.retroarchConfigPath);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  m_workingAdditionalLaunchers[row] = dialog.launcher();
  const int currentDefault = ui->launcherPanel->defaultLauncherComboBox()
                                 ? ui->launcherPanel->defaultLauncherComboBox()->currentIndex()
                                 : 0;
  CollectionConfig snapshot;
  snapshot.launcher.additionalLaunchers = m_workingAdditionalLaunchers;
  snapshot.launcher.defaultLauncherIndex = currentDefault;
  loadAdditionalLaunchersToUI(snapshot);
  ui->launcherPanel->additionalLaunchersList()->setCurrentRow(row);
  checkForChanges();
}

void SettingsDialog::onRemoveAdditionalLauncher() {
  if (!ui->launcherPanel->additionalLaunchersList()) {
    return;
  }
  const int row = ui->launcherPanel->additionalLaunchersList()->currentRow();
  if (row < 0 || row >= m_workingAdditionalLaunchers.size()) {
    return;
  }
  m_workingAdditionalLaunchers.removeAt(row);
  // The unified launcher index for the removed row is `row + 1`. If the
  // current default points at it, fall back to the primary; if the default
  // points past it, shift up by one to keep referring to the same launcher.
  int currentDefault = ui->launcherPanel->defaultLauncherComboBox()
                           ? ui->launcherPanel->defaultLauncherComboBox()->currentIndex()
                           : 0;
  const int removedUnified = row + 1;
  if (currentDefault == removedUnified) {
    currentDefault = 0;
  } else if (currentDefault > removedUnified) {
    currentDefault -= 1;
  }
  CollectionConfig snapshot;
  snapshot.launcher.additionalLaunchers = m_workingAdditionalLaunchers;
  snapshot.launcher.defaultLauncherIndex = currentDefault;
  loadAdditionalLaunchersToUI(snapshot);
  if (!m_workingAdditionalLaunchers.isEmpty()) {
    const int lastRow = static_cast<int>(m_workingAdditionalLaunchers.size()) - 1;
    ui->launcherPanel->additionalLaunchersList()->setCurrentRow(std::min(row, lastRow));
  }
  checkForChanges();
}

void SettingsDialog::onAdditionalLauncherSelectionChanged() {
  updateAdditionalLauncherButtonsState();
}
