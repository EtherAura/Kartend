// Sorted-items + queryUuids temp-table cache cluster extracted from querymanager.cpp:
//   - ensureQueryUuidsTempTable, clearQueryUuidsTempTable, populateQueryUuidsTempTable
//   - buildUuidFilterClause, computeUuidListHash, ensureQueryUuidsPopulated
//   - ensureSortedItemsCacheTable, clearSortedItemsCache, computeSortCacheHash
//   - scheduleDeferredCacheBuild, performDeferredCacheBuild, populateSortedItemsCache
// Members of QueryManager; access existing class state.
#include "querymanager.h"

#include "loggingcategories.h"
#include <algorithm>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QTimer>
#include <random>

#include "errorutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "uiconstants.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using QueryManagerInternal::buildFtsPrefixQuery;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg

bool QueryManager::ensureQueryUuidsTempTable() {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS query_uuids (uuid TEXT PRIMARY "
              "KEY)")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to create query_uuids temp table",
                                               "QueryManager::ensureQueryUuidsTempTable")
                             .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

void QueryManager::clearQueryUuidsTempTable() {
  if (!m_db.isOpen()) {
    return;
  }
  QSqlQuery q(m_db);
  q.exec("DELETE FROM query_uuids");
}

bool QueryManager::populateQueryUuidsTempTable(const QStringList &uuids) {
  if (!m_db.isOpen() || uuids.isEmpty()) {
    return false;
  }

  if (!ensureQueryUuidsTempTable()) {
    return false;
  }
  clearQueryUuidsTempTable();

  // Wrap in transaction for much faster batch inserts
  m_db.transaction();

  // Insert UUIDs in batches to stay under SQLite variable limit
  // Each row has 1 column, so batch size can be up to 999
  constexpr int BATCH_SIZE = 500;

  for (int batchStart = 0; batchStart < uuids.size(); batchStart += BATCH_SIZE) {
    const int batchEnd = qMin(batchStart + BATCH_SIZE, uuids.size());
    const int batchCount = batchEnd - batchStart;

    QString sql = "INSERT OR IGNORE INTO query_uuids (uuid) VALUES ";
    QStringList placeholders;
    placeholders.reserve(batchCount);
    for (int i = 0; i < batchCount; ++i) {
      placeholders.append("(?)");
    }
    sql += placeholders.join(", ");

    QSqlQuery ins(m_db);
    ins.prepare(sql);
    for (int i = batchStart; i < batchEnd; ++i) {
      ins.addBindValue(uuids[i]);
    }

    if (!ins.exec()) {
      m_db.rollback();
      ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                                 "Failed to populate query_uuids batch",
                                                 "QueryManager::populateQueryUuidsTempTable")
                               .withDetails(ins.lastError().text()));
      return false;
    }
  }

  m_db.commit();
  return true;
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
  // Fast hash of UUID list - concatenate sizes and first/last UUIDs
  // Full hash would be expensive for 3000+ UUIDs on every call
  QByteArray data;
  data.reserve(128);
  data.append(QByteArray::number(uuids.size()));
  if (!uuids.isEmpty()) {
    data.append(uuids.first().toUtf8());
    if (uuids.size() > 1) {
      data.append(uuids.last().toUtf8());
    }
    // Include a middle element for extra collision resistance
    if (uuids.size() > 2) {
      data.append(uuids[uuids.size() / 2].toUtf8());
    }
  }
  return QCryptographicHash::hash(data, QCryptographicHash::Md5);
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
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  // position is the 0-based index in sorted order, used for instant BETWEEN
  // queries
  if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS sorted_items_cache ("
              "position INTEGER PRIMARY KEY, "
              "path TEXT NOT NULL, "
              "uuid TEXT NOT NULL)")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to create sorted_items_cache temp table",
                                               "QueryManager::ensureSortedItemsCacheTable")
                             .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

void QueryManager::clearSortedItemsCache() {
  m_sortedItemsCacheValid = false;
  m_sortedItemsCacheHash.clear();
  if (!m_db.isOpen()) {
    return;
  }
  QSqlQuery q(m_db);
  q.exec("DELETE FROM sorted_items_cache");
}

