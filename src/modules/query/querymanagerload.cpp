// Top-level load + fetch slots extracted from querymanager.cpp:
//   - loadAllCollections, loadItems, loadItemsWithSubcollections
//   - updateCachedCounts
//   - collectCollectionUuids, buildDirectoryMaps, buildUuidInClause
//   - fetchItemCountImpl, fetchItemCount, fetchItemCountWithToken
//   - ensureScannedForContext, fetchVisualIndexForPath
// Members of QueryManager; access existing class state.
#include "querymanager.h"

#include <atomic>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
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

void QueryManager::loadAllCollections(const QList<CollectionConfig> &allCollections) {
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

  for (int collectionIndex = 0; collectionIndex < allCollections.size(); ++collectionIndex) {
    CollectionConfig collection = allCollections[collectionIndex];

    collection.mediaDirectory =
        PathUtils::validateAndExpandPath(collection.mediaDirectory, collection.name);
    collection.artworkDirectory =
        PathUtils::validateAndExpandPath(collection.artworkDirectory, collection.name);

    if (collection.mediaDirectory.trimmed().isEmpty()) {
      continue;
    }

    // Only emit progress signal when a scan is actually needed (not for cached
    // loads)
    if (needsRescan(collectionIndex, collection)) {
      emit scanProgress(collectionIndex + 1, totalCollections, collection.name);
    }

    QHash<QString, QDateTime> timestamps;
    QStringList filePaths = loadOrScanCollection(collectionIndex, collection, timestamps);

    appendFileMapsAndListCanonical(
        collectionIndex, collection,
        CollectionUtils::resolveArtworkDirectory(collectionIndex, allCollections), filePaths,
        allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir, fileToCollectionIndex, false);
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
                                   "Invalid collection context", "QueryManager::loadItems");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext ctx = context;
  ctx.config.mediaDirectory =
      PathUtils::validateAndExpandPath(ctx.config.mediaDirectory, ctx.config.name);
  ctx.config.artworkDirectory =
      PathUtils::validateAndExpandPath(ctx.config.artworkDirectory, ctx.config.name);

  if (ctx.config.mediaDirectory.trimmed().isEmpty()) {
    emit itemsLoaded(QStringList(), QHash<QString, QString>(), QHash<QString, QString>(),
                     QHash<QString, QString>(), QHash<QString, int>());
    return;
  }

  QHash<QString, QDateTime> timestamps;
  QStringList filePaths = loadOrScanCollection(ctx.currentIndex, ctx.config, timestamps);

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
  } else if (ctx.config.includeContentSubfolders && !ctx.config.showAllSubfolderItems) {
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
  QString resolvedArtworkDir =
      CollectionUtils::resolveArtworkDirectory(ctx.currentIndex, allCollections);

  appendFileMapsAndListCanonical(ctx.currentIndex, ctx.config, resolvedArtworkDir, filePaths,
                                 allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                                 fileToCollectionIndex, false);

  sortFiles(allFilePaths, ctx.sortMode);

  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                   fileToCollectionIndex);
}

