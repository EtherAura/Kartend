// Top-level load + fetch slots extracted from querymanager.cpp:
//   - loadAllCollections, loadItems, loadItemsWithSubcollections
//   - updateCachedCounts
//   - collectCollectionUuids, buildDirectoryMaps, buildUuidInClause
//   - fetchItemCountImpl, fetchItemCount, fetchItemCountWithToken
//   - ensureScannedForContext, fetchVisualIndexForPath
// Members of QueryManager; access existing class state.
#include "querymanager.h"

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
#include <atomic>
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

void QueryManager::loadAllCollections(
    const QList<CollectionConfig> &allCollections) {
  if (!ensureDatabaseAvailable("QueryManager::loadAllCollections")) {
    // Emit safe default so listeners (UI item count, etc.) don't hang.
    emit itemsLoaded({}, {}, {}, {}, {});
    return;
  }

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  const int totalCollections = allCollections.size();

  for (int collectionIndex = 0; collectionIndex < allCollections.size();
       ++collectionIndex) {
    CollectionConfig collection = allCollections[collectionIndex];

    collection.mediaDirectory = PathUtils::validateAndExpandPath(
        collection.mediaDirectory, collection.name);
    collection.artworkDirectory = PathUtils::validateAndExpandPath(
        collection.artworkDirectory, collection.name);

    if (collection.mediaDirectory.trimmed().isEmpty()) {
      continue;
    }

    // Only emit progress signal when a scan is actually needed (not for cached
    // loads)
    if (needsRescan(collectionIndex, collection)) {
      emit scanProgress(collectionIndex + 1, totalCollections, collection.name);
    }

    QHash<QString, QDateTime> timestamps;
    QStringList filePaths =
        loadOrScanCollection(collectionIndex, collection, timestamps);

    appendFileMapsAndListCanonical(collectionIndex, collection,
                                   CollectionUtils::resolveArtworkDirectory(
                                       collectionIndex, allCollections),
                                   filePaths, allFilePaths, allFileNames,
                                   fileToArtworkDir, fileToMediaDir,
                                   fileToCollectionIndex, false);
  }

  sortFiles(allFilePaths);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                   fileToCollectionIndex);
}

void QueryManager::loadItems(const CollectionContext &context,
                             const QList<CollectionConfig> &allCollections) {
  if (!ensureDatabaseAvailable("QueryManager::loadItems")) {
    // Emit safe default so listeners don't hang awaiting itemsLoaded.
    emit itemsLoaded({}, {}, {}, {}, {});
    return;
  }

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::loadItems");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      ctx.config.artworkDirectory, ctx.config.name);

  if (ctx.config.mediaDirectory.trimmed().isEmpty()) {
    emit itemsLoaded(QStringList(), QHash<QString, QString>(),
                     QHash<QString, QString>(), QHash<QString, QString>(),
                     QHash<QString, int>());
    return;
  }

  QHash<QString, QDateTime> timestamps;
  QStringList filePaths =
      loadOrScanCollection(ctx.currentIndex, ctx.config, timestamps);

  // Apply subfolder filtering
  const QString &subfolder = ctx.config.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose path starts with subfolder/
    const QString prefix = subfolder + "/";
    QStringList filtered;
    for (const QString &path : filePaths) {
      if (path.startsWith(prefix)) {
        filtered.append(path);
      }
    }
    filePaths = filtered;
  } else if (ctx.config.includeContentSubfolders &&
             !ctx.config.showAllSubfolderItems) {
    // At root with subfolders enabled but NOT showing all items - exclude items
    // in subfolders
    QStringList filtered;
    for (const QString &path : filePaths) {
      if (!path.contains('/')) {
        filtered.append(path);
      }
    }
    filePaths = filtered;
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown
  // mixed together

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  // Resolve artwork directory with parent fallback for subcollections
  QString resolvedArtworkDir = CollectionUtils::resolveArtworkDirectory(
      ctx.currentIndex, allCollections);

  appendFileMapsAndListCanonical(ctx.currentIndex, ctx.config,
                                 resolvedArtworkDir, filePaths, allFilePaths,
                                 allFileNames, fileToArtworkDir, fileToMediaDir,
                                 fileToCollectionIndex, false);

  sortFiles(allFilePaths, ctx.sortMode);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                   fileToCollectionIndex);
}

