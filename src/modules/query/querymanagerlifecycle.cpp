// Collection lifecycle persistence methods extracted from querymanager.cpp:
//   - saveItemsToDatabase (~187 LOC)
//   - loadItemsFromDatabaseByUuid (~30 LOC)
//   - invalidateCollectionCache (~13 LOC)
//   - clearCollectionFromDatabaseByUuid (~56 LOC)
// All remain QueryManager members and access existing class state.
//
// The SynchronousPragmaGuard helper is duplicated here in an anonymous
// namespace (internal linkage); the original lives next to
// scanAndSaveItemsToDatabase in querymanager.cpp.
#include "querymanager.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QThread>
#include <stdexcept>

#include "collectionutils.h"
#include "errorutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using QueryManagerInternal::SynchronousPragmaGuard;

void QueryManager::saveItemsToDatabase(int collectionIndex, const QStringList &filePaths,
                                       const QHash<QString, QDateTime> &timestamps,
                                       const CollectionConfig &collection,
                                       const QString &dirSignature) {
  Q_UNUSED(collectionIndex)

  if (!m_db.isOpen()) {
    auto err = ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database is not open",
                                   "QueryManager::saveItemsToDatabase");
    ErrorUtils::logError(err);
    return;
  }

  // Include includeContentSubfolders in the signature to match needsRescan
  QString extSignature =
      collection.extensions.isEmpty() ? QString() : collection.extensions.join('|');
  extSignature += collection.includeContentSubfolders ? "|subfolders" : "";

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // Temporarily disable synchronous writes for bulk insert performance
  QSqlQuery pragmaOff(m_db);
  if (!pragmaOff.exec("PRAGMA synchronous = OFF")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to set synchronous=OFF for bulk insert",
                                               "QueryManager::saveItemsToDatabase")
                             .withDetails(pragmaOff.lastError().text()));
  }
  const SynchronousPragmaGuard restoreSynchronous(m_db);

  // Batch insert for performance - SQLite handles up to 999 variables per
  // statement With 5 columns per row, we can insert 199 rows per batch (995
  // variables)
  constexpr int BATCH_SIZE = 199;
  // Commit every N batches to save incremental progress (~100K items per
  // commit)
  constexpr int COMMIT_INTERVAL_BATCHES = 500;
  constexpr int PROGRESS_REPORT_INTERVAL = 50000; // Report every 50K items

  const int totalItems = filePaths.size();

  // Cancellation-safe writes: stage into TEMP table and only apply to
  // persistent DB on success.
  if (!ensureScannedItemsTempTable()) {
    return;
  }

  clearScannedItemsTempTable();

  struct ScannedItemsTempTableCleanup {
    QueryManager *self = nullptr;
    bool enabled = false;
    ~ScannedItemsTempTableCleanup() {
      if (enabled && self) {
        self->clearScannedItemsTempTable();
      }
    }
  } scannedItemsCleanup{this, true};

  // Second phase: insert items in batches with periodic commits
  int batchesSinceCommit = 0;
  bool inTransaction = false;
  int itemsInserted = 0; // cppcheck-suppress unreadVariable - used for progress reporting

  for (int batchStart = 0; batchStart < totalItems; batchStart += BATCH_SIZE) {
    // Check for cancellation between batches
    if (isScanCancelled()) {
      if (inTransaction) {
        m_db.rollback();
        inTransaction = false;
      }
      break;
    }

    // Start new transaction if needed
    if (!inTransaction) {
      if (!m_db.transaction()) {
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to start transaction for bulk insert",
                                          "QueryManager::saveItemsToDatabase")
                       .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return;
      }
      inTransaction = true;
      batchesSinceCommit = 0;
    }

    const int batchEnd = qMin(batchStart + BATCH_SIZE, totalItems);
    const int batchCount = batchEnd - batchStart;

    // Stage scanned items into TEMP table.
    QStringList batchPaths;
    batchPaths.reserve(batchCount);
    for (int i = batchStart; i < batchEnd; ++i) {
      batchPaths.append(filePaths[i]);
    }
    insertScannedItemsBatch(batchPaths, timestamps);

    itemsInserted = batchEnd;
    ++batchesSinceCommit;

    // Commit periodically to save incremental progress
    if (batchesSinceCommit >= COMMIT_INTERVAL_BATCHES) {
      if (!m_db.commit()) {
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to commit bulk insert transaction",
                                          "QueryManager::saveItemsToDatabase")
                       .withDetails(m_db.lastError().text());
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        m_db.rollback();
        return;
      }
      inTransaction = false;

      // Report progress
      emit scanItemsProgress(itemsInserted, totalItems);
    }

    // Report progress at intervals even within transaction
    if (itemsInserted % PROGRESS_REPORT_INTERVAL == 0) {
      emit scanItemsProgress(itemsInserted, totalItems);
    }
  }

  // Final commit for any remaining items
  if (inTransaction) {
    if (!m_db.commit()) {
      auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                        "Failed to commit final bulk insert transaction",
                                        "QueryManager::saveItemsToDatabase")
                     .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      m_db.rollback();
      return;
    }
  }

  // Final progress report
  if (!isScanCancelled()) {
    emit scanItemsProgress(totalItems, totalItems);

    int legacyId = -1;
    if (!prepareCollectionForItemsInsert(collection, uuid, extSignature, legacyId)) {
      return;
    }

    // Apply staged results atomically.
    if (!m_db.transaction()) {
      auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                        "Failed to start transaction to apply staged scan results",
                                        "QueryManager::saveItemsToDatabase")
                     .withDetails(m_db.lastError().text());
      ErrorUtils::logError(err);
      emit errorOccurred(err);
      return;
    }

    const bool upsertOk = applyScannedItemsToDatabase(legacyId, uuid);
    const bool deleteOk = deleteMissingItemsByUuidUsingScannedItems(uuid);

    QSqlQuery &meta = getPreparedStatement(QuerySQL::UPDATE_COLLECTION_SCAN_METADATA);
    meta.bindValue(0, QDateTime::currentDateTime().toString(Qt::ISODate));
    meta.bindValue(1, dirSignature);
    meta.bindValue(2, uuid);
    const bool metaOk = meta.exec();

    if (upsertOk && deleteOk && metaOk) {
      (void)m_db.commit();
    } else {
      m_db.rollback();
    }
  }
}

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
  // Cancel any ongoing scan before clearing cache to prevent lock conflicts
  requestCancelScan();

  // Invalidate UUID temp table cache so next query repopulates
  m_cachedQueryUuidsHash.clear();

  // Invalidate sorted items cache - forces rebuild on next fetchItemCount
  clearSortedItemsCache();

  clearCollectionFromDatabaseByUuid(collectionUuid);
  emit cacheInvalidated(collectionUuid);
}

