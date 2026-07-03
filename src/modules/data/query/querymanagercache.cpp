// Sorted-items + queryUuids temp-table cache cluster extracted from querymanager.cpp:
//   - ensureQueryUuidsTempTable, clearQueryUuidsTempTable, populateQueryUuidsTempTable
//   - buildUuidFilterClause, computeUuidListHash, ensureQueryUuidsPopulated
//   - ensureSortedItemsCacheTable, clearSortedItemsCache, computeSortCacheHash
//   - scheduleDeferredCacheBuild, performDeferredCacheBuild, populateSortedItemsCache
// Members of QueryManager; access existing class state.
#include "querymanager.h"

#include "sqllike.h"

#include "batchsizes.h"
#include "loggingcategories.h"
#include <algorithm>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QTimer>
#include <random>

#include "errorutils.h"
#include "querycachehash.h"
#include "queryhelpers.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "searchqueryparser.h"
#include "smartfilter.h"
#include "smartplaylistevaluator.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using QueryManagerInternal::buildFtsPrefixQuery;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg

namespace {
// Builds a multi-row VALUES placeholder list, e.g. "(?, ?, ?), (?, ?, ?)" for
// `rowCount` rows of `columnsPerRow` columns each — shared by the batch INSERT
// builders so the placeholder assembly exists once (Kartend audit D-06).
QString buildRowPlaceholders(int rowCount, int columnsPerRow) {
  const QString row = QStringLiteral("(") +
                      QStringList(columnsPerRow, QStringLiteral("?")).join(QStringLiteral(", ")) +
                      QStringLiteral(")");
  return QStringList(rowCount, row).join(QStringLiteral(", "));
}
} // namespace

bool QueryManager::ensureTempTable(const char *createSql, const char *failureMessage,
                                   const char *context) {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  if (!q.exec(createSql)) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed, failureMessage, context)
            .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

void QueryManager::clearTempTable(const char *tableName) {
  if (!m_db.isOpen()) {
    return;
  }
  QSqlQuery q(m_db);
  q.exec(QStringLiteral("DELETE FROM ") + QString::fromLatin1(tableName));
}

bool QueryManager::ensureQueryUuidsTempTable() {
  return ensureTempTable("CREATE TEMP TABLE IF NOT EXISTS query_uuids (uuid TEXT PRIMARY KEY)",
                         "Failed to create query_uuids temp table",
                         "QueryManager::ensureQueryUuidsTempTable");
}

void QueryManager::clearQueryUuidsTempTable() {
  clearTempTable("query_uuids");
}

bool QueryManager::populateQueryUuidsTempTable(const QStringList &uuids) {
  if (!m_db.isOpen() || uuids.isEmpty()) {
    return false;
  }

  if (!ensureQueryUuidsTempTable()) {
    return false;
  }
  clearQueryUuidsTempTable();

  // Wrap in transaction for much faster batch inserts. Depth-tracked: when this
  // runs inside buildSortedItemsCache's transaction it joins that one rather
  // than issuing a second BEGIN (which SQLite drops) and a commit that would
  // close the outer transaction early (Kartend-gv7f).
  QueryManagerInternal::DbTransaction txn(m_db, m_txnDepth,
                                          "QueryManager::populateQueryUuidsTempTable");
  if (!txn.activeOrReport("Failed to begin transaction to populate query_uuids",
                          ErrorUtils::Severity::Warning)) {
    return false;
  }

  constexpr int BATCH_SIZE = KartendDb::BatchSizes::UuidListBatch;

  for (int batchStart = 0; batchStart < uuids.size(); batchStart += BATCH_SIZE) {
    const int batchEnd = qMin(batchStart + BATCH_SIZE, uuids.size());
    const int batchCount = batchEnd - batchStart;

    const QString sql = QStringLiteral("INSERT OR IGNORE INTO query_uuids (uuid) VALUES ") +
                        buildRowPlaceholders(batchCount, 1);

    QSqlQuery ins(m_db);
    ins.prepare(sql);
    for (int i = batchStart; i < batchEnd; ++i) {
      ins.addBindValue(uuids[i]);
    }

    if (!ins.exec()) {
      ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                                 "Failed to populate query_uuids batch",
                                                 "QueryManager::populateQueryUuidsTempTable")
                               .withDetails(ins.lastError().text()));
      return false; // txn rolls back on scope exit when it owns the transaction
    }
  }

  return txn.commit();
}