void QueryManager::loadItemsWithSubcollections(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections) {
  if (!ensureDatabaseAvailable("QueryManager::loadItemsWithSubcollections")) {
    // Emit safe default so listeners don't hang awaiting itemsLoaded.
    emit itemsLoaded({}, {}, {}, {}, {});
    return;
  }

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::loadItemsWithSubcollections");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext mainCtx = context;
  mainCtx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      mainCtx.config.mediaDirectory, mainCtx.config.name);
  mainCtx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      mainCtx.config.artworkDirectory, mainCtx.config.name);

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  QSet<QString> seenCanonicalPaths;
  QHash<QString, QString> canonicalPathCache;

  bool hasMainMediaDirectory =
      !mainCtx.config.mediaDirectory.trimmed().isEmpty();
  if (hasMainMediaDirectory) {
    QHash<QString, QDateTime> timestamps;
    QStringList mainFilePaths =
        loadOrScanCollection(mainCtx.currentIndex, mainCtx.config, timestamps);

    // Apply subfolder filtering for the main collection
    const QString &subfolder = mainCtx.config.currentSubfolder;
    if (!subfolder.isEmpty()) {
      // In a subfolder - show only items whose path starts with subfolder/
      const QString prefix = subfolder + "/";
      QStringList filtered;
      for (const QString &path : mainFilePaths) {
        if (path.startsWith(prefix)) {
          filtered.append(path);
        }
      }
      mainFilePaths = filtered;
    } else if (mainCtx.config.includeContentSubfolders &&
               !mainCtx.config.showAllSubfolderItems) {
      // At root with subfolders enabled but NOT showing all items - exclude
      // items in subfolders
      QStringList filtered;
      for (const QString &path : mainFilePaths) {
        if (!path.contains('/')) {
          filtered.append(path);
        }
      }
      mainFilePaths = filtered;
    }

    seenCanonicalPaths.reserve(mainFilePaths.size());
    canonicalPathCache.reserve(mainFilePaths.size());

    appendFileMapsAndListCanonical(mainCtx.currentIndex, mainCtx.config,
                                   CollectionUtils::resolveArtworkDirectory(
                                       mainCtx.currentIndex, allCollections),
                                   mainFilePaths, allFilePaths, allFileNames,
                                   fileToArtworkDir, fileToMediaDir,
                                   fileToCollectionIndex, true,
                                   &seenCanonicalPaths, &canonicalPathCache);
  }

  // Use pre-computed descendants if available (O(1) from cache), otherwise
  // fall back to O(n²) tree traversal for backward compatibility
  const QList<int> &rawDescendants =
      mainCtx.precomputedDescendants.isEmpty()
          ? CollectionUtils::collectDescendantIndices(mainCtx.currentIndex,
                                                      allCollections)
          : mainCtx.precomputedDescendants;
  QSet<int> seenDesc;
  QList<int> descendants;
  descendants.reserve(rawDescendants.size());
  for (int descendantIndex : rawDescendants) {
    if (descendantIndex == mainCtx.currentIndex) {
      continue;
    }
    if (descendantIndex < 0 || descendantIndex >= allCollections.size()) {
      continue;
    }
    if (!seenDesc.contains(descendantIndex)) {
      seenDesc.insert(descendantIndex);
      descendants.append(descendantIndex);
    }
  }

  for (int collectionIndex : descendants) {
    CollectionConfig collection = allCollections[collectionIndex];
    collection.mediaDirectory = PathUtils::validateAndExpandPath(
        collection.mediaDirectory, collection.name);
    collection.artworkDirectory = PathUtils::validateAndExpandPath(
        collection.artworkDirectory, collection.name);

    if (collection.mediaDirectory.trimmed().isEmpty()) {
      continue;
    }

    QHash<QString, QDateTime> subTimestamps;
    QStringList subFilePaths =
        loadOrScanCollection(collectionIndex, collection, subTimestamps);

    appendFileMapsAndListCanonical(collectionIndex, collection,
                                   CollectionUtils::resolveArtworkDirectory(
                                       collectionIndex, allCollections),
                                   subFilePaths, allFilePaths, allFileNames,
                                   fileToArtworkDir, fileToMediaDir,
                                   fileToCollectionIndex, true,
                                   &seenCanonicalPaths, &canonicalPathCache);
  }

  sortFiles(allFilePaths, context.sortMode);
  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                   fileToCollectionIndex);
}

