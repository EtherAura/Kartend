// Aggregate usage statistics dialog. Reads from DatabaseManager via
// UsageStatsStore and renders four tabs (most played / recently played /
// per-collection breakdown / history) plus a header with whole-library totals.
#include "statisticsdialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include "errordialog.h"
#include "idatabasemanager.h"
#include "isettingsmanager.h"
#include "pathutils.h"

namespace {
constexpr int DIALOG_WIDTH = 920;
constexpr int DIALOG_HEIGHT = 640;
constexpr int TOP_LIST_LIMIT = 50;
constexpr int RECENT_LIST_LIMIT = 50;
constexpr int NEVER_LIST_LIMIT = 50;
constexpr int HISTORY_LIST_LIMIT = 1000;
constexpr int RECENT_DAYS_WINDOW = 7;
} // namespace

StatisticsDialog::StatisticsDialog(IDatabaseManager *databaseManager,
                                   const QList<CollectionConfig> *collections,
                                   bool runtimeDetectionEnabled, GeneralSettings *generalSettings,
                                   ISettingsManager *settingsManager, QWidget *parent)
    : QDialog(parent), m_databaseManager(databaseManager), m_collections(collections),
      m_runtimeDetectionEnabled(runtimeDetectionEnabled), m_generalSettings(generalSettings),
      m_settingsManager(settingsManager) {
  setWindowTitle(tr("Usage Statistics"));
  setModal(true);
  resize(DIALOG_WIDTH, DIALOG_HEIGHT);
  setupUI();
  refresh();
}

void StatisticsDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(10);
  mainLayout->setContentsMargins(12, 12, 12, 12);

  // Header: aggregate counters split across two side-by-side forms so the
  // wider dialog isn't dominated by a single narrow column. Bold values +
  // dim labels match the metadata sidebar's typographic rhythm.
  auto *headerFrame = new QFrame(this);
  headerFrame->setFrameShape(QFrame::StyledPanel);
  auto *headerHBox = new QHBoxLayout(headerFrame);
  headerHBox->setContentsMargins(16, 12, 16, 12);
  headerHBox->setSpacing(20);

  auto makeForm = [] {
    auto *f = new QFormLayout();
    f->setLabelAlignment(Qt::AlignRight);
    f->setHorizontalSpacing(8);
    f->setVerticalSpacing(6);
    f->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    return f;
  };
  auto *headerLeft = makeForm();
  auto *headerRight = makeForm();

  auto makeValueLabel = [this]() {
    auto *lbl = new QLabel(this);
    QFont f = lbl->font();
    f.setBold(true);
    lbl->setFont(f);
    lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return lbl;
  };

  m_totalItemsValue = makeValueLabel();
  m_totalLaunchesValue = makeValueLabel();
  m_totalTimeValue = makeValueLabel();
  m_itemsLaunchedValue = makeValueLabel();
  m_played7DaysValue = makeValueLabel();

  headerLeft->addRow(tr("Total items:"), m_totalItemsValue);
  headerLeft->addRow(tr("Items launched at least once:"), m_itemsLaunchedValue);
  headerLeft->addRow(tr("Played in last %1 days:").arg(RECENT_DAYS_WINDOW), m_played7DaysValue);
  headerRight->addRow(tr("Total launches:"), m_totalLaunchesValue);
  headerRight->addRow(tr("Total time played:"), m_totalTimeValue);

  headerHBox->addLayout(headerLeft, 1);
  headerHBox->addLayout(headerRight, 1);

  mainLayout->addWidget(headerFrame);

  // Runtime-detection caveat. Always present — hidden via setVisible() when
  // the user has runtime detection on so they don't see a redundant hint.
  m_runtimeNote = new QLabel(this);
  m_runtimeNote->setWordWrap(true);
  m_runtimeNote->setStyleSheet("color: palette(mid); font-style: italic;");
  m_runtimeNote->setText(
      tr("Time played is only tracked when Runtime Detection is enabled in Settings → "
         "General. Without it, launched items still count toward play count and last "
         "played, but session duration is unknown."));
  mainLayout->addWidget(m_runtimeNote);

  // Tabbed lists. QTreeWidget gives us cheap sortable columns without
  // pulling in a model class for what is essentially a static read.
  m_tabs = new QTabWidget(this);

  auto buildTree = [this](const QStringList &headers) {
    auto *tree = new QTreeWidget(this);
    tree->setHeaderLabels(headers);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->setSortingEnabled(true);
    tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    tree->header()->setStretchLastSection(true);
    return tree;
  };

  m_mostPlayedTree =
      buildTree({tr("Item"), tr("Collection"), tr("Plays"), tr("Time"), tr("Last played")});
  m_recentlyPlayedTree =
      buildTree({tr("Item"), tr("Collection"), tr("Last played"), tr("Plays"), tr("Time")});
  m_neverPlayedTree = buildTree({tr("Item"), tr("Collection")});
  m_byCollectionTree = buildTree({tr("Collection"), tr("Items"), tr("Launches"), tr("Time")});

  m_tabs->addTab(m_mostPlayedTree, tr("Most played"));
  m_tabs->addTab(m_recentlyPlayedTree, tr("Recently played"));

  // never-played tab needs a summary label above the tree so the user can
  // tell at a glance whether the visible list is exhaustive or capped.
  auto *neverPlayedTab = new QWidget(this);
  auto *neverPlayedLayout = new QVBoxLayout(neverPlayedTab);
  neverPlayedLayout->setContentsMargins(0, 0, 0, 0);
  neverPlayedLayout->setSpacing(8);
  m_neverPlayedSummary = new QLabel(neverPlayedTab);
  m_neverPlayedSummary->setStyleSheet("color: palette(mid);");
  m_neverPlayedSummary->setWordWrap(true);
  neverPlayedLayout->addWidget(m_neverPlayedSummary);
  neverPlayedLayout->addWidget(m_neverPlayedTree, 1);
  m_tabs->addTab(neverPlayedTab, tr("Never played"));

  m_tabs->addTab(m_byCollectionTree, tr("By collection"));

  // history tab. Sits inside a container so the tab can host
  // its own toggle/clear controls without stuffing them into the dialog
  // footer (which already owns the global Reset/Refresh/Close buttons).
  auto *historyTab = new QWidget(this);
  auto *historyLayout = new QVBoxLayout(historyTab);
  historyLayout->setContentsMargins(0, 0, 0, 0);
  historyLayout->setSpacing(8);

  // Top row: enable toggle + entry-count label. Both stay above the tree
  // so the user sees them without scrolling, even when the dialog is sized
  // small.
  auto *historyTopRow = new QHBoxLayout();
  m_historyEnabledCheckBox = new QCheckBox(tr("Record launch history"), historyTab);
  m_historyEnabledCheckBox->setToolTip(
      tr("When off, new launches are not logged. Existing entries remain until "
         "you click Clear history."));
  connect(m_historyEnabledCheckBox, &QCheckBox::toggled, this,
          &StatisticsDialog::onHistoryDisableToggled);
  historyTopRow->addWidget(m_historyEnabledCheckBox);
  historyTopRow->addStretch();
  auto *countLabel = new QLabel(tr("Entries:"), historyTab);
  countLabel->setStyleSheet("color: palette(mid);");
  historyTopRow->addWidget(countLabel);
  m_historyCountValue = new QLabel(historyTab);
  QFont countFont = m_historyCountValue->font();
  countFont.setBold(true);
  m_historyCountValue->setFont(countFont);
  m_historyCountValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
  historyTopRow->addWidget(m_historyCountValue);
  historyLayout->addLayout(historyTopRow);

  // "History is disabled" hint stays mounted but hidden when the gate is
  // on; toggling it doesn't reflow the layout.
  m_historyDisabledNote = new QLabel(historyTab);
  m_historyDisabledNote->setWordWrap(true);
  m_historyDisabledNote->setStyleSheet("color: palette(mid); font-style: italic;");
  m_historyDisabledNote->setText(
      tr("History recording is currently off. Existing entries are still listed "
         "below but no new launches will be added."));
  historyLayout->addWidget(m_historyDisabledNote);

  m_historyTree = buildTree({tr("Launched at"), tr("Item"), tr("Collection"), tr("Path")});
  // History rows arrive newest-first from the store; let the user re-sort
  // via the column header but keep that initial order.
  m_historyTree->setSortingEnabled(false);
  historyLayout->addWidget(m_historyTree, 1);

  // Per-tab Clear button: scoped to history so it doesn't sit next to the
  // global Reset (which already covers stats).
  auto *historyButtonRow = new QHBoxLayout();
  historyButtonRow->addStretch();
  m_clearHistoryButton = new QPushButton(tr("Clear history…"), historyTab);
  m_clearHistoryButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
  m_clearHistoryButton->setToolTip(
      tr("Permanently delete every history entry. This cannot be undone."));
  connect(m_clearHistoryButton, &QPushButton::clicked, this,
          &StatisticsDialog::onClearHistoryClicked);
  historyButtonRow->addWidget(m_clearHistoryButton);
  historyLayout->addLayout(historyButtonRow);

  m_tabs->addTab(historyTab, tr("History"));

  mainLayout->addWidget(m_tabs, 1);

  // Buttons: Reset (left, destructive) + Refresh + Close (right).
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(8);
  auto *resetButton = new QPushButton(tr("Reset usage stats…"), this);
  resetButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
  resetButton->setToolTip(tr("Clear play count, last played, and time played for every item."));
  connect(resetButton, &QPushButton::clicked, this, &StatisticsDialog::onResetClicked);
  buttonLayout->addWidget(resetButton);

  buttonLayout->addStretch();

  auto *refreshButton = new QPushButton(tr("Refresh"), this);
  refreshButton->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
  connect(refreshButton, &QPushButton::clicked, this, &StatisticsDialog::refresh);
  buttonLayout->addWidget(refreshButton);

  auto *closeButton = new QPushButton(tr("Close"), this);
  closeButton->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
  closeButton->setDefault(true);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(closeButton);

  mainLayout->addLayout(buttonLayout);
}

