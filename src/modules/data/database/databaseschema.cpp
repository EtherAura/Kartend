#include "databaseschema.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "dbmigrations.h"
#include "errorutils.h"
#include "uiconstants/database.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatabaseSchema {

bool openConnection(QSqlDatabase &db, const QString &dbPath) {
  if (!QDir().mkpath(dbPath)) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to create database directory",
                                      "DatabaseSchema::openConnection")
                   .withDetails(QString("Path: %1").arg(dbPath));
    ErrorUtils::logError(err);
    return false;
  }
  db.setDatabaseName(dbPath + "/media.db");
  if (!db.open()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to open database", "DatabaseSchema::openConnection")
                   .withDetails(db.lastError().text());
    ErrorUtils::logError(err);
    return false;
  }
  return true;
}

void applyConnectionPragmas(QSqlDatabase &db) {
  QSqlQuery query(db);
  if (!query.exec("PRAGMA foreign_keys = ON")) {
    auto err =
        ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to enable foreign keys",
                              "DatabaseSchema::applyConnectionPragmas")
            .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
  if (!query.exec("PRAGMA journal_mode = WAL")) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                     "Failed to enable WAL mode, falling back to DELETE mode",
                                     "DatabaseSchema::applyConnectionPragmas")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
    if (!query.exec("PRAGMA journal_mode = DELETE")) {
      auto fallbackErr =
          ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to set DELETE journal mode",
                                "DatabaseSchema::applyConnectionPragmas")
              .withDetails(query.lastError().text());
      ErrorUtils::logError(fallbackErr);
    }
  }
  const QString busyTimeoutPragma =
      QStringLiteral("PRAGMA busy_timeout = %1").arg(UIConstants::Database::MAIN_BUSY_TIMEOUT_MS);
  if (!query.exec(busyTimeoutPragma)) {
    auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to set busy timeout",
                                     "DatabaseSchema::applyConnectionPragmas")
                   .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }
}

void createTables(QSqlDatabase &db) {
  QSqlQuery query(db);

  const QString collectionsTable = "CREATE TABLE IF NOT EXISTS collections ("
                                   "id INTEGER PRIMARY KEY, "
                                   "name TEXT NOT NULL, "
                                   "last_scanned TEXT NOT NULL, "
                                   "ext_signature TEXT DEFAULT '', "
                                   "uuid TEXT DEFAULT ''"
                                   ")";
  if (!query.exec(collectionsTable)) {
    auto err =
        ErrorContext::critical(ErrorCode::DatabaseQueryFailed, "Failed to create collections table",
                               "DatabaseSchema::createTables")
            .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  const QString itemsTable =
      "CREATE TABLE IF NOT EXISTS items ("
      "id INTEGER PRIMARY KEY AUTOINCREMENT, "
      "collection_id INTEGER NOT NULL, "
      "path TEXT NOT NULL, "
      "name TEXT NOT NULL, "
      "artwork_path TEXT, "
      "last_modified TEXT NOT NULL, "
      "file_size INTEGER DEFAULT 0, "
      "play_count INTEGER DEFAULT 0, "
      "last_played TEXT, "
      "rating INTEGER DEFAULT 0, "
      "collection_uuid TEXT DEFAULT '', "
      "UNIQUE(collection_id, path), "
      "FOREIGN KEY(collection_id) REFERENCES collections(id) ON DELETE CASCADE"
      ")";
  if (!query.exec(itemsTable)) {
    auto err =
        ErrorContext::critical(ErrorCode::DatabaseQueryFailed, "Failed to create items table",
                               "DatabaseSchema::createTables")
            .withDetails(query.lastError().text());
    ErrorUtils::logError(err);
  }

  DbMigrations::applySchemaMigrations(db, QStringLiteral("DatabaseSchema::createTables"));
}

void createIndexes(QSqlDatabase &db) {
  QSqlQuery idx(db);

  // Each index is independently optional from SQLite's POV but load-bearing
  // for query planner performance — fetchItemsRange and the UUID-based
  // lookups collapse to full table scans if any of these silently fails to
  // create (disk full, locked DB, EROFS on the data dir).
  const auto runIndex = [&idx](const char *sql) {
    idx.prepare(QString::fromUtf8(sql));
    if (!idx.exec()) {
      auto err =
          ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to create database index",
                                "DatabaseSchema::createIndexes")
              .withDetails(QStringLiteral("Statement: %1; Error: %2")
                               .arg(QString::fromUtf8(sql), idx.lastError().text()));
      ErrorUtils::logError(err);
    }
  };

  runIndex("CREATE INDEX IF NOT EXISTS idx_collections_uuid ON collections(uuid)");
  runIndex("CREATE INDEX IF NOT EXISTS idx_items_collection_uuid ON items(collection_uuid)");
  runIndex(
      "CREATE UNIQUE INDEX IF NOT EXISTS uniq_items_uuid_path ON items(collection_uuid, path)");
  runIndex("CREATE INDEX IF NOT EXISTS idx_items_name ON items(name COLLATE NOCASE)");
  runIndex("CREATE INDEX IF NOT EXISTS idx_items_uuid_name ON "
           "items(collection_uuid, name COLLATE NOCASE)");
  runIndex("CREATE INDEX IF NOT EXISTS idx_items_covering ON "
           "items(collection_uuid, name COLLATE NOCASE, path)");
}

} // namespace DatabaseSchema
