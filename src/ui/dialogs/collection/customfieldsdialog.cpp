// Per-item editor for user-defined custom metadata fields.
// See header for rationale on how invalid rows are handled.
#include "customfieldsdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

CustomFieldsDialog::CustomFieldsDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void CustomFieldsDialog::setupUi() {
  setWindowTitle(tr("Edit custom fields"));
  resize(480, 360);

  auto *layout = new QVBoxLayout(this);

  auto *header = new QLabel(this);
  header->setObjectName("customFieldsHeader");
  header->setWordWrap(true);
  layout->addWidget(header);

  m_table = new QTableWidget(0, 2, this);
  m_table->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
  m_table->verticalHeader()->setVisible(false);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked |
                           QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
  layout->addWidget(m_table, /*stretch=*/1);

  auto *buttonRow = new QHBoxLayout();
  m_addButton = new QPushButton(tr("Add"), this);
  m_removeButton = new QPushButton(tr("Remove"), this);
  buttonRow->addWidget(m_addButton);
  buttonRow->addWidget(m_removeButton);
  buttonRow->addStretch(1);
  layout->addLayout(buttonRow);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  layout->addWidget(buttons);

  connect(m_addButton, &QPushButton::clicked, this, &CustomFieldsDialog::onAddRow);
  connect(m_removeButton, &QPushButton::clicked, this, &CustomFieldsDialog::onRemoveSelected);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void CustomFieldsDialog::setItemTitle(const QString &title) {
  if (auto *header = findChild<QLabel *>("customFieldsHeader")) {
    if (title.isEmpty()) {
      header->setText(tr("Add or edit custom key/value pairs."));
    } else {
      header->setText(tr("Custom fields for: %1").arg(title));
    }
  }
}

void CustomFieldsDialog::setFields(const ItemMetadataStore::CustomFieldList &fields) {
  m_table->setRowCount(0);
  for (const auto &pair : fields) {
    appendRow(pair.first, pair.second);
  }
}

ItemMetadataStore::CustomFieldList CustomFieldsDialog::fields() const {
  ItemMetadataStore::CustomFieldList out;
  out.reserve(m_table->rowCount());
  for (int row = 0; row < m_table->rowCount(); ++row) {
    QTableWidgetItem *keyItem = m_table->item(row, 0);
    QTableWidgetItem *valueItem = m_table->item(row, 1);
    const QString key = keyItem ? keyItem->text() : QString();
    const QString value = valueItem ? valueItem->text() : QString();
    out.append({key, value});
  }
  return out;
}

void CustomFieldsDialog::onAddRow() {
  appendRow(QString(), QString());
  // Drop the user straight into editing the new key cell so they can start
  // typing immediately rather than clicking again.
  const int newRow = m_table->rowCount() - 1;
  m_table->setCurrentCell(newRow, 0);
  m_table->editItem(m_table->item(newRow, 0));
}

void CustomFieldsDialog::onRemoveSelected() {
  const auto selected = m_table->selectionModel()->selectedRows();
  // Iterate rows in descending order so removal of an earlier row doesn't
  // shift the index of the next pending removal.
  QList<int> rows;
  rows.reserve(selected.size());
  for (const auto &index : selected) {
    rows.append(index.row());
  }
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  for (int row : rows) {
    m_table->removeRow(row);
  }
}

void CustomFieldsDialog::appendRow(const QString &key, const QString &value) {
  const int row = m_table->rowCount();
  m_table->insertRow(row);
  m_table->setItem(row, 0, new QTableWidgetItem(key));
  m_table->setItem(row, 1, new QTableWidgetItem(value));
}
