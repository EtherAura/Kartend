#include "searchpresetsdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "collection/view_settings.h"

SearchPresetsDialog::SearchPresetsDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void SearchPresetsDialog::setupUi() {
  setWindowTitle(tr("Saved filters"));
  resize(520, 360);

  auto *outer = new QVBoxLayout(this);
  auto *header = new QLabel(
      tr("A saved filter keeps the search box's contents together with the sort order and "
         "filters around it, so a query worth composing twice — say "
         "\"played:false tag:soundtrack\" sorted by date — can be brought back by name."),
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

  connect(m_saveButton, &QPushButton::clicked, this, &SearchPresetsDialog::onSaveCurrent);
  connect(m_applyButton, &QPushButton::clicked, this, &SearchPresetsDialog::onApplySelected);
  connect(m_deleteButton, &QPushButton::clicked, this, &SearchPresetsDialog::onDeleteSelected);
  connect(m_list, &QListWidget::itemSelectionChanged, this,
          &SearchPresetsDialog::onSelectionChanged);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

  onSelectionChanged();
}

void SearchPresetsDialog::setRegistry(QList<SearchPreset> *presets, const ViewSettings *currentView,
                                      const QString &currentSearchText, ApplyHandler onApply) {
  m_presets = presets;
  m_currentView = currentView;
  m_currentSearchText = currentSearchText;
  m_onApply = std::move(onApply);
  m_saveButton->setEnabled(m_currentView != nullptr);
  refreshList();
}

void SearchPresetsDialog::refreshList(const QString &selectName) {
  if (!m_list) return;
  m_list->clear();
  if (!m_presets) {
    onSelectionChanged();
    return;
  }
  int selectRow = -1;
  for (int i = 0; i < m_presets->size(); ++i) {
    const SearchPreset &p = m_presets->at(i);
    QString display = p.name.trimmed().isEmpty() ? tr("(unnamed)") : p.name;
    // Show the query beside the name: a list of bare names is unreadable once
    // there are more than a handful, and the query is what the user is
    // actually choosing between. An empty query means the preset is pure
    // filter/sort state, which is worth saying rather than leaving blank.
    const QString query = p.searchText.trimmed();
    display +=
        query.isEmpty() ? tr("  —  (filters and sort only)") : QStringLiteral("  —  %1").arg(query);
    m_list->addItem(display);
    if (!selectName.isEmpty() &&
        p.name.trimmed().compare(selectName.trimmed(), Qt::CaseInsensitive) == 0) {
      selectRow = i;
    }
  }
  if (selectRow >= 0) m_list->setCurrentRow(selectRow);
  onSelectionChanged();
}

int SearchPresetsDialog::selectedRow() const {
  return m_list ? m_list->currentRow() : -1;
}

void SearchPresetsDialog::onSelectionChanged() {
  const bool hasSelection = m_presets && selectedRow() >= 0 && selectedRow() < m_presets->size();
  m_applyButton->setEnabled(hasSelection && m_onApply);
  m_deleteButton->setEnabled(hasSelection);
}

void SearchPresetsDialog::onSaveCurrent() {
  if (!m_presets || !m_currentView) return;
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Save filter"), tr("Filter name:"),
                                             QLineEdit::Normal, QString(), &ok);
  if (!ok) return;
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty()) {
    QMessageBox::warning(this, tr("Save filter"), tr("The filter name cannot be empty."));
    return;
  }
  // addOrReplace is name-keyed and case-insensitive, so a clashing name would
  // silently replace. Ask first — the registry has no undo.
  for (const SearchPreset &existing : *m_presets) {
    if (existing.name.trimmed().compare(trimmed, Qt::CaseInsensitive) == 0) {
      const auto choice = QMessageBox::question(
          this, tr("Overwrite filter"),
          tr("A saved filter named \"%1\" already exists. Overwrite it?").arg(trimmed),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (choice != QMessageBox::Yes) return;
      break;
    }
  }
  const SearchPreset preset =
      SearchPresetIO::fromViewSettings(*m_currentView, m_currentSearchText, trimmed);
  *m_presets = SearchPresetIO::addOrReplace(*m_presets, preset);
  refreshList(trimmed);
}

void SearchPresetsDialog::onApplySelected() {
  if (!m_presets || !m_onApply) return;
  const int row = selectedRow();
  if (row < 0 || row >= m_presets->size()) return;
  m_onApply(m_presets->at(row));
}

void SearchPresetsDialog::onDeleteSelected() {
  if (!m_presets) return;
  const int row = selectedRow();
  if (row < 0 || row >= m_presets->size()) return;
  const QString name = m_presets->at(row).name;
  const auto choice =
      QMessageBox::question(this, tr("Delete filter"),
                            tr("Delete the saved filter \"%1\"? This can't be undone.").arg(name),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (choice != QMessageBox::Yes) return;
  *m_presets = SearchPresetIO::removeByName(*m_presets, name);
  refreshList();
}
