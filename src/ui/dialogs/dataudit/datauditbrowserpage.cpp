#include "datauditbrowserpage.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>

#include "databaseschema.h"
#include "datauditbrowsermodels.h"
#include "datauditbuckets.h"
#include "datauditprofile.h"
#include "datauditresultdelegate.h"
#include "datcache.h"
#include "dattreebadgedelegate.h"
#include "errorutils.h"

namespace {

// Short-lived UI-thread connection to the app DB (same pattern as the dialog's
// withProfileDb). The browser's reads are light + occasional.
template <typename Fn> void withDb(Fn &&fn) {
  static int counter = 0; // UI thread only
  const QString conn = QStringLiteral("dataudit_browser_%1").arg(counter++);
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (DatabaseSchema::openConnection(db, dir)) {
      DatabaseSchema::applyConnectionPragmas(db);
      fn(db);
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

// Apply the shared results-table look (Kartend-m6qsb.20 idiom).
void configureTable(QTableView *t) {
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setSelectionMode(QAbstractItemView::SingleSelection);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setSortingEnabled(true);
  t->setAlternatingRowColors(true);
  t->verticalHeader()->setVisible(false);
  t->horizontalHeader()->setStretchLastSection(false);
  t->setItemDelegate(new DatAudit::DatAuditResultDelegate(t));
}

} // namespace

DatAuditBrowserPage::DatAuditBrowserPage(QWidget *parent) : QWidget(parent) {
  buildUi();
}

void DatAuditBrowserPage::buildUi() {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  m_hSplitter = new QSplitter(Qt::Horizontal, this);
  auto *split = m_hSplitter;
  split->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; }"));
  split->setChildrenCollapsible(false);

  // Left: the global tree.
  m_tree = new QTreeView(split);
  m_tree->setHeaderHidden(true);
  m_tree->setUniformRowHeights(true);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_treeModel = new DatAudit::AuditTreeModel(this);
  m_tree->setModel(m_treeModel);
  m_tree->setItemDelegate(new DatAudit::DatTreeBadgeDelegate(m_tree));

  // Right: DAT info + (game list / rom detail).
  auto *right = new QWidget(split);
  auto *rightCol = new QVBoxLayout(right);
  rightCol->setContentsMargins(0, 0, 0, 0);

  auto *infoBox = new QGroupBox(tr("DAT info"), right);
  auto *infoForm = new QFormLayout(infoBox);
  const auto makeValue = [&](QLabel *&slot) {
    slot = new QLabel(infoBox);
    slot->setTextInteractionFlags(Qt::TextSelectableByMouse);
    slot->setWordWrap(true);
    return slot;
  };
  infoForm->addRow(tr("Name:"), makeValue(m_infoName));
  infoForm->addRow(tr("Description:"), makeValue(m_infoDescription));
  infoForm->addRow(tr("Version:"), makeValue(m_infoVersion));
  infoForm->addRow(tr("Path:"), makeValue(m_infoPath));
  infoForm->addRow(tr("Status:"), makeValue(m_infoCounts));
  rightCol->addWidget(infoBox);

  m_vSplitter = new QSplitter(Qt::Vertical, right);
  auto *detailSplit = m_vSplitter;
  detailSplit->setStyleSheet(QStringLiteral("QSplitter::handle { background: transparent; }"));
  detailSplit->setChildrenCollapsible(false);

  m_gameModel = new DatAudit::GameListModel(this);
  m_gameProxy = new DatAudit::GameListFilterProxy(this);
  m_gameProxy->setSourceModel(m_gameModel);
  m_gameTable = new QTableView(detailSplit);
  m_gameTable->setModel(m_gameProxy);
  configureTable(m_gameTable);
  m_gameTable->horizontalHeader()->setSectionResizeMode(DatAudit::GameListModel::GameColumn,
                                                        QHeaderView::Stretch);

  m_romModel = new DatAudit::RomFileModel(this);
  m_romTable = new QTableView(detailSplit);
  m_romTable->setModel(m_romModel);
  configureTable(m_romTable);
  m_romTable->horizontalHeader()->setSectionResizeMode(DatAudit::RomFileModel::RomColumn,
                                                       QHeaderView::Stretch);

  detailSplit->addWidget(m_gameTable);
  detailSplit->addWidget(m_romTable);
  detailSplit->setSizes({300, 300});
  rightCol->addWidget(detailSplit, 1);

  split->addWidget(m_tree);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  split->setSizes({320, 640});
  root->addWidget(split, 1);

  // Filter row.
  auto *filterRow = new QHBoxLayout();
  // Set each checkbox's initial state BEFORE wiring toggled→applyFilters().
  // Otherwise flipping a checkbox during construction fires applyFilters()
  // while a sibling checkbox is still null — applyFilters() reads all five, so
  // that crashes. The proxy's defaults already match these states, so no
  // initial applyFilters() call is needed.
  const auto makeCheck = [&](QCheckBox *&slot, const QString &text, bool checked) {
    slot = new QCheckBox(text, this);
    slot->setChecked(checked);
    filterRow->addWidget(slot);
    connect(slot, &QCheckBox::toggled, this, &DatAuditBrowserPage::applyFilters);
  };
  makeCheck(m_filterComplete, tr("Complete"), true);
  makeCheck(m_filterPartial, tr("Partial"), true);
  makeCheck(m_filterEmpty, tr("Empty"), true);
  // Fixes / MIA default off — they are "only show rows that have…" gates.
  makeCheck(m_filterFixes, tr("Fixes"), false);
  makeCheck(m_filterMia, tr("MIA"), false);
  filterRow->addStretch(1);
  m_search = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Filter games…"));
  m_search->setClearButtonEnabled(true);
  m_search->setMaximumWidth(220);
  filterRow->addWidget(m_search);
  root->addLayout(filterRow);

  connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
          &DatAuditBrowserPage::onTreeSelectionChanged);
  connect(m_gameTable->selectionModel(), &QItemSelectionModel::currentChanged, this,
          &DatAuditBrowserPage::onGameSelectionChanged);
  connect(m_search, &QLineEdit::textChanged, this,
          [this](const QString &t) { m_gameProxy->setFilterFixedString(t); });

