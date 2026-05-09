#ifndef PREPAREDSTATEMENTCACHE_H
#define PREPAREDSTATEMENTCACHE_H

#include <QCache>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

/// LRU cache of prepared QSqlQuery statements bound to a worker thread's
/// QSqlDatabase connection. Reuses compiled statements across slot
/// invocations to skip the prepare() round-trip on every call. Each return
/// from get() runs finish() + reassignment + prepare() on the cached entry
/// so stale bound values from a prior exec() can't leak into a fresh call —
/// SQLite reports "Parameter count mismatch" otherwise when dynamic search
/// SQL reuses a slot that previously bound a different number of values.
///
/// Not thread-safe: the cache is meant to live alongside the QSqlDatabase
/// connection on one worker thread and never to be shared.
class PreparedStatementCache {
public:
  explicit PreparedStatementCache(int maxSize = 32);

  /// Bind the cache to the active database connection. Calling this with a
  /// new connection clears the cache so previously-prepared statements
  /// don't survive a reconnect.
  void setDatabase(const QSqlDatabase &db);

  /// Get-or-create a prepared statement for @p sql. The returned reference
  /// is owned by the cache; the caller binds values and calls exec(),
  /// then never deletes.
  [[nodiscard]] QSqlQuery &get(const QString &sql);

  /// Drop every cached statement. Call after reconnect or when the
  /// connection's underlying schema changes.
  void clear();

private:
  QSqlDatabase m_db;
  QCache<QString, QSqlQuery> m_cache;
};

#endif // PREPAREDSTATEMENTCACHE_H
