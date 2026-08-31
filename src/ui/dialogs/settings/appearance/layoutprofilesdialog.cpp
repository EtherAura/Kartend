#include "layoutprofilesdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "collection/collectionconfig.h"

LayoutProfilesDialog::LayoutProfilesDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void LayoutProfilesDialog::setupUi() {
  setWindowTitle(tr("Layout profiles"));
  resize(520, 360);

  auto *outer = new QVBoxLayout(this);
  auto *header = new QLabel(
      tr("Saved layout profiles capture the visual settings (grid, sidebar, background, list "
         "view) so you can switch between, e.g., a couch-mode layout and a dense desktop list. "
         "Apply targets the current collection."),
      this);
  header->setWordWrap(true);
  header->setStyleSheet(UIConstants::Color::mutedLabelStyleSheet(/*italic=*/true));
  outer->addWidget(header);

  m_list = new QListWidget(this);
  m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  outer->addWidget(m_list, /*stretch=*/1);

  auto *row = new QHBoxLayout();
  m_saveButton = new QPushButton(tr("Save current as…"), this);
  m_applyButton = new QPushButton(tr("Apply"), this);
  m_deleteButton = new QPushButton(tr("Delete"), this);
  row->addWidget(m_saveButton);
  row->addWidget(m_applyButton);
  row->addWidget(m_deleteButton);
  row->addStretch(1);
  outer->addLayout(row);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  outer->addWidget(buttons);

  connect(m_saveButton, &QPushButton::clicked, this, &LayoutProfilesDialog::onSaveCurrent);
  connect(m_applyButton, &QPushButton::clicked, this, &LayoutProfilesDialog::onApplySelected);
  connect(m_deleteButton, &QPushButton::clicked, this, &LayoutProfilesDialog::onDeleteSelected);
  connect(m_list, &QListWidget::itemSelectionChanged, this,
          &LayoutProfilesDialog::onSelectionChanged);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  // No selection yet → Apply / Delete disabled until the user picks.
  onSelectionChanged();
}

void LayoutProfilesDialog::setRegistry(QList<ThemePreset> *profiles,
                                       const CollectionConfig *currentCollection,
                                       ApplyHandler onApply) {
  m_profiles = profiles;
  m_currentCollection = currentCollection;
  m_onApply = std::move(onApply);
  // Save Current is only meaningful when a collection is in view; we
  // disable it without a current collection so the user gets a visible
  // affordance instead of a silent no-op.
  m_saveButton->setEnabled(m_currentCollection != nullptr);
  refreshList();
}

void LayoutProfilesDialog::refreshList(const QString &selectName) {
  if (!m_list) return;
  m_list->clear();
  if (!m_profiles) {
    onSelectionChanged();
    return;
  }
  int selectRow = -1;
  for (int i = 0; i < m_profiles->size(); ++i) {
    const ThemePreset &p = m_profiles->at(i);
    const QString display = p.name.trimmed().isEmpty() ? tr("(unnamed)") : p.name;
    m_list->addItem(display);
    if (!selectName.isEmpty() &&
        p.name.trimmed().compare(selectName.trimmed(), Qt::CaseInsensitive) == 0) {
      selectRow = i;
    }
  }
  if (selectRow >= 0) {
    m_list->setCurrentRow(selectRow);
  }
  onSelectionChanged();
}

int LayoutProfilesDialog::selectedRow() const {
  if (!m_list) return -1;
  return m_list->currentRow();
}

void LayoutProfilesDialog::onSelectionChanged() {
  const bool hasSelection = m_profiles && selectedRow() >= 0 && selectedRow() < m_profiles->size();
  m_applyButton->setEnabled(hasSelection && m_currentCollection && m_onApply);
  m_deleteButton->setEnabled(hasSelection);
}

void LayoutProfilesDialog::onSaveCurrent() {
  if (!m_profiles || !m_currentCollection) {
    return;
  }
  // Pre-fill with the collection's name so the user can just hit OK for
  // a "Couch" layout saved from a collection named "Couch". They can
  // rename in the dialog if they prefer.
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Save layout profile"), tr("Profile name:"),
                                             QLineEdit::Normal, m_currentCollection->name, &ok);
  if (!ok) {
    return;
  }
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    QMessageBox::warning(this, tr("Save layout profile"), tr("The profile name cannot be empty."));
    return;
  }
  // Confirm on overwrite — saving "Couch" twice is usually intentional
  // (capturing tweaks), but an accidental name collision should still
  // surface a yes/no prompt.
  for (const ThemePreset &existing : *m_profiles) {
    if (existing.name.trimmed().compare(trimmed, Qt::CaseInsensitive) == 0) {
      const auto choice = QMessageBox::question(
          this, tr("Overwrite profile"),
          tr("A profile named \"%1\" already exists. Overwrite it?").arg(trimmed),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (choice != QMessageBox::Yes) {
        return;
      }
      break;
    }
  }
  ThemePreset preset = ThemePresetIO::fromCollection(*m_currentCollection, trimmed);
  *m_profiles = ThemePresetIO::addOrReplace(*m_profiles, preset);
  refreshList(trimmed);
}

void LayoutProfilesDialog::onApplySelected() {
  if (!m_profiles || !m_onApply) {
    return;
  }
  const int row = selectedRow();
  if (row < 0 || row >= m_profiles->size()) {
    return;
  }
  m_onApply(m_profiles->at(row));
}

void LayoutProfilesDialog::onDeleteSelected() {
  if (!m_profiles) return;
  const int row = selectedRow();
  if (row < 0 || row >= m_profiles->size()) {
    return;
  }
  const QString name = m_profiles->at(row).name;
  const auto choice =
      QMessageBox::question(this, tr("Delete profile"),
                            tr("Delete the layout profile \"%1\"? This can't be undone.").arg(name),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (choice != QMessageBox::Yes) {
    return;
  }
  *m_profiles = ThemePresetIO::removeByName(*m_profiles, name);
  refreshList();
}
