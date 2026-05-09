#ifndef DATABASESCHEMA_H
#define DATABASESCHEMA_H

#include <QString>

class QSqlDatabase;

namespace DatabaseSchema {

bool openConnection(QSqlDatabase &db, const QString &dbPath);

void applyConnectionPragmas(QSqlDatabase &db);

void createTables(QSqlDatabase &db);

void createIndexes(QSqlDatabase &db);

} // namespace DatabaseSchema

#endif
