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

#include <atomic>
#include <memory>
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
#include <stdexcept>

#include "collectionutils.h"
#include "errorutils.h"
#include "pathutils.h"
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

  const QString uuid =
      CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);

  // A scan that already failed this session is not retried automatically.
  // The failure is deterministic (schema/IO/bind-limit) and rolls back without
  // persisting dir_signature, so needsRescan() stays true. Re-running it on
  // every collectionScanCompleted-driven reload spins an unbreakable
  // scan->fail->reload loop. Return without emitting scanStarting/
  // collectionScanCompleted: emitting them would re-trigger the reload and
  // keep the loop alive. The collection is left untouched until the next
  // session (a fresh QueryManager starts with an empty set) or until the
  // user changes the media directory (which yields a different UUID).
  if (m_failedScanUuids.contains(uuid)) {
    return false;
  }

  if (!needsRescan(collectionIndex, collection)) {
    return false;
  }

  // Reset cancellation flag before starting new scan
  resetScanCancellation();

  // Notify UI that a scan is starting (estimated items unknown, use -1)
  emit scanStarting(collection.name, -1);

  // Stream scan results directly into DB inserts to reduce peak memory.
  // capture scan stats so we can emit the summary signal used
  // by the settings dialog's "X of Y items added" confirmation.
  int itemsScanned = 0;
  int itemsApplied = 0;
  const bool success =
      scanAndSaveItemsToDatabase(collectionIndex, collection, &itemsScanned, &itemsApplied);

  // Remember a failed scan so the next reload-driven pass skips it instead of
  // retrying forever (see the m_failedScanUuids guard above).
  if (!success) {
    m_failedScanUuids.insert(uuid);
  }

  // Always emit collectionScanCompleted when we emitted scanStarting, even if
  // the scan failed. This ensures MainWindow's m_activeScanCount is decremented
  // properly and the overlay is hidden. The caller can still check the return
  // value to know if the scan was successful.
  emit collectionScanCompleted(uuid);
  emit collectionScanSummary(uuid, itemsScanned, itemsApplied, success);

  return success;
}

void QueryManager::insertItemsBatch(int legacyId, const QString &uuid, const QStringList &paths,
                                    const QHash<QString, QDateTime> &timestamps,
                                    const QString &mediaRoot) {
  if (paths.isEmpty()) {
    return;
  }

  // Batch upsert for performance.
  // IMPORTANT: preserve user state fields
  // (play_count/last_played/rating/artwork_path). We only update name +
  // last_modified (and collection_id) when a row already exists.
  // path stores the ABSOLUTE filesystem path; rel_path stores the
  // media-dir-relative form (see dbmigrations.cpp v13).
  QString sql = "INSERT INTO items (collection_id, collection_uuid, "
                "path, rel_path, name, last_modified, file_size) VALUES ";
  QStringList valueSets;
  valueSets.reserve(paths.size());
  for (int i = 0; i < paths.size(); ++i) {
    valueSets.append("(?, ?, ?, ?, ?, ?, ?)");
  }
  sql += valueSets.join(", ");
  sql += " ON CONFLICT(collection_uuid, path) DO UPDATE SET "
         "collection_id=excluded.collection_id, "
         "rel_path=excluded.rel_path, "
         "name=excluded.name, "
         "last_modified=excluded.last_modified, "
         "file_size=excluded.file_size";

  QSqlQuery ins(m_db);
  ins.prepare(sql);
  const QDir mediaDir(mediaRoot);
  for (const QString &filePath : paths) {
    const QString absolutePath =
        QDir::isAbsolutePath(filePath) ? filePath : mediaDir.absoluteFilePath(filePath);
    const QFileInfo info(absolutePath);
    ins.addBindValue(legacyId);
    ins.addBindValue(uuid);
    ins.addBindValue(absolutePath);
    ins.addBindValue(filePath);
    ins.addBindValue(QFileInfo(filePath).completeBaseName());
    ins.addBindValue(timestamps.value(filePath).toString(Qt::ISODate));
    ins.addBindValue(info.exists() ? info.size() : 0);
  }

  if (!ins.exec()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to insert items batch",
                                     "QueryManager::insertItemsBatch")
                   .withDetails(ins.lastError().text());
    ErrorUtils::logError(err);
  }
}