void StatisticsDialog::refresh() {
  if (!m_databaseManager) {
    return;
  }

  // Recency cutoff is derived once per refresh so the header counter and any
  // future tab filters stay consistent. UTC ISO-8601 matches what
  // recordLaunch() writes, so a string comparison in SQL is cheap and safe.
  const QString cutoffIso =
      QDateTime::currentDateTimeUtc().addDays(-RECENT_DAYS_WINDOW).toString(Qt::ISODate);
  const auto agg = m_databaseManager->loadAggregateUsageStats();
  const qint64 played7Days = m_databaseManager->countItemsPlayedSince(cutoffIso);
  const qint64 totalNever = std::max<qint64>(0, agg.totalItems - agg.itemsLaunchedAtLeastOnce);

  populateAggregate(agg, played7Days);
  populateMostPlayed(m_databaseManager->loadTopPlayedItems(TOP_LIST_LIMIT));
  populateRecentlyPlayed(m_databaseManager->loadRecentlyPlayedItems(RECENT_LIST_LIMIT));
  populateNeverPlayed(m_databaseManager->loadNeverPlayedItems(NEVER_LIST_LIMIT), totalNever);
  populateByCollection(m_databaseManager->loadUsageByCollection());
  populateHistory(m_databaseManager->loadRecentHistory(HISTORY_LIST_LIMIT),
                  m_databaseManager->historyEntryCount());

  if (m_runtimeNote) {
    m_runtimeNote->setVisible(!m_runtimeDetectionEnabled);
  }

  // Reflect the live historyEnabled gate in the toggle. Block signals so
  // syncing it doesn't bounce through onHistoryDisableToggled and write
  // back the same value.
  if (m_historyEnabledCheckBox) {
    const bool on = m_generalSettings ? m_generalSettings->historyEnabled : true;
    m_historyEnabledCheckBox->blockSignals(true);
    m_historyEnabledCheckBox->setChecked(on);
    m_historyEnabledCheckBox->blockSignals(false);
    if (m_historyDisabledNote) {
      m_historyDisabledNote->setVisible(!on);
    }
  }
}

void StatisticsDialog::populateAggregate(const UsageStatsStore::AggregateStats &agg,
                                         qint64 playedInLast7Days) {
  const QLocale locale;
  if (m_totalItemsValue) {
    m_totalItemsValue->setText(locale.toString(agg.totalItems));
  }
  if (m_itemsLaunchedValue) {
    m_itemsLaunchedValue->setText(locale.toString(agg.itemsLaunchedAtLeastOnce));
  }
  if (m_played7DaysValue) {
    m_played7DaysValue->setText(locale.toString(playedInLast7Days));
  }
  if (m_totalLaunchesValue) {
    m_totalLaunchesValue->setText(locale.toString(agg.totalLaunches));
  }
  if (m_totalTimeValue) {
    const QString formatted = UsageStatsStore::formatDuration(agg.totalPlaySeconds);
    m_totalTimeValue->setText(formatted.isEmpty() ? tr("0s") : formatted);
  }
}