  clearDatInfo();
}

void DatAuditBrowserPage::refresh() {
  QList<DatAuditProfile::Profile> profiles;
  QList<DatAuditProfile::RollupRow> rollups;
  withDb([&](QSqlDatabase &db) {
    if (auto p = DatAuditProfile::listAll(db); p.isOk()) {
      profiles = p.value();
    }
    if (auto r = DatAuditProfile::loadAllRollups(db); r.isOk()) {
      rollups = r.value();
    } else {
      ErrorUtils::logError(r.error());
    }
  });
  m_treeModel->setTree(profiles, rollups);
  clearDatInfo();
  m_gameModel->clear();
  m_romModel->clear();
  m_currentProfileId = -1;
  m_currentSourceName.clear();
  m_currentDatPath.clear();
}

void DatAuditBrowserPage::clearDatInfo() {
  const QString dash = QStringLiteral("—");
  m_infoName->setText(dash);
  m_infoDescription->setText(dash);
  m_infoVersion->setText(dash);
  m_infoPath->setText(dash);
  m_infoCounts->setText(dash);
}

void DatAuditBrowserPage::onTreeSelectionChanged() {
  const QModelIndex idx = m_tree->currentIndex();
  if (!idx.isValid()) {
    clearDatInfo();
    m_gameModel->clear();
    m_romModel->clear();
    return;
  }
  const qint64 profileId = m_treeModel->profileIdAt(idx);
  const QString sourceName = m_treeModel->sourceNameAt(idx);
  const QString datPath = m_treeModel->datPathAt(idx);

  // DAT info: name from the node label; header from the cache peek; counts from
  // the node's rollup.
  m_infoName->setText(idx.data(Qt::DisplayRole).toString());
  QString desc = QStringLiteral("—");
  QString version = QStringLiteral("—");
  if (!datPath.isEmpty()) {
    DatCache::Store cache(DatCache::defaultPath());
    if (const auto src = cache.peek(datPath)) {
      if (!src->headerDescription.isEmpty()) {
        desc = src->headerDescription;
      }
      if (!src->headerVersion.isEmpty()) {
        version = src->headerVersion;
      }
    }
  }
  m_infoDescription->setText(desc);
  m_infoVersion->setText(version);
  m_infoPath->setText(datPath.isEmpty() ? QStringLiteral("—") : datPath);

  const auto counts =
      qvariant_cast<DatAudit::BucketCounts>(idx.data(DatAudit::AuditTreeModel::CountsRole));
  if (idx.data(DatAudit::AuditTreeModel::UnscannedRole).toBool()) {
    m_infoCounts->setText(tr("Not yet audited"));
  } else {
    m_infoCounts->setText(tr("Have %1   Missing %2   Fixable %3   MIA %4   (%5 total)")
                              .arg(counts.have)
                              .arg(counts.missing)
                              .arg(counts.fixable)
                              .arg(counts.mia)
                              .arg(counts.total));
  }

  if (!sourceName.isEmpty()) {
    loadGamesFor(profileId, sourceName, datPath);
  } else {
    // A multi-source profile node — pick a source child to see its games.
    m_gameModel->clear();
    m_romModel->clear();
    m_currentProfileId = -1;
    m_currentSourceName.clear();
  }
}

void DatAuditBrowserPage::loadGamesFor(qint64 profileId, const QString &sourceName,
                                       const QString &datPath) {
  m_currentProfileId = profileId;
  m_currentSourceName = sourceName;
  m_currentDatPath = datPath;
  m_romModel->clear();
  QList<DatAuditProfile::GameRollupRow> games;
  withDb([&](QSqlDatabase &db) {
    if (auto g = DatAuditProfile::loadGameRollups(db, profileId, sourceName); g.isOk()) {
      games = g.value();
    } else {
      ErrorUtils::logError(g.error());
    }
  });
  m_gameModel->setGames(games);
}

