#ifndef STATISTICSDIALOG_H
#define STATISTICSDIALOG_H

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

#include "collectionutils.h"
#include "usagestatsstore.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QTabWidget;
class QTreeWidget;
QT_END_NAMESPACE

class DatabaseManager;

/// Aggregate usage-statistics dialog (Kartend-7vi).
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
public:
  StatisticsDialog(DatabaseManager *databaseManager, const QList<CollectionConfig> *collections,
                   bool runtimeDetectionEnabled, QWidget *parent = nullptr);

private:
  void setupUI();
  void refresh();
  void populateAggregate(const UsageStatsStore::AggregateStats &agg);
  void populateMostPlayed(const QList<UsageStatsStore::ItemUsageRow> &rows);
  void populateRecentlyPlayed(const QList<UsageStatsStore::ItemUsageRow> &rows);
  void populateByCollection(const QHash<QString, UsageStatsStore::CollectionUsage> &byUuid);
  void onResetClicked();

  /// Resolves a collection's display name from its UUID using the in-memory
  /// CollectionConfig list. Returns the truncated UUID when no config matches
  /// (rows from deleted collections survive in `items`) so the user still has
  /// something readable.
  [[nodiscard]] QString labelForCollectionUuid(const QString &uuid) const;

  DatabaseManager *m_databaseManager = nullptr;
  const QList<CollectionConfig> *m_collections = nullptr;
  bool m_runtimeDetectionEnabled = false;

  QLabel *m_totalItemsValue = nullptr;
  QLabel *m_totalLaunchesValue = nullptr;
  QLabel *m_totalTimeValue = nullptr;
  QLabel *m_itemsLaunchedValue = nullptr;
  QLabel *m_runtimeNote = nullptr;
  QTabWidget *m_tabs = nullptr;
  QTreeWidget *m_mostPlayedTree = nullptr;
  QTreeWidget *m_recentlyPlayedTree = nullptr;
  QTreeWidget *m_byCollectionTree = nullptr;
};

#endif // STATISTICSDIALOG_H