namespace {
// Numeric column items use UserRole-as-Int sorting so the Tree widget sorts
// "12" < "100" instead of lexically ("100" < "12").
class NumericTreeItem : public QTreeWidgetItem {
public:
  using QTreeWidgetItem::QTreeWidgetItem;
  bool operator<(const QTreeWidgetItem &other) const override {
    const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
    const QVariant a = data(col, Qt::UserRole);
    const QVariant b = other.data(col, Qt::UserRole);
    if (a.isValid() && b.isValid()) {
      return a.toLongLong() < b.toLongLong();
    }
    return text(col) < other.text(col);
  }
};
} // namespace

void StatisticsDialog::populateMostPlayed(const QList<UsageStatsStore::ItemUsageRow> &rows) {
  if (!m_mostPlayedTree) {
    return;
  }
  m_mostPlayedTree->clear();
  m_mostPlayedTree->setSortingEnabled(false);
  for (const auto &row : rows) {
    auto *item = new NumericTreeItem(m_mostPlayedTree);
    const QString name = row.name.isEmpty() ? QFileInfo(row.path).completeBaseName() : row.name;
    item->setText(0, name);
    item->setText(1, labelForCollectionUuid(row.collectionUuid));
    item->setText(2, QLocale().toString(row.playCount));
    item->setData(2, Qt::UserRole, row.playCount);
    item->setText(3, UsageStatsStore::formatDuration(row.totalPlaySeconds));
    item->setData(3, Qt::UserRole, row.totalPlaySeconds);
    item->setText(4, UsageStatsStore::formatTimestamp(row.lastPlayed));
    item->setData(4, Qt::UserRole, row.lastPlayed);
    item->setToolTip(0, row.path);
  }
  m_mostPlayedTree->setSortingEnabled(true);
  m_mostPlayedTree->sortByColumn(2, Qt::DescendingOrder);
  for (int col = 0; col < m_mostPlayedTree->columnCount(); ++col) {
    m_mostPlayedTree->resizeColumnToContents(col);
  }
}

void StatisticsDialog::populateRecentlyPlayed(const QList<UsageStatsStore::ItemUsageRow> &rows) {
  if (!m_recentlyPlayedTree) {
    return;
  }
  m_recentlyPlayedTree->clear();
  m_recentlyPlayedTree->setSortingEnabled(false);
  for (const auto &row : rows) {
    auto *item = new NumericTreeItem(m_recentlyPlayedTree);
    const QString name = row.name.isEmpty() ? QFileInfo(row.path).completeBaseName() : row.name;
    item->setText(0, name);
    item->setText(1, labelForCollectionUuid(row.collectionUuid));
    item->setText(2, UsageStatsStore::formatTimestamp(row.lastPlayed));
    item->setData(2, Qt::UserRole, row.lastPlayed);
    item->setText(3, QLocale().toString(row.playCount));
    item->setData(3, Qt::UserRole, row.playCount);
    item->setText(4, UsageStatsStore::formatDuration(row.totalPlaySeconds));
    item->setData(4, Qt::UserRole, row.totalPlaySeconds);
    item->setToolTip(0, row.path);
  }
  m_recentlyPlayedTree->setSortingEnabled(true);
  m_recentlyPlayedTree->sortByColumn(2, Qt::DescendingOrder);
  for (int col = 0; col < m_recentlyPlayedTree->columnCount(); ++col) {
    m_recentlyPlayedTree->resizeColumnToContents(col);
  }
}