QString QueryManager::buildUuidFilterClause(const QStringList &uuids, bool &useTempTable) {
  if (uuids.size() <= MAX_UUIDS_FOR_IN_CLAUSE) {
    // Small enough - use standard IN clause with placeholders
    useTempTable = false;
    return buildUuidInClause(uuids.size());
  }

  // Too many UUIDs - use temp table. Use EXISTS which is often faster than IN
  // for subqueries
  useTempTable = true;
  // For items table: collection_uuid; for items_fts: collection_uuid
  // The caller must use this in an EXISTS clause context
  return QStringLiteral("(SELECT 1 FROM query_uuids WHERE query_uuids.uuid = collection_uuid)");
}

QByteArray QueryManager::computeUuidListHash(const QStringList &uuids) {
  // Pure generation-token hash — see querycachehash.h (Kartend-z8i0c).
  return QueryCacheHash::uuidListHash(uuids);
}

bool QueryManager::ensureQueryUuidsPopulated(const QStringList &uuids) {
  const QByteArray newHash = computeUuidListHash(uuids);

  // Skip repopulation if hash matches (same UUIDs as last query)
  if (newHash == m_cachedQueryUuidsHash) {
    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qCDebug(lcSearchDiag) << "[RangeDiag] UUID temp table cache HIT, uuids=" << uuids.size();
    }
    return true;
  }

  QElapsedTimer timer;
  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    timer.start();
    qCDebug(lcSearchDiag) << "[RangeDiag] UUID temp table cache MISS, populating" << uuids.size()
                          << "uuids...";
  }

  if (!populateQueryUuidsTempTable(uuids)) {
    // A failed repopulation leaves query_uuids in an unknown state: the
    // pre-transaction DELETE is durable when the populate ran standalone, and
    // a populate joined to an outer transaction can be rolled back later. The
    // memo must not keep claiming the previous uuid set is resident — a
    // subsequent query for that set would skip repopulation and silently
    // filter against the wrong (or empty) uuid scope.
    m_cachedQueryUuidsHash.clear();
    return false;
  }

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qCDebug(lcSearchDiag) << "[RangeDiag] UUID temp table populated in" << timer.elapsed() << "ms";
  }

  m_cachedQueryUuidsHash = newHash;
  return true;
}

// ============================================================================
// Sorted Items Cache - precomputed sort order for O(1) range lookups
// ============================================================================
// For large collections (>10k items), ORDER BY + OFFSET is extremely slow for
// high offsets. Instead, we precompute the sorted order once into a temp table
// with sequential position numbers, then use position-based range queries.

bool QueryManager::ensureSortedItemsCacheTable() {
  // position is the 0-based index in sorted order, used for instant BETWEEN queries.
  return ensureTempTable("CREATE TEMP TABLE IF NOT EXISTS sorted_items_cache ("
                         "position INTEGER PRIMARY KEY, "
                         "path TEXT NOT NULL, "
                         "uuid TEXT NOT NULL)",
                         "Failed to create sorted_items_cache temp table",
                         "QueryManager::ensureSortedItemsCacheTable");
}

void QueryManager::clearSortedItemsCache() {
  m_sortedItemsCacheValid = false;
  m_sortedItemsCacheHash.clear();
  clearTempTable("sorted_items_cache");
}

QByteArray QueryManager::computeSortCacheHash(const QStringList &uuids, const QString &filter,
                                              SortMode sortMode) {
  // Pure generation-token hash — see querycachehash.h (Kartend-z8i0c).
  // NB (Kartend-5jgg1): this hash keys ONLY on (uuids, filter, sortMode) — it
  // does NOT fold in an items-content / items-generation token, so a rescan
  // that mutates a collection's item set leaves the hash matching. Post-scan
  // freshness is therefore the invalidation hook's job, NOT the hash's: a
  // background rescan (on the scan worker) routes scan completion to this
  // worker's invalidateQueryCaches() in DatabaseManager (Kartend-6r4g2).
  return QueryCacheHash::sortCacheHash(uuids, filter, sortMode);
}