void QueryManager::loadItemsWithSubcollections(const CollectionContext &context,
                                               const QList<CollectionConfig> &allCollections) {
  if (!ensureDatabaseAvailable("QueryManager::loadItemsWithSubcollections")) {
    // Emit safe default so listeners don't hang awaiting itemsLoaded.
    emit itemsLoaded({}, {}, {}, {}, {});
    return;
  }

  if (!context.isValid()) {
    auto err =
        ErrorContext::error(ErrorCode::InvalidCollectionContext, "Invalid collection context",
                            "QueryManager::loadItemsWithSubcollections");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return;
  }

  CollectionContext mainCtx = context;
  mainCtx.config.mediaDirectory =
      PathUtils::validateAndExpandPath(mainCtx.config.mediaDirectory, mainCtx.config.name);
  mainCtx.config.artworkDirectory =
      PathUtils::validateAndExpandPath(mainCtx.config.artworkDirectory, mainCtx.config.name);

  QStringList allFilePaths;
  QHash<QString, QString> allFileNames;
  QHash<QString, QString> fileToArtworkDir;
  QHash<QString, QString> fileToMediaDir;
  QHash<QString, int> fileToCollectionIndex;

  QSet<QString> seenCanonicalPaths;
  QHash<QString, QString> canonicalPathCache;

  bool hasMainMediaDirectory = !mainCtx.config.mediaDirectory.trimmed().isEmpty();
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
    } else if (mainCtx.config.includeContentSubfolders && !mainCtx.config.showAllSubfolderItems) {
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

    appendFileMapsAndListCanonical(
        mainCtx.currentIndex, mainCtx.config,
        CollectionUtils::resolveArtworkDirectory(mainCtx.currentIndex, allCollections),
        mainFilePaths, allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
        fileToCollectionIndex, true, &seenCanonicalPaths, &canonicalPathCache);
  }

  // Use pre-computed descendants if available (O(1) from cache), otherwise
  // fall back to O(n²) tree traversal for backward compatibility
  const QList<int> &rawDescendants =
      mainCtx.precomputedDescendants.isEmpty()
          ? CollectionUtils::collectDescendantIndices(mainCtx.currentIndex, allCollections)
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
    collection.mediaDirectory =
        PathUtils::validateAndExpandPath(collection.mediaDirectory, collection.name);
    collection.artworkDirectory =
        PathUtils::validateAndExpandPath(collection.artworkDirectory, collection.name);

    if (collection.mediaDirectory.trimmed().isEmpty()) {
      continue;
    }

    QHash<QString, QDateTime> subTimestamps;
    QStringList subFilePaths = loadOrScanCollection(collectionIndex, collection, subTimestamps);

    appendFileMapsAndListCanonical(
        collectionIndex, collection,
        CollectionUtils::resolveArtworkDirectory(collectionIndex, allCollections), subFilePaths,
        allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir, fileToCollectionIndex, true,
        &seenCanonicalPaths, &canonicalPathCache);
  }

  sortFiles(allFilePaths, context.sortMode);
  emit itemsLoaded(allFilePaths, allFileNames, fileToArtworkDir, fileToMediaDir,
                   fileToCollectionIndex);
}

void QueryManager::updateCachedCounts(quint64 generation, const QStringList &collectionUuids) {
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
        directCountsByUuid.insert(query.value(0).toString(), query.value(1).toLongLong());
      }
    }
  }

  emit cachedCountsComputed(generation, globalCount, directCountsByUuid);
}

// Collects UUIDs for a collection and optionally its descendants
auto QueryManager::collectCollectionUuids(const CollectionContext &ctx,
                                          const QList<CollectionConfig> &allCollections)
    -> QStringList {
  QStringList uuids;

  // Kartend-vlm7: a playlist's "scope" is the union of source collection
  // uuids across its items — there's no single owning collection. The fetch
  // SQL still uses this list for its top-level WHERE, then layers a
  // (uuid, path) EXISTS clause via query_playlist_scope to narrow down to
  // playlist members.
  if (ctx.config.isPlaylist) {
    return loadPlaylistSourceUuids(ctx.config.playlistId);
  }

  if (ctx.queryIncludeAllCollections) {
    QSet<QString> seen;
    uuids.reserve(allCollections.size());
    for (int i = 0; i < allCollections.size(); ++i) {
      CollectionConfig c = allCollections[i];
      c.mediaDirectory = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      if (c.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      const QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
      if (!uuid.isEmpty() && !seen.contains(uuid)) {
        seen.insert(uuid);
        uuids.append(uuid);
      }
    }
    return uuids;
  }

  // Check if we need descendants (for subcollection search modes)
  const bool needsDescendants = ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants;

  // Use pre-computed UUIDs if available and we need descendants
  // This provides major performance improvement for large hierarchies (3000+
  // subcollections) by eliminating repeated filesystem exists() checks and SHA1
  // hash computations
  if (needsDescendants && !ctx.precomputedDescendantUuids.isEmpty()) {
    return ctx.precomputedDescendantUuids;
  }

  uuids << CollectionUtils::computeCollectionUuid(ctx.config.name, ctx.config.mediaDirectory);

  if (needsDescendants) {
    // Use pre-computed descendants if available (O(1) from cache), otherwise
    // fall back to O(n²) tree traversal for backward compatibility
    const QList<int> &descendants =
        ctx.precomputedDescendants.isEmpty()
            ? CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections)
            : ctx.precomputedDescendants;
    uuids.reserve(uuids.size() + descendants.size());
    for (int descendantIndex : descendants) {
      if (descendantIndex == ctx.currentIndex || descendantIndex < 0 ||
          descendantIndex >= allCollections.size()) {
        continue;
      }
      CollectionConfig subCol = allCollections[descendantIndex];
      subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      if (subCol.mediaDirectory.trimmed().isEmpty()) {
        continue;
      }
      uuids << CollectionUtils::computeCollectionUuid(subCol.name, subCol.mediaDirectory);
    }
  }
  return uuids;
}