void StatisticsDialog::populateNeverPlayed(const QList<UsageStatsStore::ItemUsageRow> &rows,
                                           qint64 totalNever) {
  if (m_neverPlayedSummary) {
    if (totalNever <= 0) {
      m_neverPlayedSummary->setText(
          tr("Every item in your library has been launched at least once."));
    } else {
      const QString totalText = QLocale().toString(totalNever);
      const qint64 shown = std::min<qint64>(totalNever, rows.size());
      const QString shownText = QLocale().toString(shown);
      // %n forms get split-out so the plural rules apply per-clause; the
      // showing-N caveat only renders when the cap actually clipped the list.
      if (shown < totalNever) {
        m_neverPlayedSummary->setText(
            tr("%1 items have never been launched (showing first %2).").arg(totalText, shownText));
      } else {
        m_neverPlayedSummary->setText(tr("%1 items have never been launched.").arg(totalText));
      }
    }
  }
  if (!m_neverPlayedTree) {
    return;
  }
  m_neverPlayedTree->clear();
  m_neverPlayedTree->setSortingEnabled(false);
  for (const auto &row : rows) {
    auto *item = new NumericTreeItem(m_neverPlayedTree);
    const QString name = row.name.isEmpty() ? QFileInfo(row.path).completeBaseName() : row.name;
    item->setText(0, name);
    item->setText(1, labelForCollectionUuid(row.collectionUuid));
    item->setToolTip(0, row.path);
  }
  m_neverPlayedTree->setSortingEnabled(true);
  m_neverPlayedTree->sortByColumn(0, Qt::AscendingOrder);
  for (int col = 0; col < m_neverPlayedTree->columnCount(); ++col) {
    m_neverPlayedTree->resizeColumnToContents(col);
  }
}

void StatisticsDialog::populateByCollection(
    const QHash<QString, UsageStatsStore::CollectionUsage> &byUuid) {
  if (!m_byCollectionTree) {
    return;
  }
  m_byCollectionTree->clear();
  m_byCollectionTree->setSortingEnabled(false);

  // roll up descendant items into each parent collection's row
  // so the count matches what the title bar shows for that collection (which
  // is always recursive — see refreshTitleCounts() in mainwindow.cpp). Without
  // this, a root parent with subcollections shows 0 items in stats while the
  // title bar shows the full subtree total. Map uuid → collectionIndex once,
  // then walk each row's subtree summing every stat field so launches and
  // play time stay in step with itemCount.
  QHash<QString, int> uuidToIndex;
  if (m_collections) {
    uuidToIndex.reserve(m_collections->size());
    for (int i = 0; i < m_collections->size(); ++i) {
      const CollectionConfig &c = (*m_collections)[i];
      const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      uuidToIndex.insert(CollectionUtils::computeCollectionUuid(c.name, expanded), i);
    }
  }

  auto rollUpForUuid = [&](const QString &uuid) -> UsageStatsStore::CollectionUsage {
    UsageStatsStore::CollectionUsage acc;
    acc.collectionUuid = uuid;
    if (!m_collections || !uuidToIndex.contains(uuid)) {
      // Stale row (collection deleted) — use the raw stored values so the
      // user can still see and clear it.
      auto it = byUuid.constFind(uuid);
      if (it != byUuid.constEnd()) {
        acc = it.value();
      }
      return acc;
    }
    QList<int> stack;
    stack.append(uuidToIndex.value(uuid));
    while (!stack.isEmpty()) {
      const int idx = stack.takeLast();
      if (idx < 0 || idx >= m_collections->size()) {
        continue;
      }
      const CollectionConfig &c = (*m_collections)[idx];
      const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      const QString descUuid = CollectionUtils::computeCollectionUuid(c.name, expanded);
      auto it = byUuid.constFind(descUuid);
      if (it != byUuid.constEnd()) {
        acc.itemCount += it.value().itemCount;
        acc.totalLaunches += it.value().totalLaunches;
        acc.totalPlaySeconds += it.value().totalPlaySeconds;
      }
      for (int childIdx : CollectionUtils::directChildrenOf(idx, *m_collections)) {
        stack.append(childIdx);
      }
    }
    return acc;
  };

  for (auto it = byUuid.constBegin(); it != byUuid.constEnd(); ++it) {
    const UsageStatsStore::CollectionUsage row = rollUpForUuid(it.key());
    auto *item = new NumericTreeItem(m_byCollectionTree);
    item->setText(0, labelForCollectionUuid(row.collectionUuid));
    item->setText(1, QLocale().toString(row.itemCount));
    item->setData(1, Qt::UserRole, row.itemCount);
    item->setText(2, QLocale().toString(row.totalLaunches));
    item->setData(2, Qt::UserRole, row.totalLaunches);
    item->setText(3, UsageStatsStore::formatDuration(row.totalPlaySeconds));
    item->setData(3, Qt::UserRole, row.totalPlaySeconds);
  }
  m_byCollectionTree->setSortingEnabled(true);
  m_byCollectionTree->sortByColumn(2, Qt::DescendingOrder);
  for (int col = 0; col < m_byCollectionTree->columnCount(); ++col) {
    m_byCollectionTree->resizeColumnToContents(col);
  }
}

