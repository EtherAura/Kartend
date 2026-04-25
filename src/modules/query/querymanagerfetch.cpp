// Sibling TU: item-count and visual-index fetch slots for QueryManager.
#include "querymanager.h"

#include "loggingcategories.h"
#include <atomic>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <stdexcept>

#include "collectionutils.h"
#include "errorutils.h"
#include "pathutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "uiconstants.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using QueryManagerInternal::canonicalKeyPath;
using QueryManagerInternal::displayNameForBase;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg

using QueryManagerInternal::buildFtsPrefixQuery;

int QueryManager::fetchItemCountImpl(const CollectionContext &context,
                                     const QList<CollectionConfig> &allCollections,
                                     const QString &filter) {
  if (!ensureDatabaseAvailable("QueryManager::fetchItemCountImpl")) {
    return 0;
  }

  // Ensure we see the latest data committed by the scan worker.
  // Without this, our connection can return stale counts from a cached
  // WAL snapshot, causing the UI to show old item counts after scans.
  refreshWalView();

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context", "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return 0;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory =
      PathUtils::validateAndExpandPath(ctx.config.mediaDirectory, ctx.config.name);

  // IMPORTANT: Do not scan synchronously here.
  // Scans are dispatched to a dedicated scan worker so this query worker can
  // return counts immediately and keep the UI responsive.

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  if (uuids.isEmpty()) {
    auto err = ErrorContext::warning(ErrorCode::InvalidArgument,
                                     "No valid collection UUIDs for item count query",
                                     "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return 0;
  }
  const QString trimmedFilter = filter.trimmed();

  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: uuidCount=" << uuids.size()
                        << "showAllSubcollectionItems=" << ctx.config.showAllSubcollectionItems
                        << "queryIncludeDescendants=" << ctx.queryIncludeDescendants;
  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: collIndex=" << context.currentIndex
                        << "filter='" << trimmedFilter
                        << "' includeSubfolders=" << context.config.includeContentSubfolders
                        << " showAllSubfolderItems=" << context.config.showAllSubfolderItems
                        << " currentSubfolder='" << context.config.currentSubfolder << "'";
  if (m_itemsFtsAvailable && !m_itemsFtsReady) {
    qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: checking FTS "
                             "readiness from DB...";
    m_itemsFtsReady = isItemsFtsReadyFromDb();
    qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: FTS ready =" << m_itemsFtsReady;
  }
  const QString ftsQuery = (m_itemsFtsAvailable && m_itemsFtsReady && !trimmedFilter.isEmpty())
                               ? buildFtsPrefixQuery(trimmedFilter)
                               : QString();
  const bool useFts = !ftsQuery.isEmpty();

  // Check if we need to use temp table for large UUID lists
  // SQLite has a default limit of 999 bind variables
  bool useTempTable = false;
  const QString uuidClause = buildUuidFilterClause(uuids, useTempTable);

  if (useTempTable) {
    if (!ensureQueryUuidsPopulated(uuids)) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to populate UUID temp table for item count query",
                                       "QueryManager::fetchItemCount");
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return 0;
    }
  }

  QString sql;
  if (useFts) {
    // Query FTS table directly - the FTS table contains name,
    // path, and collection_uuid so we can filter directly.
    // Use COUNT(DISTINCT path) to match fetchItemsRange's GROUP BY path
    // dedup — same path can appear in multiple collections (e.g. with
    // showAllSubcollectionItems=true), which would otherwise leave blank
    // placeholder tiles at the tail when count > distinct rows
    // (bd Kartend-m9s).
    if (useTempTable) {
      sql = "SELECT COUNT(DISTINCT path) FROM items_fts "
            "WHERE items_fts MATCH ? AND EXISTS " +
            uuidClause;
    } else {
      sql = "SELECT COUNT(DISTINCT path) FROM items_fts "
            "WHERE items_fts MATCH ? AND collection_uuid IN " +
            uuidClause;
    }
  } else {
    if (useTempTable) {
      sql = "SELECT COUNT(DISTINCT path) FROM items WHERE EXISTS " + uuidClause;
    } else {
      sql = "SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid IN " + uuidClause;
    }
  }

  // Apply subfolder filtering when browsing subfolders
  const QString &subfolder = ctx.config.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose path starts with subfolder/
    sql += " AND path LIKE ?";
  } else if (ctx.config.includeContentSubfolders && !ctx.config.showAllSubfolderItems &&
             trimmedFilter.isEmpty()) {
    // At root with subfolders enabled but NOT showing all items, we normally
    // exclude items in subfolders so the UI can present folder tiles.
    //
    // When a search filter is active, include subfolder items so search can
    // find matches even in \"virtual folders only\" collections.
    sql += " AND path NOT LIKE '%/%'";
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown
  // mixed together

  if (!trimmedFilter.isEmpty()) {
    if (!useFts) {
      sql += " AND name LIKE ?";
    }
  }

  // Use cached prepared statement - dynamic SQL is cached by query string
  QSqlQuery &query = getPreparedStatement(sql);
  int bindPos = 0;

  // Bind FTS MATCH first (it appears first in the FTS SQL)
  if (useFts) {
    query.bindValue(bindPos++, ftsQuery);
  }
  // Then bind collection UUIDs (only when not using temp table)
  if (!useTempTable) {
    for (const QString &uuid : uuids) {
      query.bindValue(bindPos++, uuid);
    }
  }
  if (!subfolder.isEmpty()) {
    query.bindValue(bindPos++, subfolder + "/%");
  }
  if (!trimmedFilter.isEmpty() && !useFts) {
    query.bindValue(bindPos++, "%" + trimmedFilter + "%");
  }

  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: executing SQL, useFts=" << useFts
                        << "useTempTable=" << useTempTable << "uuidCount=" << uuids.size();
  if (query.exec() && query.next()) {
    const int count = query.value(0).toInt();
    qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: result=" << count;
    // For large collections, schedule deferred cache build for O(1) range
    // lookups. This avoids expensive ORDER BY + OFFSET on every fetchItemsRange
    // call. We don't block here - the cache builds after this function returns,
    // so the UI can start displaying items immediately using the slow path.
    //
    // Random sort mode ALWAYS requires a cache because SQL ORDER BY RANDOM()
    // with OFFSET cannot provide consistent results across paginated requests.
    const bool isRandomSort = (ctx.sortMode == SortMode::Random);
    if (isRandomSort || count >= UIConstants::Database::PRECOMPUTE_SORT_THRESHOLD) {
      if (!hasSortedItemsCache() && !m_sortCacheBuildPending) {
        if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
          qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemCount: count=" << count
                                << (isRandomSort ? " (random mode)" : " >= threshold")
                                << ", scheduling deferred cache build...";
        }
        scheduleDeferredCacheBuild(uuids, trimmedFilter, ctx.sortMode);
      }
    } else {
      // Small collection with non-random sort - clear any stale cache and use
      // standard ORDER BY
      if (m_sortedItemsCacheValid) {
        clearSortedItemsCache();
      }
    }

    return count;
  }

  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: query FAILED"
                        << query.lastError().text();
  auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Fetch item count failed",
                                   "QueryManager::fetchItemCount")
                 .withDetails(query.lastError().text());
  ErrorUtils::logError(err);
  emit errorOccurred(err);
  return 0;
}