void QueryManager::updateCachedCounts(quint64 generation,
                                      const QStringList &collectionUuids) {
  if (!ensureDatabaseAvailable("QueryManager::updateCachedCounts")) {
    emit cachedCountsComputed(generation, 0, {});
    return;
  }

  qint64 globalCount = 0;
  {
    QSqlQuery globalQuery(m_db);
    if (globalQuery.exec("SELECT COUNT(*) FROM items") && globalQuery.next()) {
      globalCount = globalQuery.value(0).toLongLong();
    }
  }

  QHash<QString, qint64> directCountsByUuid;
  directCountsByUuid.reserve(collectionUuids.size());

  if (!collectionUuids.isEmpty()) {
    // Check if we need to use temp table for large UUID lists
    bool useTempTable = false;
    const QString clause = buildUuidFilterClause(collectionUuids, useTempTable);

    if (useTempTable) {
      if (!ensureQueryUuidsPopulated(collectionUuids)) {
        emit cachedCountsComputed(generation, globalCount, directCountsByUuid);
        return;
      }
    }

    QSqlQuery query(m_db);
    QString sql;
    if (useTempTable) {
      sql = "SELECT collection_uuid, COUNT(DISTINCT path) "
            "FROM items WHERE EXISTS " +
            clause + " GROUP BY collection_uuid";
    } else {
      sql = "SELECT collection_uuid, COUNT(DISTINCT path) "
            "FROM items WHERE collection_uuid IN " +
            clause + " GROUP BY collection_uuid";
    }
    query.prepare(sql);

    // Only bind UUIDs when not using temp table
    if (!useTempTable) {
      for (const QString &uuid : collectionUuids) {
        query.addBindValue(uuid);
      }
    }

    if (query.exec()) {
      while (query.next()) {
        directCountsByUuid.insert(query.value(0).toString(),
                                  query.value(1).toLongLong());
      }
    }
  }

  emit cachedCountsComputed(generation, globalCount, directCountsByUuid);
}

// Collects UUIDs for a collection and optionally its descendants
auto QueryManager::collectCollectionUuids(
    const CollectionContext &ctx, const QList<CollectionConfig> &allCollections)
    -> QStringList {
  QStringList uuids;

  if (ctx.queryIncludeAllCollections) {
    QSet<QString> seen;
    uuids.reserve(allCollections.size());
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig c = allCollections[i];
      c.mediaDirectory =
          PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      if (c.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      const QString uuid =
          CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
      if (!uuid.isEmpty() && !seen.contains(uuid)) {
        seen.insert(uuid);
        uuids.append(uuid);
      }
    }
    return uuids;
  }

  // Check if we need descendants (for subcollection search modes)
  const bool needsDescendants =
      ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants;

  // Use pre-computed UUIDs if available and we need descendants
  // This provides major performance improvement for large hierarchies (3000+
  // subcollections) by eliminating repeated filesystem exists() checks and SHA1
  // hash computations
  if (needsDescendants && !ctx.precomputedDescendantUuids.isEmpty()) {
    return ctx.precomputedDescendantUuids;
  }

  uuids << CollectionUtils::computeCollectionUuid(ctx.config.name,
                                                  ctx.config.mediaDirectory);

  if (needsDescendants) {
    // Use pre-computed descendants if available (O(1) from cache), otherwise
    // fall back to O(n²) tree traversal for backward compatibility
    const QList<int> &descendants =
        ctx.precomputedDescendants.isEmpty()
            ? CollectionUtils::collectDescendantIndices(ctx.currentIndex,
                                                        allCollections)
            : ctx.precomputedDescendants;
    uuids.reserve(uuids.size() + descendants.size());
    for (int descendantIndex : descendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }
      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory =
          PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      uuids << CollectionUtils::computeCollectionUuid(subCol.name,
                                                      subCol.mediaDirectory);
    }
  }
  return uuids;
}

