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
    // Release the previous use's cursor (a cheap sqlite3_reset) and hand the
    // still-prepared statement back for rebinding. Deliberately NOT the old
    // reassign-and-prepare(), which made every hit cost the same as a miss
    // (Kartend-de4ft). The "Parameter count mismatch" that re-preparing
    // papered over is addBindValue's append semantics on a dirty counter —
    // see the class-level CONTRACT: cached statements are bound positionally
    // at every call site, which overwrites instead of appending.
    cached->finish();
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

void PreparedStatementCache::finishAll() {
  const QList<QString> keys = m_cache.keys();
  for (const QString &key : keys) {
    if (QSqlQuery *cached = m_cache.object(key)) {
      cached->finish();
    }
  }
}
