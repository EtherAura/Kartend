#include "datauditfixdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

using DatAudit::FixAction;
using DatAudit::FixActionKind;
using DatAudit::FixPlan;
using DatAudit::FixSettings;

namespace {

QString actionLine(const FixAction &a) {
  const QString from = QFileInfo(a.fromPath).fileName();
  const QString to = QFileInfo(a.toPath).fileName();
  switch (a.kind) {
  case FixActionKind::Rename:
    return QObject::tr("Rename: %1 → %2").arg(from, to);
  case FixActionKind::Relocate:
    return QObject::tr("Copy to sorted output: %1 → %2").arg(from, a.toPath);
  case FixActionKind::Quarantine:
    return QObject::tr("Quarantine: %1 → %2").arg(from, a.toPath);
  case FixActionKind::Repack:
    return QObject::tr("Repack: %1 → %2 (%3 inner)").arg(from, to).arg(a.innerRenames.size());
  }
  return from;
}

} // namespace

DatAuditFixDialog::DatAuditFixDialog(QList<DatAudit::AuditRow> rows, QWidget *parent)
    : QDialog(parent), m_rows(std::move(rows)) {
  setWindowTitle(tr("Fix"));
  resize(640, 480);

  auto *root = new QVBoxLayout(this);

  // Options.
  auto *form = new QFormLayout();
  m_rename = new QCheckBox(tr("Rename misnamed files to their canonical name"), this);
  m_rename->setChecked(true);
  form->addRow(m_rename);

  m_quarantine =
      new QCheckBox(tr("Move unknown and wrong-content files to a quarantine folder"), this);
  auto *quarRow = new QHBoxLayout();
  m_quarantineDir = new QLineEdit(this);
  m_quarantineDir->setPlaceholderText(tr("Quarantine folder…"));
  m_browseQuarantine = new QPushButton(tr("Browse…"), this);
  quarRow->addWidget(m_quarantineDir, 1);
  quarRow->addWidget(m_browseQuarantine);
  form->addRow(m_quarantine, quarRow);

  m_relocate = new QCheckBox(tr("Copy present files into a clean sorted folder"), this);
  auto *managedRow = new QHBoxLayout();
  m_managedDir = new QLineEdit(this);
  m_managedDir->setPlaceholderText(tr("Sorted output folder…"));
  m_browseManaged = new QPushButton(tr("Browse…"), this);
  managedRow->addWidget(m_managedDir, 1);
  managedRow->addWidget(m_browseManaged);
  form->addRow(m_relocate, managedRow);

  // Structured output: group the sorted copy into per-item subfolders
  // (Kartend-m6qsb.14). Only meaningful alongside the relocate option.
  m_perItemSubfolder =
      new QCheckBox(tr("…into a subfolder per item (group multi-file sets)"), this);
  m_perItemSubfolder->setEnabled(false);
  form->addRow(QString(), m_perItemSubfolder);
  root->addLayout(form);

  root->addWidget(new QLabel(tr("Planned actions (nothing changes until you click Apply):"), this));
  m_planList = new QListWidget(this);
  root->addWidget(m_planList, 1);

  m_status = new QLabel(this);
  root->addWidget(m_status);

  auto *buttons = new QHBoxLayout();
  m_undoButton = new QPushButton(tr("Undo last apply"), this);
  m_undoButton->setEnabled(false);
  m_applyButton = new QPushButton(tr("Apply"), this);
  auto *closeButton = new QPushButton(tr("Close"), this);
  buttons->addWidget(m_undoButton);
  buttons->addStretch();
  buttons->addWidget(m_applyButton);
  buttons->addWidget(closeButton);
  root->addLayout(buttons);

  connect(m_rename, &QCheckBox::toggled, this, &DatAuditFixDialog::recomputePlan);
  connect(m_quarantine, &QCheckBox::toggled, this, &DatAuditFixDialog::recomputePlan);
  connect(m_relocate, &QCheckBox::toggled, this, &DatAuditFixDialog::recomputePlan);
  connect(m_relocate, &QCheckBox::toggled, m_perItemSubfolder, &QWidget::setEnabled);
  connect(m_perItemSubfolder, &QCheckBox::toggled, this, &DatAuditFixDialog::recomputePlan);
  connect(m_quarantineDir, &QLineEdit::textChanged, this, &DatAuditFixDialog::recomputePlan);
  connect(m_managedDir, &QLineEdit::textChanged, this, &DatAuditFixDialog::recomputePlan);
  connect(m_browseQuarantine, &QPushButton::clicked, this, &DatAuditFixDialog::onBrowseQuarantine);
  connect(m_browseManaged, &QPushButton::clicked, this, &DatAuditFixDialog::onBrowseManaged);
  connect(m_applyButton, &QPushButton::clicked, this, &DatAuditFixDialog::onApply);
  connect(m_undoButton, &QPushButton::clicked, this, &DatAuditFixDialog::onUndo);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

  recomputePlan();
}