// Builds UUID-to-directory mappings for resolving paths from query results
auto QueryManager::buildDirectoryMaps(
    const CollectionContext &ctx, const QList<CollectionConfig> &allCollections)
    -> CollectionDirMaps {
  CollectionDirMaps maps;

  // Check if we need descendants (for subcollection search modes)
  const bool needsDescendants =
      ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants;

  // Use pre-computed directory maps if available and we need descendants
  // This provides major performance improvement for large hierarchies (3000+
  // subcollections) by eliminating repeated path expansions and artwork
  // resolution during range loading
  if (needsDescendants && !ctx.precomputedUuidToMediaDir.isEmpty()) {
    maps.uuidToMediaDir = ctx.precomputedUuidToMediaDir;
    maps.uuidToArtworkDir = ctx.precomputedUuidToArtworkDir;
    maps.uuidToCollectionIndex = ctx.precomputedUuidToCollectionIndex;
    return maps;
  }

  // Helper to add mapping for a collection at a given index, resolving artwork
  // directory from parent chain if not set on the collection itself
  auto addMapping = [&](int collectionIndex, const CollectionConfig &c) {
    const QString uuid =
        CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
    if (uuid.isEmpty()) {
      return;
    }
    if (!maps.uuidToMediaDir.contains(uuid)) {
      maps.uuidToMediaDir[uuid] = c.mediaDirectory;
    }
    if (!maps.uuidToArtworkDir.contains(uuid)) {
      // Resolve artwork directory with parent fallback - if this collection
      // has no artwork directory, walk up the parent chain to find one
      QString resolvedArtwork = CollectionUtils::resolveArtworkDirectory(
          collectionIndex, allCollections);
      maps.uuidToArtworkDir[uuid] = resolvedArtwork;
    }
    if (!maps.uuidToCollectionIndex.contains(uuid)) {
      maps.uuidToCollectionIndex[uuid] = collectionIndex;
    }
  };

  if (ctx.queryIncludeAllCollections) {
    maps.uuidToMediaDir.reserve(allCollections.size());
    maps.uuidToArtworkDir.reserve(allCollections.size());
    maps.uuidToCollectionIndex.reserve(allCollections.size());
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig c = allCollections[i];
      c.mediaDirectory =
          PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      c.artworkDirectory =
          PathUtils::validateAndExpandPath(c.artworkDirectory, c.name);
      if (c.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      addMapping(i, c);
    }
    return maps;
  }

  QList<int> descendants;
  int expectedMappings = 1;
  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    // Use pre-computed descendants if available (O(1) from cache), otherwise
    // fall back to O(n²) tree traversal for backward compatibility
    descendants = ctx.precomputedDescendants.isEmpty()
                      ? CollectionUtils::collectDescendantIndices(
                            ctx.currentIndex, allCollections)
                      : ctx.precomputedDescendants;
    expectedMappings += descendants.size();
  }

  maps.uuidToMediaDir.reserve(expectedMappings);
  maps.uuidToArtworkDir.reserve(expectedMappings);
  maps.uuidToCollectionIndex.reserve(expectedMappings);

  addMapping(ctx.currentIndex, ctx.config);

  if (ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants) {
    for (int descendantIndex : descendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }
      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory =
          PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      subCol.artworkDirectory = PathUtils::validateAndExpandPath(
          subCol.artworkDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      addMapping(descendantIndex, subCol);
    }
  }
  return maps;
}

