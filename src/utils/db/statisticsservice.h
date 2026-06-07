#ifndef KARTEND_UTILS_DB_STATISTICSSERVICE_H
#define KARTEND_UTILS_DB_STATISTICSSERVICE_H

#include <QHash>
#include <QList>
#include <QString>

#include "historystore.h"
#include "usagestatsstore.h"

// Aggregates the whole library's usage statistics into a single value bundle by
// opening a private read-only SQLite connection and running every UsageStatsStore
// / HistoryStore query. Pure over its inputs (a db file path + a recency cutoff)
// and touches no GUI or shared state, so the StatisticsDialog can run it off the
// UI thread and so the aggregation can be exercised in isolation.

namespace StatisticsService {

/// Bundle of every DB-derived figure the Statistics dialog renders. `ok` is
/// false when the worker couldn't open the database.
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

/// Opens a private read-only connection to @p dbPath and runs every stats query,
/// returning the bundle. @p cutoffIso is the ISO-8601 UTC timestamp marking the
/// start of the "played recently" window (computed by the caller so this stays a
/// pure value function). WAL mode lets the read see a consistent snapshot
/// concurrently with the main connection's writes. Touches no shared state, so
/// it is safe to run off the main thread. Returns a default-constructed snapshot
/// (ok == false) when @p dbPath is empty or the database cannot be opened.
[[nodiscard]] StatsSnapshot gather(const QString &dbPath, const QString &cutoffIso);

} // namespace StatisticsService

#endif // KARTEND_UTILS_DB_STATISTICSSERVICE_H
