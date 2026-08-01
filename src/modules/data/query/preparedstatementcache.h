#ifndef PREPAREDSTATEMENTCACHE_H
#define PREPAREDSTATEMENTCACHE_H

#include <QCache>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

/// LRU cache of prepared QSqlQuery statements bound to a worker thread's
/// QSqlDatabase connection. Reuses compiled statements across slot
/// invocations to skip the prepare() round-trip on every call. A cache hit
/// runs finish() on the entry — releasing the previous use's cursor — and
/// returns the still-prepared statement for rebinding (Kartend-de4ft: the
/// previous version re-created and re-prepared on every hit, so a hit cost
/// the same as a miss and the cache cached nothing).
///
/// CONTRACT: bind cached statements positionally (bindValue(0, …)) or by
/// name — never addBindValue(). QSqlQuery::exec() resets the append counter
/// at entry, so addBindValue is only correct when the statement's previous
/// user reached exec(); a caller that binds and bails out early leaves the
/// counter dirty, the next addBindValue round appends instead of
/// overwriting, and SQLite reports "Parameter count mismatch". Positional
/// binds overwrite by index and are immune to a dirty counter.
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

  /// finish() every cached statement without discarding the compiled
  /// statements. An exec'd-but-unfinished SELECT keeps the connection's
  /// implicit read transaction open, which pins the WAL read snapshot —
  /// QueryManager::refreshWalView() calls this so its snapshot refresh
  /// actually observes later commits instead of no-op'ing inside a
  /// still-open read transaction.
  void finishAll();

private:
  QSqlDatabase m_db;
  QCache<QString, QSqlQuery> m_cache;
};

#endif // PREPAREDSTATEMENTCACHE_H