QByteArray QueryManager::computeSortCacheHash(const QStringList &uuids, const QString &filter,
                                              SortMode sortMode) {
  // Hash of UUIDs + filter + sortMode to detect when cache needs rebuilding
  QByteArray data;
  data.reserve(256);
  data.append(QByteArray::number(uuids.size()));
  if (!uuids.isEmpty()) {
    data.append(uuids.first().toUtf8());
    if (uuids.size() > 1) {
      data.append(uuids.last().toUtf8());
    }
    if (uuids.size() > 2) {
      data.append(uuids[uuids.size() / 2].toUtf8());
    }
  }
  data.append(filter.toUtf8());
  data.append(QByteArray::number(static_cast<int>(sortMode)));
  return QCryptographicHash::hash(data, QCryptographicHash::Md5);
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

bool QueryManager::populateSortedItemsCache(const QStringList &uuids, const QString &filter,
                                            SortMode sortMode) {
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

  m_db.transaction();

  // Clear existing cache
  {
    QSqlQuery clear(m_db);
    if (!clear.exec("DELETE FROM sorted_items_cache")) {
      m_db.rollback();
      return false;
    }
  }

  // Build the sorted result set once and insert with position numbers
  // This is the expensive operation, but we only do it once per collection load
  const QString trimmedFilter = filter.trimmed();

  bool useTempTable = uuids.size() > MAX_UUIDS_FOR_IN_CLAUSE;
  if (useTempTable && !ensureQueryUuidsPopulated(uuids)) {
    m_db.rollback();
    return false;
  }

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
  const QString ftsQuery = (m_itemsFtsAvailable && m_itemsFtsReady && !trimmedFilter.isEmpty())
                               ? buildFtsPrefixQuery(trimmedFilter)
                               : QString();
  const bool useFts = !ftsQuery.isEmpty();

  QString sql;
  QString filterClause;
  if (!trimmedFilter.isEmpty() && !useFts) {
    filterClause =
        (needsCollectionJoin || needsItemsTable) ? " AND i.name LIKE ?" : " AND name LIKE ?";
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

  // Apply sort order based on sortMode. No GROUP BY anymore, so reference
  // the per-row columns directly instead of MIN/MAX aggregates.
  const bool isRandomSort = (sortMode == SortMode::Random);
  if (!isRandomSort) {
    switch (sortMode) {
    case SortMode::NameDescending:
      sql += " ORDER BY path COLLATE NOCASE DESC";
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
      sql += " ORDER BY path COLLATE NOCASE";
      break;
    }
  }
  // For random sort, no ORDER BY - we'll shuffle in memory after fetching

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
  if (!trimmedFilter.isEmpty() && !useFts) {
    selectQuery.bindValue(bindPos++, "%" + trimmedFilter + "%");
  }

  if (!selectQuery.exec()) {
    m_db.rollback();
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to fetch sorted items for cache",
                                               "QueryManager::populateSortedItemsCache")
                             .withDetails(selectQuery.lastError().text()));
    return false;
  }

  // For random sort: collect all items, shuffle, then insert
  // For other sorts: stream directly into cache (already ordered by SQL)
  struct PathUuidPair {
    QString path;
    QString uuid;
  };

  QVector<PathUuidPair> allItems;
  if (isRandomSort) {
    // Collect all items for shuffling
    while (selectQuery.next()) {
      allItems.append({selectQuery.value(0).toString(), selectQuery.value(1).toString()});
    }

    // Fisher-Yates shuffle
    auto seed = static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch());
    std::mt19937 rng(seed);
    for (int i = allItems.size() - 1; i > 0; --i) {
      std::uniform_int_distribution<int> dist(0, i);
      int j = dist(rng);
      std::swap(allItems[i], allItems[j]);
    }

    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qCDebug(lcSearchDiag) << "[RangeDiag] Random sort: shuffled" << allItems.size() << "items";
    }
  }

  // Insert in batches for efficiency
  constexpr int INSERT_BATCH_SIZE = 500;
  QStringList paths;
  QStringList pathUuids;
  paths.reserve(INSERT_BATCH_SIZE);
  pathUuids.reserve(INSERT_BATCH_SIZE);
  int position = 0;

  auto flushBatch = [&]() -> bool {
    if (paths.isEmpty()) return true;

    QString insertSql = "INSERT INTO sorted_items_cache (position, path, uuid) VALUES ";
    QStringList placeholders;
    placeholders.reserve(paths.size());
    for (int i = 0; i < paths.size(); ++i) {
      placeholders.append("(?, ?, ?)");
    }
    insertSql += placeholders.join(", ");

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
    for (const auto &item : allItems) {
      paths.append(item.path);
      pathUuids.append(item.uuid);
      ++position;

      if (paths.size() >= INSERT_BATCH_SIZE) {
        if (!flushBatch()) {
          m_db.rollback();
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
        if (!flushBatch()) {
          m_db.rollback();
          return false;
        }
      }
    }
  }

  // Flush remaining
  if (!flushBatch()) {
    m_db.rollback();
    return false;
  }

  m_db.commit();

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
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS query_playlist_scope ("
              "uuid TEXT NOT NULL, "
              "path TEXT NOT NULL, "
              "PRIMARY KEY (uuid, path))")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to create query_playlist_scope temp table",
                                               "QueryManager::ensurePlaylistScopeTempTable")
                             .withDetails(q.lastError().text()));
    return false;
  }
  return true;
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

  // Cheap invalidation token: max(rowid) for this playlist's items combined
  // with the playlist id. Inserts and deletes both move max(rowid), so an
  // unchanged token guarantees the temp table is still in sync. Empty
  // playlists yield max(rowid) NULL → token "<id>|0", which is fine.
  QSqlQuery probe(m_db);
  probe.prepare("SELECT COALESCE(MAX(rowid), 0) FROM playlist_items WHERE playlist_id = ?");
  probe.addBindValue(playlistId);
  if (!probe.exec() || !probe.next()) {
    return false;
  }
  const QString key = playlistId + QStringLiteral("|") + probe.value(0).toString();
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
