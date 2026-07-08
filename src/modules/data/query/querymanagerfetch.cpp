// Sibling TU: item-count and visual-index fetch slots for QueryManager.
#include "querymanager.h"

#include "loggingcategories.h"
#include "sqllike.h"
#include <atomic>
#include <QDateTime>
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

#include "artworkutils.h"
#include "collection/collectioncontext.h"
#include "errorutils.h"
#include "itemartwork.h"
#include "itemmetadata.h"
#include "pathutils.h"
#include "queryhelpers.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "searchqueryparser.h"
#include "uiconstants/database.h"
#include "usagestatsstore.h"
#include "videoutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using QueryManagerInternal::canonicalKeyPath;
using QueryManagerInternal::displayNameForBase;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg

using QueryHelpers::buildSearchTokenClauses;
using QueryHelpers::SearchTokenClauses;
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
    // an empty playlist is a valid state — show "0 items" rather
    // than raising a "no valid uuids" error. The scope is empty by definition;
    // there's nothing to query.
    if (ctx.config.isPlaylist) {
      return 0;
    }
    auto err = ErrorContext::warning(ErrorCode::InvalidArgument,
                                     "No valid collection UUIDs for item count query",
                                     "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return 0;
  }
  const QString trimmedFilter = filter.trimmed();
  // Parse out structured tokens (played:, tag:, missing:artwork, favorite:)
  // so the FTS / LIKE portion only sees the free-text remainder. Unknown
  // tokens are stripped from the free text too — they go into
  // parsedQuery.unknownTokens for the caller (SearchManager) to surface as
  // a non-disruptive log warning.
  const SearchQueryParser::SearchQuery parsedQuery = SearchQueryParser::parse(trimmedFilter);
  const QString freeText = parsedQuery.freeText.trimmed();
  const SearchTokenClauses tokenClauses = buildSearchTokenClauses(parsedQuery, QStringLiteral(""));
  if (!parsedQuery.unknownTokens.isEmpty()) {
    qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: unknown search tokens (ignored):"
                          << parsedQuery.unknownTokens;
  }

  // refresh the playlist scope temp table when it's stale so the
  // EXISTS clause appended below filters against the right (uuid, path) set.
  if (ctx.config.isPlaylist && !ensurePlaylistScopePopulated(ctx.config.playlistId)) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to populate playlist scope for item count",
                                     "QueryManager::fetchItemCount");
    ErrorUtils::logError(err);
    emit errorOccurred(err);
    return 0;
  }

  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: uuidCount=" << uuids.size()
                        << "showAllSubcollectionItems=" << ctx.config.showAllSubcollectionItems
                        << "queryIncludeDescendants=" << ctx.queryIncludeDescendants;
  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: collIndex=" << context.currentIndex
                        << "filter='" << trimmedFilter << "' includeSubfolders="
                        << context.config.folderBrowsing.includeContentSubfolders
                        << " showAllSubfolderItems="
                        << context.config.folderBrowsing.showAllSubfolderItems
                        << " currentSubfolder='" << context.config.folderBrowsing.currentSubfolder
                        << "'";
  if (m_itemsFtsAvailable && !m_itemsFtsReady) {
    qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: checking FTS "
                             "readiness from DB...";
    m_itemsFtsReady = isItemsFtsReadyFromDb();
    qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCount: FTS ready =" << m_itemsFtsReady;
  }
  const QString ftsQuery = (m_itemsFtsAvailable && m_itemsFtsReady && !freeText.isEmpty())
                               ? buildFtsPrefixQuery(freeText)
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
    // (collection_uuid, path) is the item identity (UNIQUE(collection_uuid,
    // path) on the items table), so COUNT(*) counts each subcollection's
    // copy of a same-named file separately. fetchItemsRange has the same
    // semantics so counts and rendered tiles stay in sync.
    if (useTempTable) {
      sql = "SELECT COUNT(*) FROM items_fts "
            "WHERE items_fts MATCH ? AND EXISTS " +
            uuidClause;
    } else {
      sql = "SELECT COUNT(*) FROM items_fts "
            "WHERE items_fts MATCH ? AND collection_uuid IN " +
            uuidClause;
    }
  } else {
    if (useTempTable) {
      sql = "SELECT COUNT(*) FROM items WHERE EXISTS " + uuidClause;
    } else {
      sql = "SELECT COUNT(*) FROM items WHERE collection_uuid IN " + uuidClause;
    }
  }

  // Apply subfolder filtering when browsing subfolders.
  // Subfolder predicates key on rel_path (the media-dir-relative form) — with
  // items.path now absolute, a "no slash" test on path would never match a
  // root-level item. COALESCE(rel_path, path) is a defensive fallback for any
  // row left with a NULL rel_path before the v13 reconcile runs.
  const QString &subfolder = ctx.config.folderBrowsing.currentSubfolder;
  if (!subfolder.isEmpty()) {
    // In a subfolder - show only items whose rel_path starts with subfolder/.
    // ESCAPE + escapeLike (Kartend-joird): an unescaped underscore in the
    // folder name is a single-char wildcard, so `my_videos/%` also matched
    // sibling `myXvideos/...` rows.
    sql += QStringLiteral(" AND COALESCE(rel_path, path) LIKE ?") + KartendDb::kLikeEscape;
  } else if (ctx.config.folderBrowsing.includeContentSubfolders &&
             !ctx.config.folderBrowsing.showAllSubfolderItems && freeText.isEmpty() &&
             tokenClauses.sql.isEmpty()) {
    // At root with subfolders enabled but NOT showing all items, we normally
    // exclude items in subfolders so the UI can present folder tiles.
    //
    // When a search filter (free text OR structured tokens) is active,
    // include subfolder items so search can find matches even in
    // "virtual folders only" collections.
    sql += " AND COALESCE(rel_path, path) NOT LIKE '%/%'";
  }
  // If showAllSubfolderItems is true, we don't filter - all items are shown
  // mixed together

  if (!freeText.isEmpty()) {
    if (!useFts) {
      sql += QStringLiteral(" AND name LIKE ?") + KartendDb::kLikeEscape;
    }
  }

  // Append parsed structured-token clauses (played / tag / missing:artwork
  // / favorite). Each is its own AND/EXISTS fragment built by the helper so
  // adding a token never reshapes the surrounding SQL.
  sql += tokenClauses.sql;

  // narrow to playlist members. Composing this as an EXISTS
  // clause keeps it orthogonal to the existing filter / FTS / temp-table
  // logic so we don't fork the SQL builder.
  if (ctx.config.isPlaylist) {
    sql += " AND EXISTS (SELECT 1 FROM query_playlist_scope p "
           "WHERE p.uuid = collection_uuid AND p.path = path)";
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
    query.bindValue(bindPos++, KartendDb::escapeLike(subfolder) + "/%");
  }
  if (!freeText.isEmpty() && !useFts) {
    query.bindValue(bindPos++, "%" + KartendDb::escapeLike(freeText) + "%");
  }
  for (const QVariant &v : tokenClauses.binds) {
    query.bindValue(bindPos++, v);
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
    // Playlists skip the sorted_items_cache in v1 — its hash
    // collides with non-playlist queries over the same source uuids; revisit
    // when we add a playlist-aware cache key.
    const bool isRandomSort = (ctx.sortMode == SortMode::Random);
    if (!ctx.config.isPlaylist &&
        (isRandomSort || count >= UIConstants::Database::PRECOMPUTE_SORT_THRESHOLD)) {
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
  assertOwnerThread();
  emit itemCountLoaded(fetchItemCountImpl(context, allCollections, filter));
}

void QueryManager::fetchItemCountWithToken(const CollectionContext &context,
                                           const QList<CollectionConfig> &allCollections,
                                           const QString &filter, int requestToken) {
  assertOwnerThread();
  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCountWithToken: ENTRY token=" << requestToken
                        << "filter='" << filter << "'";
  const int count = fetchItemCountImpl(context, allCollections, filter);
  qCDebug(lcSearchDiag) << "[QueryManager] fetchItemCountWithToken: EMIT token=" << requestToken
                        << "count=" << count;
  emit itemCountLoadedWithToken(count, requestToken);
}

void QueryManager::ensureScannedForContext(const CollectionContext &context,
                                           const QList<CollectionConfig> &allCollections) {
  assertOwnerThread();
  if (!ensureDatabaseAvailable("QueryManager::ensureScannedForContext")) {
    return;
  }

  // Ensure we see the latest data committed by the query worker (which may have
  // just invalidated/cleared the collection cache). Without this, the scan
  // worker's WAL snapshot may be stale, causing needsRescan to return false
  // because it still sees old collection data that was just deleted.
  refreshWalView();

  // Connection availability + WAL freshness are QueryManager's responsibility;
  // the actual scan work (including the v13 path-absolutize reconcile) is the
  // scan subsystem's. Delegate the rest.
  m_scanService.ensureScannedForContext(context, allCollections);
}

void QueryManager::fetchVisualIndexForPath(const CollectionContext &context,
                                           const QList<CollectionConfig> &allCollections,
                                           const QString &filePath) {
  assertOwnerThread();
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

  // empty playlist short-circuits — there's nothing to find.
  if (ctx.config.isPlaylist && uuids.isEmpty()) {
    emit visualIndexForPathLoaded(-1, filePath);
    return;
  }
  if (ctx.config.isPlaylist && !ensurePlaylistScopePopulated(ctx.config.playlistId)) {
    emit visualIndexForPathLoaded(-1, filePath);
    return;
  }

  // Try sorted cache first (fast path)
  // cache is bypassed for playlists (see fetchItemsRange).
  if (!ctx.config.isPlaylist && hasSortedItemsCache()) {
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

  // Slow path: query with ROW_NUMBER to find position. Build ORDER BY clause
  // (bare column names, no LIMIT/OFFSET — pagination is via the outer
  // `WHERE rn = ?` filter, not SQL OFFSET).
  const QString orderClause =
      QueryHelpers::orderByForSortMode(ctx.sortMode, /*useSortPrefix=*/false,
                                       /*withLimitOffset=*/false);

  // Build WHERE clause for UUIDs
  const QString uuidPlaceholders = QueryHelpers::placeholderList(uuids.size());

  // Convert the absolute filePath back to (collection_uuid, relative path).
  // (collection_uuid, path) is the item identity, so we need both to
  // disambiguate same-named files in different subcollections.
  QString relPath;
  QString matchedUuid;
  for (auto it = dirMaps.uuidToMediaDir.begin(); it != dirMaps.uuidToMediaDir.end(); ++it) {
    if (filePath.startsWith(it.value())) {
      relPath = filePath.mid(it.value().length());
      if (relPath.startsWith('/')) {
        relPath = relPath.mid(1);
      }
      matchedUuid = it.key();
      break;
    }
  }

  // narrow the inner subquery to playlist members so the
  // ROW_NUMBER mapping matches what fetchItemsRange returns.
  const QString playlistClause =
      ctx.config.isPlaylist
          ? QStringLiteral(" AND EXISTS (SELECT 1 FROM query_playlist_scope p WHERE p.uuid = "
                           "collection_uuid AND p.path = path)")
          : QString();

  // Inner subquery enumerates every (collection_uuid, path) row in sort
  // order; outer WHERE matches both keys when the uuid is known so the
  // selection landing on a duplicate-named file resolves to the right tile.
  const QString outerWhere = matchedUuid.isEmpty()
                                 ? QStringLiteral("path = ?")
                                 : QStringLiteral("path = ? AND collection_uuid = ?");

  QString sql =
      QString("SELECT rn FROM ("
              "  SELECT path, collection_uuid, ROW_NUMBER() OVER (%1) - 1 as rn "
              "  FROM (SELECT path, collection_uuid, name, last_modified, file_size FROM items "
              "WHERE collection_uuid IN (%2)%3)"
              ") WHERE %4")
          .arg(orderClause, uuidPlaceholders, playlistClause, outerWhere);

  QSqlQuery query(m_db);
  query.prepare(sql);

  int bindPos = 0;
  for (const QString &uuid : uuids) {
    query.bindValue(bindPos++, uuid);
  }

  if (relPath.isEmpty()) {
    // Try the full path as-is
    query.bindValue(bindPos++, filePath);
  } else {
    query.bindValue(bindPos++, relPath);
  }
  if (!matchedUuid.isEmpty()) {
    query.bindValue(bindPos++, matchedUuid);
  }

  // Debug: count rows the subquery enumerates (should match fetchItemCount)
  QString countSql =
      QString("SELECT COUNT(*) FROM items WHERE collection_uuid IN (%1)").arg(uuidPlaceholders);
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

void QueryManager::loadItemDetail(int requestToken, const QString &collectionUuid,
                                  const QString &filePath, const QString &artworkDir,
                                  const QString &videoDir, const QString &manualDir) {
  assertOwnerThread();
  ItemDetailData data;
  if (!ensureDatabaseAvailable("QueryManager::loadItemDetail")) {
    emit itemDetailLoaded(data, requestToken); // valid == false
    return;
  }
  // See the freshest committed rows from the scan worker (same guard the fetch
  // paths use) — the detail page is often opened right after a scan.
  refreshWalView();

  const QString baseName = QFileInfo(filePath).completeBaseName();

  // Per-item artwork overrides keyed by type (empty without a DB row); custom
  // (non-standard) types keep their stored order so the gallery lists them
  // after the standard set. The DB-backed fields below only load with a uuid,
  // but typed-artwork resolution runs regardless — a hash-only / DAT-less item
  // still surfaces standard-subdir artwork (matches the prior inline path).
  QHash<QString, QString> overridesByType;
  QStringList customOrder;
  if (!collectionUuid.isEmpty()) {
    if (auto md = ItemMetadataStore::load(m_db, collectionUuid, filePath); md.isOk()) {
      data.metadata = md.value();
    }
    if (auto us = UsageStatsStore::loadForItem(m_db, collectionUuid, filePath); us.isOk()) {
      data.usage = us.value();
    }
    data.manualPath =
        ItemMetadataStore::resolveManualFile(data.metadata.manualPath, baseName, manualDir);

    if (auto aw = ItemArtworkStore::loadAllForItem(m_db, collectionUuid, filePath); aw.isOk()) {
      const auto &rows = aw.value();
      overridesByType.reserve(rows.size());
      for (const auto &row : rows) {
        overridesByType.insert(row.artworkType, row.manualPath);
        if (!ItemArtworkStore::isStandardType(row.artworkType)) {
          customOrder.append(row.artworkType);
        }
      }
    }
  }

  const auto resolveType = [&](const QString &type) {
    const QString resolved = ItemArtworkStore::resolveArtworkPath(overridesByType.value(type),
                                                                  baseName, artworkDir, type);
    if (!resolved.isEmpty()) {
      data.typedArtwork.append({type, resolved});
    }
  };
  for (const QString &type : ItemArtworkStore::standardTypes()) {
    resolveType(type);
  }
  for (const QString &type : customOrder) {
    resolveType(type);
  }

  // Flat-layout fallback cover ({artworkDir}/{baseName}.{ext}) via the cached
  // directory scan (Kartend-4p8o: was findArtworkForFile, an uncached scan).
  if (!artworkDir.isEmpty()) {
    data.fallbackCoverPath =
        ArtworkUtils::findArtworkForFileCached(QFileInfo(filePath).fileName(), artworkDir);
  }
  // Two video roots in priority order (matches detailspanemanagermetadata.cpp):
  // the explicit videoDirectory, then {artworkDir}/video/ — the single-root
  // layout the scraper writes to.
  if (!videoDir.isEmpty()) {
    data.videoPath = VideoUtils::findVideoForFile(filePath, videoDir);
  }
  if (data.videoPath.isEmpty() && !artworkDir.isEmpty()) {
    data.videoPath = VideoUtils::findVideoForFile(filePath, QDir(artworkDir).filePath("video"));
  }

  // File info — the per-open stat is part of the stall this moves off the UI
  // thread.
  if (const QFileInfo fi(filePath); fi.exists()) {
    data.fileSize = fi.size();
    data.fileModified = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    data.fileExtension =
        fi.suffix().isEmpty() ? QString() : QStringLiteral(".%1").arg(fi.suffix().toLower());
  }

  data.valid = true;
  emit itemDetailLoaded(data, requestToken);
}

void QueryManager::fetchItemStateFlagsForCollection(const QString &collectionUuid) {
  assertOwnerThread();
  QStringList pinned;
  QStringList hidden;
  QStringList continueLater;
  if (!ensureDatabaseAvailable("QueryManager::fetchItemStateFlagsForCollection") ||
      collectionUuid.isEmpty()) {
    emit itemStateFlagsLoaded(collectionUuid, pinned, hidden, continueLater);
    return;
  }
  // Flag toggles commit on the main-thread connection; refresh this
  // connection's WAL view (same guard as the other fetch paths) so a pin
  // made just before the collection switch is visible to the query.
  refreshWalView();

  QSqlQuery q(m_db);
  if (!q.prepare("SELECT path, is_pinned, is_hidden, continue_later "
                 "FROM item_metadata "
                 "WHERE collection_uuid = ? "
                 "AND (is_pinned = 1 OR is_hidden = 1 OR continue_later = 1)")) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare item-state-flags query",
                                             "QueryManager::fetchItemStateFlagsForCollection")
                             .withDetails(q.lastError().text()));
    emit itemStateFlagsLoaded(collectionUuid, pinned, hidden, continueLater);
    return;
  }
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to enumerate item state flags",
                                             "QueryManager::fetchItemStateFlagsForCollection")
                             .withDetails(q.lastError().text()));
    emit itemStateFlagsLoaded(collectionUuid, pinned, hidden, continueLater);
    return;
  }
  while (q.next()) {
    const QString path = q.value(0).toString();
    if (q.value(1).toInt() != 0) {
      pinned.append(path);
    }
    if (q.value(2).toInt() != 0) {
      hidden.append(path);
    }
    if (q.value(3).toInt() != 0) {
      continueLater.append(path);
    }
  }
  emit itemStateFlagsLoaded(collectionUuid, pinned, hidden, continueLater);
}
