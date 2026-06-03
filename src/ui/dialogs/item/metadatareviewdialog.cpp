#include "metadatareviewdialog.h"

#include "uiconstants/color.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

MetadataReviewDialog::MetadataReviewDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
}

void MetadataReviewDialog::setupUi() {
  setWindowTitle(tr("Review missing metadata"));
  resize(520, 320);

  auto *outer = new QVBoxLayout(this);

  m_progressLabel = new QLabel(this);
  m_progressLabel->setStyleSheet(UIConstants::Color::MUTED_TEXT);
  outer->addWidget(m_progressLabel);

  m_headerLabel = new QLabel(this);
  m_headerLabel->setWordWrap(true);
  m_headerLabel->setStyleSheet("font-weight: bold;");
  outer->addWidget(m_headerLabel);

  m_pathLabel = new QLabel(this);
  m_pathLabel->setWordWrap(true);
  m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_pathLabel->setStyleSheet(UIConstants::Color::MUTED_ITALIC_TEXT);
  outer->addWidget(m_pathLabel);

  m_missingLabel = new QLabel(this);
  m_missingLabel->setWordWrap(true);
  outer->addWidget(m_missingLabel, /*stretch=*/1);

  auto *row = new QHBoxLayout();
  m_editButton = new QPushButton(tr("Edit metadata…"), this);
  m_editButton->setDefault(true);
  m_skipButton = new QPushButton(tr("Skip"), this);
  m_closeButton = new QPushButton(tr("Close"), this);
  row->addWidget(m_editButton);
  row->addWidget(m_skipButton);
  row->addStretch(1);
  row->addWidget(m_closeButton);
  outer->addLayout(row);

  connect(m_editButton, &QPushButton::clicked, this, &MetadataReviewDialog::onEditClicked);
  connect(m_skipButton, &QPushButton::clicked, this, &MetadataReviewDialog::onSkipClicked);
  connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void MetadataReviewDialog::setQueue(QList<MetadataQueue::Entry> entries, EditHandler onEdit,
                                    ReloadHandler onReload) {
  m_entries = std::move(entries);
  m_onEdit = std::move(onEdit);
  m_onReload = std::move(onReload);
  m_cursor = 0;
  renderCurrent();
}

void MetadataReviewDialog::renderCurrent() {
  if (m_cursor < 0 || m_cursor >= m_entries.size()) {
    // Queue exhausted — surface an empty state for a single render and
    // disable the action buttons. The caller can choose to accept()
    // immediately or leave the dialog open for review of the closing
    // message.
    m_progressLabel->setText(tr("No more items to review."));
    m_headerLabel->clear();
    m_pathLabel->clear();
    m_missingLabel->clear();
    m_editButton->setEnabled(false);
    m_skipButton->setEnabled(false);
    return;
  }
  const auto &entry = m_entries.at(m_cursor);
  m_progressLabel->setText(tr("Reviewing item %1 of %2").arg(m_cursor + 1).arg(m_entries.size()));
  m_headerLabel->setText(entry.itemName.toHtmlEscaped());
  m_pathLabel->setText(entry.filePath);
  QStringList lines;
  lines.reserve(entry.missingFields.size() + 1);
  lines << tr("Missing:");
  for (const QString &field : entry.missingFields) {
    lines << QStringLiteral("  • ") + field;
  }
  m_missingLabel->setText(lines.join(QLatin1Char('\n')));
  m_editButton->setEnabled(static_cast<bool>(m_onEdit));
  m_skipButton->setEnabled(true);
}

void MetadataReviewDialog::onEditClicked() {
  if (m_cursor < 0 || m_cursor >= m_entries.size() || !m_onEdit) {
    return;
  }
  const auto &entry = m_entries.at(m_cursor);
  const QString filePath = entry.filePath;
  const QString itemName = entry.itemName;
  const QString uuid = entry.collectionUuid;
  if (!m_onEdit(filePath, itemName)) {
    return; // user cancelled, stay on the current item
  }
  // After save, re-evaluate the entry. The caller has already persisted
  // to disk by this point; we reload to see if the user actually filled
  // in the missing fields (the editor allows partial saves).
  if (m_onReload) {
    auto fresh = m_onReload(uuid, filePath);
    MetadataQueue::Entry mutableEntry = entry;
    if (MetadataQueue::reevaluate(mutableEntry, fresh)) {
      m_entries[m_cursor] = mutableEntry;
      renderCurrent(); // still missing — keep cursor where it is
      return;
    }
  }
  // Fully populated → drop from the queue and stay on the same cursor
  // (the next entry slides into this slot).
  m_entries.removeAt(m_cursor);
  renderCurrent();
}

void MetadataReviewDialog::onSkipClicked() {
  advance();
}

void MetadataReviewDialog::advance() {
  m_cursor += 1;
  renderCurrent();
}
