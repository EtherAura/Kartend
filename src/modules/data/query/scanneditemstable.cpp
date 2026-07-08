// scanned_items TEMP-table staging operations — extracted from QueryManager.
// See scanneditemstable.h.
#include "scanneditemstable.h"

#include "errorutils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

bool ScannedItemsTable::ensure() {
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
                                               "ScannedItemsTable::ensure")
                             .withDetails(q.lastError().text()));
    return false;
  }
  return true;
}

void ScannedItemsTable::clear() {
  if (!m_db.isOpen()) {
    return;
  }
  QSqlQuery q(m_db);
  if (!q.exec("DELETE FROM scanned_items")) {
    // A silent failure here lets insertBatch() upsert on top of stale rows —
    // the rescan then commits duplicates / orphans / a partial merge.
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to clear scanned_items staging table",
                                               "ScannedItemsTable::clear")
                             .withDetails(q.lastError().text()));
  }
}

bool ScannedItemsTable::insertBatch(const QStringList &paths,
                                    const QHash<QString, QDateTime> &timestamps,
                                    const QString &mediaRoot) {
  if (!m_db.isOpen()) {
    return false;
  }
  if (paths.isEmpty()) {
    return true; // empty batch is a no-op success
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
    QString stagedName = QFileInfo(p).completeBaseName();
    if (stagedName.isEmpty()) {
      // Dotfiles (e.g. ".gitignore") have an empty completeBaseName; fall back
      // to the full file name so items.name (sort key / FTS) isn't blank.
      stagedName = QFileInfo(p).fileName();
    }
    ins.addBindValue(stagedName);
    ins.addBindValue(timestamps.value(p).toString(Qt::ISODate));
    ins.addBindValue(absoluteInfo.exists() ? absoluteInfo.size() : 0);
  }
  if (!ins.exec()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                               "Failed to insert scanned_items batch",
                                               "ScannedItemsTable::insertBatch")
                             .withDetails(ins.lastError().text()));
    return false;
  }
  return true;
}

bool ScannedItemsTable::applyToItems(int legacyId, const QString &collectionUuid,
                                     QString *errorDetailsOut) {
  if (!m_db.isOpen()) {
    if (errorDetailsOut) {
      *errorDetailsOut = QStringLiteral("database connection not open");
    }
    return false;
  }

  QSqlQuery upsert(m_db);
  // The trailing "WHERE true" disambiguates the grammar: without it SQLite
  // parses "FROM scanned_items ON CONFLICT ..." as the start of a JOIN ... ON
  // and rejects the upsert with a "near DO: syntax error". This is the
  // documented workaround for an INSERT ... SELECT ... ON CONFLICT statement.
  //
  // date_added: stamped at insert time and intentionally OMITTED from the
  // ON CONFLICT update clause (rescans must not reset it) — mirrors the
  // streaming pipeline in scanservice_persist.cpp. It was missing here
  // entirely (Kartend-fux2w), so rows persisted through this legacy path
  // took DEFAULT 0 and never matched ByDateAdded smart playlists (the
  // evaluator treats date_added <= 0 as "unknown" and excludes the row).
  if (!upsert.prepare(
          "INSERT INTO items (collection_id, collection_uuid, path, "
          "rel_path, name, last_modified, file_size, date_added) "
          "SELECT ?, ?, path, rel_path, name, last_modified, file_size, ? FROM scanned_items "
          "WHERE true "
          "ON CONFLICT(collection_uuid, path) DO UPDATE SET "
          "collection_id=excluded.collection_id, "
          "rel_path=excluded.rel_path, "
          "name=excluded.name, "
          "last_modified=excluded.last_modified, "
          "file_size=excluded.file_size")) {
    if (errorDetailsOut) {
      *errorDetailsOut = upsert.lastError().text();
    } else {
      ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                                 "Failed to prepare scanned_items upsert",
                                                 "ScannedItemsTable::applyToItems")
                               .withDetails(upsert.lastError().text()));
    }
    return false;
  }
  upsert.addBindValue(legacyId);
  upsert.addBindValue(collectionUuid);
  upsert.addBindValue(QDateTime::currentSecsSinceEpoch());
  if (!upsert.exec()) {
    if (errorDetailsOut) {
      // Caller owns reporting (it classifies lock contention for its retry
      // ladder) — logging here too would double up.
      *errorDetailsOut = upsert.lastError().text();
    } else {
      ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                                 "Failed to apply scanned_items upsert",
                                                 "ScannedItemsTable::applyToItems")
                               .withDetails(upsert.lastError().text()));
    }
    return false;
  }
  return true;
}

bool ScannedItemsTable::deleteItemsMissingFromScan(const QString &collectionUuid,
                                                   QString *errorDetailsOut) {
  if (!m_db.isOpen()) {
    if (errorDetailsOut) {
      *errorDetailsOut = QStringLiteral("database connection not open");
    }
    return false;
  }
  QSqlQuery q(m_db);
  q.prepare("DELETE FROM items WHERE collection_uuid = ? "
            "AND NOT EXISTS (SELECT 1 FROM scanned_items si WHERE si.path = "
            "items.path)");
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    if (errorDetailsOut) {
      // Caller owns reporting (it classifies lock contention for its retry
      // ladder, Kartend-kt39d) — logging here too would double up.
      *errorDetailsOut = q.lastError().text();
    } else {
      ErrorUtils::logError(
          ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                "Failed to delete missing items using scanned_items",
                                "ScannedItemsTable::deleteItemsMissingFromScan")
              .withDetails(q.lastError().text()));
    }
    return false;
  }
  return true;
}
