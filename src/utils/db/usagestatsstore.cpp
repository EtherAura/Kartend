// Read/write access to per-item usage statistics on the items table.
//
// Three columns power:
//   - play_count          (v1)   incremented on launch
//   - last_played         (v1)   stamped on launch (ISO-8601 UTC)
//   - total_play_seconds  (v7)   accumulated runtime when runtime detection is on
//
// All writes are best-effort; failures are surfaced as ErrorContext but the
// caller logs and continues so usage tracking never blocks a launch.
#include "usagestatsstore.h"

#include "detailsformat.h"

#include <algorithm>

#include <QDateTime>
#include <QLocale>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

#include "errorutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace UsageStatsStore {

namespace {

constexpr const char *SELECT_ITEM_SQL = "SELECT play_count, last_played, total_play_seconds "
                                        "FROM items WHERE collection_uuid = ? AND path = ?";

constexpr const char *RECORD_LAUNCH_SQL =
    "UPDATE items SET play_count = COALESCE(play_count, 0) + 1, "
    "last_played = ? "
    "WHERE collection_uuid = ? AND path = ?";

constexpr const char *RECORD_SESSION_SQL =
    "UPDATE items SET total_play_seconds = COALESCE(total_play_seconds, 0) + ? "
    "WHERE collection_uuid = ? AND path = ?";

constexpr const char *AGGREGATE_SQL =
    "SELECT COUNT(*), "
    "COALESCE(SUM(play_count), 0), "
    "COALESCE(SUM(total_play_seconds), 0), "
    "COALESCE(SUM(CASE WHEN play_count > 0 THEN 1 ELSE 0 END), 0) "
    "FROM items";

constexpr const char *TOP_PLAYED_SQL =
    "SELECT name, collection_uuid, path, play_count, last_played, "
    "total_play_seconds "
    "FROM items WHERE play_count > 0 "
    "ORDER BY play_count DESC, last_played DESC "
    "LIMIT ?";

constexpr const char *RECENTLY_PLAYED_SQL =
    "SELECT name, collection_uuid, path, play_count, last_played, "
    "total_play_seconds "
    "FROM items WHERE last_played IS NOT NULL AND last_played != '' "
    "ORDER BY last_played DESC "
    "LIMIT ?";

constexpr const char *NEVER_PLAYED_SQL =
    "SELECT name, collection_uuid, path, play_count, last_played, "
    "total_play_seconds "
    "FROM items WHERE play_count = 0 OR play_count IS NULL "
    "ORDER BY name COLLATE NOCASE ASC "
    "LIMIT ?";

constexpr const char *COUNT_PLAYED_SINCE_SQL =
    "SELECT COUNT(*) FROM items "
    "WHERE last_played IS NOT NULL AND last_played != '' AND last_played >= ?";

constexpr const char *COLLECTION_BREAKDOWN_SQL = "SELECT collection_uuid, COUNT(*), "
                                                 "COALESCE(SUM(play_count), 0), "
                                                 "COALESCE(SUM(total_play_seconds), 0) "
                                                 "FROM items GROUP BY collection_uuid";

constexpr const char *RESET_ALL_SQL = "UPDATE items SET play_count = 0, last_played = NULL, "
                                      "total_play_seconds = 0";

int clampLimit(int limit) {
  return std::clamp(limit, 1, 1000);
}

} // namespace

ErrorUtils::Result<ItemUsageStats> loadForItem(QSqlDatabase &db, const QString &collectionUuid,
                                               const QString &path) {
  ItemUsageStats stats;
  stats.collectionUuid = collectionUuid;
  stats.path = path;

  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "UsageStatsStore::loadForItem");
  }

  QSqlQuery q(db);
  if (!q.prepare(SELECT_ITEM_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to prepare usage select",
                               "UsageStatsStore::loadForItem")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query usage stats",
                               "UsageStatsStore::loadForItem")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return stats; // No matching item -> empty stats with keys preserved.
  }
  stats.playCount = q.value(0).toLongLong();
  const QVariant lastPlayed = q.value(1);
  stats.lastPlayed = lastPlayed.isNull() ? QString() : lastPlayed.toString();
  stats.totalPlaySeconds = q.value(2).toLongLong();
  return stats;
}

ErrorUtils::Result<bool> recordLaunch(QSqlDatabase &db, const QString &collectionUuid,
                                      const QString &path) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "UsageStatsStore::recordLaunch");
  }
  if (collectionUuid.isEmpty() || path.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot record launch without collection uuid + path",
                                 "UsageStatsStore::recordLaunch");
  }
  QSqlQuery q(db);
  if (!q.prepare(RECORD_LAUNCH_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare record-launch update",
                               "UsageStatsStore::recordLaunch")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to record launch",
                               "UsageStatsStore::recordLaunch")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<bool> recordPlaySession(QSqlDatabase &db, const QString &collectionUuid,
                                           const QString &path, qint64 seconds) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "UsageStatsStore::recordPlaySession");
  }
  if (collectionUuid.isEmpty() || path.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot record session without collection uuid + path",
                                 "UsageStatsStore::recordPlaySession");
  }
  if (seconds <= 0) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Refusing to record non-positive session duration",
                                 "UsageStatsStore::recordPlaySession");
  }
  QSqlQuery q(db);
  if (!q.prepare(RECORD_SESSION_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare record-session update",
                               "UsageStatsStore::recordPlaySession")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(seconds);
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to record play session",
                               "UsageStatsStore::recordPlaySession")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<AggregateStats> loadAggregate(QSqlDatabase &db) {
  AggregateStats agg;
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "UsageStatsStore::loadAggregate");
  }
  QSqlQuery q(db);
  if (!q.exec(AGGREGATE_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load aggregate stats",
                               "UsageStatsStore::loadAggregate")
        .withDetails(q.lastError().text());
  }
  if (q.next()) {
    agg.totalItems = q.value(0).toLongLong();
    agg.totalLaunches = q.value(1).toLongLong();
    agg.totalPlaySeconds = q.value(2).toLongLong();
    agg.itemsLaunchedAtLeastOnce = q.value(3).toLongLong();
  }
  return agg;
}