void QueryManager::scheduleDeferredCacheBuild(const QStringList &uuids, const QString &filter,
                                              SortMode sortMode) {
  // Schedule cache build to run after current event processing completes.
  // This allows the slow-path query to return immediately while the cache
  // builds in the background. Subsequent queries will use the cache once ready.
  if (m_sortCacheBuildPending) {
    return; // Already scheduled
  }

  m_sortCacheBuildPending = true;
  m_pendingCacheUuids = uuids;
  m_pendingCacheFilter = filter;
  m_pendingCacheSortMode = sortMode;

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qCDebug(lcSearchDiag) << "[RangeDiag] Deferred cache build scheduled for" << uuids.size()
                          << "uuids";
  }

  // Use queued invocation so this runs after the current function returns
  QMetaObject::invokeMethod(this, &QueryManager::performDeferredCacheBuild, Qt::QueuedConnection);
}

void QueryManager::performDeferredCacheBuild() {
  if (!m_sortCacheBuildPending) {
    return;
  }

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qCDebug(lcSearchDiag) << "[RangeDiag] Starting deferred cache build...";
  }

  (void)populateSortedItemsCache(m_pendingCacheUuids, m_pendingCacheFilter, m_pendingCacheSortMode);

  m_sortCacheBuildPending = false;
  m_pendingCacheUuids.clear();
  m_pendingCacheFilter.clear();
}