void QueryManager::fetchItemCount(const CollectionContext &context,
                                  const QList<CollectionConfig> &allCollections,
                                  const QString &filter) {
  emit itemCountLoaded(fetchItemCountImpl(context, allCollections, filter));
}

void QueryManager::fetchItemCountWithToken(const CollectionContext &context,
                                           const QList<CollectionConfig> &allCollections,
                                           const QString &filter, int requestToken) {
  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCountWithToken: ENTRY token=" << requestToken
                        << "filter='" << filter << "'";
  const int count = fetchItemCountImpl(context, allCollections, filter);
  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCountWithToken: EMIT token=" << requestToken
                        << "count=" << count;
  emit itemCountLoadedWithToken(count, requestToken);
}

void QueryManager::ensureScannedForContext(const CollectionContext &context,
                                           const QList<CollectionConfig> &allCollections) {
  if (!ensureDatabaseAvailable("QueryManager::ensureScannedForContext")) {
    return;
  }

  // Ensure we see the latest data committed by the query worker (which may have
  // just invalidated/cleared the collection cache). Without this, the scan
  // worker's WAL snapshot may be stale, causing needsRescan to return false
  // because it still sees old collection data that was just deleted.
  refreshWalView();

  if (!context.isValid()) {
    auto err =
        ErrorContext::error(ErrorCode::InvalidCollectionContext, "Invalid collection context",
                            "QueryManager::ensureScannedForContext");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory =
      PathUtils::validateAndExpandPath(ctx.config.mediaDirectory, ctx.config.name);

  // Scan current collection if needed. ensureCollectionScanned now handles
  // emitting both scanStarting and collectionScanCompleted signals internally,
  // ensuring proper overlay tracking even when scans fail.
  if (!ctx.config.mediaDirectory.trimmed().isEmpty()) {
    (void)ensureCollectionScanned(ctx.currentIndex, ctx.config);
  }

  // Scan all collections if the query scope requests it.
  if (ctx.queryIncludeAllCollections) {
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig col = allCollections[i];
      col.mediaDirectory = PathUtils::validateAndExpandPath(col.mediaDirectory, col.name);
      if (col.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      (void)ensureCollectionScanned(i, col);
    }
    return;
  }

  // Scan descendants if requested.
  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    // Use pre-computed descendants if available (O(1) from cache), otherwise
    // fall back to O(n²) tree traversal for backward compatibility
    const QList<int> &rawDescendants =
        ctx.precomputedDescendants.isEmpty()
            ? CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections)
            : ctx.precomputedDescendants;
    for (int descendantIndex : rawDescendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }

      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }

      (void)ensureCollectionScanned(descendantIndex, subCol);
    }
  }
}

