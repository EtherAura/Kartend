// Aggregate usage statistics dialog. Reads from DatabaseManager via
// UsageStatsStore and renders three tabs (most played / recently played /
// per-collection breakdown) plus a header with whole-library totals.
#include "statisticsdialog.h"

#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "databasemanager.h"
#include "pathutils.h"

namespace {
constexpr int DIALOG_WIDTH = 720;
constexpr int DIALOG_HEIGHT = 540;
constexpr int TOP_LIST_LIMIT = 50;
constexpr int RECENT_LIST_LIMIT = 50;
} // namespace

StatisticsDialog::StatisticsDialog(DatabaseManager *databaseManager,
                                   const QList<CollectionConfig> *collections,
                                   bool runtimeDetectionEnabled, QWidget *parent)
    : QDialog(parent), m_databaseManager(databaseManager), m_collections(collections),
      m_runtimeDetectionEnabled(runtimeDetectionEnabled) {
  setWindowTitle(tr("Usage Statistics"));
  setModal(true);
  resize(DIALOG_WIDTH, DIALOG_HEIGHT);
  setupUI();
  refresh();
}

void StatisticsDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(12);
  mainLayout->setContentsMargins(20, 20, 20, 20);

  // Header: aggregate counters in a two-column form. Bold values + dim
  // labels match the metadata sidebar's typographic rhythm.
  auto *headerFrame = new QFrame(this);
  headerFrame->setFrameShape(QFrame::StyledPanel);
  auto *headerLayout = new QFormLayout(headerFrame);
  headerLayout->setLabelAlignment(Qt::AlignRight);
  headerLayout->setContentsMargins(16, 12, 16, 12);

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

  headerLayout->addRow(tr("Total items:"), m_totalItemsValue);
  headerLayout->addRow(tr("Items launched at least once:"), m_itemsLaunchedValue);
  headerLayout->addRow(tr("Total launches:"), m_totalLaunchesValue);
  headerLayout->addRow(tr("Total time played:"), m_totalTimeValue);

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
  m_byCollectionTree = buildTree({tr("Collection"), tr("Items"), tr("Launches"), tr("Time")});

  m_tabs->addTab(m_mostPlayedTree, tr("Most played"));
  m_tabs->addTab(m_recentlyPlayedTree, tr("Recently played"));
  m_tabs->addTab(m_byCollectionTree, tr("By collection"));

  mainLayout->addWidget(m_tabs, 1);

  // Buttons: Reset (left, destructive) + Refresh + Close (right).
  auto *buttonLayout = new QHBoxLayout();
  auto *resetButton = new QPushButton(tr("Reset usage stats…"), this);
  resetButton->setToolTip(tr("Clear play count, last played, and time played for every item."));
  connect(resetButton, &QPushButton::clicked, this, &StatisticsDialog::onResetClicked);
  buttonLayout->addWidget(resetButton);

  buttonLayout->addStretch();

  auto *refreshButton = new QPushButton(tr("Refresh"), this);
  connect(refreshButton, &QPushButton::clicked, this, &StatisticsDialog::refresh);
  buttonLayout->addWidget(refreshButton);

  auto *closeButton = new QPushButton(tr("Close"), this);
  closeButton->setDefault(true);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(closeButton);

  mainLayout->addLayout(buttonLayout);
}

void StatisticsDialog::refresh() {
  if (!m_databaseManager) {
    return;
  }

  populateAggregate(m_databaseManager->loadAggregateUsageStats());
  populateMostPlayed(m_databaseManager->loadTopPlayedItems(TOP_LIST_LIMIT));
  populateRecentlyPlayed(m_databaseManager->loadRecentlyPlayedItems(RECENT_LIST_LIMIT));
  populateByCollection(m_databaseManager->loadUsageByCollection());

  if (m_runtimeNote) {
    m_runtimeNote->setVisible(!m_runtimeDetectionEnabled);
  }
}

void StatisticsDialog::populateAggregate(const UsageStatsStore::AggregateStats &agg) {
  const QLocale locale;
  if (m_totalItemsValue) {
    m_totalItemsValue->setText(locale.toString(agg.totalItems));
  }
  if (m_itemsLaunchedValue) {
    m_itemsLaunchedValue->setText(locale.toString(agg.itemsLaunchedAtLeastOnce));
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

void StatisticsDialog::populateByCollection(
    const QHash<QString, UsageStatsStore::CollectionUsage> &byUuid) {
  if (!m_byCollectionTree) {
    return;
  }
  m_byCollectionTree->clear();
  m_byCollectionTree->setSortingEnabled(false);
  for (auto it = byUuid.constBegin(); it != byUuid.constEnd(); ++it) {
    const auto &row = it.value();
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
