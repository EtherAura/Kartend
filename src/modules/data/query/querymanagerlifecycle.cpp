// Collection lifecycle persistence methods extracted from querymanager.cpp:
//   - loadItemsFromDatabaseByUuid (~30 LOC)
//   - invalidateCollectionCache (~13 LOC)
// All remain QueryManager members and access existing class state.
//
// saveItemsToDatabase moved into ScanService (scanservice.{h,cpp}).
// clearCollectionFromDatabaseByUuid became a free function in
// querymanagerhelpers.h::QueryManagerInternal.
#include "querymanager.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QThread>

#include "errorutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

QStringList QueryManager::loadItemsFromDatabaseByUuid(const QString &collectionUuid,
                                                      QHash<QString, QDateTime> *timestamps,
                                                      QHash<QString, qint64> *sizes) {
  QStringList filePaths;

  if (!m_db.isOpen()) {
    auto err =
        ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database is not open, cannot load items",
                            "QueryManager::loadItemsFromDatabaseByUuid");
    ErrorUtils::logError(err);
    return filePaths;
  }

  // Use cached prepared statement for loading items
  QSqlQuery &query = getPreparedStatement(QuerySQL::LOAD_ITEMS_BY_UUID);
  query.bindValue(0, collectionUuid);

  if (!query.exec()) {
    auto err =
        ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load items from database",
                            "QueryManager::loadItemsFromDatabaseByUuid")
            .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
    return filePaths;
  }

  // The SQL used to be SELECT DISTINCT over the path column alone; with
  // last_modified/file_size now in the projection (Kartend-m9r1s) the dedup
  // moved here so rows sharing a path can't reappear as duplicates.
  QSet<QString> seenPaths;
  while (query.next()) {
    const QString path = query.value(0).toString();
    if (seenPaths.contains(path)) {
      continue;
    }
    seenPaths.insert(path);
    filePaths.append(path);

    // Hand back the persisted sort metadata so Date/Size sorts don't have to
    // re-stat every file (Kartend-m9r1s). Invalid timestamps and non-positive
    // sizes are intentionally NOT inserted: sortFiles falls back to a stat for
    // missing keys, which keeps rows the streaming scan pipeline persisted
    // without a file_size (DEFAULT 0) sorting by their real on-disk size.
    if (timestamps) {
      const QDateTime mtime = QDateTime::fromString(query.value(1).toString(), Qt::ISODate);
      if (mtime.isValid()) {
        timestamps->insert(path, mtime);
      }
    }
    if (sizes) {
      const qint64 size = query.value(2).toLongLong();
      if (size > 0) {
        sizes->insert(path, size);
      }
    }
  }

  return filePaths;
}

void QueryManager::invalidateQueryCaches() {
  assertOwnerThread();
  // Drop the in-memory + temp-table query caches without touching item rows.
  // The sorted-items cache validity hash keys on (collection uuids, filter,
  // sortMode) — NOT item contents — so adding/removing items in an existing
  // collection (e.g. a background rescan) leaves the hash matching and the
  // cache serving stale ranges/counts. The scan worker that commits those
  // changes is a different QueryManager instance, so it can't clear this
  // (query) worker's caches itself; DatabaseManager wires scan completion to
  // this slot (Kartend-6r4g2 / Kartend-fkvs).
  m_cachedQueryUuidsHash.clear();
  clearSortedItemsCache();
  m_cachedPlaylistScopeKey.clear();
  // Kartend-4fmu1: also drop the cross-load canonical-path cache. Its mtime
  // stamps catch touched/replaced files, but a rescan can retarget a symlink
  // at a different file with an unchanged mtime — re-resolving after
  // invalidation keeps the flattened-view dedup honest. Also prunes entries
  // for files deleted since they were cached.
  m_persistentCanonicalPaths.clear();
}

void QueryManager::invalidateCollectionCache(const QString &collectionUuid) {
  assertOwnerThread();
  // Cancel any ongoing scan before clearing cache to prevent lock conflicts
  requestCancelScan();

  // Drop the query caches (uuid temp table, sorted-items, playlist scope).
  invalidateQueryCaches();

  QueryManagerInternal::clearCollectionFromDatabaseByUuid(m_db, m_statementCache, collectionUuid);
  emit cacheInvalidated(collectionUuid);
}

void QueryManager::invalidateSmartPlaylistScope() {
  assertOwnerThread();
  // Clearing the key alone is enough: the next ensurePlaylistScopePopulated
  // sees a key mismatch and re-evaluates the filter against current item data.
  m_cachedPlaylistScopeKey.clear();
}

void QueryManager::runWrite(const std::function<bool(QSqlDatabase &)> &op, const QString &context) {
  assertOwnerThread();
  if (!op) {
    return;
  }
  // Kartend-dbqt5: the worker connection opens lazily on the first query
  // slot. A queued write dispatched before any read (launch-on-startup
  // stats, the startup orphan purge) used to be SILENTLY dropped here when
  // no query had opened the connection yet — ensure it like the query
  // slots do (ensureDatabaseAvailable falls back to full init on a
  // never-registered cold-start connection; ensureDatabaseConnection alone
  // refuses those).
  if (!m_db.isOpen() && !ensureDatabaseAvailable("QueryManager::runWrite")) {
    return;
  }
  // Kartend-cbtml: the scan worker is a SEPARATE connection that holds long
  // batched write transactions; this connection's busy_timeout is
  // deliberately short (WORKER_BUSY_TIMEOUT_MS — fail-fast for the UI query
  // slots), so a launch / history write landing inside a scan-commit window
  // used to fail with SQLITE_BUSY and be dropped silently. Retry the op with
  // the same bounded exponential backoff as the scanservice retry ladder
  // (Kartend-6yhq: 100/200/400/800ms, worst-case 1.5s of sleep) — each
  // attempt additionally waits busy_timeout inside SQLite before reporting
  // busy. The op returns true when settled (success, or a permanent failure
  // it already logged) and false only on transient lock contention.
  constexpr int MAX_ATTEMPTS = 5;
  constexpr int BASE_DELAY_MS = 100;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt) {
    if (op(m_db)) {
      if (attempt > 1) {
        qCWarning(lcQueryManager).nospace() << context << ": worker write landed on attempt "
                                            << attempt << " after database-is-locked contention";
      }
      return;
    }
    if (attempt == MAX_ATTEMPTS) {
      break;
    }
    // Mirror the Kartend-kfnv7 teardown bail: sleeping through the remaining
    // backoff could exceed the destructor's wait budget.
    if (teardownRequested()) {
      qCWarning(lcQueryManager).nospace()
          << context << ": worker write abandoned during teardown (attempt " << attempt << "/"
          << MAX_ATTEMPTS << ", database locked)";
      return;
    }
    qCWarning(lcQueryManager).nospace()
        << context << ": worker write hit database-is-locked (attempt " << attempt << "/"
        << MAX_ATTEMPTS << "), backing off " << (BASE_DELAY_MS * (1 << (attempt - 1))) << "ms";
    QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
  }
  qCWarning(lcQueryManager).nospace()
      << context << ": worker write LOST after " << MAX_ATTEMPTS
      << " attempts — database stayed locked (long-running scan transaction?)";
}
