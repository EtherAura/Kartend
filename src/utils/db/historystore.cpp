// Append-only chronological launch-history log.
//
// Stats lives on the `items` table as aggregates; history
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

// Kartend-neb85: ordered by launched_at, NOT by id. Ids stopped tracking launch
// order once launches began carrying their queue-time stamp — a lock-contended
// write inserts later (higher id) while carrying an earlier launched_at. id
// DESC remains as the tiebreaker for same-second launches; launched_at is
// ISO-8601 to one-second resolution and its fixed-width format sorts correctly
// as text, so a lexicographic ORDER BY is a true chronological order.
constexpr const char *SELECT_RECENT_SQL = "SELECT id, collection_uuid, path, name, launched_at "
                                          "FROM launch_history "
                                          "ORDER BY launched_at DESC, id DESC LIMIT ?";

constexpr const char *COUNT_SQL = "SELECT COUNT(*) FROM launch_history";

// Keep the N newest rows under the same (launched_at, id) order the reader
// uses; delete the rest. Kartend-neb85 replaced an `id < (… LIMIT 1 OFFSET ?)`
// cutoff, which expressed "keep the N highest ids" — after the stamp change
// that could evict a genuinely NEWER entry in favour of a deferred older one.
// NOT IN over an explicit keep-set states the intent directly and sidesteps the
// old form's strict-`<` and NULL-at-OFFSET subtleties: when the table holds
// <= N rows the subquery returns every id and nothing is deleted. `id` is the
// primary key, so it is never NULL — which is the one input NOT IN mishandles.
constexpr const char *TRIM_SQL = "DELETE FROM launch_history WHERE id NOT IN "
                                 "(SELECT id FROM launch_history "
                                 "ORDER BY launched_at DESC, id DESC LIMIT ?)";

constexpr const char *CLEAR_SQL = "DELETE FROM launch_history";

int clampLimit(int limit) {
  return std::clamp(limit, 1, 100000);
}

} // namespace

ErrorUtils::Result<bool> recordLaunch(QSqlDatabase &db, const QString &collectionUuid,
                                      const QString &path, const QString &name,
                                      const QDateTime &stamp) {
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
  // Kartend-neb85: the caller's launch-time stamp when it has one; now only as
  // a fallback (see the header for why the worker path must not stamp here).
  q.addBindValue((stamp.isValid() ? stamp : QDateTime::currentDateTimeUtc()).toString(Qt::ISODate));
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
  // Kartend-neb85: binds the keep-COUNT directly. The previous form bound an
  // OFFSET (maxEntries - 1) to locate a cutoff id; the NOT IN keep-set takes
  // the limit itself, so exactly maxEntries rows survive.
  q.addBindValue(maxEntries);
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
