#ifndef STATISTICSDIALOG_H
#define STATISTICSDIALOG_H

#include <QDialog>
#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QString>

#include "collection/collectionconfig.h"
#include "collection/generalsettings.h"
#include "historystore.h"
#include "usagestatsstore.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QTabWidget;
class QTreeWidget;
QT_END_NAMESPACE

class IDatabaseManager;
struct GeneralSettings;
class ISettingsManager;

/// Aggregate usage-statistics dialog.
///
/// Header: total items / total launches / total time played, plus a note when
/// runtime detection is disabled (time-played is 0 in that case).
///
/// Tabs:
///   - "Most played"     — top items by play_count
///   - "Recently played" — most recent last_played
///   - "By collection"   — per-collection item count + launches + time
///
/// "Reset usage stats…" clears every tracking column across the library.
class StatisticsDialog : public QDialog {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(StatisticsDialog)
public:
  StatisticsDialog(IDatabaseManager *databaseManager, const QList<CollectionConfig> *collections,
                   bool runtimeDetectionEnabled, GeneralSettings *generalSettings,
                   ISettingsManager *settingsManager, QWidget *parent = nullptr);

signals:
  /// Emitted when the user double-clicks an item row in one of the
  /// per-item analytics trees (Most played, Recently played, Never
  /// played, History). The caller (typically MenuController) is
  /// responsible for routing the navigation — close the dialog, switch
  /// to the owning collection, and select the item. `filePath` is the
  /// absolute item path as stored in items.path.
  void navigateToItemRequested(const QString &filePath);

private:
  void setupUI();
  void refresh();
  void populateAggregate(const UsageStatsStore::AggregateStats &agg, qint64 playedInLast7Days);
  void populateMostPlayed(const QList<UsageStatsStore::ItemUsageRow> &rows);
  void populateRecentlyPlayed(const QList<UsageStatsStore::ItemUsageRow> &rows);
  void populateNeverPlayed(const QList<UsageStatsStore::ItemUsageRow> &rows, qint64 totalNever);
  void populateByCollection(const QHash<QString, UsageStatsStore::CollectionUsage> &byUuid);
  void populateHistory(const QList<HistoryStore::HistoryEntry> &rows, qint64 totalCount);
  void onResetClicked();
  void onClearHistoryClicked();
  void onHistoryDisableToggled(bool checked);

  /// Rebuild the per-refresh collection-UUID caches (uuid->index, index->uuid,
  /// uuid->label) from m_collections. Called once at the top of refresh() so
  /// the populate* passes resolve labels and roll up subtrees with hash
  /// lookups instead of recomputing each collection's UUID — which does an FS
  /// path-expansion — per row (the O(n^2) + sync-FS hotspot, Kartend-umwix).
  void rebuildCollectionUuidMaps();

  /// Resolves a collection's display name from its UUID via the m_uuidToLabel
  /// cache (rebuilt each refresh). Returns the truncated UUID when no config
  /// matches (rows from deleted collections survive in `items`) so the user
  /// still has something readable.
  [[nodiscard]] QString labelForCollectionUuid(const QString &uuid) const;

  /// Bundle of every DB-derived figure the dialog renders, produced off the UI
  /// thread by gatherStats() so refresh() never blocks on SQLite (Kartend-umwix
  /// part B). `ok` is false when the worker couldn't open the database.
  struct StatsSnapshot {
    UsageStatsStore::AggregateStats agg;
    qint64 played7Days = 0;
    qint64 totalNever = 0;
    QList<UsageStatsStore::ItemUsageRow> topPlayed;
    QList<UsageStatsStore::ItemUsageRow> recentlyPlayed;
    QList<UsageStatsStore::ItemUsageRow> neverPlayed;
    QHash<QString, UsageStatsStore::CollectionUsage> byCollection;
    QList<HistoryStore::HistoryEntry> history;
    qint64 historyCount = 0;
    bool ok = false;
  };
  /// Worker body (runs on a QThreadPool thread): opens a private read-only
  /// connection to @p dbPath and runs every stats query, returning the bundle.
  /// Touches no member or GUI state — only the value args — so it is safe to
  /// run off the main thread.
  [[nodiscard]] static StatsSnapshot gatherStats(const QString &dbPath, const QString &cutoffIso);
  /// Render a gathered snapshot onto the trees/labels (main thread).
  void applyStatsSnapshot(const StatsSnapshot &snap);

  IDatabaseManager *m_databaseManager = nullptr;
  const QList<CollectionConfig> *m_collections = nullptr;
  // Per-refresh collection-UUID caches, rebuilt by rebuildCollectionUuidMaps()
  // at the top of refresh(). Computing a collection's UUID expands its media
  // path on disk, so the populate* passes share these instead of recomputing
  // per row (Kartend-umwix). m_indexToUuid is parallel to *m_collections.
  QHash<QString, int> m_uuidToIndex;
  QList<QString> m_indexToUuid;
  QHash<QString, QString> m_uuidToLabel;
  /// In-flight async stats load, or nullptr. Superseded on each refresh() and
  /// parented to the dialog so teardown drops a late result (Kartend-umwix).
  QFutureWatcher<StatsSnapshot> *m_statsWatcher = nullptr;
  bool m_runtimeDetectionEnabled = false;
  /// the dialog drives historyEnabled (live toggle on the
  /// History tab) by writing through these. Both can be null when the
  /// caller didn't wire them — the tab still renders its rows but the
  /// toggle is hidden.
  GeneralSettings *m_generalSettings = nullptr;
  ISettingsManager *m_settingsManager = nullptr;

  QLabel *m_totalItemsValue = nullptr;
  QLabel *m_totalLaunchesValue = nullptr;
  QLabel *m_totalTimeValue = nullptr;
  QLabel *m_itemsLaunchedValue = nullptr;
  QLabel *m_played7DaysValue = nullptr;
  QLabel *m_runtimeNote = nullptr;
  QTabWidget *m_tabs = nullptr;
  QTreeWidget *m_mostPlayedTree = nullptr;
  QTreeWidget *m_recentlyPlayedTree = nullptr;
  QTreeWidget *m_neverPlayedTree = nullptr;
  /// Header row above the never-played tree showing "(N items have never
  /// been launched)" so the user knows whether the displayed list is the
  /// full set or just the first 50 entries.
  QLabel *m_neverPlayedSummary = nullptr;
  QTreeWidget *m_byCollectionTree = nullptr;
  // History tab
  QTreeWidget *m_historyTree = nullptr;
  QLabel *m_historyDisabledNote = nullptr;
  class QCheckBox *m_historyEnabledCheckBox = nullptr;
  class QPushButton *m_clearHistoryButton = nullptr;
  QLabel *m_historyCountValue = nullptr;
};

#endif // STATISTICSDIALOG_H