void QueryManager::maybeAbsolutizeItemPaths(const QList<CollectionConfig> &allCollections) {
  if (!m_db.isOpen()) {
    return;
  }

  // Gate: the v13 reconcile only needs to run once. Seed the flag at '0' if
  // it doesn't exist yet, then bail immediately if a prior run completed.
  {
    QSqlQuery seed(m_db);
    if (!seed.exec("INSERT OR IGNORE INTO meta(key, value) "
                   "VALUES('items_paths_absolutized', '0')")) {
      // meta table may not exist on a very old DB that never reached the v3
      // migration; nothing to reconcile in that case either.
      return;
    }
  }
  {
    QSqlQuery check(m_db);
    if (check.exec("SELECT value FROM meta WHERE key = 'items_paths_absolutized'") &&
        check.next() && check.value(0).toString() == QStringLiteral("1")) {
      return;
    }
  }

  if (!m_db.transaction()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseTransactionFailed,
                                     "Failed to start transaction to absolutize item paths",
                                     "QueryManager::maybeAbsolutizeItemPaths")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    return;
  }

  bool allCollectionsProcessed = true;
  bool reconcileFailed = false;

  for (const CollectionConfig &cfg : allCollections) {
    // Playlists are virtual collections — they own no items rows of their
    // own, so there is nothing to rewrite (and no media directory to use).
    if (cfg.isPlaylist) {
      continue;
    }

    // Compute the expanded media dir + uuid IDENTICALLY to every other call
    // site (loadAllCollections, computeCollectionUuid, the scanner).
    const QString expandedMediaDir = PathUtils::validateAndExpandPath(cfg.mediaDirectory, cfg.name);
    if (expandedMediaDir.trimmed().isEmpty()) {
      // Media directory unresolved (e.g. an offline mount). Skip this
      // collection and remember the run is incomplete so the next startup
      // retries it.
      allCollectionsProcessed = false;
      continue;
    }
    const QString uuid = CollectionUtils::computeCollectionUuid(cfg.name, expandedMediaDir);
    if (uuid.isEmpty()) {
      allCollectionsProcessed = false;
      continue;
    }

    QSqlQuery rows(m_db);
    rows.prepare("SELECT id, path, rel_path FROM items WHERE collection_uuid = ?");
    rows.addBindValue(uuid);
    if (!rows.exec()) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to read items for path absolutization",
                                       "QueryManager::maybeAbsolutizeItemPaths")
                     .withDetails(rows.lastError().text());
      ErrorUtils::logError(err);
      reconcileFailed = true;
      break;
    }

    const QDir mediaDir(expandedMediaDir);

    struct PendingUpdate {
      qint64 id = 0;
      QString path;
      QString relPath;
    };
    QList<PendingUpdate> updates;
    while (rows.next()) {
      const qint64 id = rows.value(0).toLongLong();
      const QString storedPath = rows.value(1).toString();
      const QVariant relPathValue = rows.value(2);

      if (!QDir::isAbsolutePath(storedPath)) {
        // Pre-v13 row: path is relative. Rewrite to absolute and stash the
        // original relative form into rel_path.
        updates.append({id, mediaDir.absoluteFilePath(storedPath), storedPath});
      } else if (relPathValue.isNull() || relPathValue.toString().isEmpty()) {
        // Path is already absolute (written by a post-v13 scan, or a prior
        // partial reconcile) but rel_path was never populated — backfill it.
        updates.append({id, storedPath, mediaDir.relativeFilePath(storedPath)});
      }
      // else: already fully migrated, nothing to do.
    }

    QSqlQuery update(m_db);
    update.prepare("UPDATE items SET path = ?, rel_path = ? WHERE id = ?");
    for (const PendingUpdate &u : updates) {
      update.bindValue(0, u.path);
      update.bindValue(1, u.relPath);
      update.bindValue(2, u.id);
      if (!update.exec()) {
        auto err =
            ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to absolutize item path",
                                  "QueryManager::maybeAbsolutizeItemPaths")
                .withDetails(update.lastError().text());
        ErrorUtils::logError(err);
        reconcileFailed = true;
        break;
      }
    }
    if (reconcileFailed) {
      break;
    }
  }

  if (reconcileFailed) {
    m_db.rollback();
    return;
  }

  // Only flip the flag to '1' when EVERY non-playlist collection was
  // processed. If any was skipped (offline mount), leave it '0' so the next
  // startup retries — the per-row guards above make the retry idempotent.
  if (allCollectionsProcessed) {
    QSqlQuery mark(m_db);
    mark.prepare("UPDATE meta SET value = '1' WHERE key = 'items_paths_absolutized'");
    if (!mark.exec()) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to mark item paths as absolutized",
                                       "QueryManager::maybeAbsolutizeItemPaths")
                     .withDetails(mark.lastError().text());
      ErrorUtils::logError(err);
      m_db.rollback();
      return;
    }
  }

  if (!m_db.commit()) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseTransactionFailed,
                                     "Failed to commit item path absolutization",
                                     "QueryManager::maybeAbsolutizeItemPaths")
                   .withDetails(m_db.lastError().text());
    ErrorUtils::logError(err);
    m_db.rollback();
  }
}

