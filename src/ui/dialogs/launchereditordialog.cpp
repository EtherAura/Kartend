#include "launchereditordialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

LauncherEditorDialog::LauncherEditorDialog(QWidget *parent, const LauncherConfig &initial,
                                           const QString &title,
                                           const QList<LauncherPreset> &availablePresets)
    : QDialog(parent), m_availablePresets(availablePresets) {
  setWindowTitle(title);
  setModal(true);

  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout();
  m_form = form;

  // Kartend-p1jd: only render the preset combo when the caller passed a
  // non-empty list. The preset-management UI itself reuses this dialog to
  // edit preset entries directly — and presets can't reference other
  // presets, so the combo is omitted in that flow.
  if (!m_availablePresets.isEmpty()) {
    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItem(tr("Inline (no preset)"), QString());
    int initialPresetIndex = -1;
    for (int i = 0; i < m_availablePresets.size(); ++i) {
      const LauncherPreset &p = m_availablePresets[i];
      const QString label = p.name.trimmed().isEmpty() ? p.id : p.name;
      m_presetCombo->addItem(label, p.id);
      if (!initial.presetId.isEmpty() && p.id == initial.presetId) {
        initialPresetIndex = i;
      }
    }
    // Combo index 0 = "Inline"; preset entries occupy indices 1..N.
    m_presetCombo->setCurrentIndex(initialPresetIndex >= 0 ? initialPresetIndex + 1 : 0);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LauncherEditorDialog::onPresetChanged);
    form->addRow(tr("Use preset:"), m_presetCombo);
  }

  m_nameEdit = new QLineEdit(initial.name, this);
  m_nameEdit->setPlaceholderText(tr("Display name (optional)"));
  form->addRow(tr("Name:"), m_nameEdit);

  m_launcherEdit = new QLineEdit(initial.launcherPath, this);
  m_launcherEdit->setPlaceholderText(tr("Executable path or command in PATH"));
  auto *launcherRow = new QHBoxLayout();
  launcherRow->addWidget(m_launcherEdit);
  auto *browseLauncher = new QPushButton(tr("Browse"), this);
  connect(browseLauncher, &QPushButton::clicked, this, &LauncherEditorDialog::onBrowseLauncher);
  launcherRow->addWidget(browseLauncher);
  form->addRow(tr("Launcher:"), launcherRow);

  m_coreEdit = new QLineEdit(initial.corePath, this);
  m_coreEdit->setPlaceholderText(tr("Libretro core (.so/.dll/.dylib)"));
  // Kartend-bafi: wrap the core row in a container widget so QFormLayout's
  // setRowVisible can hide the label + fields together when the launcher
  // isn't retroarch.
  m_coreRowWidget = new QWidget(this);
  auto *coreRow = new QHBoxLayout(m_coreRowWidget);
  coreRow->setContentsMargins(0, 0, 0, 0);
  coreRow->addWidget(m_coreEdit);
  auto *browseCore = new QPushButton(tr("Browse"), this);
  connect(browseCore, &QPushButton::clicked, this, &LauncherEditorDialog::onBrowseCore);
  coreRow->addWidget(browseCore);
  form->addRow(tr("Core Path:"), m_coreRowWidget);

  m_paramsEdit = new QLineEdit(initial.launchParameters, this);
  m_paramsEdit->setPlaceholderText(tr("Optional parameters (e.g. -fullscreen)"));
  form->addRow(tr("Launch Parameters:"), m_paramsEdit);

  layout->addLayout(form);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  resize(480, 220);

  // Kartend-bafi: live-track the launcher path so the core row appears the
  // moment the user types/browses to a retroarch executable.
  connect(m_launcherEdit, &QLineEdit::textChanged, this,
          [this](const QString &) { updateCoreRowVisibility(); });

  // Apply the initial preset selection (if any) so the dialog opens with
  // the fields locked to the preset values when editing a referenced
  // launcher. Skipped when there's no combo at all.
  if (m_presetCombo) {
    applyPresetSelection(m_presetCombo->currentIndex() - 1);
  }

  // Kartend-bafi: seed visibility from the initial launcher path so freshly
  // opened dialogs hide the core field when it doesn't apply.
  updateCoreRowVisibility();
}

LauncherConfig LauncherEditorDialog::launcher() const {
  LauncherConfig out;
  out.name = m_nameEdit ? m_nameEdit->text().trimmed() : QString();
  out.launcherPath = m_launcherEdit ? m_launcherEdit->text().trimmed() : QString();
  out.corePath = m_coreEdit ? m_coreEdit->text().trimmed() : QString();
  out.launchParameters = m_paramsEdit ? m_paramsEdit->text().trimmed() : QString();
  out.presetId = m_presetCombo ? m_presetCombo->currentData().toString() : QString();
  return out;
}

void LauncherEditorDialog::onBrowseLauncher() {
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select Launcher Executable"));
  if (!fileName.isEmpty() && m_launcherEdit) {
    m_launcherEdit->setText(fileName);
  }
}

void LauncherEditorDialog::onBrowseCore() {
  QString fileName = QFileDialog::getOpenFileName(this, tr("Select Core File"));
  if (!fileName.isEmpty() && m_coreEdit) {
    m_coreEdit->setText(fileName);
  }
}

void LauncherEditorDialog::onPresetChanged(int comboIndex) {
  // Combo index 0 is "Inline"; preset entries start at 1, so subtract one.
  applyPresetSelection(comboIndex - 1);
}

void LauncherEditorDialog::applyPresetSelection(int presetIndex) {
  const bool inlineMode = presetIndex < 0 || presetIndex >= m_availablePresets.size();
  if (!inlineMode) {
    const LauncherPreset &preset = m_availablePresets[presetIndex];
    if (m_nameEdit) m_nameEdit->setText(preset.name);
    if (m_launcherEdit) m_launcherEdit->setText(preset.launcherPath);
    if (m_coreEdit) m_coreEdit->setText(preset.corePath);
    if (m_paramsEdit) m_paramsEdit->setText(preset.launchParameters);
  }
  // Lock the form fields when a preset drives the values so the user can't
  // silently desync the inline copy from the preset. Switching back to
  // "Inline" re-enables them with whatever was last shown.
  const bool fieldsEditable = inlineMode;
  if (m_nameEdit) m_nameEdit->setEnabled(fieldsEditable);
  if (m_launcherEdit) m_launcherEdit->setEnabled(fieldsEditable);
  if (m_coreEdit) m_coreEdit->setEnabled(fieldsEditable);
  if (m_paramsEdit) m_paramsEdit->setEnabled(fieldsEditable);
}

void LauncherEditorDialog::updateCoreRowVisibility() {
  if (!m_form || !m_coreRowWidget || !m_launcherEdit) {
    return;
  }
  m_form->setRowVisible(m_coreRowWidget, LauncherUtils::usesLibretroCore(m_launcherEdit->text()));
}
