// Item-persist cluster extracted from querymanager.cpp:
//   - ensureCollectionScanned, insertItemsBatch
//   - ensureScannedItemsTempTable, clearScannedItemsTempTable
//   - insertScannedItemsBatch, applyScannedItemsToDatabase
//   - deleteMissingItemsByUuidUsingScannedItems, prepareCollectionForItemsInsert
//   - scanAndSaveItemsToDatabase (~553 LOC, the main scan+persist driver)
// Members of QueryManager; access existing class state.
//
// Scan helpers (DirectoryScanTask, ScanCompletionQueue, SynchronousPragmaGuard,
// dirSignature*) live in querymanagerhelpers.h::QueryManagerInternal — pulled
// in via `using namespace QueryManagerInternal;` below.
#include "querymanager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutex>
#include <QRegularExpression>
#include <QRunnable>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVector>
#include <QWaitCondition>
#include <atomic>
#include <memory>
#include <stdexcept>

#include "collectionutils.h"
#include "errorutils.h"
#include "querymanagerhelpers.h"
#include "querymanagersql.h"
#include "uiconstants.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using namespace QueryManagerInternal;

Q_DECLARE_LOGGING_CATEGORY(lcQueryManager)
#define debugLog(msg) qCDebug(lcQueryManager) << msg


bool QueryManager::ensureCollectionScanned(int collectionIndex,
                                           const CollectionConfig &collection) {
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return false;
  }

  // If the directory isn't present, don't emit scan-starting UI or attempt a
  // scan. This avoids rapid "scan" loops when a collection points to a missing
  // mount.
  if (!QFileInfo(collection.mediaDirectory).exists()) {
    return false;
  }

  if (!needsRescan(collectionIndex, collection)) {
    return false;
  }

  // Reset cancellation flag before starting new scan
  resetScanCancellation();

  // Compute UUID for completion signal
  const QString uuid = CollectionUtils::computeCollectionUuid(
      collection.name, collection.mediaDirectory);

  // Notify UI that a scan is starting (estimated items unknown, use -1)
  emit scanStarting(collection.name, -1);

  // Stream scan results directly into DB inserts to reduce peak memory.
  const bool success = scanAndSaveItemsToDatabase(collectionIndex, collection);

  // Always emit collectionScanCompleted when we emitted scanStarting, even if
  // the scan failed. This ensures MainWindow's m_activeScanCount is decremented
  // properly and the overlay is hidden. The caller can still check the return
  // value to know if the scan was successful.
  emit collectionScanCompleted(uuid);

  return success;
}

void QueryManager::insertItemsBatch(
    int legacyId, const QString &uuid, const QStringList &paths,
    const QHash<QString, QDateTime> &timestamps) {
  if (paths.isEmpty()) {
    return;
  }

  // Batch upsert for performance.
  // IMPORTANT: preserve user state fields
  // (play_count/last_played/rating/artwork_path). We only update name +
  // last_modified (and collection_id) when a row already exists.
  QString sql = "INSERT INTO items (collection_id, collection_uuid, "
                "path, name, last_modified) VALUES ";
  QStringList valueSets;
  valueSets.reserve(paths.size());
  for (int i = 0; i < paths.size(); ++i) {
    valueSets.append("(?, ?, ?, ?, ?)");
  }
  sql += valueSets.join(", ");
  sql += " ON CONFLICT(collection_uuid, path) DO UPDATE SET "
         "collection_id=excluded.collection_id, "
         "name=excluded.name, "
         "last_modified=excluded.last_modified";

  QSqlQuery ins(m_db);
  ins.prepare(sql);
  for (const QString &filePath : paths) {
    ins.addBindValue(legacyId);
    ins.addBindValue(uuid);
    ins.addBindValue(filePath);
    ins.addBindValue(QFileInfo(filePath).completeBaseName());
    ins.addBindValue(timestamps.value(filePath).toString(Qt::ISODate));
  }

  if (!ins.exec()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to insert items batch",
                                     "QueryManager::insertItemsBatch")
                   .withDetails(ins.lastError().text());
    ErrorUtils::logError(err);
  }
}

bool QueryManager::ensureScannedItemsTempTable() {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS scanned_items ("
              "path TEXT PRIMARY KEY, "
              "name TEXT, "
              "last_modified TEXT"
              ")")) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to create scanned_items temp table",
                              "QueryManager::ensureScannedItemsTempTable")
            .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

void QueryManager::clearScannedItemsTempTable() {
  if (!m_db.isOpen()) {
    return;
  }
  QSqlQuery q(m_db);
  q.exec("DELETE FROM scanned_items");
}

// ============================================================================
// Query UUIDs temp table - used when UUID count exceeds SQLite variable limit
// ============================================================================