// Builds SQL IN clause with placeholders: "(?, ?, ...)"
auto QueryManager::buildUuidInClause(int uuidCount) -> QString {
  QString clause = "(";
  for (int i = 0; i < uuidCount; ++i) {
    clause += (i == 0 ? "?" : ", ?");
  }
  clause += ")";
  return clause;
}


int QueryManager::fetchItemCountImpl(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections, const QString &filter) {
  if (!ensureDatabaseAvailable("QueryManager::fetchItemCountImpl")) {
    return 0;
  }

  // Ensure we see the latest data committed by the scan worker.
  // Without this, our connection can return stale counts from a cached
  // WAL snapshot, causing the UI to show old item counts after scans.
  refreshWalView();

  if (!context.isValid()) {
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return 0;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);

  // IMPORTANT: Do not scan synchronously here.
  // Scans are dispatched to a dedicated scan worker so this query worker can
  // return counts immediately and keep the UI responsive.

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  if (uuids.isEmpty()) {
    auto err =
        ErrorContext::warning(ErrorCode::InvalidArgument,
                              "No valid collection UUIDs for item count query",
                              "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return 0;
  }
  const QString trimmedFilter = filter.trimmed();

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][QueryManager] fetchItemCount: uuidCount="
               << uuids.size() << "showAllSubcollectionItems="
               << ctx.config.showAllSubcollectionItems
               << "queryIncludeDescendants=" << ctx.queryIncludeDescendants;
  }

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][QueryManager] fetchItemCount: collIndex="
               << context.currentIndex << "filter='" << trimmedFilter
               << "' includeSubfolders="
               << context.config.includeContentSubfolders
               << " showAllSubfolderItems="
               << context.config.showAllSubfolderItems << " currentSubfolder='"
               << context.config.currentSubfolder << "'";
  }
  if (m_itemsFtsAvailable && !m_itemsFtsReady) {
    if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
      qWarning() << "[SearchDiag][QueryManager] fetchItemCount: checking FTS "
                    "readiness from DB...";
    }
    m_itemsFtsReady = isItemsFtsReadyFromDb();
    if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
      qWarning() << "[SearchDiag][QueryManager] fetchItemCount: FTS ready ="
                 << m_itemsFtsReady;
    }
  }
  const QString ftsQuery =
      (m_itemsFtsAvailable && m_itemsFtsReady && !trimmedFilter.isEmpty())
          ? buildFtsPrefixQuery(trimmedFilter)
          : QString();
  const bool useFts = !ftsQuery.isEmpty();

  // Check if we need to use temp table for large UUID lists
  // SQLite has a default limit of 999 bind variables
  bool useTempTable = false;
  const QString uuidClause = buildUuidFilterClause(uuids, useTempTable);

  if (useTempTable) {
    if (!ensureQueryUuidsPopulated(uuids)) {
      auto err = ErrorContext::warning(
          ErrorCode::DatabaseQueryFailed,
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
      sql = "SELECT COUNT(DISTINCT path) FROM items WHERE collection_uuid IN " +
            uuidClause;
    }
  }

  // Apply subfolder filtering when browsing subfolders
  const QString &subfolder = ctx.config.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose path starts with subfolder/
    sql += " AND path LIKE ?";
  } else if (ctx.config.includeContentSubfolders &&
             !ctx.config.showAllSubfolderItems && trimmedFilter.isEmpty()) {
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

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning()
        << "[SearchDiag][QueryManager] fetchItemCount: executing SQL, useFts="
        << useFts << "useTempTable=" << useTempTable
        << "uuidCount=" << uuids.size();
  }
  if (query.exec() && query.next()) {
    const int count = query.value(0).toInt();
    if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
      qWarning() << "[SearchDiag][QueryManager] fetchItemCount: result="
                 << count;
    }

    // For large collections, schedule deferred cache build for O(1) range
    // lookups. This avoids expensive ORDER BY + OFFSET on every fetchItemsRange
    // call. We don't block here - the cache builds after this function returns,
    // so the UI can start displaying items immediately using the slow path.
    //
    // Random sort mode ALWAYS requires a cache because SQL ORDER BY RANDOM()
    // with OFFSET cannot provide consistent results across paginated requests.
    const bool isRandomSort = (ctx.sortMode == SortMode::Random);
    if (isRandomSort ||
        count >= UIConstants::Database::PRECOMPUTE_SORT_THRESHOLD) {
      if (!hasSortedItemsCache() && !m_sortCacheBuildPending) {
        if (qEnvironmentVariableIsSet("KARTEND_RANGE_DIAG")) {
          qWarning() << "[RangeDiag] fetchItemCount: count=" << count
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

  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning() << "[SearchDiag][QueryManager] fetchItemCount: query FAILED"
               << query.lastError().text();
  }
  auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                   "Fetch item count failed",
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

void QueryManager::fetchItemCountWithToken(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections, const QString &filter,
    int requestToken) {
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning()
        << "[SearchDiag][QueryManager] fetchItemCountWithToken: ENTRY token="
        << requestToken << "filter='" << filter << "'";
  }
  const int count = fetchItemCountImpl(context, allCollections, filter);
  if (qEnvironmentVariableIsSet("KARTEND_SEARCH_DIAG")) {
    qWarning()
        << "[SearchDiag][QueryManager] fetchItemCountWithToken: EMIT token="
        << requestToken << "count=" << count;
  }
  emit itemCountLoadedWithToken(count, requestToken);
}

void QueryManager::ensureScannedForContext(
    const CollectionContext &context,
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
    auto err = ErrorContext::error(ErrorCode::InvalidCollectionContext,
                                   "Invalid collection context",
                                   "QueryManager::ensureScannedForContext");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);

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
      col.mediaDirectory =
          PathUtils::validateAndExpandPath(col.mediaDirectory, col.name);
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
            ? CollectionUtils::collectDescendantIndices(ctx.currentIndex,
                                                        allCollections)
            : ctx.precomputedDescendants;
    for (int descendantIndex : rawDescendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }

      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory =
          PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }

      (void)ensureCollectionScanned(descendantIndex, subCol);
    }
  }
}


