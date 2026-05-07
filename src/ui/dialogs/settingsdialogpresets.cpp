// Launcher-presets controls for SettingsDialog (Kartend-p1jd).
// Sibling translation unit for the global "Launchers" tab. Preset edits
// mutate m_generalSettings.launcherPresets in place; persistence happens
// via the standard saveGeneralSettings() flow when the user clicks Save.
#include "launchereditordialog.h"
#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUuid>

void SettingsDialog::loadLauncherPresetsToUI() {
  refreshLauncherPresetsList();
}

void SettingsDialog::refreshLauncherPresetsList() {
  if (!ui->launcherPresetsList) {
    return;
  }
  const int previousRow = ui->launcherPresetsList->currentRow();
  QSignalBlocker blocker(ui->launcherPresetsList);
  ui->launcherPresetsList->clear();
  for (const LauncherPreset &preset : std::as_const(m_generalSettings.launcherPresets)) {
    const QString label = preset.name.trimmed().isEmpty() ? tr("(unnamed preset)") : preset.name;
    ui->launcherPresetsList->addItem(label);
  }
  if (previousRow >= 0 && previousRow < ui->launcherPresetsList->count()) {
    ui->launcherPresetsList->setCurrentRow(previousRow);
  }
  updateLauncherPresetButtonsState();
}

void SettingsDialog::updateLauncherPresetButtonsState() {
  const bool hasSelection = ui->launcherPresetsList && ui->launcherPresetsList->currentRow() >= 0;
  if (ui->editLauncherPresetButton) {
    ui->editLauncherPresetButton->setEnabled(hasSelection);
  }
  if (ui->removeLauncherPresetButton) {
    ui->removeLauncherPresetButton->setEnabled(hasSelection);
  }
}

void SettingsDialog::onAddLauncherPreset() {
  // Presets can't reference other presets, so the editor opens in inline-only
  // mode (empty availablePresets). The dialog's preset combo is hidden.
  LauncherEditorDialog dialog(this, LauncherConfig{}, tr("Add Launcher Preset"));
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const LauncherConfig edited = dialog.launcher();
  LauncherPreset preset;
  // Stable id for preset references — using a UUID means rename never breaks
  // a collection's launcher entry that points at this preset.
  preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  preset.name = edited.name;
  preset.launcherPath = edited.launcherPath;
  preset.corePath = edited.corePath;
  preset.launchParameters = edited.launchParameters;
  m_generalSettings.launcherPresets.append(preset);
  refreshLauncherPresetsList();
  if (ui->launcherPresetsList) {
    ui->launcherPresetsList->setCurrentRow(m_generalSettings.launcherPresets.size() - 1);
  }
  checkForChanges();
}

void SettingsDialog::onEditLauncherPreset() {
  if (!ui->launcherPresetsList) {
    return;
  }
  const int row = ui->launcherPresetsList->currentRow();
  if (row < 0 || row >= m_generalSettings.launcherPresets.size()) {
    return;
  }
  const LauncherPreset &existing = m_generalSettings.launcherPresets[row];
  // Repackage the preset as a LauncherConfig so the same editor dialog can
  // reuse the same form. The presetId on the temporary config is left empty
  // so the dialog stays in inline-edit mode.
  LauncherConfig seed;
  seed.name = existing.name;
  seed.launcherPath = existing.launcherPath;
  seed.corePath = existing.corePath;
  seed.launchParameters = existing.launchParameters;
  LauncherEditorDialog dialog(this, seed, tr("Edit Launcher Preset"));
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const LauncherConfig edited = dialog.launcher();
  // Mutate in place so the stable preset id survives renames.
  m_generalSettings.launcherPresets[row].name = edited.name;
  m_generalSettings.launcherPresets[row].launcherPath = edited.launcherPath;
  m_generalSettings.launcherPresets[row].corePath = edited.corePath;
  m_generalSettings.launcherPresets[row].launchParameters = edited.launchParameters;
  refreshLauncherPresetsList();
  ui->launcherPresetsList->setCurrentRow(row);
  checkForChanges();
}

void SettingsDialog::onRemoveLauncherPreset() {
  if (!ui->launcherPresetsList) {
    return;
  }
  const int row = ui->launcherPresetsList->currentRow();
  if (row < 0 || row >= m_generalSettings.launcherPresets.size()) {
    return;
  }
  // Removing a preset doesn't actively scrub references on collections —
  // LauncherUtils::resolvePreset already falls back to the inline fields
  // when no matching preset is found. The user sees the affected entries
  // launch with their previously inherited inline data (typically empty,
  // surfaced as a clear "no launcher configured" error at launch time).
  m_generalSettings.launcherPresets.removeAt(row);
  refreshLauncherPresetsList();
  if (!m_generalSettings.launcherPresets.isEmpty()) {
    const int lastRow = static_cast<int>(m_generalSettings.launcherPresets.size()) - 1;
    ui->launcherPresetsList->setCurrentRow(std::min(row, lastRow));
  }
  checkForChanges();
}

void SettingsDialog::onLauncherPresetSelectionChanged() {
  updateLauncherPresetButtonsState();
}
