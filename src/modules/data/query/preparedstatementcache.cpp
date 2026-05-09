#include "preparedstatementcache.h"

PreparedStatementCache::PreparedStatementCache(int maxSize) {
  m_cache.setMaxCost(maxSize);
}

void PreparedStatementCache::setDatabase(const QSqlDatabase &db) {
  m_db = db;
  m_cache.clear();
}

QSqlQuery &PreparedStatementCache::get(const QString &sql) {
  if (QSqlQuery *cached = m_cache.object(sql)) {
    // Fully reset before reuse. QSqlQuery may retain bound values /
    // internal state across exec() calls. If the cached instance is reused
    // for dynamic search SQL, SQLite can report "Parameter count mismatch"
    // unless we reinitialize it.
    cached->finish();
    *cached = QSqlQuery(m_db);
    cached->prepare(sql);
    return *cached;
  }

  // Create new prepared statement and cache it (QCache takes ownership).
  // Cost (1) is well below maxCost so the freshly-inserted entry is
  // guaranteed to land in the cache rather than being deleted on insert.
  // Reading the value back through the cache lookup keeps the lifetime
  // contract explicit (and quiets the clang-analyzer-cplusplus.NewDelete
  // false positive on `*query` after an insert that could *theoretically*
  // evict the just-inserted item).
  auto *query = new QSqlQuery(m_db);
  query->prepare(sql);
  m_cache.insert(sql, query, 1);
  return *m_cache.object(sql);
}

void PreparedStatementCache::clear() {
  m_cache.clear();
}