void DatAuditBrowserPage::onGameSelectionChanged() {
  const QModelIndex proxyIdx = m_gameTable->currentIndex();
  if (!proxyIdx.isValid() || m_currentProfileId < 0) {
    m_romModel->clear();
    return;
  }
  const int sourceRow = m_gameProxy->mapToSource(proxyIdx).row();
  const QString gameName = m_gameModel->gameNameAt(sourceRow);
  if (gameName.isEmpty()) {
    m_romModel->clear();
    return;
  }

  QList<DatAuditProfile::ResultRow> results;
  withDb([&](QSqlDatabase &db) {
    if (auto r = DatAuditProfile::loadGameResultRows(db, m_currentProfileId, m_currentSourceName,
                                                     gameName);
        r.isOk()) {
      results = r.value();
    }
  });

  QList<DatLookup::DatRecord> records;
  if (!m_currentDatPath.isEmpty()) {
    DatCache::Store cache(DatCache::defaultPath());
    if (const auto src = cache.peek(m_currentDatPath)) {
      records = cache.recordsForGame(*src, gameName);
    }
  }
  m_romModel->setGame(records, results);
  m_romTable->resizeColumnsToContents();
}

void DatAuditBrowserPage::applyFilters() {
  m_gameProxy->setStateFilter(m_filterComplete->isChecked(), m_filterPartial->isChecked(),
                              m_filterEmpty->isChecked());
  m_gameProxy->setRequireFixes(m_filterFixes->isChecked());
  m_gameProxy->setRequireMia(m_filterMia->isChecked());
}

// Layout persistence (Kartend-o46gy). Keys live under the same
// kartend/ui-state "DatManagerWindow" group the dialog uses for its own
// geometry, namespaced with a "browser" prefix so they don't collide.
void DatAuditBrowserPage::persistState() const {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(QStringLiteral("DatManagerWindow"));
  if (m_hSplitter != nullptr) {
    settings.setValue(QStringLiteral("browserHSplitter"), m_hSplitter->saveState());
  }
  if (m_vSplitter != nullptr) {
    settings.setValue(QStringLiteral("browserVSplitter"), m_vSplitter->saveState());
  }
  if (m_tree != nullptr) {
    settings.setValue(QStringLiteral("browserTreeHeader"), m_tree->header()->saveState());
  }
  if (m_gameTable != nullptr) {
    settings.setValue(QStringLiteral("browserGameHeader"),
                      m_gameTable->horizontalHeader()->saveState());
  }
  if (m_romTable != nullptr) {
    settings.setValue(QStringLiteral("browserRomHeader"),
                      m_romTable->horizontalHeader()->saveState());
  }
  settings.setValue(QStringLiteral("browserFilterComplete"), m_filterComplete->isChecked());
  settings.setValue(QStringLiteral("browserFilterPartial"), m_filterPartial->isChecked());
  settings.setValue(QStringLiteral("browserFilterEmpty"), m_filterEmpty->isChecked());
  settings.setValue(QStringLiteral("browserFilterFixes"), m_filterFixes->isChecked());
  settings.setValue(QStringLiteral("browserFilterMia"), m_filterMia->isChecked());
  settings.endGroup();
}

void DatAuditBrowserPage::restoreState_() {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  settings.beginGroup(QStringLiteral("DatManagerWindow"));

  const auto restoreState = [&](auto *target, const QString &key) {
    const QByteArray st = settings.value(key).toByteArray();
    if (target != nullptr && !st.isEmpty()) {
      target->restoreState(st);
    }
  };
  restoreState(m_hSplitter, QStringLiteral("browserHSplitter"));
  restoreState(m_vSplitter, QStringLiteral("browserVSplitter"));
  restoreState(m_tree != nullptr ? m_tree->header() : nullptr, QStringLiteral("browserTreeHeader"));
  restoreState(m_gameTable != nullptr ? m_gameTable->horizontalHeader() : nullptr,
               QStringLiteral("browserGameHeader"));
  restoreState(m_romTable != nullptr ? m_romTable->horizontalHeader() : nullptr,
               QStringLiteral("browserRomHeader"));

  // Filter checkboxes: only override the build-time defaults when a value was
  // saved. Block each toggle so the five restores don't each fire applyFilters()
  // — a single applyFilters() below picks up the final state.
  const auto restoreCheck = [&](QCheckBox *box, const QString &key) {
    if (box != nullptr && settings.contains(key)) {
      const QSignalBlocker blocker(box);
      box->setChecked(settings.value(key).toBool());
    }
  };
  restoreCheck(m_filterComplete, QStringLiteral("browserFilterComplete"));
  restoreCheck(m_filterPartial, QStringLiteral("browserFilterPartial"));
  restoreCheck(m_filterEmpty, QStringLiteral("browserFilterEmpty"));
  restoreCheck(m_filterFixes, QStringLiteral("browserFilterFixes"));
  restoreCheck(m_filterMia, QStringLiteral("browserFilterMia"));
  settings.endGroup();

  applyFilters();
}