namespace {
ErrorUtils::Result<QList<ItemUsageRow>> runRowQuery(QSqlDatabase &db, const char *sql, int limit,
                                                    const char *origin) {
  QList<ItemUsageRow> out;
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open", origin);
  }
  QSqlQuery q(db);
  if (!q.prepare(QString::fromUtf8(sql))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to prepare usage query",
                               origin)
        .withDetails(q.lastError().text());
  }
  q.addBindValue(clampLimit(limit));
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query usage rows", origin)
        .withDetails(q.lastError().text());
  }
  while (q.next()) {
    ItemUsageRow row;
    row.name = q.value(0).toString();
    row.collectionUuid = q.value(1).toString();
    row.path = q.value(2).toString();
    row.playCount = q.value(3).toLongLong();
    row.lastPlayed = q.value(4).isNull() ? QString() : q.value(4).toString();
    row.totalPlaySeconds = q.value(5).toLongLong();
    out.append(row);
  }
  return out;
}
} // namespace

ErrorUtils::Result<QList<ItemUsageRow>> loadTopPlayed(QSqlDatabase &db, int limit) {
  return runRowQuery(db, TOP_PLAYED_SQL, limit, "UsageStatsStore::loadTopPlayed");
}

ErrorUtils::Result<QList<ItemUsageRow>> loadRecentlyPlayed(QSqlDatabase &db, int limit) {
  return runRowQuery(db, RECENTLY_PLAYED_SQL, limit, "UsageStatsStore::loadRecentlyPlayed");
}

ErrorUtils::Result<QList<ItemUsageRow>> loadNeverPlayed(QSqlDatabase &db, int limit) {
  return runRowQuery(db, NEVER_PLAYED_SQL, limit, "UsageStatsStore::loadNeverPlayed");
}

ErrorUtils::Result<qint64> countPlayedSince(QSqlDatabase &db, const QString &isoCutoffUtc) {
  if (isoCutoffUtc.isEmpty()) {
    return qint64(0);
  }
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "UsageStatsStore::countPlayedSince");
  }
  QSqlQuery q(db);
  if (!q.prepare(COUNT_PLAYED_SINCE_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare played-since count",
                               "UsageStatsStore::countPlayedSince")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(isoCutoffUtc);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query played-since count",
                               "UsageStatsStore::countPlayedSince")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return qint64(0);
  }
  return q.value(0).toLongLong();
}

ErrorUtils::Result<QHash<QString, CollectionUsage>> loadCollectionBreakdown(QSqlDatabase &db) {
  QHash<QString, CollectionUsage> out;
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "UsageStatsStore::loadCollectionBreakdown");
  }
  QSqlQuery q(db);
  if (!q.exec(COLLECTION_BREAKDOWN_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to load per-collection usage",
                               "UsageStatsStore::loadCollectionBreakdown")
        .withDetails(q.lastError().text());
  }
  while (q.next()) {
    CollectionUsage row;
    row.collectionUuid = q.value(0).toString();
    row.itemCount = q.value(1).toLongLong();
    row.totalLaunches = q.value(2).toLongLong();
    row.totalPlaySeconds = q.value(3).toLongLong();
    out.insert(row.collectionUuid, row);
  }
  return out;
}

ErrorUtils::Result<bool> resetAll(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "UsageStatsStore::resetAll");
  }
  QSqlQuery q(db);
  if (!q.exec(RESET_ALL_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to reset usage stats",
                               "UsageStatsStore::resetAll")
        .withDetails(q.lastError().text());
  }
  return true;
}

QString formatDuration(qint64 seconds) {
  // Kartend-vbu6y: delegate to the canonical h/m/s formatter so the format
  // lives in one place. Keep the <= 0 guard — formatDuration intentionally
  // hides a zero/negative duration, whereas DetailsFormat::formatRuntime would
  // render "0s". (A play-time total exceeding INT_MAX seconds is ~68 years, so
  // the narrowing cast is safe.)
  if (seconds <= 0) {
    return {};
  }
  return DetailsFormat::formatRuntime(static_cast<int>(seconds));
}

QString formatTimestamp(const QString &isoUtc) {
  if (isoUtc.isEmpty()) {
    return {};
  }
  // Strict ISO parse first so we can convert UTC -> local for the user. Fall
  // back to the original string if the column ever holds a value we didn't
  // write (defensive — keeps the dialog from rendering blank cells).
  QDateTime parsed = QDateTime::fromString(isoUtc, Qt::ISODate);
  if (!parsed.isValid()) {
    return isoUtc;
  }
  parsed.setTimeZone(QTimeZone::utc());
  const QDateTime local = parsed.toLocalTime();
  return QLocale().toString(local, QLocale::ShortFormat);
}

} // namespace UsageStatsStore