QString QueryManager::buildSortedSelectSql(const QStringList &uuids, bool useTempTable,
                                           bool needsItemsTable, bool needsCollectionJoin,
                                           bool useFts, bool isRandomSort, const QString &freeText,
                                           const QString &tokenClausesSql, SortMode sortMode) {
  QString sql;
  QString filterClause;
  if (!freeText.isEmpty() && !useFts) {
    filterClause =
        QString::fromLatin1((needsCollectionJoin || needsItemsTable) ? " AND i.name LIKE ?"
                                                                     : " AND name LIKE ?") +
        KartendDb::kLikeEscape; // Kartend-joird: bound needle is escaped
  }

  // (collection_uuid, path) is the item identity — emit one row per item so
  // same-named files across subcollections produce distinct cache entries.
  if (useFts) {
    // FTS-backed select: filter rowids via items_fts MATCH then resolve to
    // items rows for collection joining. Mirrors fetchItemsRange's FTS branch
    // so cache size matches count.
    if (needsCollectionJoin || needsItemsTable) {
      if (useTempTable) {
        sql = "SELECT i.path, i.collection_uuid FROM items i "
              "JOIN items_fts f ON f.rowid = i.id "
              "LEFT JOIN collections c ON i.collection_uuid = c.uuid "
              "WHERE f MATCH ? AND EXISTS "
              "(SELECT 1 FROM query_uuids WHERE query_uuids.uuid = i.collection_uuid)";
      } else {
        sql = "SELECT i.path, i.collection_uuid FROM items i "
              "JOIN items_fts f ON f.rowid = i.id "
              "LEFT JOIN collections c ON i.collection_uuid = c.uuid "
              "WHERE f MATCH ? AND i.collection_uuid IN " +
              buildUuidInClause(uuids.size());
      }
    } else {
      if (useTempTable) {
        sql = "SELECT path, collection_uuid FROM items_fts "
              "WHERE items_fts MATCH ? AND EXISTS "
              "(SELECT 1 FROM query_uuids WHERE query_uuids.uuid = collection_uuid)";
      } else {
        sql = "SELECT path, collection_uuid FROM items_fts "
              "WHERE items_fts MATCH ? AND collection_uuid IN " +
              buildUuidInClause(uuids.size());
      }
    }
  } else if (needsCollectionJoin || needsItemsTable) {
    // Join with collections to get collection name for sorting.
    if (useTempTable) {
      sql = "SELECT i.path, i.collection_uuid FROM items i "
            "LEFT JOIN collections c ON i.collection_uuid = c.uuid "
            "WHERE EXISTS (SELECT 1 FROM query_uuids WHERE query_uuids.uuid = "
            "i.collection_uuid)" +
            filterClause;
    } else {
      sql = "SELECT i.path, i.collection_uuid FROM items i "
            "LEFT JOIN collections c ON i.collection_uuid = c.uuid "
            "WHERE i.collection_uuid IN " +
            buildUuidInClause(uuids.size()) + filterClause;
    }
  } else {
    if (useTempTable) {
      sql = "SELECT path, collection_uuid FROM items "
            "WHERE EXISTS (SELECT 1 FROM query_uuids WHERE query_uuids.uuid = "
            "collection_uuid)" +
            filterClause;
    } else {
      sql = "SELECT path, collection_uuid FROM items "
            "WHERE collection_uuid IN " +
            buildUuidInClause(uuids.size()) + filterClause;
    }
  }

  // Append parsed token clauses (played / tag / missing:artwork / favorite)
  // before the ORDER BY so the cached result set matches what
  // fetchItemCount/fetchItemsRange would produce for the same input.
  sql += tokenClausesSql;

  // Apply sort order based on sortMode. No GROUP BY anymore, so reference
  // the per-row columns directly instead of MIN/MAX aggregates.
  //
  // Name sorts order by the `name` column, not `path`: items.path is absolute,
  // so a path sort groups items by their owning (sub)collection's directory.
  // When a shell collection aggregates several subcollections that surfaces as
  // a collection-then-name order instead of a flat name order. Ordering by
  // `name` also matches the slow-path QueryHelpers::orderByForSortMode.
  if (!isRandomSort) {
    switch (sortMode) {
    case SortMode::NameDescending:
      sql += " ORDER BY name COLLATE NOCASE DESC";
      break;
    case SortMode::DateDescending:
      sql += " ORDER BY i.last_modified DESC, path COLLATE NOCASE";
      break;
    case SortMode::DateAscending:
      sql += " ORDER BY i.last_modified ASC, path COLLATE NOCASE";
      break;
    case SortMode::SizeDescending:
      sql += " ORDER BY i.file_size DESC, path COLLATE NOCASE";
      break;
    case SortMode::SizeAscending:
      sql += " ORDER BY i.file_size ASC, path COLLATE NOCASE";
      break;
    case SortMode::CollectionAscending:
      sql += " ORDER BY c.name COLLATE NOCASE, path COLLATE NOCASE";
      break;
    case SortMode::CollectionDescending:
      sql += " ORDER BY c.name COLLATE NOCASE DESC, path COLLATE NOCASE";
      break;
    default:
      sql += " ORDER BY name COLLATE NOCASE";
      break;
    }
  }
  // For random sort, no ORDER BY - we'll shuffle in memory after fetching

  return sql;
}

bool QueryManager::insertSortedRows(QSqlQuery &selectQuery, bool isRandomSort,
                                    const QVector<SortedRow> &shuffledItems, int &position) {
  constexpr int INSERT_BATCH_SIZE = KartendDb::BatchSizes::PathInsertBatch;
  QStringList paths;
  QStringList pathUuids;
  paths.reserve(INSERT_BATCH_SIZE);
  pathUuids.reserve(INSERT_BATCH_SIZE);

  auto flushBatch = [&]() -> bool {
    if (paths.isEmpty()) return true;

    const QString insertSql =
        QStringLiteral("INSERT INTO sorted_items_cache (position, path, uuid) VALUES ") +
        buildRowPlaceholders(static_cast<int>(paths.size()), 3);

    QSqlQuery ins(m_db);
    ins.prepare(insertSql);
    int startPos = position - paths.size();
    for (int i = 0; i < paths.size(); ++i) {
      ins.addBindValue(startPos + i);
      ins.addBindValue(paths[i]);
      ins.addBindValue(pathUuids[i]);
    }

    if (!ins.exec()) {
      ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                                 "Failed to insert sorted items cache batch",
                                                 "QueryManager::populateSortedItemsCache")
                               .withDetails(ins.lastError().text()));
      return false;
    }

    paths.clear();
    pathUuids.clear();
    return true;
  };

  if (isRandomSort) {
    // Insert from shuffled vector
    for (const auto &item : shuffledItems) {
      paths.append(item.path);
      pathUuids.append(item.uuid);
      ++position;

      if (paths.size() >= INSERT_BATCH_SIZE) {
        // Kartend-kfnv7: bail per batch when ~DatabaseManager requested
        // teardown — a 100k-item build straddling shutdown used to outlive
        // the dtor's 2s wait budget (debug abort / release leak). Returning
        // false rolls the outer build transaction back via its guard.
        if (teardownRequested() || !flushBatch()) {
          return false;
        }
      }
    }
  } else {
    // Stream from query result (already ordered)
    while (selectQuery.next()) {
      paths.append(selectQuery.value(0).toString());
      pathUuids.append(selectQuery.value(1).toString());
      ++position;

      if (paths.size() >= INSERT_BATCH_SIZE) {
        // See the random-sort arm: teardown poll per batch (Kartend-kfnv7).
        if (teardownRequested() || !flushBatch()) {
          return false;
        }
      }
    }
  }

  // Flush remaining
  if (!flushBatch()) {
    return false;
  }

  return true;
}

