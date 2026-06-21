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
#include <QStringList>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>

#include "datauditbrowsermodels.h"
#include "datauditbuckets.h"
#include "datauditprofile.h"
#include "datauditresultdelegate.h"
#include "dattreebadgedelegate.h"
#include "formbuilders.h"

namespace {

// Apply the shared results-table look (Kartend-m6qsb.20 idiom) via the
// centralized FormBuilders helper (Kartend-t06mx). The per-table status
// delegate stays a data-audit concern, so it is constructed here and handed
// to the generic helper rather than baked into it.
void configureTable(QTableView *t) {
  FormBuilders::configureResultTable(t, new DatAudit::DatAuditResultDelegate(t),
                                     /*stretchLastSection=*/false);
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
  // Track expand/collapse so the expanded set survives the setTree() rebuilds
  // (Kartend-q66m4). Source nodes are leaves, so only multi-source Profile nodes
  // ever fire these — keyed by their stable profile id.
  connect(m_tree, &QTreeView::expanded, this, [this](const QModelIndex &idx) {
    m_expandedProfiles.insert(m_treeModel->profileIdAt(idx));
  });
  connect(m_tree, &QTreeView::collapsed, this, [this](const QModelIndex &idx) {
    m_expandedProfiles.remove(m_treeModel->profileIdAt(idx));
  });
  connect(m_gameTable->selectionModel(), &QItemSelectionModel::currentChanged, this,
          &DatAuditBrowserPage::onGameSelectionChanged);
  connect(m_search, &QLineEdit::textChanged, this,
          [this](const QString &t) { m_gameProxy->setFilterFixedString(t); });

  loadPresets();
  clearDatInfo();
}

void DatAuditBrowserPage::refresh() {
  const QList<DatAuditProfile::Profile> profiles = m_repo.profiles();
  const QList<DatAuditProfile::RollupRow> rollups = m_repo.rollups();
  {
    // Block expand/collapse emissions during the rebuild so the model reset's
    // view-collapse can't clear m_expandedProfiles before we re-apply it
    // (Kartend-q66m4).
    const QSignalBlocker blocker(m_tree);
    const bool groupByCategory = m_groupByCategory != nullptr && m_groupByCategory->isChecked();
    m_treeModel->setTree(profiles, rollups, groupByCategory, m_collectionNamesByUuid);
  }
  restoreExpandedState();
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
  if (const auto src = m_repo.datHeader(datPath)) {
    if (!src->headerDescription.isEmpty()) {
      desc = src->headerDescription;
    }
    if (!src->headerVersion.isEmpty()) {
      version = src->headerVersion;
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
    const QList<DatAuditProfile::ResultRow> results = m_repo.sourceRows(profileId, sourceName);
    m_gameModel->setFolders(
        DatAudit::groupResultsByFolder(results, m_scanRootsByProfile.value(profileId)));
    return;
  }
  m_gameModel->setGames(m_repo.gamesFor(profileId, sourceName));
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
    const QList<DatAuditProfile::ResultRow> all =
        m_repo.sourceRows(m_currentProfileId, m_currentSourceName);
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

  const QList<DatAuditProfile::ResultRow> results =
      m_repo.romsFor(m_currentProfileId, m_currentSourceName, gameName);
  const QList<DatLookup::DatRecord> records = m_repo.datRecordsFor(m_currentDatPath, gameName);
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

  // Expanded tree nodes (Kartend-q66m4): store the profile ids as strings so
  // the QSet round-trips through QSettings.
  QStringList expandedIds;
  expandedIds.reserve(m_expandedProfiles.size());
  for (const qint64 id : m_expandedProfiles) {
    expandedIds << QString::number(id);
  }
  settings.setValue(QStringLiteral("browserExpandedProfiles"), expandedIds);
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

  // Expanded tree nodes (Kartend-q66m4). The tree is empty until the first
  // refresh(), so this only seeds the set; restoreExpandedState() applies it
  // after each setTree().
  m_expandedProfiles.clear();
  const QStringList expandedIds =
      settings.value(QStringLiteral("browserExpandedProfiles")).toStringList();
  for (const QString &idStr : expandedIds) {
    bool ok = false;
    const qint64 id = idStr.toLongLong(&ok);
    if (ok) {
      m_expandedProfiles.insert(id);
    }
  }
  settings.endGroup();

  applyFilters();
}

void DatAuditBrowserPage::restoreExpandedState() {
  if (m_tree == nullptr || m_treeModel == nullptr || m_expandedProfiles.isEmpty()) {
    return;
  }
  // Block signals so the expand() calls below don't recurse into the
  // expanded() handler that mutates m_expandedProfiles while we iterate it.
  const QSignalBlocker blocker(m_tree);
  const int rows = m_treeModel->rowCount();
  for (int row = 0; row < rows; ++row) {
    const QModelIndex idx = m_treeModel->index(row, 0);
    if (m_expandedProfiles.contains(m_treeModel->profileIdAt(idx))) {
      m_tree->expand(idx);
    }
  }
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
