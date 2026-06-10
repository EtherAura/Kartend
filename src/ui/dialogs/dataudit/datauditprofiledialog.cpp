#include "datauditprofiledialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"
#include "datauditregion.h"
#include "pathutils.h"

using DatAuditProfile::FixMode;
using DatAuditProfile::MergeMode;

namespace {

// A group box: a multi-select list with Add / Remove buttons beneath it.
QGroupBox *pathGroup(const QString &title, const QString &addText, QListWidget *&listOut,
                     QPushButton *&addOut, QPushButton *&removeOut, QWidget *parent) {
  auto *box = new QGroupBox(title, parent);
  auto *v = new QVBoxLayout(box);
  listOut = new QListWidget(box);
  listOut->setSelectionMode(QAbstractItemView::ExtendedSelection);
  v->addWidget(listOut);
  auto *row = new QHBoxLayout();
  addOut = new QPushButton(addText, box);
  removeOut = new QPushButton(QObject::tr("Remove"), box);
  row->addWidget(addOut);
  row->addWidget(removeOut);
  row->addStretch();
  v->addLayout(row);
  return box;
}

} // namespace

DatAuditProfileDialog::DatAuditProfileDialog(const DatAuditProfile::Profile &seed,
                                             const QList<CollectionConfig> *collections,
                                             QWidget *parent)
    : QDialog(parent), m_seed(seed) {
  setWindowTitle(m_seed.id < 0 ? tr("New DAT profile") : tr("Edit DAT profile"));
  resize(660, 680);

  auto *root = new QVBoxLayout(this);

  // Name + optional collection link.
  auto *form = new QFormLayout();
  m_name = new QLineEdit(m_seed.name, this);
  form->addRow(tr("Name:"), m_name);

  // "(none)" plus one entry per collection, each carrying its computed UUID as
  // item data (same name+expanded-media-dir SHA1 the rest of the app keys on,
  // so the link resolves elsewhere). The entry matching the seed's stored UUID
  // is preselected; a stored link to a collection no longer in the list is kept
  // under a trailing "(unavailable)" entry so saving never silently drops it.
  m_collection = new QComboBox(this);
  m_collection->addItem(tr("(none)"), QString());
  bool linkResolved = m_seed.collectionUuid.isEmpty();
  if (collections != nullptr) {
    for (const CollectionConfig &c : *collections) {
      const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      const QString uuid = CollectionUtils::computeCollectionUuid(c.name, expanded);
      m_collection->addItem(c.name, uuid);
      if (!linkResolved && uuid == m_seed.collectionUuid) {
        m_collection->setCurrentIndex(m_collection->count() - 1);
        linkResolved = true;
      }
    }
  }
  if (!linkResolved) {
    m_collection->addItem(tr("(linked collection unavailable)"), m_seed.collectionUuid);
    m_collection->setCurrentIndex(m_collection->count() - 1);
  }
  form->addRow(tr("Linked collection:"), m_collection);
  root->addLayout(form);

  // DAT files + scan folders.
  QPushButton *addDat = nullptr, *removeDat = nullptr, *addRoot = nullptr, *removeRoot = nullptr;
  auto *inputs = new QHBoxLayout();
  inputs->addWidget(pathGroup(tr("DAT files"), tr("Add DAT…"), m_datList, addDat, removeDat, this));
  inputs->addWidget(
      pathGroup(tr("Scan folders"), tr("Add folder…"), m_rootList, addRoot, removeRoot, this));
  root->addLayout(inputs);
  for (const DatAuditProfile::DatRef &d : m_seed.dats) {
    m_datList->addItem(d.path);
  }
  m_rootList->addItems(m_seed.scanRoots);

  // Catalogue options: merge mode + 1G1R region priority.
  auto *cat = new QGroupBox(tr("Catalogue options"), this);
  auto *catV = new QVBoxLayout(cat);

  auto *mergeRow = new QHBoxLayout();
  mergeRow->addWidget(new QLabel(tr("Merge mode:"), cat));
  m_mergeMode = new QComboBox(cat);
  m_mergeMode->addItem(tr("Split"), int(MergeMode::Split));
  m_mergeMode->addItem(tr("Merged"), int(MergeMode::Merged));
  m_mergeMode->addItem(tr("Non-merged"), int(MergeMode::NonMerged));
  m_mergeMode->setCurrentIndex(qMax(0, m_mergeMode->findData(int(m_seed.mergeMode))));
  mergeRow->addWidget(m_mergeMode);
  mergeRow->addStretch();
  catV->addLayout(mergeRow);

  m_onePerGame = new QCheckBox(tr("One ROM per game (1G1R) — keep only the preferred region"), cat);
  m_onePerGame->setChecked(m_seed.onePerGame);
  catV->addWidget(m_onePerGame);

  // Region priority: a list (top = most preferred) with a known-region picker
  // and add / remove / reorder controls.
  auto *regRow = new QHBoxLayout();
  m_regionList = new QListWidget(cat);
  m_regionList->addItems(m_seed.regionPrefs);
  regRow->addWidget(m_regionList, 1);
  auto *regCol = new QVBoxLayout();
  m_regionToAdd = new QComboBox(cat);
  m_regionToAdd->addItems(DatAudit::Region::knownRegions());
  auto *addReg = new QPushButton(tr("Add"), cat);
  auto *removeReg = new QPushButton(tr("Remove"), cat);
  auto *upReg = new QPushButton(tr("Move up"), cat);
  auto *downReg = new QPushButton(tr("Move down"), cat);
  regCol->addWidget(m_regionToAdd);
  regCol->addWidget(addReg);
  regCol->addWidget(removeReg);
  regCol->addWidget(upReg);
  regCol->addWidget(downReg);
  regCol->addStretch();
  regRow->addLayout(regCol);
  catV->addWidget(new QLabel(tr("Region priority (most preferred first):"), cat));
  catV->addLayout(regRow);
  root->addWidget(cat);

  // Ignore globs.
  auto *ign = new QGroupBox(tr("Ignore globs (one per line)"), this);
  auto *ignV = new QVBoxLayout(ign);
  m_ignoreRules = new QPlainTextEdit(ign);
  m_ignoreRules->setPlainText(m_seed.ignoreRules.join(QLatin1Char('\n')));
  m_ignoreRules->setPlaceholderText(QStringLiteral("*.txt\n*.nfo"));
  m_ignoreRules->setMaximumHeight(80);
  ignV->addWidget(m_ignoreRules);
  root->addWidget(ign);

  // Fix output mode.
  auto *fix = new QGroupBox(tr("Fix output"), this);
  auto *fixV = new QVBoxLayout(fix);
  m_fixInPlace = new QRadioButton(tr("In place — rename/reorganize the scanned folders"), fix);
  m_fixManaged =
      new QRadioButton(tr("Managed output — build a clean set into a separate folder"), fix);
  (m_seed.fixMode == FixMode::ManagedOutput ? m_fixManaged : m_fixInPlace)->setChecked(true);
  fixV->addWidget(m_fixInPlace);
  fixV->addWidget(m_fixManaged);
  auto *mrow = new QHBoxLayout();
  mrow->addWidget(new QLabel(tr("Output root:"), fix));
  m_managedRoot = new QLineEdit(m_seed.managedOutputRoot, fix);
  m_managedBrowse = new QPushButton(tr("Browse…"), fix);
  mrow->addWidget(m_managedRoot, 1);
  mrow->addWidget(m_managedBrowse);
  fixV->addLayout(mrow);
  root->addWidget(fix);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  root->addWidget(buttons);

  connect(addDat, &QPushButton::clicked, this, &DatAuditProfileDialog::onAddDat);
  connect(removeDat, &QPushButton::clicked, this, [this] { removeSelected(m_datList); });
  connect(addRoot, &QPushButton::clicked, this, &DatAuditProfileDialog::onAddRoot);
  connect(removeRoot, &QPushButton::clicked, this, [this] { removeSelected(m_rootList); });
  connect(addReg, &QPushButton::clicked, this, &DatAuditProfileDialog::onAddRegion);
  connect(removeReg, &QPushButton::clicked, this, [this] { removeSelected(m_regionList); });
  connect(upReg, &QPushButton::clicked, this, &DatAuditProfileDialog::onMoveRegionUp);
  connect(downReg, &QPushButton::clicked, this, &DatAuditProfileDialog::onMoveRegionDown);
  connect(m_managedBrowse, &QPushButton::clicked, this,
          &DatAuditProfileDialog::onBrowseManagedRoot);
  connect(m_fixInPlace, &QRadioButton::toggled, this, &DatAuditProfileDialog::onFixModeChanged);
  connect(buttons, &QDialogButtonBox::accepted, this, &DatAuditProfileDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  onFixModeChanged(); // initial enabled state of the managed-output row
}

void DatAuditProfileDialog::removeSelected(QListWidget *list) {
  const auto items = list->selectedItems();
  for (QListWidgetItem *item : items) {
    delete list->takeItem(list->row(item));
  }
}

void DatAuditProfileDialog::onAddDat() {
  const QStringList files = QFileDialog::getOpenFileNames(
      this, tr("Add DAT files"), QString(), tr("DAT files (*.dat *.xml);;All files (*)"));
  for (const QString &f : files) {
    if (!f.isEmpty() && m_datList->findItems(f, Qt::MatchExactly).isEmpty()) {
      m_datList->addItem(f);
    }
  }
}

void DatAuditProfileDialog::onAddRoot() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Add scan folder"));
  if (!dir.isEmpty() && m_rootList->findItems(dir, Qt::MatchExactly).isEmpty()) {
    m_rootList->addItem(dir);
  }
}