void StatisticsDialog::populateHistory(const QList<HistoryStore::HistoryEntry> &rows,
                                       qint64 totalCount) {
  if (m_historyCountValue) {
    m_historyCountValue->setText(QLocale().toString(totalCount));
  }
  if (!m_historyTree) {
    return;
  }
  m_historyTree->clear();
  // History rows arrive newest-first; suppress sortingEnabled while we
  // populate so the tree doesn't reorder under us.
  const bool wasSortingEnabled = m_historyTree->isSortingEnabled();
  m_historyTree->setSortingEnabled(false);
  for (const auto &row : rows) {
    auto *item = new NumericTreeItem(m_historyTree);
    // Column 0: launched_at — use the raw ISO string for sort, the
    // localized rendering for display.
    item->setText(0, UsageStatsStore::formatTimestamp(row.launchedAt));
    item->setData(0, Qt::UserRole, row.launchedAt);
    const QString name = row.name.isEmpty() ? QFileInfo(row.path).completeBaseName() : row.name;
    item->setText(1, name);
    item->setText(2, labelForCollectionUuid(row.collectionUuid));
    item->setText(3, row.path);
    item->setToolTip(1, row.path);
  }
  m_historyTree->setSortingEnabled(wasSortingEnabled);
  // Default sort by the launched_at column, descending — the store already
  // returns rows in this order so no shuffling actually happens on first
  // populate, but keeping the indicator visible matches the user's
  // expectation when they click headers later.
  m_historyTree->sortByColumn(0, Qt::DescendingOrder);
  for (int col = 0; col < m_historyTree->columnCount(); ++col) {
    m_historyTree->resizeColumnToContents(col);
  }
}

QString StatisticsDialog::labelForCollectionUuid(const QString &uuid) const {
  if (uuid.isEmpty()) {
    return tr("(unknown)");
  }
  if (m_collections) {
    for (const CollectionConfig &c : *m_collections) {
      const QString expanded = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      if (CollectionUtils::computeCollectionUuid(c.name, expanded) == uuid) {
        return CollectionUtils::hierarchicalNameFor(c, *m_collections);
      }
    }
  }
  // Stale rows from deleted collections still need a label — show the
  // truncated uuid so the row stays identifiable across refreshes.
  return tr("(deleted) %1").arg(uuid.left(8));
}

void StatisticsDialog::onResetClicked() {
  const auto reply = QMessageBox::question(
      this, tr("Reset usage stats?"),
      tr("This will clear play count, last played, and time played for every item in "
         "your library. This cannot be undone.\n\nContinue?"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) {
    return;
  }
  if (!m_databaseManager) {
    return;
  }
  if (m_databaseManager->resetAllUsageStats()) {
    refresh();
  }
}

void StatisticsDialog::onClearHistoryClicked() {
  const auto reply = QMessageBox::question(
      this, tr("Clear launch history?"),
      tr("This will permanently delete every entry from the launch history log. "
         "This cannot be undone.\n\nContinue?"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) {
    return;
  }
  if (!m_databaseManager) {
    return;
  }
  if (m_databaseManager->clearHistory()) {
    refresh();
  }
}

void StatisticsDialog::onHistoryDisableToggled(bool checked) {
  // Flip the live setting + persist immediately. Without a SettingsManager
  // we still toggle the in-memory value so the next launch sees it; the
  // change just won't survive restart.
  if (m_generalSettings) {
    m_generalSettings->historyEnabled = checked;
    if (m_settingsManager) {
      // If the disk write fails the toggle still flips in-memory (which is the
      // documented behaviour without a SettingsManager); surface the failure so
      // the user knows the change won't survive restart.
      if (auto result = m_settingsManager->saveGeneralSettings(*m_generalSettings);
          result.isError()) {
        ErrorDialog::showError(this, result.error());
      }
    }
  }
  if (m_historyDisabledNote) {
    m_historyDisabledNote->setVisible(!checked);
  }
}
