#include "datlibrarystate.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatLibraryState {

ErrorUtils::Result<QSet<QString>> loadDismissalKeys(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatLibraryState::loadDismissalKeys");
  }
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("SELECT path, mtime_unix_ms FROM dat_library_dismissal"))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load dismissals",
                               "DatLibraryState::loadDismissalKeys")
        .withDetails(q.lastError().text());
  }
  QSet<QString> keys;
  while (q.next()) {
    keys.insert(dismissalKey(q.value(0).toString(), q.value(1).toLongLong()));
  }
  return keys;
}

ErrorUtils::Result<bool> addDismissal(QSqlDatabase &db, const QString &canonicalPath,
                                      qint64 mtimeMs) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatLibraryState::addDismissal");
  }
  if (canonicalPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty path",
                               "DatLibraryState::addDismissal");
  }
  QSqlQuery q(db);
  // OR REPLACE: re-dismissing an updated DAT moves its mtime forward, which
  // is exactly the intended "ask once per catalogue revision" semantic.
  q.prepare(QStringLiteral("INSERT OR REPLACE INTO dat_library_dismissal "
                           "(path, mtime_unix_ms, dismissed_at_unix_ms) VALUES (?, ?, ?)"));
  q.addBindValue(canonicalPath);
  q.addBindValue(mtimeMs);
  q.addBindValue(QDateTime::currentMSecsSinceEpoch());
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to record dismissal",
                               "DatLibraryState::addDismissal")
        .withDetails(q.lastError().text());
  }
  return true;
}

} // namespace DatLibraryState