void DatAuditProfileDialog::onAddRegion() {
  const QString region = m_regionToAdd->currentText();
  if (region.isEmpty() || !m_regionList->findItems(region, Qt::MatchExactly).isEmpty()) {
    return; // empty or already in the priority list
  }
  m_regionList->addItem(region);
}

void DatAuditProfileDialog::onMoveRegionUp() {
  const int row = m_regionList->currentRow();
  if (row > 0) {
    m_regionList->insertItem(row - 1, m_regionList->takeItem(row));
    m_regionList->setCurrentRow(row - 1);
  }
}

void DatAuditProfileDialog::onMoveRegionDown() {
  const int row = m_regionList->currentRow();
  if (row >= 0 && row < m_regionList->count() - 1) {
    m_regionList->insertItem(row + 1, m_regionList->takeItem(row));
    m_regionList->setCurrentRow(row + 1);
  }
}

void DatAuditProfileDialog::onBrowseManagedRoot() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Managed output root"));
  if (!dir.isEmpty()) {
    m_managedRoot->setText(dir);
  }
}

void DatAuditProfileDialog::onFixModeChanged() {
  const bool managed = m_fixManaged->isChecked();
  m_managedRoot->setEnabled(managed);
  m_managedBrowse->setEnabled(managed);
}

void DatAuditProfileDialog::accept() {
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, tr("DAT profile"), tr("Give the profile a name."));
    return;
  }
  if (m_fixManaged->isChecked() && m_managedRoot->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, tr("DAT profile"),
                         tr("Managed-output mode needs an output root folder."));
    return;
  }
  QDialog::accept();
}

