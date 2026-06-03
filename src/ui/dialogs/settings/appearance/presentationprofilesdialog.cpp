#include "presentationprofilesdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "collection/generalsettings.h"

PresentationProfilesDialog::PresentationProfilesDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void PresentationProfilesDialog::setupUi() {
  setWindowTitle(tr("Presentation profiles"));
  resize(520, 360);

  auto *outer = new QVBoxLayout(this);
  auto *header = new QLabel(
      tr("Presentation profiles bundle attract-mode timing, marquee, and splash settings "
         "so you can switch quickly between, e.g., 'desktop quiet' and 'showcase loop'."),
      this);
  header->setWordWrap(true);
  header->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
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

  connect(m_saveButton, &QPushButton::clicked, this, &PresentationProfilesDialog::onSaveCurrent);
  connect(m_applyButton, &QPushButton::clicked, this, &PresentationProfilesDialog::onApplySelected);
  connect(m_deleteButton, &QPushButton::clicked, this,
          &PresentationProfilesDialog::onDeleteSelected);
  connect(m_list, &QListWidget::itemSelectionChanged, this,
          &PresentationProfilesDialog::onSelectionChanged);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  onSelectionChanged();
}

void PresentationProfilesDialog::setRegistry(QList<PresentationProfile> *profiles,
                                             const GeneralSettings *currentSettings,
                                             ApplyHandler onApply) {
  m_profiles = profiles;
  m_currentSettings = currentSettings;
  m_onApply = std::move(onApply);
  m_saveButton->setEnabled(m_currentSettings != nullptr);
  refreshList();
}

void PresentationProfilesDialog::refreshList(const QString &selectName) {
  if (!m_list) return;
  m_list->clear();
  if (!m_profiles) {
    onSelectionChanged();
    return;
  }
  int selectRow = -1;
  for (int i = 0; i < m_profiles->size(); ++i) {
    const PresentationProfile &p = m_profiles->at(i);
    const QString display = p.name.trimmed().isEmpty() ? tr("(unnamed)") : p.name;
    m_list->addItem(display);
    if (!selectName.isEmpty() &&
        p.name.trimmed().compare(selectName.trimmed(), Qt::CaseInsensitive) == 0) {
      selectRow = i;
    }
  }
  if (selectRow >= 0) m_list->setCurrentRow(selectRow);
  onSelectionChanged();
}

int PresentationProfilesDialog::selectedRow() const {
  return m_list ? m_list->currentRow() : -1;
}

void PresentationProfilesDialog::onSelectionChanged() {
  const bool hasSelection = m_profiles && selectedRow() >= 0 && selectedRow() < m_profiles->size();
  m_applyButton->setEnabled(hasSelection && m_onApply);
  m_deleteButton->setEnabled(hasSelection);
}

void PresentationProfilesDialog::onSaveCurrent() {
  if (!m_profiles || !m_currentSettings) return;
  bool ok = false;
  const QString name =
      QInputDialog::getText(this, tr("Save presentation profile"), tr("Profile name:"),
                            QLineEdit::Normal, QString(), &ok);
  if (!ok) return;
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    QMessageBox::warning(this, tr("Save presentation profile"),
                         tr("The profile name cannot be empty."));
    return;
  }
  for (const PresentationProfile &existing : *m_profiles) {
    if (existing.name.trimmed().compare(trimmed, Qt::CaseInsensitive) == 0) {
      const auto choice = QMessageBox::question(
          this, tr("Overwrite profile"),
          tr("A profile named \"%1\" already exists. Overwrite it?").arg(trimmed),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (choice != QMessageBox::Yes) return;
      break;
    }
  }
  PresentationProfile p = PresentationProfileIO::fromGeneralSettings(*m_currentSettings, trimmed);
  *m_profiles = PresentationProfileIO::addOrReplace(*m_profiles, p);
  refreshList(trimmed);
}

void PresentationProfilesDialog::onApplySelected() {
  if (!m_profiles || !m_onApply) return;
  const int row = selectedRow();
  if (row < 0 || row >= m_profiles->size()) return;
  m_onApply(m_profiles->at(row));
}

void PresentationProfilesDialog::onDeleteSelected() {
  if (!m_profiles) return;
  const int row = selectedRow();
  if (row < 0 || row >= m_profiles->size()) return;
  const QString name = m_profiles->at(row).name;
  const auto choice = QMessageBox::question(
      this, tr("Delete profile"),
      tr("Delete the presentation profile \"%1\"? This can't be undone.").arg(name),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (choice != QMessageBox::Yes) return;
  *m_profiles = PresentationProfileIO::removeByName(*m_profiles, name);
  refreshList();
}