// Builds UUID-to-directory mappings for resolving paths from query results
auto QueryManager::buildDirectoryMaps(const CollectionContext &ctx,
                                      const QList<CollectionConfig> &allCollections)
    -> CollectionDirMaps {
  CollectionDirMaps maps;

  // Kartend-vlm7: a playlist's items can come from any collection. Build
  // mappings for every real (non-playlist) collection whose uuid appears in
  // the playlist's source uuid set so the post-fetch path resolution still
  // turns relative paths into absolutes via the right media directory.
  if (ctx.config.isPlaylist) {
    const QStringList sourceUuids = loadPlaylistSourceUuids(ctx.config.playlistId);
    if (sourceUuids.isEmpty()) {
      return maps;
    }
    QSet<QString> wanted(sourceUuids.begin(), sourceUuids.end());
    maps.uuidToMediaDir.reserve(wanted.size());
    maps.uuidToArtworkDir.reserve(wanted.size());
    maps.uuidToCollectionIndex.reserve(wanted.size());
    for (int i = 0; i < allCollections.size(); ++i) {
      if (allCollections[i].isPlaylist) {
        continue;
      }
      CollectionConfig c = allCollections[i];
      c.mediaDirectory = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      c.artworkDirectory = PathUtils::validateAndExpandPath(c.artworkDirectory, c.name);
      const QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
      if (uuid.isEmpty() || !wanted.contains(uuid)) {
        continue;
      }
      maps.uuidToMediaDir[uuid] = c.mediaDirectory;
      maps.uuidToArtworkDir[uuid] = CollectionUtils::resolveArtworkDirectory(i, allCollections);
      maps.uuidToCollectionIndex[uuid] = i;
    }
    return maps;
  }

  // Check if we need descendants (for subcollection search modes)
  const bool needsDescendants = ctx.config.showAllSubcollectionItems || ctx.queryIncludeDescendants;

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
    const QString uuid = CollectionUtils::computeCollectionUuid(c.name, c.mediaDirectory);
    if (uuid.isEmpty()) {
      return;
    }
    if (!maps.uuidToMediaDir.contains(uuid)) {
      maps.uuidToMediaDir[uuid] = c.mediaDirectory;
    }
    if (!maps.uuidToArtworkDir.contains(uuid)) {
      // Resolve artwork directory with parent fallback - if this collection
      // has no artwork directory, walk up the parent chain to find one
      QString resolvedArtwork =
          CollectionUtils::resolveArtworkDirectory(collectionIndex, allCollections);
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
      c.mediaDirectory = PathUtils::validateAndExpandPath(c.mediaDirectory, c.name);
      c.artworkDirectory = PathUtils::validateAndExpandPath(c.artworkDirectory, c.name);
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
                      ? CollectionUtils::collectDescendantIndices(ctx.currentIndex, allCollections)
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
      subCol.mediaDirectory = PathUtils::validateAndExpandPath(subCol.mediaDirectory, subCol.name);
      subCol.artworkDirectory =
          PathUtils::validateAndExpandPath(subCol.artworkDirectory, subCol.name);
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