DatAuditProfile::Profile DatAuditProfileDialog::profile() const {
  DatAuditProfile::Profile p = m_seed; // carry id / timestamps through
  p.name = m_name->text().trimmed();
  // The picker always carries the chosen (or preserved) UUID as item data, so a
  // null guard is enough; "(none)" stores an empty string, clearing the link.
  p.collectionUuid = m_collection ? m_collection->currentData().toString() : m_seed.collectionUuid;

  p.dats.clear();
  for (int i = 0; i < m_datList->count(); ++i) {
    DatAuditProfile::DatRef ref;
    ref.path = m_datList->item(i)->text();
    p.dats.append(ref);
  }
  p.scanRoots.clear();
  for (int i = 0; i < m_rootList->count(); ++i) {
    p.scanRoots.append(m_rootList->item(i)->text());
  }

  p.mergeMode = MergeMode(m_mergeMode->currentData().toInt());
  p.onePerGame = m_onePerGame->isChecked();
  p.regionPrefs.clear();
  for (int i = 0; i < m_regionList->count(); ++i) {
    p.regionPrefs.append(m_regionList->item(i)->text());
  }

  p.ignoreRules.clear();
  const QStringList lines = m_ignoreRules->toPlainText().split(QLatin1Char('\n'));
  for (const QString &line : lines) {
    const QString t = line.trimmed();
    if (!t.isEmpty()) {
      p.ignoreRules.append(t);
    }
  }

  p.fixMode = m_fixManaged->isChecked() ? FixMode::ManagedOutput : FixMode::InPlace;
  p.managedOutputRoot = m_managedRoot->text().trimmed();
  return p;
}