void QueryManager::fetchVisualIndexForPath(
    const CollectionContext &context,
    const QList<CollectionConfig> &allCollections, const QString &filePath) {
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
  ctx.config.mediaDirectory = PathUtils::validateAndExpandPath(
      ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory = PathUtils::validateAndExpandPath(
      ctx.config.artworkDirectory, ctx.config.name);

  QStringList uuids = collectCollectionUuids(ctx, allCollections);
  CollectionDirMaps dirMaps = buildDirectoryMaps(ctx, allCollections);

  // Try sorted cache first (fast path)
  if (hasSortedItemsCache()) {
    const QByteArray currentHash =
        computeSortCacheHash(uuids, QString(), ctx.sortMode);
    if (currentHash == m_sortedItemsCacheHash) {
      // Query sorted cache for this path
      // The cache stores relative paths, so we need to check both
      QSqlQuery cacheQuery(m_db);
      cacheQuery.prepare(
          "SELECT position FROM sorted_items_cache WHERE path = ?");

      // Convert to relative path if it's absolute
      for (auto it = dirMaps.uuidToMediaDir.begin();
           it != dirMaps.uuidToMediaDir.end(); ++it) {
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
    if (i > 0)
      uuidPlaceholders += ", ";
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
  for (auto it = dirMaps.uuidToMediaDir.begin();
       it != dirMaps.uuidToMediaDir.end(); ++it) {
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
  qWarning() << "[SelectionRestore] fetchVisualIndexForPath:"
             << "rowCount in subquery:" << rowCount << "uuids:" << uuids.size()
             << "filePath:" << filePath << "relPath:" << relPath
             << "boundPath:" << boundPath;

  if (query.exec() && query.next()) {
    int position = query.value(0).toInt();
    qWarning() << "[SelectionRestore] Query returned position:" << position;
    emit visualIndexForPathLoaded(position, filePath);
  } else {
    qWarning() << "[SelectionRestore] Query returned NO MATCH, lastError:"
               << query.lastError().text();
    emit visualIndexForPathLoaded(-1, filePath);
  }
}
