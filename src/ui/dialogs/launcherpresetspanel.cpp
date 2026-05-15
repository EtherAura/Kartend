#include "launcherpresetspanel.h"

#include "launchereditordialog.h"
#include "launcherprobe.h"
#include "ui_launcherpresetspanel.h"

#include <QBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUuid>
#include <QVBoxLayout>

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

  // Inject the detect button into the existing button column. The .ui
  // ships Add / Edit / Remove + a vertical spacer; the new button sits
  // between Remove and the spacer so the bulk-add affordance stays
  // grouped with the other mutations rather than living off in its own
  // section.
  if (auto *removeBtn = ui->removeLauncherPresetButton) {
    if (auto *col = qobject_cast<QBoxLayout *>(removeBtn->parentWidget()->layout())) {
      const int removeIdx = col->indexOf(removeBtn);
      m_detectButton = new QPushButton(tr("Detect installed…"), removeBtn->parentWidget());
      m_detectButton->setToolTip(
          tr("Probe your PATH for well-known media players, readers, image "
             "viewers, and emulators. Pick which detected binaries to add as "
             "launcher presets."));
      connect(m_detectButton, &QPushButton::clicked, this, &LauncherPresetsPanel::onDetect);
      // insertWidget at removeIdx+1 places the button right after Remove.
      col->insertWidget(removeIdx + 1, m_detectButton);
    }
  }

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
      const QString label = preset.name.trimmed().isEmpty() ? tr("(unnamed preset)") : preset.name;
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

void LauncherPresetsPanel::onDetect() {
  if (!m_presets) {
    return;
  }
  const QList<LauncherProbe::KnownLauncher> detected = LauncherProbe::probeInstalled();
  if (detected.isEmpty()) {
    QMessageBox::information(this, tr("No launchers detected"),
                             tr("None of the well-known media-player, reader, image-viewer, or "
                                "emulator binaries Kartend probes for were found on your PATH. "
                                "Add launchers manually with the Add… button."));
    return;
  }

  // Build a small checkable picker. Categories appear as section
  // headers (non-selectable); each detected binary is a checkable row
  // pre-checked by default so a user who just wants "everything"
  // accepts one click.
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Detected launchers"));
  dialog.setModal(true);
  dialog.resize(420, 480);

  auto *root = new QVBoxLayout(&dialog);
  auto *intro = new QLabel(tr("These launchers were found on your PATH. Pick which ones to add "
                              "as launcher presets — uncheck any you don't want."),
                           &dialog);
  intro->setWordWrap(true);
  root->addWidget(intro);

  auto *list = new QListWidget(&dialog);
  list->setSelectionMode(QAbstractItemView::NoSelection);
  LauncherProbe::Category lastCategory = LauncherProbe::Category::Video;
  bool sawAny = false;
  for (const auto &k : detected) {
    if (!sawAny || k.category != lastCategory) {
      auto *header = new QListWidgetItem(LauncherProbe::categoryLabel(k.category), list);
      QFont f = header->font();
      f.setBold(true);
      header->setFont(f);
      header->setFlags(Qt::NoItemFlags); // section header — not checkable
      lastCategory = k.category;
      sawAny = true;
    }
    auto *item = new QListWidgetItem(k.displayName + QStringLiteral("  (") + k.binary + ')', list);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    // Stash the binary so we can map back to the curated entry on accept
    // without depending on list order matching the detected vector.
    item->setData(Qt::UserRole, k.binary);
  }
  root->addWidget(list, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  if (auto *ok = buttons->button(QDialogButtonBox::Ok)) {
    ok->setText(tr("Add selected"));
  }
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  root->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  // Map binary → curated KnownLauncher so we can copy the displayName +
  // launchParameters fields without re-running the probe.
  QHash<QString, LauncherProbe::KnownLauncher> byBinary;
  byBinary.reserve(detected.size());
  for (const auto &k : detected) {
    byBinary.insert(k.binary, k);
  }

  // Skip duplicates: if a preset with this name already exists, leave
  // it alone so re-running detect doesn't pile up identical entries.
  // Name match is intentional — the user might have manually edited
  // the launcherPath of an existing preset and we shouldn't clobber it.
  QHash<QString, bool> existingNames;
  for (const auto &p : std::as_const(*m_presets)) {
    existingNames.insert(p.name, true);
  }

  int added = 0;
  for (int i = 0; i < list->count(); ++i) {
    auto *item = list->item(i);
    if (!(item->flags() & Qt::ItemIsUserCheckable) || item->checkState() != Qt::Checked) {
      continue;
    }
    const QString binary = item->data(Qt::UserRole).toString();
    if (binary.isEmpty() || !byBinary.contains(binary)) {
      continue;
    }
    const auto &k = byBinary.value(binary);
    if (existingNames.contains(k.displayName)) {
      continue;
    }
    LauncherPreset preset;
    preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = k.displayName;
    // launcherPath stores the bare command name — QStandardPaths-
    // resolution at launch time means the user's shell-resolved
    // binary still wins if they later install a different copy.
    preset.launcherPath = k.binary;
    preset.corePath.clear();
    preset.launchParameters = k.launchParameters;
    m_presets->append(preset);
    existingNames.insert(preset.name, true);
    ++added;
  }

  if (added > 0) {
    refresh();
    ui->launcherPresetsList->setCurrentRow(m_presets->size() - 1);
    emit presetsChanged();
  }
}

void LauncherPresetsPanel::onSelectionChanged() {
  updateButtonsState();
}
