#include "launcherpresetspanel.h"

#include "launchereditordialog.h"
#include "ui_launcherpresetspanel.h"

#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUuid>

LauncherPresetsPanel::LauncherPresetsPanel(QWidget *parent)
    : QWidget(parent), ui(new Ui::LauncherPresetsPanel) {
  ui->setupUi(this);

  connect(ui->addLauncherPresetButton, &QPushButton::clicked, this, &LauncherPresetsPanel::onAdd);
  connect(ui->editLauncherPresetButton, &QPushButton::clicked, this, &LauncherPresetsPanel::onEdit);
  connect(ui->removeLauncherPresetButton, &QPushButton::clicked, this,
          &LauncherPresetsPanel::onRemove);
  connect(ui->launcherPresetsList, &QListWidget::currentRowChanged, this,
          [this](int) { onSelectionChanged(); });
  connect(ui->launcherPresetsList, &QListWidget::itemDoubleClicked, this,
          [this](QListWidgetItem *) { onEdit(); });

  updateButtonsState();
}

LauncherPresetsPanel::~LauncherPresetsPanel() {
  delete ui;
}

void LauncherPresetsPanel::setPresets(QList<LauncherPreset> *presets) {
  m_presets = presets;
  refresh();
}

void LauncherPresetsPanel::refresh() {
  const int previousRow = ui->launcherPresetsList->currentRow();
  QSignalBlocker blocker(ui->launcherPresetsList);
  ui->launcherPresetsList->clear();
  if (m_presets) {
    for (const LauncherPreset &preset : std::as_const(*m_presets)) {
      const QString label =
          preset.name.trimmed().isEmpty() ? tr("(unnamed preset)") : preset.name;
      ui->launcherPresetsList->addItem(label);
    }
  }
  if (previousRow >= 0 && previousRow < ui->launcherPresetsList->count()) {
    ui->launcherPresetsList->setCurrentRow(previousRow);
  }
  updateButtonsState();
}

void LauncherPresetsPanel::updateButtonsState() {
  const bool hasSelection = ui->launcherPresetsList->currentRow() >= 0;
  ui->editLauncherPresetButton->setEnabled(hasSelection);
  ui->removeLauncherPresetButton->setEnabled(hasSelection);
}

void LauncherPresetsPanel::onAdd() {
  if (!m_presets) {
    return;
  }
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
  m_presets->append(preset);
  refresh();
  ui->launcherPresetsList->setCurrentRow(m_presets->size() - 1);
  emit presetsChanged();
}

void LauncherPresetsPanel::onEdit() {
  if (!m_presets) {
    return;
  }
  const int row = ui->launcherPresetsList->currentRow();
  if (row < 0 || row >= m_presets->size()) {
    return;
  }
  const LauncherPreset &existing = (*m_presets)[row];
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
  (*m_presets)[row].name = edited.name;
  (*m_presets)[row].launcherPath = edited.launcherPath;
  (*m_presets)[row].corePath = edited.corePath;
  (*m_presets)[row].launchParameters = edited.launchParameters;
  refresh();
  ui->launcherPresetsList->setCurrentRow(row);
  emit presetsChanged();
}

void LauncherPresetsPanel::onRemove() {
  if (!m_presets) {
    return;
  }
  const int row = ui->launcherPresetsList->currentRow();
  if (row < 0 || row >= m_presets->size()) {
    return;
  }
  // Removing a preset doesn't actively scrub references on collections —
  // LauncherUtils::resolvePreset already falls back to the inline fields
  // when no matching preset is found.
  m_presets->removeAt(row);
  refresh();
  if (!m_presets->isEmpty()) {
    const int lastRow = static_cast<int>(m_presets->size()) - 1;
    ui->launcherPresetsList->setCurrentRow(std::min(row, lastRow));
  }
  emit presetsChanged();
}

void LauncherPresetsPanel::onSelectionChanged() {
  updateButtonsState();
}