void QueryManager::fetchVisualIndexForPath(const CollectionContext &context,
                                           const QList<CollectionConfig> &allCollections,
                                           const QString &filePath) {
  if (filePath.isEmpty()) {
    emit visualIndexForPathLoaded(-1, filePath);
    return;
  }

  if (!ensureDatabaseAvailable("QueryManager::fetchVisualIndexForPath")) {
    emit visualIndexForPathLoaded(-1, filePath);
    return;
  }

  refreshWalView();

  if (!context.isValid()) {
    emit visualIndexForPathLoaded(-1, filePath);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory =
      PathUtils::validateAndExpandPath(ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory =
      PathUtils::validateAndExpandPath(ctx.config.artworkDirectory, ctx.config.name);

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  CollectionDirMaps dirMaps = buildDirectoryMaps(ctx, allCollections);

  // Try sorted cache first (fast path)
  if (hasSortedItemsCache()) {
    const QByteArray currentHash = computeSortCacheHash(uuids, QString(), ctx.sortMode);
    if (currentHash == m_sortedItemsCacheHash) {
      // Query sorted cache for this path
      // The cache stores relative paths, so we need to check both
      QSqlQuery cacheQuery(m_db);
      cacheQuery.prepare("SELECT position FROM sorted_items_cache WHERE path = ?");

      // Convert to relative path if it's absolute
      for (auto it = dirMaps.uuidToMediaDir.begin(); it != dirMaps.uuidToMediaDir.end(); ++it) {
        if (filePath.startsWith(it.value())) {
          QString candidate = filePath.mid(it.value().length());
          if (candidate.startsWith('/')) {
            candidate = candidate.mid(1);
          }
          // Try this relative path
          cacheQuery.bindValue(0, candidate);
          if (cacheQuery.exec() && cacheQuery.next()) {
            int position = cacheQuery.value(0).toInt();
            emit visualIndexForPathLoaded(position, filePath);
            return;
          }
        }
      }

      // Also try with the full canonical path
      cacheQuery.bindValue(0, filePath);
      if (cacheQuery.exec() && cacheQuery.next()) {
        int position = cacheQuery.value(0).toInt();
        emit visualIndexForPathLoaded(position, filePath);
        return;
      }
    }
  }

  // Slow path: query with ROW_NUMBER to find position
  // Build ORDER BY clause based on sort mode
  QString orderClause;
  switch (ctx.sortMode) {
  case SortMode::NameDescending:
    orderClause = "ORDER BY name COLLATE NOCASE DESC";
    break;
  case SortMode::CollectionAscending:
    orderClause = "ORDER BY collection_uuid, name COLLATE NOCASE";
    break;
  case SortMode::CollectionDescending:
    orderClause = "ORDER BY collection_uuid DESC, name COLLATE NOCASE";
    break;
  case SortMode::NameAscending:
  case SortMode::ArtworkFirst:
  case SortMode::ArtworkLast:
  case SortMode::Random:
  default:
    orderClause = "ORDER BY name COLLATE NOCASE";
    break;
  }

  // Build WHERE clause for UUIDs
  QString uuidPlaceholders;
  for (int i = 0; i < uuids.size(); ++i) {
    if (i > 0) uuidPlaceholders += ", ";
    uuidPlaceholders += "?";
  }

  // Use window function to get row number for matching path.
  // IMPORTANT: Use GROUP BY path to deduplicate paths that appear in multiple
  // collections (e.g., when showAllSubcollectionItems=true). SELECT DISTINCT
  // path, name doesn't work because the same path can have different
  // collection_uuid values. GROUP BY path ensures exactly one row per unique
  // path, matching COUNT(DISTINCT path) used by fetchItemCount.
  QString sql = QString("SELECT rn FROM ("
                        "  SELECT path, ROW_NUMBER() OVER (%1) - 1 as rn "
                        "  FROM (SELECT path, name FROM items WHERE "
                        "collection_uuid IN (%2) GROUP BY path)"
                        ") WHERE path = ?")
                    .arg(orderClause, uuidPlaceholders);

  QSqlQuery query(m_db);
  query.prepare(sql);

  int bindPos = 0;
  for (const QString &uuid : uuids) {
    query.bindValue(bindPos++, uuid);
  }

  // Convert full path to relative for database lookup
  QString relPath;
  for (auto it = dirMaps.uuidToMediaDir.begin(); it != dirMaps.uuidToMediaDir.end(); ++it) {
    if (filePath.startsWith(it.value())) {
      relPath = filePath.mid(it.value().length());
      if (relPath.startsWith('/')) {
        relPath = relPath.mid(1);
      }
      break;
    }
  }

  if (relPath.isEmpty()) {
    // Try the full path as-is
    query.bindValue(bindPos++, filePath);
  } else {
    query.bindValue(bindPos++, relPath);
  }

  // Debug: count how many distinct paths the subquery produces (should match
  // fetchItemCount)
  QString countSql = QString("SELECT COUNT(DISTINCT path) FROM items WHERE "
                             "collection_uuid IN (%1)")
                         .arg(uuidPlaceholders);
  QSqlQuery countQuery(m_db);
  countQuery.prepare(countSql);
  int countBindPos = 0;
  for (const QString &uuid : uuids) {
    countQuery.bindValue(countBindPos++, uuid);
  }
  int rowCount = -1;
  if (countQuery.exec() && countQuery.next()) {
    rowCount = countQuery.value(0).toInt();
  }

  QString boundPath = relPath.isEmpty() ? filePath : relPath;
  qCDebug(lcSearchDiag) << "[SelectionRestore] fetchVisualIndexForPath:"
                        << "rowCount in subquery:" << rowCount << "uuids:" << uuids.size()
                        << "filePath:" << filePath << "relPath:" << relPath
                        << "boundPath:" << boundPath;

  if (query.exec() && query.next()) {
    int position = query.value(0).toInt();
    qCDebug(lcSearchDiag) << "[SelectionRestore] Query returned position:" << position;
    emit visualIndexForPathLoaded(position, filePath);
  } else {
    qCDebug(lcSearchDiag) << "[SelectionRestore] Query returned NO MATCH, lastError:"
                          << query.lastError().text();
    emit visualIndexForPathLoaded(-1, filePath);
  }
}
