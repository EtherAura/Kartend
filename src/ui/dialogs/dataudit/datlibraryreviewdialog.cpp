#include "datlibraryreviewdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

DatLibraryReviewDialog::DatLibraryReviewDialog(
    const QString &libraryPath, const QList<DatCollectionMatch::CollectionInfo> &collections,
    const DatLibraryScan::ScanResult &initial, Hooks hooks, QWidget *parent)
    : QDialog(parent), m_collections(collections), m_hooks(std::move(hooks)) {
  setWindowTitle(tr("DAT library"));
  resize(720, 480);

  auto *root = new QVBoxLayout(this);

  // Library folder row: path + browse + rescan.
  auto *pathRow = new QHBoxLayout();
  pathRow->addWidget(new QLabel(tr("Library folder:"), this));
  m_libraryPath = new QLineEdit(libraryPath, this);
  auto *browse = new QPushButton(tr("Browse…"), this);
  auto *rescan = new QPushButton(tr("Rescan"), this);
  pathRow->addWidget(m_libraryPath, 1);
  pathRow->addWidget(browse);
  pathRow->addWidget(rescan);
  root->addLayout(pathRow);

  // Proposals: one row per catalogue, with a per-row target picker holding
  // every surviving candidate (best match preselected, never auto-applied).
  m_tree = new QTreeWidget(this);
  m_tree->setColumnCount(3);
  m_tree->setHeaderLabels({tr("Catalogue"), tr("Version"), tr("Attach to")});
  m_tree->setRootIsDecorated(false);
  m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  root->addWidget(m_tree, 1);

  m_status = new QLabel(this);
  root->addWidget(m_status);

  // Show-unmatched toggle (Kartend-m6qsb.24): library DATs the matcher found no
  // collection for, listed so they can be attached by hand.
  auto *optRow = new QHBoxLayout();
  m_showUnmatched = new QCheckBox(tr("Show catalogues with no match"), this);
  optRow->addWidget(m_showUnmatched);
  optRow->addStretch();
  root->addLayout(optRow);

  auto *actions = new QHBoxLayout();
  m_attachAllButton = new QPushButton(tr("Attach all best matches"), this);
  auto *attach = new QPushButton(tr("Apply selected"), this);
  auto *dismiss = new QPushButton(tr("Dismiss selected (don't ask again)"), this);
  actions->addWidget(m_attachAllButton);
  actions->addWidget(attach);
  actions->addWidget(dismiss);
  actions->addStretch();
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  actions->addWidget(buttons);
  root->addLayout(actions);

  connect(browse, &QPushButton::clicked, this, &DatLibraryReviewDialog::onBrowseLibrary);
  connect(rescan, &QPushButton::clicked, this, &DatLibraryReviewDialog::onRescan);
  connect(attach, &QPushButton::clicked, this, &DatLibraryReviewDialog::onAttachSelected);
  connect(dismiss, &QPushButton::clicked, this, &DatLibraryReviewDialog::onDismissSelected);
  connect(m_attachAllButton, &QPushButton::clicked, this,
          &DatLibraryReviewDialog::onAttachAllBestMatches);
  connect(m_showUnmatched, &QCheckBox::toggled, this, [this] { rebuildRows(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  populate(initial);
}

QString DatLibraryReviewDialog::collectionName(const QString &uuid) const {
  for (const DatCollectionMatch::CollectionInfo &c : m_collections) {
    if (c.uuid == uuid) {
      return c.name;
    }
  }
  return uuid; // unresolved — show something traceable rather than nothing
}

void DatLibraryReviewDialog::populate(const DatLibraryScan::ScanResult &result) {
  m_result = result;
  rebuildRows();
}

int DatLibraryReviewDialog::rowCount() const {
  return m_tree->topLevelItemCount();
}

void DatLibraryReviewDialog::rebuildRows() {
  m_tree->clear();
  // Matched proposals always; unmatched only when the toggle is on. m_proposals
  // stays row-aligned with the tree for the action slots.
  m_proposals = m_result.proposals;
  const bool showUnmatched = m_showUnmatched != nullptr && m_showUnmatched->isChecked();
  if (showUnmatched) {
    m_proposals.append(m_result.unmatched);
  }

  for (const DatLibraryScan::Proposal &p : m_proposals) {
    auto *item = new QTreeWidgetItem(m_tree);
    const QString label = p.headerName.isEmpty() ? QFileInfo(p.datPath).fileName() : p.headerName;
    item->setText(0, label);
    item->setToolTip(0, p.datPath);
    item->setText(1, p.headerVersion);
    // Per-row target picker. Association is optional (Kartend-m6qsb.18): two
    // synthetic choices precede the targets — keep the DAT in the library, or
    // spin up a new collection. Item data carries uuids; '' = no collection,
    // "+new" = create one.
    auto *combo = new QComboBox(m_tree);
    combo->addItem(tr("(No collection — keep in library)"), QString());
    combo->addItem(tr("Add to new collection…"), QStringLiteral("+new"));
    combo->insertSeparator(combo->count());
    const int firstTarget = combo->count();
    if (!p.candidates.isEmpty()) {
      // Matched: only the ranked candidates, best first.
      for (const DatCollectionMatch::Candidate &c : p.candidates) {
        combo->addItem(collectionName(c.collectionUuid), c.collectionUuid);
        combo->setItemData(combo->count() - 1, c.reason, Qt::ToolTipRole);
      }
      combo->setCurrentIndex(firstTarget); // best match preselected
    } else {
      // Unmatched: offer every collection for a manual pick; default to none.
      for (const DatCollectionMatch::CollectionInfo &c : m_collections) {
        combo->addItem(c.name, c.uuid);
      }
      combo->setCurrentIndex(0); // (No collection)
    }
    m_tree->setItemWidget(item, 2, combo);
  }
  m_tree->resizeColumnToContents(0);

  const int matched = static_cast<int>(m_result.proposals.size());
  QString status = tr("%n catalogue(s) with proposed matches", nullptr, matched);
  if (!m_result.unmatched.isEmpty()) {
    status += tr(" · %n with no match", nullptr, static_cast<int>(m_result.unmatched.size()));
  }
  m_status->setText(status);
  m_attachAllButton->setEnabled(matched > 0);
}

void DatLibraryReviewDialog::onAttachAllBestMatches() {
  // Attach every MATCHED row to its best candidate in one go; unmatched rows
  // (no candidate) are left for a manual pick.
  if (!m_hooks.attach) {
    return;
  }
  for (const DatLibraryScan::Proposal &p : m_result.proposals) {
    if (!p.candidates.isEmpty()) {
      m_hooks.attach(p.candidates.first().collectionUuid, p.datPath);
    }
  }
  m_result.proposals.clear();
  rebuildRows();
}

void DatLibraryReviewDialog::onBrowseLibrary() {
  const QString dir =
      QFileDialog::getExistingDirectory(this, tr("DAT library folder"), m_libraryPath->text());
  if (dir.isEmpty()) {
    return;
  }
  m_libraryPath->setText(dir);
  if (m_hooks.saveLibraryPath) {
    m_hooks.saveLibraryPath(dir);
  }
  onRescan();
}

void DatLibraryReviewDialog::onRescan() {
  if (!m_hooks.rescan) {
    return;
  }
  if (m_hooks.saveLibraryPath) {
    m_hooks.saveLibraryPath(m_libraryPath->text().trimmed());
  }
  populate(m_hooks.rescan(m_libraryPath->text().trimmed()));
}

namespace {
// Drop a proposal by datPath from both result lists so a rebuild can't
// resurrect an already-acted row.
void dropFromResult(DatLibraryScan::ScanResult &r, const QString &datPath) {
  const auto byPath = [&datPath](const DatLibraryScan::Proposal &p) {
    return p.datPath == datPath;
  };
  r.proposals.removeIf(byPath);
  r.unmatched.removeIf(byPath);
}
} // namespace

void DatLibraryReviewDialog::onAttachSelected() {
  // Snapshot each selected row's (datPath, combo choice) before mutating, since
  // a rebuild (or the "+new" sub-dialog) invalidates the row widgets.
  struct Pick {
    QString datPath;
    QString sel;
  };
  QList<Pick> picks;
  for (QTreeWidgetItem *item : m_tree->selectedItems()) {
    const int row = m_tree->indexOfTopLevelItem(item);
    if (row < 0 || row >= m_proposals.size()) {
      continue;
    }
    auto *combo = qobject_cast<QComboBox *>(m_tree->itemWidget(item, 2));
    picks.append(
        {m_proposals.at(row).datPath, combo ? combo->currentData().toString() : QString()});
  }

  for (const Pick &pick : picks) {
    bool handled = true;
    if (pick.sel == QLatin1String("+new")) {
      // Create a new collection (the hook attaches the DAT). A cancelled
      // creation leaves the row for another try.
      QString newUuid;
      if (m_hooks.addToNewCollection) {
        newUuid = m_hooks.addToNewCollection(pick.datPath);
      }
      handled = !newUuid.isEmpty();
    } else if (pick.sel.isEmpty()) {
      // (No collection): acknowledge — stays in the library, NOT dismissed, so
      // a later scan can re-propose it. Just drop it from this view.
    } else if (m_hooks.attach) {
      m_hooks.attach(pick.sel, pick.datPath);
    }
    if (handled) {
      dropFromResult(m_result, pick.datPath);
    }
  }
  rebuildRows();
}

void DatLibraryReviewDialog::onDismissSelected() {
  QList<DatLibraryScan::Proposal> picks;
  for (QTreeWidgetItem *item : m_tree->selectedItems()) {
    const int row = m_tree->indexOfTopLevelItem(item);
    if (row >= 0 && row < m_proposals.size()) {
      picks.append(m_proposals.at(row));
    }
  }
  for (const DatLibraryScan::Proposal &p : picks) {
    if (m_hooks.dismiss) {
      m_hooks.dismiss(p.canonicalPath, p.mtimeMs);
    }
    dropFromResult(m_result, p.datPath);
  }
  rebuildRows();
}
