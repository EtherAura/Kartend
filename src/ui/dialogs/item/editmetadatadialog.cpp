#include "editmetadatadialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "starratingwidget.h"
#include "uiconstants/dialog.h"

namespace {

QString formatRatingForLabel(int rating) {
  if (rating < 0) {
    return EditMetadataDialog::tr("Unrated");
  }
  // Render as e.g. "3.5 / 5". Half-step granularity matches the widget's
  // internal 0-10 representation without leaking it to the user.
  const double stars = rating / 2.0;
  return EditMetadataDialog::tr("%1 / 5").arg(
      QString::number(stars, 'f', stars == int(stars) ? 0 : 1));
}

} // namespace

EditMetadataDialog::EditMetadataDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void EditMetadataDialog::setupUi() {
  setWindowTitle(tr("Edit item metadata"));
  resize(UIConstants::EditMetadataDialog::DEFAULT_WIDTH,
         UIConstants::EditMetadataDialog::DEFAULT_HEIGHT);

  auto *outerLayout = new QVBoxLayout(this);

  m_headerLabel = new QLabel(this);
  m_headerLabel->setWordWrap(true);
  outerLayout->addWidget(m_headerLabel);

  auto *form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

  m_notesEdit = new QPlainTextEdit(this);
  m_notesEdit->setPlaceholderText(tr("Personal notes about this item (optional)"));
  m_notesEdit->setTabChangesFocus(true);
  // Cap height so the dialog stays compact; the field still scrolls when
  // the user writes more than fits.
  m_notesEdit->setFixedHeight(UIConstants::EditMetadataDialog::NOTES_FIELD_HEIGHT);
  form->addRow(tr("Notes:"), m_notesEdit);

  m_tagsEdit = new QLineEdit(this);
  m_tagsEdit->setPlaceholderText(tr("Comma-separated, e.g. 'live, jazz, archive'"));
  form->addRow(tr("Tags:"), m_tagsEdit);

  auto *ratingRow = new QHBoxLayout();
  m_ratingWidget = new StarRatingWidget(this);
  m_ratingValueLabel = new QLabel(formatRatingForLabel(-1), this);
  m_clearRatingButton = new QPushButton(tr("Clear"), this);
  m_clearRatingButton->setFlat(true);
  ratingRow->addWidget(m_ratingWidget);
  ratingRow->addWidget(m_ratingValueLabel);
  ratingRow->addStretch(1);
  ratingRow->addWidget(m_clearRatingButton);
  form->addRow(tr("Rating:"), ratingRow);

  m_sourceUrlEdit = new QLineEdit(this);
  m_sourceUrlEdit->setPlaceholderText(tr("Optional URL (where this item came from)"));
  form->addRow(tr("Source URL:"), m_sourceUrlEdit);

  outerLayout->addLayout(form);

  auto *customLabel = new QLabel(tr("Custom fields:"), this);
  outerLayout->addWidget(customLabel);

  m_customFieldsTable = new QTableWidget(0, 2, this);
  m_customFieldsTable->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
  m_customFieldsTable->horizontalHeader()->setStretchLastSection(true);
  m_customFieldsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
  m_customFieldsTable->verticalHeader()->setVisible(false);
  m_customFieldsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_customFieldsTable->setEditTriggers(
      QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked |
      QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
  outerLayout->addWidget(m_customFieldsTable, /*stretch=*/1);

  auto *rowButtons = new QHBoxLayout();
  m_addRowButton = new QPushButton(tr("Add row"), this);
  m_removeRowButton = new QPushButton(tr("Remove row"), this);
  rowButtons->addWidget(m_addRowButton);
  rowButtons->addWidget(m_removeRowButton);
  rowButtons->addStretch(1);
  outerLayout->addLayout(rowButtons);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  outerLayout->addWidget(buttons);

  connect(m_ratingWidget, &StarRatingWidget::ratingChanged, this,
          [this](int rating) { m_ratingValueLabel->setText(formatRatingForLabel(rating)); });
  connect(m_clearRatingButton, &QPushButton::clicked, this,
          [this]() { m_ratingWidget->setRating(-1); });
  connect(m_addRowButton, &QPushButton::clicked, this, &EditMetadataDialog::onAddCustomFieldRow);
  connect(m_removeRowButton, &QPushButton::clicked, this,
          &EditMetadataDialog::onRemoveSelectedCustomFieldRows);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void EditMetadataDialog::setItemTitle(const QString &title) {
  if (!m_headerLabel) {
    return;
  }
  if (title.isEmpty()) {
    m_headerLabel->setText(tr("Edit personal curation fields for this item."));
  } else {
    m_headerLabel->setText(tr("Editing metadata for: %1").arg(title));
  }
}

void EditMetadataDialog::setPayload(const EditMetadataPayload &payload) {
  m_notesEdit->setPlainText(payload.notes);
  m_tagsEdit->setText(payload.tags.join(QStringLiteral(", ")));
  m_ratingWidget->setRating(payload.rating);
  m_ratingValueLabel->setText(formatRatingForLabel(payload.rating));
  m_sourceUrlEdit->setText(payload.sourceUrl);

  m_customFieldsTable->setRowCount(0);
  for (const auto &pair : payload.customFields) {
    appendCustomFieldRow(pair.first, pair.second);
  }
}

EditMetadataPayload EditMetadataDialog::payload() const {
  EditMetadataPayload out;
  out.notes = m_notesEdit->toPlainText();
  // Split on commas, trim, drop empties. Dedup is left to the store's
  // serializeTags() so the user's casing wins (first-seen).
  const QStringList rawTags = m_tagsEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts);
  for (const QString &tag : rawTags) {
    const QString trimmed = tag.trimmed();
    if (!trimmed.isEmpty()) {
      out.tags.append(trimmed);
    }
  }
  out.rating = m_ratingWidget->rating();
  out.sourceUrl = m_sourceUrlEdit->text().trimmed();

  out.customFields.reserve(m_customFieldsTable->rowCount());
  for (int row = 0; row < m_customFieldsTable->rowCount(); ++row) {
    QTableWidgetItem *keyItem = m_customFieldsTable->item(row, 0);
    QTableWidgetItem *valueItem = m_customFieldsTable->item(row, 1);
    out.customFields.append(
        {keyItem ? keyItem->text() : QString(), valueItem ? valueItem->text() : QString()});
  }
  return out;
}

void EditMetadataDialog::onAddCustomFieldRow() {
  appendCustomFieldRow(QString(), QString());
  const int newRow = m_customFieldsTable->rowCount() - 1;
  m_customFieldsTable->setCurrentCell(newRow, 0);
  m_customFieldsTable->editItem(m_customFieldsTable->item(newRow, 0));
}

void EditMetadataDialog::onRemoveSelectedCustomFieldRows() {
  const auto selected = m_customFieldsTable->selectionModel()->selectedRows();
  QList<int> rows;
  rows.reserve(selected.size());
  for (const auto &index : selected) {
    rows.append(index.row());
  }
  // Descending order so an earlier removal doesn't shift later indices.
  std::sort(rows.begin(), rows.end(), std::greater<int>());
  for (int row : rows) {
    m_customFieldsTable->removeRow(row);
  }
}

void EditMetadataDialog::appendCustomFieldRow(const QString &key, const QString &value) {
  const int row = m_customFieldsTable->rowCount();
  m_customFieldsTable->insertRow(row);
  m_customFieldsTable->setItem(row, 0, new QTableWidgetItem(key));
  m_customFieldsTable->setItem(row, 1, new QTableWidgetItem(value));
}
