#ifndef KARTDB_H
#define KARTDB_H

#include <QSqlDatabase>
#include <QString>

#include "errorutils.h"

namespace kart {

[[nodiscard]] ErrorUtils::Result<QSqlDatabase> openMediaDbConnection(const QString &connectionName);

void closeMediaDbConnection(QSqlDatabase &db);

} // namespace kart

#endif