bool QueryManager::ensureScannedItemsTempTable() {
  if (!m_db.isOpen()) {
    return false;
  }
  QSqlQuery q(m_db);
  // path holds the ABSOLUTE filesystem path (the PK / upsert key); rel_path
  // holds the media-dir-relative form that subfolder / virtual-folder queries
  // need. See dbmigrations.cpp v13 for the items-table counterpart.
  if (!q.exec("CREATE TEMP TABLE IF NOT EXISTS scanned_items ("
              "path TEXT PRIMARY KEY, "
              "rel_path TEXT, "
              "name TEXT, "
              "last_modified TEXT, "
              "file_size INTEGER DEFAULT 0"
              ")")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
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

void QueryManager::insertScannedItemsBatch(const QStringList &paths,
                                           const QHash<QString, QDateTime> &timestamps,
                                           const QString &mediaRoot) {
  if (!m_db.isOpen() || paths.isEmpty()) {
    return;
  }

  // 5 columns per row -> keep under SQLite 999 variable limit.
  // path stores the ABSOLUTE filesystem path (the upsert key shared with
  // items.path); rel_path stores the original media-dir-relative path that
  // subfolder / virtual-folder queries still need.
  QString sql = "INSERT OR REPLACE INTO scanned_items (path, rel_path, name, "
                "last_modified, file_size) VALUES ";
  QStringList valueSets;
  valueSets.reserve(paths.size());
  for (int i = 0; i < paths.size(); ++i) {
    valueSets.append("(?, ?, ?, ?, ?)");
  }
  sql += valueSets.join(", ");

  QSqlQuery ins(m_db);
  ins.prepare(sql);
  const QDir mediaDir(mediaRoot);
  for (const QString &p : paths) {
    const QString absolutePath = QDir::isAbsolutePath(p) ? p : mediaDir.absoluteFilePath(p);
    const QFileInfo absoluteInfo(absolutePath);
    ins.addBindValue(absolutePath);
    ins.addBindValue(p);
    ins.addBindValue(QFileInfo(p).completeBaseName());
    ins.addBindValue(timestamps.value(p).toString(Qt::ISODate));
    ins.addBindValue(absoluteInfo.exists() ? absoluteInfo.size() : 0);
  }
  if (!ins.exec()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to insert scanned_items batch",
                                               "QueryManager::insertScannedItemsBatch")
                             .withDetails(ins.lastError().text()));
  }
}

bool QueryManager::applyScannedItemsToDatabase(int legacyId, const QString &collectionUuid) {
  if (!m_db.isOpen()) {
    return false;
  }

  QSqlQuery upsert(m_db);
  // The trailing "WHERE true" disambiguates the grammar: without it SQLite
  // parses "FROM scanned_items ON CONFLICT ..." as the start of a JOIN ... ON
  // and rejects the upsert with a "near DO: syntax error". This is the
  // documented workaround for an INSERT ... SELECT ... ON CONFLICT statement.
  if (!upsert.prepare(
          "INSERT INTO items (collection_id, collection_uuid, path, "
          "rel_path, name, last_modified, file_size) "
          "SELECT ?, ?, path, rel_path, name, last_modified, file_size FROM scanned_items "
          "WHERE true "
          "ON CONFLICT(collection_uuid, path) DO UPDATE SET "
          "collection_id=excluded.collection_id, "
          "rel_path=excluded.rel_path, "
          "name=excluded.name, "
          "last_modified=excluded.last_modified, "
          "file_size=excluded.file_size")) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to prepare scanned_items upsert",
                                               "QueryManager::applyScannedItemsToDatabase")
                             .withDetails(upsert.lastError().text()));
    return false;
  }
  upsert.addBindValue(legacyId);
  upsert.addBindValue(collectionUuid);
  if (!upsert.exec()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to apply scanned_items upsert",
                                               "QueryManager::applyScannedItemsToDatabase")
                             .withDetails(upsert.lastError().text()));
    return false;
  }
  return true;
}

bool QueryManager::deleteMissingItemsByUuidUsingScannedItems(const QString &collectionUuid) {
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
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                              "Failed to delete missing items using scanned_items",
                              "QueryManager::deleteMissingItemsByUuidUsingScannedItems")
            .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

auto QueryManager::prepareCollectionForItemsInsert(const CollectionConfig &collection,
                                                   const QString &uuid, const QString &extSignature,
                                                   int &legacyIdOut) -> bool {
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
      update.prepare("UPDATE collections SET name=?, ext_signature=? WHERE uuid=?");
      update.addBindValue(collection.name);
      update.addBindValue(extSignature);
      update.addBindValue(uuid);
      update.exec();

      QSqlQuery check(m_db);
      check.prepare("SELECT COUNT(*) FROM collections WHERE uuid=?");
      check.addBindValue(uuid);
      bool exists = (check.exec() && check.next() && check.value(0).toInt() > 0);

      if (!exists) {
        QSqlQuery insert(m_db);
        insert.prepare("INSERT INTO collections (name, last_scanned, "
                       "ext_signature, uuid) VALUES (?, ?, ?, ?)");
        const QString initialLastScanned = QDateTime::fromSecsSinceEpoch(0).toString(Qt::ISODate);
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
        auto err = ErrorContext::critical(ErrorCode::DatabaseTransactionFailed,
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
