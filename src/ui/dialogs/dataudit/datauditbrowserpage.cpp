#include "datauditbrowserpage.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QProgressBar>
#include <QPushButton>
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

  auto *split = new QSplitter(Qt::Horizontal, this);
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
  // Write-actions (Kartend-7iqhl.2): right-click a node for profile-scoped
  // Re-audit / Fix. The dialog owns the runner + Fix dialog, so we only emit.
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_tree, &QTreeView::customContextMenuRequested, this,
          &DatAuditBrowserPage::onTreeContextMenu);

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

  auto *detailSplit = new QSplitter(Qt::Vertical, right);
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
  // Group-by-folder is not a status filter — it regroups the game list by item
  // folder (Kartend-m6qsb.30), so reload the current source on toggle rather
  // than wiring it to applyFilters().
  m_groupByFolder = new QCheckBox(tr("Group by folder"), this);
  filterRow->addWidget(m_groupByFolder);
  connect(m_groupByFolder, &QCheckBox::toggled, this, [this](bool) {
    if (m_currentProfileId >= 0 && !m_currentSourceName.isEmpty()) {
      loadGamesFor(m_currentProfileId, m_currentSourceName, m_currentDatPath);
    }
  });
  // Group-by-category (Kartend-7iqhl.5) restructures the TREE, so rebuild it on
  // toggle (refresh() re-reads rollups and re-runs setTree with the new flag).
  m_groupByCategory = new QCheckBox(tr("Group by category"), this);
  m_groupByCategory->setToolTip(tr("Add a category level to the tree, from each profile's category "
                                   "(or its linked collection)."));
  filterRow->addWidget(m_groupByCategory);
  connect(m_groupByCategory, &QCheckBox::toggled, this, [this](bool) { refresh(); });

  // Named view presets (Kartend-7iqhl.1): a combo of four named slots that each
  // captures the live filter view, with Save (overwrite the selected slot) and
  // Rename buttons. Selecting a slot recalls it. Wire the combo BEFORE
  // loadPresets() populates it — loadPresets blocks the combo's signals so the
  // initial fill does not recall a preset (the page opens at its defaults).
  filterRow->addWidget(new QLabel(tr("Preset:"), this));
  m_presetCombo = new QComboBox(this);
  m_presetCombo->setToolTip(tr("Select a saved view to recall it."));
  filterRow->addWidget(m_presetCombo);
  m_presetSave = new QPushButton(tr("Save"), this);
  m_presetSave->setToolTip(tr("Overwrite the selected preset with the current filters."));
  filterRow->addWidget(m_presetSave);
  m_presetRename = new QPushButton(tr("Rename"), this);
  m_presetRename->setToolTip(tr("Rename the selected preset."));
  filterRow->addWidget(m_presetRename);
  connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &DatAuditBrowserPage::recallPreset);
  connect(m_presetSave, &QPushButton::clicked, this, &DatAuditBrowserPage::saveCurrentToPreset);
  connect(m_presetRename, &QPushButton::clicked, this, &DatAuditBrowserPage::renameSelectedPreset);

  filterRow->addStretch(1);
  m_search = new QLineEdit(this);
  m_search->setPlaceholderText(tr("Filter games…"));
  m_search->setClearButtonEnabled(true);
  m_search->setMaximumWidth(220);
  filterRow->addWidget(m_search);
  root->addLayout(filterRow);

  // Inline progress for a browser-initiated re-audit (Kartend-7iqhl.3); hidden
  // until the dialog reports an audit is running on this page.
  m_auditProgress = new QProgressBar(this);
  m_auditProgress->setVisible(false);
  m_auditProgress->setTextVisible(true);
  m_auditProgress->setFormat(tr("Re-auditing… %p%"));
  root->addWidget(m_auditProgress);

  connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
          &DatAuditBrowserPage::onTreeSelectionChanged);
  connect(m_gameTable->selectionModel(), &QItemSelectionModel::currentChanged, this,
          &DatAuditBrowserPage::onGameSelectionChanged);
  connect(m_search, &QLineEdit::textChanged, this,
          [this](const QString &t) { m_gameProxy->setFilterFixedString(t); });

  loadPresets();
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
  const bool groupByCategory = m_groupByCategory != nullptr && m_groupByCategory->isChecked();
  m_treeModel->setTree(profiles, rollups, groupByCategory, m_collectionNamesByUuid);
  // Cache each profile's scan roots so the folder-as-item view can map a result
  // row's absolute filePath back to its item-folder (Kartend-m6qsb.30).
  m_scanRootsByProfile.clear();
  for (const DatAuditProfile::Profile &p : profiles) {
    m_scanRootsByProfile.insert(p.id, p.scanRoots);
  }
  clearDatInfo();
  m_gameModel->clear();
  m_romModel->clear();
  m_currentProfileId = -1;
  m_currentSourceName.clear();
  m_currentDatPath.clear();
}

