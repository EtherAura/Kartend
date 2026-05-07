// Append-only chronological launch-history log (Kartend-fse).
//
// Stats (Kartend-7vi) lives on the `items` table as aggregates; history
// keeps a separate table because the dialog needs to show repeated launches
// of the same item as distinct rows ("X, Y, X, Z") rather than a deduped
// roll-up.
#include "historystore.h"

#include <algorithm>

#include <QDateTime>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "errorutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace HistoryStore {

namespace {

constexpr const char *INSERT_SQL =
    "INSERT INTO launch_history (collection_uuid, path, name, launched_at) "
    "VALUES (?, ?, ?, ?)";

constexpr const char *SELECT_RECENT_SQL = "SELECT id, collection_uuid, path, name, launched_at "
                                          "FROM launch_history ORDER BY id DESC LIMIT ?";

constexpr const char *COUNT_SQL = "SELECT COUNT(*) FROM launch_history";

// Trim by deleting any row whose id is strictly older than the Nth-most-
// recent id. A subquery with LIMIT/OFFSET is the simplest way to express
// "keep the most recent N rows" in SQLite without a window function. The
// strict `<` matters: with `<=` we'd also drop the Nth-most-recent row
// itself, leaving only N-1 rows behind. When the table holds <= N rows,
// OFFSET runs past the end, the subquery returns NULL, and `id < NULL` is
// false — DELETE removes nothing.
constexpr const char *TRIM_SQL = "DELETE FROM launch_history WHERE id < "
                                 "(SELECT id FROM launch_history "
                                 "ORDER BY id DESC LIMIT 1 OFFSET ?)";

constexpr const char *CLEAR_SQL = "DELETE FROM launch_history";

int clampLimit(int limit) {
  return std::clamp(limit, 1, 100000);
}

} // namespace

ErrorUtils::Result<bool> recordLaunch(QSqlDatabase &db, const QString &collectionUuid,
                                      const QString &path, const QString &name) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "HistoryStore::recordLaunch");
  }
  if (collectionUuid.isEmpty() || path.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot record history without collection uuid + path",
                                 "HistoryStore::recordLaunch");
  }
  QSqlQuery q(db);
  if (!q.prepare(INSERT_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to prepare history insert",
                               "HistoryStore::recordLaunch")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  // Denormalize the visible name so deletion of the items row doesn't
  // leave history rows looking blank.
  q.addBindValue(name.isEmpty() ? QFileInfo(path).completeBaseName() : name);
  q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to record history entry",
                               "HistoryStore::recordLaunch")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<QList<HistoryEntry>> loadRecent(QSqlDatabase &db, int limit) {
  QList<HistoryEntry> out;
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "HistoryStore::loadRecent");
  }
  QSqlQuery q(db);
  if (!q.prepare(SELECT_RECENT_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to prepare history select",
                               "HistoryStore::loadRecent")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(clampLimit(limit));
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load history",
                               "HistoryStore::loadRecent")
        .withDetails(q.lastError().text());
  }
  while (q.next()) {
    HistoryEntry entry;
    entry.id = q.value(0).toLongLong();
    entry.collectionUuid = q.value(1).toString();
    entry.path = q.value(2).toString();
    entry.name = q.value(3).toString();
    entry.launchedAt = q.value(4).toString();
    out.append(entry);
  }
  return out;
}

ErrorUtils::Result<qint64> count(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "HistoryStore::count");
  }
  QSqlQuery q(db);
  if (!q.exec(COUNT_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to count history rows",
                               "HistoryStore::count")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return qint64(0);
  }
  return q.value(0).toLongLong();
}

ErrorUtils::Result<qint64> trimToMaxEntries(QSqlDatabase &db, qint64 maxEntries) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "HistoryStore::trimToMaxEntries");
  }
  if (maxEntries <= 0) {
    // A non-positive cap means "unlimited" — caller opted out of trimming.
    return qint64(0);
  }
  QSqlQuery q(db);
  if (!q.prepare(TRIM_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to prepare history trim",
                               "HistoryStore::trimToMaxEntries")
        .withDetails(q.lastError().text());
  }
  // OFFSET is zero-based: OFFSET=maxEntries-1 picks the (maxEntries)th
  // newest id (the cutoff we want to keep). The DELETE clause uses strict
  // `<`, so the cutoff row itself survives and exactly maxEntries rows
  // remain.
  q.addBindValue(maxEntries - 1);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to trim history",
                               "HistoryStore::trimToMaxEntries")
        .withDetails(q.lastError().text());
  }
  return static_cast<qint64>(q.numRowsAffected());
}

ErrorUtils::Result<bool> clearAll(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "HistoryStore::clearAll");
  }
  QSqlQuery q(db);
  if (!q.exec(CLEAR_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to clear history",
                               "HistoryStore::clearAll")
        .withDetails(q.lastError().text());
  }
  return true;
}

} // namespace HistoryStore