void QueryManager::insertScannedItemsBatch(
    const QStringList &paths, const QHash<QString, QDateTime> &timestamps) {
  if (!m_db.isOpen() || paths.isEmpty()) {
    return;
  }

  // 3 columns per row -> keep under SQLite 999 variable limit.
  QString sql = "INSERT OR REPLACE INTO scanned_items (path, name, "
                "last_modified) VALUES ";
  QStringList valueSets;
  valueSets.reserve(paths.size());
  for (int i = 0; i < paths.size(); ++i) {
    valueSets.append("(?, ?, ?)");
  }
  sql += valueSets.join(", ");

  QSqlQuery ins(m_db);
  ins.prepare(sql);
  for (const QString &p : paths) {
    ins.addBindValue(p);
    ins.addBindValue(QFileInfo(p).completeBaseName());
    ins.addBindValue(timestamps.value(p).toString(Qt::ISODate));
  }
  if (!ins.exec()) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to insert scanned_items batch",
                              "QueryManager::insertScannedItemsBatch")
            .withDetails(ins.lastError().text()));
  }
}

bool QueryManager::applyScannedItemsToDatabase(int legacyId,
                                               const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return false;
  }

  QSqlQuery upsert(m_db);
  upsert.prepare("INSERT INTO items (collection_id, collection_uuid, path, "
                 "name, last_modified) "
                 "SELECT ?, ?, path, name, last_modified FROM scanned_items "
                 "ON CONFLICT(collection_uuid, path) DO UPDATE SET "
                 "collection_id=excluded.collection_id, "
                 "name=excluded.name, "
                 "last_modified=excluded.last_modified");
  upsert.addBindValue(legacyId);
  upsert.addBindValue(collectionUuid);
  if (!upsert.exec()) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to apply scanned_items upsert",
                              "QueryManager::applyScannedItemsToDatabase")
            .withDetails(upsert.lastError().text()));
    return false;
  }
  return true;
}

bool QueryManager::deleteMissingItemsByUuidUsingScannedItems(
    const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  q.prepare("DELETE FROM items WHERE collection_uuid = ? "
            "AND NOT EXISTS (SELECT 1 FROM scanned_items si WHERE si.path = "
            "items.path)");
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    ErrorUtils::logError(
        ErrorContext::warning(
            ErrorCode::DatabaseQueryFailed,
            "Failed to delete missing items using scanned_items",
            "QueryManager::deleteMissingItemsByUuidUsingScannedItems")
            .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

auto QueryManager::prepareCollectionForItemsInsert(
    const CollectionConfig &collection, const QString &uuid,
    const QString &extSignature, int &legacyIdOut) -> bool {
  legacyIdOut = -1;

  // Retry constants for lock handling
  constexpr int MAX_RETRIES = 5;
  constexpr int BASE_DELAY_MS = 100;

  bool prepareSuccess = false;
  for (int attempt = 0; attempt < MAX_RETRIES && !prepareSuccess; ++attempt) {
    if (attempt > 0) {
      // Exponential backoff: 100ms, 200ms, 400ms, 800ms, 1600ms
      QThread::msleep(BASE_DELAY_MS * (1 << (attempt - 1)));
    }

    if (!m_db.transaction()) {
      continue;
    }

    try {
      QSqlQuery update(m_db);
      update.prepare(
          "UPDATE collections SET name=?, ext_signature=? WHERE uuid=?");
      update.addBindValue(collection.name);
      update.addBindValue(extSignature);
      update.addBindValue(uuid);
      update.exec();

      QSqlQuery check(m_db);
      check.prepare("SELECT COUNT(*) FROM collections WHERE uuid=?");
      check.addBindValue(uuid);
      bool exists =
          (check.exec() && check.next() && check.value(0).toInt() > 0);

      if (!exists) {
        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO collections (name, last_scanned, "
                       "ext_signature, uuid) VALUES (?, ?, ?, ?)");
        const QString initialLastScanned =
            QDateTime::fromSecsSinceEpoch(0).toString(Qt::ISODate);
        insert.addBindValue(collection.name);
        insert.addBindValue(initialLastScanned);
        insert.addBindValue(extSignature);
        insert.addBindValue(uuid);
        if (!insert.exec()) {
          throw std::runtime_error(insert.lastError().text().toStdString());
        }
      }

      {
        QSqlQuery idq(m_db);
        idq.prepare("SELECT id FROM collections WHERE uuid = ?");
        idq.addBindValue(uuid);
        if (idq.exec() && idq.next()) {
          legacyIdOut = idq.value(0).toInt();
        }
      }

      if (!m_db.commit()) {
        throw std::runtime_error(m_db.lastError().text().toStdString());
      }
      prepareSuccess = true;
    } catch (const std::exception &e) {
      m_db.rollback();

      QString errorText = QString::fromStdString(e.what());
      bool isLockError = errorText.contains("locked", Qt::CaseInsensitive);

      if (!isLockError || attempt == MAX_RETRIES - 1) {
        auto err = ErrorContext::critical(
                       ErrorCode::DatabaseTransactionFailed,
                       "Failed to prepare collection for items",
                       "QueryManager::prepareCollectionForItemsInsert")
                       .withDetails(errorText);
        ErrorUtils::logError(err);
        emit errorOccurred(err);
        return false;
      }
    }
  }

  return prepareSuccess;
}