void DatAuditBrowserPage::setCollectionNames(const QHash<QString, QString> &byUuid) {
  m_collectionNamesByUuid = byUuid;
}

void DatAuditBrowserPage::setAuditRunning(bool running) {
  if (m_auditProgress == nullptr) {
    return;
  }
  if (running) {
    m_auditProgress->setRange(0, 0); // indeterminate until the first real tick
  }
  m_auditProgress->setVisible(running);
}

void DatAuditBrowserPage::setAuditProgress(int done, int total) {
  if (m_auditProgress == nullptr || total <= 0) {
    return;
  }
  m_auditProgress->setRange(0, total);
  m_auditProgress->setValue(done);
}

void DatAuditBrowserPage::selectProfileNode(qint64 profileId) {
  if (profileId < 0) {
    return;
  }
  // Profiles are top-level in flat mode but one level down (under Category nodes,
  // whose own profileId is -1) when "Group by category" is on (Kartend-7iqhl.5),
  // so descend into each top-level node's children too. setCurrentIndex fires
  // currentChanged → onTreeSelectionChanged, which reloads the right panes.
  const int topRows = m_treeModel->rowCount(QModelIndex());
  for (int r = 0; r < topRows; ++r) {
    const QModelIndex top = m_treeModel->index(r, 0, QModelIndex());
    if (m_treeModel->profileIdAt(top) == profileId) {
      m_tree->setCurrentIndex(top);
      return;
    }
    const int childRows = m_treeModel->rowCount(top);
    for (int c = 0; c < childRows; ++c) {
      const QModelIndex child = m_treeModel->index(c, 0, top);
      if (m_treeModel->profileIdAt(child) == profileId) {
        m_tree->expand(top); // reveal the profile under its (collapsed) category
        m_tree->setCurrentIndex(child);
        return;
      }
    }
  }
}

