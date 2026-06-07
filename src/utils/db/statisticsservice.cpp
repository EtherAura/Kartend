// Off-thread aggregation of library-wide usage statistics. See the header for
// the contract; the body mirrors what the Statistics dialog used to run inline,
// moved here so the logic is reusable and testable without the dialog.
#include "statisticsservice.h"

#include <algorithm>
#include <atomic>

#include <QSqlDatabase>
#include <QSqlQuery>

#include "historystore.h"
#include "usagestatsstore.h"

namespace StatisticsService {

namespace {
constexpr int TOP_LIST_LIMIT = 50;
constexpr int RECENT_LIST_LIMIT = 50;
constexpr int NEVER_LIST_LIMIT = 50;
constexpr int HISTORY_LIST_LIMIT = 1000;
} // namespace

StatsSnapshot gather(const QString &dbPath, const QString &cutoffIso) {
  StatsSnapshot s;
  if (dbPath.isEmpty()) {
    return s;
  }
  // Unique connection name per call so concurrent / sequential gathers never
  // collide in QSqlDatabase's global registry.
  static std::atomic<quint64> connSeq{0};
  const QString conn =
      QStringLiteral("kartend_stats_%1").arg(connSeq.fetch_add(1, std::memory_order_relaxed));
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dbPath);
    if (db.open()) {
      // Wait briefly for a concurrent writer rather than failing the read.
      QSqlQuery(db).exec(QStringLiteral("PRAGMA busy_timeout = 3000"));
      if (auto r = UsageStatsStore::loadAggregate(db); r.isOk()) {
        s.agg = r.value();
      }
      if (auto r = UsageStatsStore::countPlayedSince(db, cutoffIso); r.isOk()) {
        s.played7Days = r.value();
      }
      if (auto r = UsageStatsStore::loadTopPlayed(db, TOP_LIST_LIMIT); r.isOk()) {
        s.topPlayed = r.value();
      }
      if (auto r = UsageStatsStore::loadRecentlyPlayed(db, RECENT_LIST_LIMIT); r.isOk()) {
        s.recentlyPlayed = r.value();
      }
      if (auto r = UsageStatsStore::loadNeverPlayed(db, NEVER_LIST_LIMIT); r.isOk()) {
        s.neverPlayed = r.value();
      }
      if (auto r = UsageStatsStore::loadCollectionBreakdown(db); r.isOk()) {
        s.byCollection = r.value();
      }
      if (auto r = HistoryStore::loadRecent(db, HISTORY_LIST_LIMIT); r.isOk()) {
        s.history = r.value();
      }
      if (auto r = HistoryStore::count(db); r.isOk()) {
        s.historyCount = r.value();
      }
      s.totalNever = std::max<qint64>(0, s.agg.totalItems - s.agg.itemsLaunchedAtLeastOnce);
      s.ok = true;
    }
  }
  // db is out of scope (and the store helpers destroyed their queries) before
  // removeDatabase, so Qt doesn't warn about a connection still in use.
  QSqlDatabase::removeDatabase(conn);
  return s;
}

} // namespace StatisticsService