DatAuditFixDialog::~DatAuditFixDialog() = default;

FixSettings DatAuditFixDialog::settings() const {
  FixSettings s;
  s.rename = m_rename->isChecked();
  s.quarantineUnknown = m_quarantine->isChecked();
  s.quarantineDir = m_quarantineDir->text().trimmed();
  s.relocateToManagedOutput = m_relocate->isChecked();
  s.managedOutputRoot = m_managedDir->text().trimmed();
  s.managedOutputPerItemSubfolder = m_perItemSubfolder->isChecked();
  return s;
}

void DatAuditFixDialog::setManagedOutputDefaults(bool relocateToManagedOutput,
                                                 const QString &managedOutputRoot) {
  // Seed the relocate option from the profile's persisted fix mode + root
  // (Kartend-m6qsb.14); the per-item-subfolder choice stays a per-session
  // option (not persisted on the profile).
  if (!managedOutputRoot.isEmpty()) {
    m_managedDir->setText(managedOutputRoot);
  }
  m_relocate->setChecked(relocateToManagedOutput);
  m_perItemSubfolder->setEnabled(relocateToManagedOutput);
  recomputePlan();
}

void DatAuditFixDialog::setQuarantineDefault(const QString &quarantineRoot) {
  // Prefill the quarantine folder (per-collection root, else the global default)
  // only while the field is still blank, so a manual edit is never clobbered.
  // The destructive quarantine option itself stays default-off — only the path
  // is seeded, keeping it fully overridable.
  if (!quarantineRoot.isEmpty() && m_quarantineDir->text().trimmed().isEmpty()) {
    m_quarantineDir->setText(quarantineRoot);
    recomputePlan();
  }
}

void DatAuditFixDialog::recomputePlan() {
  m_plan = DatAudit::computeFixPlan(m_rows, settings());
  m_planList->clear();
  for (const FixAction &a : m_plan.actions) {
    m_planList->addItem(actionLine(a));
  }
  m_applyButton->setEnabled(!m_plan.isEmpty());
  if (m_plan.isEmpty()) {
    m_status->setText(tr("No actions for the current options."));
  } else {
    m_status->setText(tr("%1 action(s) planned.").arg(m_plan.actions.size()));
  }
}

void DatAuditFixDialog::onBrowseQuarantine() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose quarantine folder"));
  if (!dir.isEmpty()) {
    m_quarantineDir->setText(dir);
  }
}

void DatAuditFixDialog::onBrowseManaged() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose sorted output folder"));
  if (!dir.isEmpty()) {
    m_managedDir->setText(dir);
  }
}

void DatAuditFixDialog::onApply() {
  if (m_plan.isEmpty()) {
    return;
  }
  // Count planned renames before applying — renamed files get their canonical
  // names, the trigger for the re-scrape offer (Kartend-m6qsb.27). Counting the
  // plan (not per-action success) is fine for an offer prompt.
  int renames = 0;
  for (const DatAudit::FixAction &a : m_plan.actions) {
    if (a.kind == DatAudit::FixActionKind::Rename) {
      ++renames;
    }
  }
  auto res = DatAudit::applyFixPlan(m_plan, /*dryRun=*/false);
  if (res.applied > 0) {
    m_applied = true;
    m_renamedCount += renames;
  }
  m_undo = res.undo;
  m_undoButton->setEnabled(!m_undo.isEmpty());
  m_status->setText(
      tr("Applied %1 · skipped %2 · failed %3").arg(res.applied).arg(res.skipped).arg(res.failed));
  // The on-disk state changed; recompute against the (stale) rows is not
  // meaningful, so clear the plan to avoid double-applying.
  m_plan = FixPlan{};
  m_planList->clear();
  m_applyButton->setEnabled(false);
}

void DatAuditFixDialog::onUndo() {
  const int reverted = DatAudit::applyUndo(m_undo);
  m_status->setText(tr("Reverted %1 action(s).").arg(reverted));
  m_undo.clear();
  m_undoButton->setEnabled(false);
  // The undo restored the original layout; a re-audit by the caller will
  // reflect it.
}
