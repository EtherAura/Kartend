// Collection lifecycle persistence methods extracted from querymanager.cpp:
//   - loadItemsFromDatabaseByUuid (~30 LOC)
//   - invalidateCollectionCache (~13 LOC)
// All remain QueryManager members and access existing class state.
//
// saveItemsToDatabase moved into ScanService (scanservice.{h,cpp}).
// clearCollectionFromDatabaseByUuid became a free function in
// querymanagerhelpers.h::QueryManagerInternal.
#include "querymanager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>

#include "errorutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

QStringList QueryManager::loadItemsFromDatabaseByUuid(const QString &collectionUuid) {
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

  while (query.next()) {
    filePaths.append(query.value(0).toString());
  }

  return filePaths;
}

void QueryManager::invalidateCollectionCache(const QString &collectionUuid) {
  assertOwnerThread();
  // Cancel any ongoing scan before clearing cache to prevent lock conflicts
  requestCancelScan();

  // Invalidate UUID temp table cache so next query repopulates
  m_cachedQueryUuidsHash.clear();

  // Invalidate sorted items cache - forces rebuild on next fetchItemCount
  clearSortedItemsCache();

  // Smart-playlist scopes are derived from item data, so a collection data
  // change must drop the cached scope too (Kartend-s9jw).
  m_cachedPlaylistScopeKey.clear();

  QueryManagerInternal::clearCollectionFromDatabaseByUuid(m_db, m_statementCache, collectionUuid);
  emit cacheInvalidated(collectionUuid);
}

void QueryManager::invalidateSmartPlaylistScope() {
  assertOwnerThread();
  // Clearing the key alone is enough: the next ensurePlaylistScopePopulated
  // sees a key mismatch and re-evaluates the filter against current item data.
  m_cachedPlaylistScopeKey.clear();
}
