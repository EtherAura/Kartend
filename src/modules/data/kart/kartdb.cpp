#include "kartdb.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

namespace kart {

ErrorUtils::Result<QSqlDatabase> openMediaDbConnection(const QString &connectionName) {
  const QString dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!QDir().mkpath(dbPath)) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseConnectionFailed,
                                           "Cannot create app data directory",
                                           "kart::openMediaDbConnection")
        .withDetails(dbPath);
  }
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
  db.setDatabaseName(dbPath + "/media.db");
  if (!db.open()) {
    return ErrorUtils::ErrorContext::error(ErrorUtils::ErrorCode::DatabaseConnectionFailed,
                                           "Cannot open Kart media database",
                                           "kart::openMediaDbConnection")
        .withDetails(db.lastError().text());
  }
  QSqlQuery pragma(db);
  pragma.exec("PRAGMA foreign_keys = ON");
  return db;
}

void closeMediaDbConnection(QSqlDatabase &db) {
  if (!db.isValid()) return;
  const QString name = db.connectionName();
  db.close();
  db = QSqlDatabase();
  QSqlDatabase::removeDatabase(name);
}

} // namespace kart
