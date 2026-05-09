#ifndef DBMIGRATIONS_H
#define DBMIGRATIONS_H

#include <QSqlDatabase>
#include <QString>

namespace DbMigrations {

// Applies in-place schema migrations using SQLite PRAGMA user_version.
// The operation is idempotent and safe to call on every startup.
//
// `origin` is used for structured ErrorContext logging.
void applySchemaMigrations(QSqlDatabase &db, const QString &origin);

} // namespace DbMigrations

#endif // DBMIGRATIONS_H