bool QueryManager::populateSortedItemsCache(const QStringList &uuids, const QString &filter,
                                            SortMode sortMode) {
  // Kartend-kfnv7: a build queued behind teardown (e.g. the deferred
  // re-run from performDeferredCacheBuild) must not start at all.
  if (teardownRequested()) {
    return false;
  }
  if (!m_db.isOpen() || uuids.isEmpty()) {
    return false;
  }

  const QByteArray newHash = computeSortCacheHash(uuids, filter, sortMode);

  // Skip if cache is valid and hash matches
  if (m_sortedItemsCacheValid && newHash == m_sortedItemsCacheHash) {
    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qCDebug(lcSearchDiag) << "[RangeDiag] Sorted items cache HIT";
    }
    return true;
  }

  if (!ensureSortedItemsCacheTable()) {
    return false;
  }

  QElapsedTimer timer;
  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    timer.start();
    qCDebug(lcSearchDiag) << "[RangeDiag] Building sorted items cache for" << uuids.size()
                          << "uuids, filter='" << filter
                          << "', sortMode=" << static_cast<int>(sortMode);
  }

  // Outer transaction for the whole cache build. ensureQueryUuidsPopulated
  // below nests under this via the shared depth counter so the build stays
  // atomic (Kartend-gv7f). Every error path just returns false; the guard rolls
  // back on scope exit.
  QueryManagerInternal::DbTransaction txn(m_db, m_txnDepth,
                                          "QueryManager::populateSortedItemsCache");
  if (!txn.activeOrReport("Failed to begin transaction to build sorted cache",
                          ErrorUtils::Severity::Warning)) {
    return false;
  }

  // Clear existing cache
  {
    QSqlQuery clear(m_db);
    if (!clear.exec("DELETE FROM sorted_items_cache")) {
      ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                                 "Failed to clear sorted_items_cache",
                                                 "QueryManagerCache::populateSortedItemsCache")
                               .withDetails(clear.lastError().text()));
      return false;
    }
  }

  // Build the sorted result set once and insert with position numbers
  // This is the expensive operation, but we only do it once per collection load
  const QString trimmedFilter = filter.trimmed();
  // Parse out structured tokens so the cache builder applies the same
  // filter shape as fetchItemCount + fetchItemsRange. Cache invalidation
  // continues to key on raw `trimmedFilter` (computeSortCacheHash) so a
  // change in token expression rebuilds the cache.
  const SearchQueryParser::SearchQuery parsedQuery = SearchQueryParser::parse(trimmedFilter);
  const QString freeText = parsedQuery.freeText.trimmed();

  bool useTempTable = uuids.size() > MAX_UUIDS_FOR_IN_CLAUSE;
  // A repopulation of query_uuids below runs INSIDE this build's outer
  // transaction, so its rows are only durable once that transaction commits.
  // If any later step fails, the guard's rollback restores the previous uuid
  // set while the memo would keep claiming the new one is loaded — every
  // subsequent fetchItemCount/fetchItemsRange/load would then skip
  // repopulation and silently filter against the wrong uuid scope. Detect a
  // real repopulation (a memo hit doesn't touch the table, so it stays valid
  // across a rollback) and clear the memo on every failure path; the flag is
  // settled once the commit lands.
  const QByteArray uuidMemoBefore = m_cachedQueryUuidsHash;
  if (useTempTable && !ensureQueryUuidsPopulated(uuids)) {
    return false;
  }
  bool uuidMemoSettled = !useTempTable || m_cachedQueryUuidsHash == uuidMemoBefore;
  const auto uuidMemoGuard = qScopeGuard([this, &uuidMemoSettled] {
    if (!uuidMemoSettled) m_cachedQueryUuidsHash.clear();
  });

  // Some sort modes need columns that are only available on the items table.
  const bool needsItemsTable =
      (sortMode == SortMode::DateAscending || sortMode == SortMode::DateDescending ||
       sortMode == SortMode::SizeAscending || sortMode == SortMode::SizeDescending);
  // For collection sorting, we need to join with collections table.
  const bool needsCollectionJoin =
      (sortMode == SortMode::CollectionAscending || sortMode == SortMode::CollectionDescending);

  // When filtering, use FTS to match the semantics of fetchItemCount and the
  // slow-path fetchItemsRange. Mixing FTS-prefix counting with LIKE-substring
  // cache building leaves a count > cache-size mismatch, surfacing as blank
  // placeholder tiles at the tail of the result grid.
  const QString ftsQuery = (m_itemsFtsAvailable && m_itemsFtsReady && !freeText.isEmpty())
                               ? buildFtsPrefixQuery(freeText)
                               : QString();
  const bool useFts = !ftsQuery.isEmpty();

  // Token clauses use the aliased "i." prefix when the SELECT joins items
  // (date/size/collection sort modes) and bare column names otherwise.
  const QString tokenItemsAlias =
      (needsCollectionJoin || needsItemsTable) ? QStringLiteral("i") : QString();
  const QueryHelpers::SearchTokenClauses tokenClauses =
      QueryHelpers::buildSearchTokenClauses(parsedQuery, tokenItemsAlias);

  const bool isRandomSort = (sortMode == SortMode::Random);
  const QString sql =
      buildSortedSelectSql(uuids, useTempTable, needsItemsTable, needsCollectionJoin, useFts,
                           isRandomSort, freeText, tokenClauses.sql, sortMode);

  QSqlQuery selectQuery(m_db);
  selectQuery.prepare(sql);

  int bindPos = 0;
  // Bind FTS MATCH first when present (it appears first in the FTS SQL)
  if (useFts) {
    selectQuery.bindValue(bindPos++, ftsQuery);
  }
  if (!useTempTable) {
    for (const QString &uuid : uuids) {
      selectQuery.bindValue(bindPos++, uuid);
    }
  }
  if (!freeText.isEmpty() && !useFts) {
    selectQuery.bindValue(bindPos++, "%" + KartendDb::escapeLike(freeText) + "%");
  }
  for (const QVariant &v : tokenClauses.binds) {
    selectQuery.bindValue(bindPos++, v);
  }

  if (!selectQuery.exec()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to fetch sorted items for cache",
                                               "QueryManager::populateSortedItemsCache")
                             .withDetails(selectQuery.lastError().text()));
    return false;
  }

  // For random sort: collect all items, shuffle, then insert
  // For other sorts: stream directly into cache (already ordered by SQL)
  QVector<SortedRow> allItems;
  if (isRandomSort) {
    // Collect all items for shuffling
    while (selectQuery.next()) {
      allItems.append({selectQuery.value(0).toString(), selectQuery.value(1).toString()});
    }

    // Fisher-Yates shuffle. Seed mt19937's full state from std::random_device via
    // seed_seq rather than a single truncated wall-clock value: two rebuilds in the
    // same millisecond used to yield identical orders, and 32 bits weakly seed a
    // 19937-bit state (Kartend-qir8).
    std::random_device rd;
    std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
    std::mt19937 rng(seq);
    for (int i = allItems.size() - 1; i > 0; --i) {
      std::uniform_int_distribution<int> dist(0, i);
      int j = dist(rng);
      std::swap(allItems[i], allItems[j]);
    }

    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qCDebug(lcSearchDiag) << "[RangeDiag] Random sort: shuffled" << allItems.size() << "items";
    }
  }

  int position = 0;
  if (!insertSortedRows(selectQuery, isRandomSort, allItems, position)) {
    return false;
  }

  if (!txn.commitOrReport("Failed to commit sorted items cache", ErrorUtils::Severity::Warning)) {
    return false;
  }
  // Commit landed — the repopulated uuid set is durable, the memo may stand.
  uuidMemoSettled = true;

  m_sortedItemsCacheValid = true;
  m_sortedItemsCacheHash = newHash;

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qCDebug(lcSearchDiag) << "[RangeDiag] Sorted items cache built:" << position << "items in"
                          << timer.elapsed() << "ms";
  }

  return true;
}

