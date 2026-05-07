#include "kartmergedialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>

namespace {

QString summarize(const QString &s) {
  if (s.isEmpty()) return QObject::tr("(empty)");
  return s.length() <= 60 ? s : s.left(57) + "...";
}

} // namespace

KartMergeDialog::KartMergeDialog(const QString &itemPath,
                                 const ItemMetadataStore::ItemMetadata &existing,
                                 const ItemMetadataStore::ItemMetadata &incoming, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("Item already exists"));
  setModal(true);

  auto *root = new QVBoxLayout(this);

  auto *header = new QLabel(
      tr("<b>%1</b><br/>This item already has metadata. Choose how to handle it.").arg(itemPath),
      this);
  header->setWordWrap(true);
  root->addWidget(header);

  auto *fields = new QGroupBox(tr("Merge: pick which side wins per field"), this);
  fields->setEnabled(false);
  auto *form = new QFormLayout(fields);
  auto addRow = [&](const QString &label, const QString &existingVal, const QString &incomingVal,
                    bool *flag) {
    auto *check = new QCheckBox(tr("use incoming"), this);
    check->setChecked(false);
    auto *summary = new QLabel(
        tr("existing: %1<br/>incoming: %2").arg(summarize(existingVal), summarize(incomingVal)),
        this);
    summary->setStyleSheet("color: gray;");
    auto *cell = new QWidget(this);
    auto *cellLayout = new QVBoxLayout(cell);
    cellLayout->setContentsMargins(0, 0, 0, 0);
    cellLayout->addWidget(check);
    cellLayout->addWidget(summary);
    form->addRow(label, cell);
    m_rows.append({check, flag});
  };
  addRow(tr("Title"), existing.title, incoming.title, &m_policy.preferIncomingTitle);
  addRow(tr("Description"), existing.description, incoming.description,
         &m_policy.preferIncomingDescription);
  addRow(tr("Genre"), existing.genre, incoming.genre, &m_policy.preferIncomingGenre);
  addRow(tr("Developer"), existing.developer, incoming.developer,
         &m_policy.preferIncomingDeveloper);
  addRow(tr("Publisher"), existing.publisher, incoming.publisher,
         &m_policy.preferIncomingPublisher);
  addRow(tr("Release date"), existing.releaseDate, incoming.releaseDate,
         &m_policy.preferIncomingReleaseDate);
  addRow(tr("Content rating"), existing.contentRating, incoming.contentRating,
         &m_policy.preferIncomingContentRating);
  addRow(tr("Players"), existing.players, incoming.players, &m_policy.preferIncomingPlayers);
  addRow(tr("Runtime"),
         existing.runtimeSeconds >= 0 ? QString::number(existing.runtimeSeconds) : QString(),
         incoming.runtimeSeconds >= 0 ? QString::number(incoming.runtimeSeconds) : QString(),
         &m_policy.preferIncomingRuntimeSeconds);
  addRow(tr("Tags"), existing.tags, incoming.tags, &m_policy.preferIncomingTags);
  addRow(tr("Custom fields"), existing.customFields, incoming.customFields,
         &m_policy.preferIncomingCustomFields);
  addRow(tr("Manual path"), existing.manualPath, incoming.manualPath,
         &m_policy.preferIncomingManualPath);
  addRow(tr("Launcher override"),
         existing.launcherIndex >= 0 ? QString::number(existing.launcherIndex) : QString(),
         incoming.launcherIndex >= 0 ? QString::number(incoming.launcherIndex) : QString(),
         &m_policy.preferIncomingLauncherIndex);
  addRow(tr("Source"), existing.source, incoming.source, &m_policy.preferIncomingSource);
  root->addWidget(fields);

  m_applyToAllCheck = new QCheckBox(tr("Apply this choice to all remaining conflicts"), this);
  root->addWidget(m_applyToAllCheck);

  auto *buttons = new QDialogButtonBox(this);
  auto *skipBtn = buttons->addButton(tr("Skip"), QDialogButtonBox::RejectRole);
  auto *overwriteBtn = buttons->addButton(tr("Overwrite"), QDialogButtonBox::DestructiveRole);
  auto *mergeBtn = buttons->addButton(tr("Merge..."), QDialogButtonBox::ActionRole);
  root->addWidget(buttons);

  connect(skipBtn, &QPushButton::clicked, this, [this] {
    m_choice = kart::MergeChoice::Skip;
    accept();
  });
  connect(overwriteBtn, &QPushButton::clicked, this, [this] {
    m_choice = kart::MergeChoice::Overwrite;
    accept();
  });
  connect(mergeBtn, &QPushButton::clicked, this, [this, fields] {
    if (!fields->isEnabled()) {
      fields->setEnabled(true);
      return;
    }
    m_choice = kart::MergeChoice::Merge;
    for (const FieldRow &row : std::as_const(m_rows)) {
      if (row.policyFlag) {
        *row.policyFlag = row.preferIncoming->isChecked();
      }
    }
    accept();
  });
}

bool KartMergeDialog::applyToAll() const {
  return m_applyToAllCheck && m_applyToAllCheck->isChecked();
}
