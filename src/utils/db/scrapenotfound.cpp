// Read/write access to the scrape_not_found table introduced in schema v30
// (Kartend-bb8df). Mirrors the ItemArtworkStore pattern: keyed rows survive
// item id renumbering across rescans; one statement per call on the caller's
// connection.
#include "scrapenotfound.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace ScrapeNotFoundStore {

namespace {

constexpr const char *SELECT_ONE_SQL = "SELECT lookup_key, missed_at FROM scrape_not_found "
                                       "WHERE collection_uuid = ? AND path = ? AND provider = ?";

constexpr const char *UPSERT_SQL = "INSERT INTO scrape_not_found ("
                                   "collection_uuid, path, provider, lookup_key, missed_at"
                                   ") VALUES (?,?,?,?,?) "
                                   "ON CONFLICT(collection_uuid, path, provider) DO UPDATE SET "
                                   "lookup_key=excluded.lookup_key, missed_at=excluded.missed_at";

constexpr const char *DELETE_ONE_SQL = "DELETE FROM scrape_not_found "
                                       "WHERE collection_uuid = ? AND path = ? AND provider = ?";

/// UTC ISO-8601 with milliseconds. Qt::ISODate alone drops the fraction, so
/// a stamp would no longer round-trip to the instant the caller recorded.
QString toStoredTime(const QDateTime &time) {
  return time.toUTC().toString(Qt::ISODateWithMs);
}

} // namespace

ErrorUtils::Result<std::optional<Stamp>> load(QSqlDatabase &db, const QString &collectionUuid,
                                              const QString &path, const QString &provider) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ScrapeNotFoundStore::load");
  }

  QSqlQuery q(db);
  if (!q.prepare(SELECT_ONE_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare scrape_not_found select",
                               "ScrapeNotFoundStore::load")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  q.addBindValue(provider);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query scrape_not_found",
                               "ScrapeNotFoundStore::load")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return std::optional<Stamp>(std::nullopt);
  }

  const QString storedTime = q.value(1).toString();
  const QDateTime missedAt = QDateTime::fromString(storedTime, Qt::ISODateWithMs);
  if (!missedAt.isValid()) {
    // Only a writer other than record() can leave this; refuse it rather than
    // hand back a stamp whose age cannot be judged.
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Stored scrape_not_found missed_at is not a valid timestamp",
                               "ScrapeNotFoundStore::load")
        .withDetails(storedTime);
  }

  Stamp stamp;
  stamp.collectionUuid = collectionUuid;
  stamp.path = path;
  stamp.provider = provider;
  stamp.lookupKey = q.value(0).toString();
  stamp.missedAt = missedAt.toUTC();
  return std::optional<Stamp>(stamp);
}

ErrorUtils::Result<bool> record(QSqlDatabase &db, const Stamp &stamp) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ScrapeNotFoundStore::record");
  }
  if (stamp.path.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot record a not-found stamp without a path",
                                 "ScrapeNotFoundStore::record");
  }
  if (stamp.provider.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot record a not-found stamp without a provider",
                                 "ScrapeNotFoundStore::record");
  }
  if (!stamp.missedAt.isValid()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "Cannot record a not-found stamp without a valid time",
                                 "ScrapeNotFoundStore::record");
  }

  QSqlQuery q(db);
  if (!q.prepare(UPSERT_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare scrape_not_found upsert",
                               "ScrapeNotFoundStore::record")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(stamp.collectionUuid);
  q.addBindValue(stamp.path);
  q.addBindValue(stamp.provider);
  // Bound as a non-null string: the column is NOT NULL and an empty key is a
  // legitimate (if unusual) opaque value.
  q.addBindValue(stamp.lookupKey.isNull() ? QStringLiteral("") : stamp.lookupKey);
  q.addBindValue(toStoredTime(stamp.missedAt));
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to upsert scrape_not_found",
                               "ScrapeNotFoundStore::record")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &collectionUuid,
                                const QString &path, const QString &provider) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "ScrapeNotFoundStore::remove");
  }
  QSqlQuery q(db);
  if (!q.prepare(DELETE_ONE_SQL)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                               "Failed to prepare scrape_not_found delete",
                               "ScrapeNotFoundStore::remove")
        .withDetails(q.lastError().text());
  }
  q.addBindValue(collectionUuid);
  q.addBindValue(path);
  q.addBindValue(provider);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to delete scrape_not_found",
                               "ScrapeNotFoundStore::remove")
        .withDetails(q.lastError().text());
  }
  return true;
}

} // namespace ScrapeNotFoundStore