void DatAuditBrowserPage::onTreeContextMenu(const QPoint &pos) {
  const QModelIndex idx = m_tree->indexAt(pos);
  if (!idx.isValid()) {
    return;
  }
  const qint64 profileId = m_treeModel->profileIdAt(idx);
  if (profileId < 0) {
    return;
  }
  // Profile-scoped (Kartend-7iqhl.2): label the menu with the owning profile's
  // name. Resolve by node KIND, not parent validity — under category grouping
  // (Kartend-7iqhl.5) a leaf Profile node's parent is the Category, so the old
  // parent()-validity heuristic would mislabel it with the category name.
  const bool isSource = idx.data(DatAudit::AuditTreeModel::KindRole).toInt() ==
                        static_cast<int>(DatAudit::AuditTreeModel::NodeKind::Source);
  const QModelIndex profileIdx = isSource ? idx.parent() : idx;
  const QString profileName = profileIdx.data(Qt::DisplayRole).toString();

  QMenu menu(this);
  QAction *reaudit = menu.addAction(tr("Re-audit \"%1\"").arg(profileName));
  QAction *fix = menu.addAction(tr("Fix \"%1\"…").arg(profileName));
  const QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
  if (chosen == reaudit) {
    emit reauditProfileRequested(profileId);
  } else if (chosen == fix) {
    emit fixProfileRequested(profileId);
  }
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
  if (m_groupByFolder != nullptr && m_groupByFolder->isChecked()) {
    // Folder-as-item view: regroup the whole source's rows by item-folder.
    QList<DatAuditProfile::ResultRow> results;
    withDb([&](QSqlDatabase &db) {
      if (auto r = DatAuditProfile::loadSourceResultRows(db, profileId, sourceName); r.isOk()) {
        results = r.value();
      } else {
        ErrorUtils::logError(r.error());
      }
    });
    m_gameModel->setFolders(
        DatAudit::groupResultsByFolder(results, m_scanRootsByProfile.value(profileId)));
    return;
  }
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

  if (m_groupByFolder != nullptr && m_groupByFolder->isChecked()) {
    // Folder mode: gameName is the selected item-folder. Show that folder's rows
    // (Kartend-m6qsb.30) — no catalogue join, the rows already carry their state.
    QList<DatAuditProfile::ResultRow> all;
    withDb([&](QSqlDatabase &db) {
      if (auto r =
              DatAuditProfile::loadSourceResultRows(db, m_currentProfileId, m_currentSourceName);
          r.isOk()) {
        all = r.value();
      }
    });
    const auto folders =
        DatAudit::groupResultsByFolder(all, m_scanRootsByProfile.value(m_currentProfileId));
    for (const DatAudit::FolderRollup &fr : folders) {
      if (fr.folder == gameName) {
        m_romModel->setGame({}, fr.rows);
        m_romTable->resizeColumnsToContents();
        return;
      }
    }
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

void DatAuditBrowserPage::loadPresets() {
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  m_presets = DatAudit::loadBrowserPresets(settings);
  // Block the combo while filling it so the initial population does not fire
  // currentIndexChanged → recallPreset() (the page opens at its own defaults,
  // not whatever is in slot 1).
  const QSignalBlocker block(m_presetCombo);
  m_presetCombo->clear();
  for (const DatAudit::BrowserViewPreset &p : m_presets) {
    m_presetCombo->addItem(p.name);
  }
  m_presetCombo->setCurrentIndex(0);
}

DatAudit::BrowserViewPreset DatAuditBrowserPage::captureView() const {
  DatAudit::BrowserViewPreset p;
  p.complete = m_filterComplete->isChecked();
  p.partial = m_filterPartial->isChecked();
  p.empty = m_filterEmpty->isChecked();
  p.fixes = m_filterFixes->isChecked();
  p.mia = m_filterMia->isChecked();
  p.groupByFolder = m_groupByFolder->isChecked();
  p.search = m_search->text();
  return p;
}

void DatAuditBrowserPage::recallPreset(int slot) {
  if (slot < 0 || slot >= m_presets.size()) {
    return;
  }
  const DatAudit::BrowserViewPreset &p = m_presets.at(slot);
  // Set every control with its signal blocked, then push the combined state
  // through the same paths the signals would have (one filter pass, one game
  // reload) instead of a cascade of partial updates mid-recall. The blockers
  // stay in scope for those calls — applyFilters()/loadGamesFor() read the
  // control states directly, not via the signals.
  const QSignalBlocker bc(m_filterComplete);
  const QSignalBlocker bp(m_filterPartial);
  const QSignalBlocker be(m_filterEmpty);
  const QSignalBlocker bf(m_filterFixes);
  const QSignalBlocker bm(m_filterMia);
  const QSignalBlocker bg(m_groupByFolder);
  const QSignalBlocker bs(m_search);
  m_filterComplete->setChecked(p.complete);
  m_filterPartial->setChecked(p.partial);
  m_filterEmpty->setChecked(p.empty);
  m_filterFixes->setChecked(p.fixes);
  m_filterMia->setChecked(p.mia);
  m_groupByFolder->setChecked(p.groupByFolder);
  m_search->setText(p.search);

  applyFilters();
  m_gameProxy->setFilterFixedString(p.search);
  // group-by-folder changes which model is loaded, so re-pull the current
  // source under the recalled grouping (its toggled signal was blocked above).
  if (m_currentProfileId >= 0 && !m_currentSourceName.isEmpty()) {
    loadGamesFor(m_currentProfileId, m_currentSourceName, m_currentDatPath);
  }
}

void DatAuditBrowserPage::saveCurrentToPreset() {
  const int slot = m_presetCombo->currentIndex();
  if (slot < 0 || slot >= m_presets.size()) {
    return;
  }
  DatAudit::BrowserViewPreset p = captureView();
  p.name = m_presets.at(slot).name; // Save overwrites the view, keeps the name.
  m_presets[slot] = p;
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  DatAudit::saveBrowserPreset(settings, slot, p);
}

void DatAuditBrowserPage::renameSelectedPreset() {
  const int slot = m_presetCombo->currentIndex();
  if (slot < 0 || slot >= m_presets.size()) {
    return;
  }
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Rename preset"), tr("Preset name:"),
                                             QLineEdit::Normal, m_presets.at(slot).name, &ok)
                           .trimmed();
  if (!ok || name.isEmpty()) {
    return;
  }
  m_presets[slot].name = name;
  QSettings settings(QStringLiteral("kartend"), QStringLiteral("ui-state"));
  DatAudit::saveBrowserPreset(settings, slot, m_presets.at(slot));
  // Update the combo label only — do not recall (would reset the live view).
  const QSignalBlocker block(m_presetCombo);
  m_presetCombo->setItemText(slot, name);
}