// ============================================================================
// Playlist scope temp table
// ============================================================================
// Materialises the (uuid, path) pairs for a single playlist into a temp table
// the existing fetch SQL can join against via EXISTS. We rebuild whenever the
// cached scope key changes — the key is "playlistId|max(rowid)" so any insert
// or removal in playlist_items invalidates it without us needing explicit
// signals from PlaylistManager.

bool QueryManager::ensurePlaylistScopeTempTable() {
  return ensureTempTable("CREATE TEMP TABLE IF NOT EXISTS query_playlist_scope ("
                         "uuid TEXT NOT NULL, "
                         "path TEXT NOT NULL, "
                         "PRIMARY KEY (uuid, path))",
                         "Failed to create query_playlist_scope temp table",
                         "QueryManager::ensurePlaylistScopeTempTable");
}

bool QueryManager::populatePlaylistScopeTempTable(const QString &playlistId) {
  if (!m_db.isOpen() || playlistId.isEmpty()) {
    return false;
  }
  if (!ensurePlaylistScopeTempTable()) {
    return false;
  }

  QSqlQuery clear(m_db);
  if (!clear.exec("DELETE FROM query_playlist_scope")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to clear query_playlist_scope",
                                               "QueryManager::populatePlaylistScopeTempTable")
                             .withDetails(clear.lastError().text()));
    return false;
  }

  // Sniff the playlist row so we can branch between static (copy from
  // playlist_items) and smart (evaluate filter against items table).
  // One probe per scope-populate keeps the smart-aware path unambiguous
  // without coupling QueryManager to PlaylistManager's API.
  QSqlQuery probe(m_db);
  probe.prepare("SELECT is_smart, smart_filter FROM playlists WHERE id = ?");
  probe.addBindValue(playlistId);
  if (!probe.exec()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to probe playlist for is_smart",
                                               "QueryManager::populatePlaylistScopeTempTable")
                             .withDetails(probe.lastError().text()));
    return false;
  }
  const bool found = probe.next();
  const bool isSmart = found && probe.value(0).toInt() != 0;
  const QString smartFilterJson = found ? probe.value(1).toString() : QString();

  if (isSmart) {
    // Smart playlists ignore playlist_items entirely — the row set is
    // whatever the SmartPlaylistEvaluator returns for the persisted
    // filter. A malformed filter is logged and yields an empty scope so
    // the playlist tile renders as "(empty)" rather than failing the
    // whole fetch.
    auto filterResult = SmartFilter::fromJsonString(smartFilterJson);
    if (filterResult.isError()) {
      ErrorUtils::logError(filterResult.error());
      return true; // empty scope is still a valid populated state
    }
    const auto matches = SmartPlaylistEvaluator::evaluate(m_db, filterResult.value());

    QSqlQuery insert(m_db);
    insert.prepare("INSERT OR IGNORE INTO query_playlist_scope (uuid, path) VALUES (?, ?)");
    bool anyInsertFailed = false;
    for (const auto &m : matches) {
      insert.bindValue(0, m.collectionUuid);
      insert.bindValue(1, m.path);
      if (!insert.exec()) {
        anyInsertFailed = true;
        ErrorUtils::logError(
            ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                  "Failed to insert smart-playlist match into scope",
                                  "QueryManager::populatePlaylistScopeTempTable")
                .withDetails(insert.lastError().text()));
        // Keep going — partial scope is better than refusing the whole open.
      }
    }
    // Don't let a partially-populated scope be cached as authoritative: report
    // failure so ensurePlaylistScopePopulated rebuilds on the next fetch rather
    // than serving a truncated set for the rest of the session (Kartend-ugihh).
    return !anyInsertFailed;
  }

  // INSERT OR IGNORE because (uuid, path) is the PK; a malformed playlist_items
  // table with duplicates wouldn't break the SELECT but would fail the INSERT.
  QSqlQuery insert(m_db);
  insert.prepare("INSERT OR IGNORE INTO query_playlist_scope (uuid, path) "
                 "SELECT source_collection_uuid, source_path FROM playlist_items "
                 "WHERE playlist_id = ?");
  insert.addBindValue(playlistId);
  if (!insert.exec()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to populate query_playlist_scope",
                                               "QueryManager::populatePlaylistScopeTempTable")
                             .withDetails(insert.lastError().text()));
    return false;
  }
  return true;
}

