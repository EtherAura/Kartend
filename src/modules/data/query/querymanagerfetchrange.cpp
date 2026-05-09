// fetchItemsRange extracted from querymanager.cpp (~402 LOC).
// Remains a QueryManager member; accesses existing class state.
//
// buildFtsPrefixQuery has internal linkage in querymanager.cpp; we duplicate
// it here so this TU does not depend on private symbols of the main TU.
#include "querymanager.h"

#include "loggingcategories.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "collectionutils.h"
#include "errorutils.h"
#include "pathutils.h"
#include "queryhelpers.h"
#include "querymanagerhelpers.h"
#include "uiconstants.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using QueryManagerInternal::buildFtsPrefixQuery;
using QueryManagerInternal::canonicalKeyPath;
using QueryManagerInternal::displayNameForBase;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)

void QueryManager::fetchItemsRange(const CollectionContext &context,
                                   const QList<CollectionConfig> &allCollections, int offset,
                                   int limit, const QString &filter) {
  assertOwnerThread();
  if (!ensureDatabaseAvailable("QueryManager::fetchItemsRange")) {
    emit itemsRangeLoaded(offset, QStringList(), QHash<QString, QString>(),
                          QHash<QString, QString>(), QHash<QString, QString>(),
                          QHash<QString, int>());
    return;
  }

  // Ensure we see the latest data committed by the scan worker.
  // This is critical after a rescan - without it, the range query may return
  // stale/empty results from a cached WAL snapshot.
  refreshWalView();

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context", "QueryManager::fetchItemsRange");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  // Note: We assume ensureCollectionScanned was called during fetchItemCount.
  // For performance, we don't re-scan here.

  CollectionContext ctx = context;
  ctx.config.mediaDirectory =
      PathUtils::validateAndExpandPath(ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory =
      PathUtils::validateAndExpandPath(ctx.config.artworkDirectory, ctx.config.name);

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  CollectionDirMaps dirMaps = buildDirectoryMaps(ctx, allCollections);

  // empty playlist short-circuits — no UUIDs means no items to
  // fetch. Emit an empty result so the navigation overlay clears instead of
  // hanging on an "in flight" count.
  if (ctx.config.isPlaylist && uuids.isEmpty()) {
    emit itemsRangeLoaded(offset, QStringList(), QHash<QString, QString>(),
                          QHash<QString, QString>(), QHash<QString, QString>(),
                          QHash<QString, int>());
    return;
  }
  if (ctx.config.isPlaylist && !ensurePlaylistScopePopulated(ctx.config.playlistId)) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to populate playlist scope for range query",
                                     "QueryManager::fetchItemsRange");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    emit itemsRangeLoaded(offset, QStringList(), QHash<QString, QString>(),
                          QHash<QString, QString>(), QHash<QString, QString>(),
                          QHash<QString, int>());
    return;
  }

  const QString trimmedFilter = filter.trimmed();

  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemsRange: collIndex=" << context.currentIndex
                        << "offset=" << offset << "limit=" << limit << "filter='" << trimmedFilter
                        << "' includeSubfolders=" << context.config.includeContentSubfolders
                        << " showAllSubfolderItems=" << context.config.showAllSubfolderItems
                        << " currentSubfolder='" << context.config.currentSubfolder << "'";
  QElapsedTimer rangeTimer;
  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    rangeTimer.start();
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // FAST PATH: Use precomputed sorted cache for O(1) range lookups
  // ═══════════════════════════════════════════════════════════════════════════
  // When the sorted cache is valid (built during fetchItemCount for large
  // collections), we can skip the expensive ORDER BY + OFFSET and just do:
  //   SELECT path, uuid FROM sorted_items_cache WHERE position BETWEEN ? AND ?
  // This is instant regardless of offset position.
  //
  // For cached startup paths that skip fetchItemCount, we build the cache
  // on-demand here when offset is high enough to benefit from it.

  // Schedule deferred cache build for large collections if not already built.
  // This handles the case where the app starts from cached viewport and
  // fetchItemCount was never called to build the cache. We don't block the
  // first query - instead we schedule the build to run after this query
  // returns, so subsequent queries can use the cache.
  //
  // EXCEPTION: Random sort mode requires a cache because SQL ORDER BY with
  // OFFSET cannot provide consistent random order across paginated requests.
  // Each page would get different random items. So for random mode, we build
  // the cache synchronously before proceeding.
  // EXCEPTION: Playlists bypass the sorted_items_cache in v1.
  // The cache hash keys off the uuid list + filter + sort mode, which would
  // collide with a regular query over the same uuids — so a playlist would
  // grab a cache built for "all items in those source collections" and show
  // way too many items. Sub-issue can revisit once we have a
  // playlist-aware cache key.
  const bool isPlaylist = ctx.config.isPlaylist;
  const bool isRandomSort = (ctx.sortMode == SortMode::Random);
  if (!isPlaylist && !hasSortedItemsCache()) {
    if (isRandomSort) {
      // random mode must build the cache synchronously so this
      // and every subsequent page share one stable permutation. If a deferred
      // build was already scheduled (typically by fetchItemCount running just
      // before us), cancel it — the slow path falls back to alphabetical for
      // random sort, which is exactly what users see on first start when we
      // wait for the deferred build instead of forcing it here.
      if (m_sortCacheBuildPending) {
        m_sortCacheBuildPending = false;
        m_pendingCacheUuids.clear();
        m_pendingCacheFilter.clear();
      }
      if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
        qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange: Random sort requires cache, "
                                 "building synchronously";
      }
      (void)populateSortedItemsCache(uuids, trimmedFilter, ctx.sortMode);
      // Fall through to use the cache we just built
    } else if (!m_sortCacheBuildPending && uuids.size() >= 10 &&
               offset >= UIConstants::Database::PRECOMPUTE_SORT_THRESHOLD) {
      if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
        qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange: scheduling deferred cache "
                                 "build, offset="
                              << offset;
      }
      scheduleDeferredCacheBuild(uuids, trimmedFilter, ctx.sortMode);
      // Fall through to slow path for this query
    }
  }

  if (!isPlaylist && hasSortedItemsCache()) {
    // Verify cache hash still matches (in case filter or sortMode changed)
    const QByteArray currentHash = computeSortCacheHash(uuids, trimmedFilter, ctx.sortMode);
    if (currentHash == m_sortedItemsCacheHash) {
      if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
        qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange: using sorted cache, offset="
                              << offset << "limit=" << limit;
      }

      QStringList filePaths;
      QHash<QString, QString> fileNames;
      QHash<QString, QString> fileToArtworkDir;
      QHash<QString, QString> fileToMediaDir;
      QHash<QString, int> fileToCollectionIndex;

      // Simple position-based range query - O(1) regardless of offset
      QSqlQuery cacheQuery(m_db);
      cacheQuery.prepare("SELECT path, uuid FROM sorted_items_cache "
                         "WHERE position >= ? AND position < ? "
                         "ORDER BY position");
      cacheQuery.bindValue(0, offset);
      cacheQuery.bindValue(1, offset + limit);

      if (cacheQuery.exec()) {
        while (cacheQuery.next()) {
          QString relPath = cacheQuery.value(0).toString();
          QString uuid = cacheQuery.value(1).toString();
          QString mediaDir = dirMaps.uuidToMediaDir.value(uuid);
          QString artworkDir = dirMaps.uuidToArtworkDir.value(uuid);
          int collectionIndex = dirMaps.uuidToCollectionIndex.value(uuid, -1);

          QString fullPath;
          if (QDir::isAbsolutePath(relPath)) {
            fullPath = relPath;
          } else {
            fullPath = QDir(mediaDir).absoluteFilePath(relPath);
          }

          QString keyPath = canonicalKeyPath(fullPath, false, nullptr);
          filePaths.append(keyPath);
          fileNames[keyPath] = displayNameForBase(QFileInfo(keyPath).completeBaseName());
          if (!artworkDir.isEmpty()) {
            fileToArtworkDir[keyPath] = artworkDir;
          }
          if (!mediaDir.isEmpty()) {
            fileToMediaDir[keyPath] = mediaDir;
          }
          if (collectionIndex >= 0) {
            fileToCollectionIndex[keyPath] = collectionIndex;
          }
        }

        if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
          qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange (cached): totalMs="
                                << rangeTimer.elapsed() << "resultCount=" << filePaths.size();
        }

        emit itemsRangeLoaded(offset, filePaths, fileNames, fileToArtworkDir, fileToMediaDir,
                              fileToCollectionIndex);
        return;
      } else {
        // Cache query failed - fall through to standard path
        if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
          qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange: cache query failed, "
                                   "falling back:"
                                << cacheQuery.lastError().text();
        }
      }
    } else {
      // Hash mismatch - cache is stale (filter changed)
      if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
        qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange: cache hash mismatch, using "
                                 "slow path";
      }
      clearSortedItemsCache();
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  // SLOW PATH: Standard ORDER BY + OFFSET (fallback for small collections)
  // ═══════════════════════════════════════════════════════════════════════════

  if (m_itemsFtsAvailable && !m_itemsFtsReady) {
    m_itemsFtsReady = isItemsFtsReadyFromDb();
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
                                       "Failed to populate UUID temp table for range query",
                                       "QueryManager::fetchItemsRange");
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      emit itemsRangeLoaded(offset, QStringList(), QHash<QString, QString>(),
                            QHash<QString, QString>(), QHash<QString, QString>(),
                            QHash<QString, int>());
      return;
    }
  }

  QString sql;
  if (useFts) {
    // (collection_uuid, path) is the item identity — keep both columns and
    // emit one row per matching item so same-named files across
    // subcollections render as distinct tiles. The downstream pipeline
    // re-keys by absolute path (mediaDir/path) which stays unique.
    if (useTempTable) {
      sql = "SELECT path, collection_uuid, name as sort_name, "
            "last_modified as sort_last_modified, file_size as sort_file_size FROM items "
            "WHERE id IN (SELECT rowid FROM items_fts WHERE items_fts MATCH ?) AND EXISTS " +
            uuidClause;
    } else {
      sql = "SELECT path, collection_uuid, name as sort_name, "
            "last_modified as sort_last_modified, file_size as sort_file_size FROM items "
            "WHERE id IN (SELECT rowid FROM items_fts WHERE items_fts MATCH ?) AND "
            "collection_uuid IN " +
            uuidClause;
    }
  } else {
    if (useTempTable) {
      sql = "SELECT path, collection_uuid, name as sort_name, "
            "last_modified as sort_last_modified, file_size as sort_file_size FROM items "
            "WHERE EXISTS " +
            uuidClause;
    } else {
      sql = "SELECT path, collection_uuid, name as sort_name, "
            "last_modified as sort_last_modified, file_size as sort_file_size FROM items "
            "WHERE collection_uuid IN " +
            uuidClause;
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
    // find matches even in "virtual folders only" collections.
    sql += " AND path NOT LIKE '%/%'";
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown
  // mixed together

  if (!trimmedFilter.isEmpty()) {
    if (!useFts) {
      sql += " AND name LIKE ?";
    }
  }

  // same playlist EXISTS clause as fetchItemCountImpl.
  if (isPlaylist) {
    sql += " AND EXISTS (SELECT 1 FROM query_playlist_scope p "
           "WHERE p.uuid = collection_uuid AND p.path = path)";
  }

  // Apply sort order based on sortMode
  // NOTE: Random sort should never reach the slow path - it's handled by
  // synchronous cache building above. If we get here for random, fall back to
  // alphabetical as a safeguard (items won't be truly random but at least
  // pagination will be consistent).
  if (ctx.sortMode == SortMode::Random) {
    // Random mode is supposed to force cache creation above; if we reach
    // this slow path, fall back to alphabetical for consistent pagination
    // and log a warning so the unexpected route is visible.
    qCWarning(lcQueryManager)
        << "[QueryManager] fetchItemsRange: Random sort reached slow path unexpectedly";
  }
  sql +=
      QStringLiteral(" ") + QueryHelpers::orderByForSortMode(ctx.sortMode, /*useSortPrefix=*/true,
                                                             /*withLimitOffset=*/true);

  if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
    qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange (slow path): offset=" << offset
                          << "limit=" << limit << "uuids=" << uuids.size()
                          << "useTempTable=" << useTempTable;
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
  query.bindValue(bindPos++, limit);
  query.bindValue(bindPos++, offset);

  QStringList filePaths;
  QHash<QString, QString> fileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  qint64 execMs = 0;
  if (query.exec()) {
    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      execMs = rangeTimer.elapsed();
    }
    while (query.next()) {
      QString relPath = query.value(0).toString();
      QString uuid = query.value(1).toString();
      QString mediaDir = dirMaps.uuidToMediaDir.value(uuid);
      QString artworkDir = dirMaps.uuidToArtworkDir.value(uuid);
      int collectionIndex = dirMaps.uuidToCollectionIndex.value(uuid, -1);

      QString fullPath;
      if (QDir::isAbsolutePath(relPath)) {
        fullPath = relPath;
      } else {
        fullPath = QDir(mediaDir).absoluteFilePath(relPath);
      }

      QString keyPath = canonicalKeyPath(fullPath, false, nullptr);
      filePaths.append(keyPath);
      fileNames[keyPath] = displayNameForBase(QFileInfo(keyPath).completeBaseName());
      if (!artworkDir.isEmpty()) {
        fileToArtworkDir[keyPath] = artworkDir;
      }
      if (!mediaDir.isEmpty()) {
        fileToMediaDir[keyPath] = mediaDir;
      }
      if (collectionIndex >= 0) {
        fileToCollectionIndex[keyPath] = collectionIndex;
      }
    }

    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange: execMs=" << execMs
                            << "totalMs=" << rangeTimer.elapsed()
                            << "resultCount=" << filePaths.size();
    }

    if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
      qCDebug(lcSearchDiag) << "[QueryManager] fetchItemsRange: returned paths="
                            << filePaths.size();
      if (!filePaths.isEmpty()) {
        qCDebug(lcSearchDiag) << "[QueryManager] fetchItemsRange: firstPath=" << filePaths.first();
      }
    }
  } else {
    if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
      qCDebug(lcSearchDiag) << "[RangeDiag] fetchItemsRange: QUERY FAILED after"
                            << rangeTimer.elapsed() << "ms:" << query.lastError().text();
    }
    auto err = ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Fetch items range failed",
                                   "QueryManager::fetchItemsRange")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
    emit errorOccurred(err);
  }

  emit itemsRangeLoaded(offset, filePaths, fileNames, fileToArtworkDir, fileToMediaDir,
                        fileToCollectionIndex);
}