void QueryManager::clearCollectionFromDatabaseByUuid(const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return;
  }

  // Retry logic for database lock scenarios
  constexpr int MAX_RETRIES = 5;
  constexpr int BASE_DELAY_MS = 100;

  for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
    if (attempt > 0) {
      // Exponential backoff: 100ms, 200ms, 400ms, 800ms, 1600ms
      QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
    }

    if (!m_db.transaction()) {
      continue; // Retry if can't start transaction
    }

    try {
      // Use cached prepared statement for deleting items
      QSqlQuery &query = getPreparedStatement(QuerySQL::DELETE_ITEMS_BY_UUID);
      query.bindValue(0, collectionUuid);
      if (!query.exec()) {
        throw std::runtime_error(query.lastError().text().toStdString());
      }

      // Use cached prepared statement for deleting collection
      QSqlQuery &delc = getPreparedStatement(QuerySQL::DELETE_COLLECTION_BY_UUID);
      delc.bindValue(0, collectionUuid);
      delc.exec();

      m_db.commit();
      return; // Success - exit retry loop
    } catch (const std::exception &e) {
      m_db.rollback();

      QString errorText = QString::fromStdString(e.what());
      bool isLockError = errorText.contains("locked", Qt::CaseInsensitive);

      if (!isLockError || attempt == MAX_RETRIES - 1) {
        // Non-lock error or final attempt - log and give up
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
                                          "Failed to clear collection from database",
                                          "QueryManager::clearCollectionFromDatabaseByUuid")
                       .withDetails(errorText);
        ErrorUtils::logError(err);
        return;
      }
      // Lock error - will retry
    }
  }
}