bool QueryManager::ensurePlaylistScopePopulated(const QString &playlistId) {
  if (playlistId.isEmpty()) {
    return false;
  }

  // Sniff is_smart so we can pick the right invalidation token. Static
  // playlists key on max(rowid) of playlist_items (cheap, perfectly
  // accurate). Smart playlists hash the filter JSON: identical JSON =
  // safe to reuse the scope; new JSON (filter edited) = re-evaluate.
  // The smart key intentionally does NOT include a per-call timestamp —
  // re-evaluation on every fetch during scrolling would be wasteful.
  // Trade-off: a smart playlist won't refresh mid-session when the
  // underlying data changes (e.g. user launches a new game while
  // viewing "Recently launched"). The user re-triggers evaluation by
  // editing the filter or restarting the app. A periodic-invalidation
  // hook is a follow-up if this proves unsatisfactory.
  QSqlQuery flagProbe(m_db);
  flagProbe.prepare("SELECT is_smart, smart_filter FROM playlists WHERE id = ?");
  flagProbe.addBindValue(playlistId);
  if (!flagProbe.exec() || !flagProbe.next()) {
    return false;
  }
  const bool isSmart = flagProbe.value(0).toInt() != 0;

  QString key;
  if (isSmart) {
    // Hash the filter JSON so the cache key length stays bounded even
    // for filters with long extension lists.
    const QString filterJson = flagProbe.value(1).toString();
    key = playlistId + QStringLiteral("|smart|") + QString::number(qHash(filterJson));
  } else {
    QSqlQuery rowProbe(m_db);
    rowProbe.prepare("SELECT COALESCE(MAX(rowid), 0) FROM playlist_items WHERE playlist_id = ?");
    rowProbe.addBindValue(playlistId);
    if (!rowProbe.exec() || !rowProbe.next()) {
      return false;
    }
    key = playlistId + QStringLiteral("|") + rowProbe.value(0).toString();
  }
  if (key == m_cachedPlaylistScopeKey) {
    return true;
  }

  if (!populatePlaylistScopeTempTable(playlistId)) {
    return false;
  }
  m_cachedPlaylistScopeKey = key;
  return true;
}

QStringList QueryManager::loadPlaylistSourceUuids(const QString &playlistId) {
  QStringList result;
  if (!m_db.isOpen() || playlistId.isEmpty()) {
    return result;
  }
  QSqlQuery q(m_db);
  q.prepare("SELECT DISTINCT source_collection_uuid FROM playlist_items "
            "WHERE playlist_id = ?");
  q.addBindValue(playlistId);
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to load playlist source uuids",
                                               "QueryManager::loadPlaylistSourceUuids")
                             .withDetails(q.lastError().text()));
    return result;
  }
  while (q.next()) {
    const QString uuid = q.value(0).toString();
    if (!uuid.isEmpty()) {
      result.append(uuid);
    }
  }
  return result;
}
