#include "filehashcache.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace FileHashCache {

std::optional<Entry> lookup(QSqlDatabase &db, const QString &path, qint64 currentSize,
                            qint64 currentMtimeMs) {
  if (!db.isOpen() || path.isEmpty()) {
    return std::nullopt;
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT file_size, mtime_unix_ms, crc, md5, sha1 "
                           "FROM file_hash_cache WHERE path = ?"));
  q.addBindValue(path);
  if (!q.exec() || !q.next()) {
    return std::nullopt;
  }
  const qint64 cachedSize = q.value(0).toLongLong();
  const qint64 cachedMtime = q.value(1).toLongLong();
  // Stale: the file changed since we hashed it. Report a miss so the caller
  // re-hashes and replaces the row.
  if (cachedSize != currentSize || cachedMtime != currentMtimeMs) {
    return std::nullopt;
  }
  Entry e;
  e.size = cachedSize;
  e.crc = q.value(2).toString();
  e.md5 = q.value(3).toString();
  e.sha1 = q.value(4).toString();
  return e;
}

ErrorUtils::Result<bool> store(QSqlDatabase &db, const QString &path, qint64 size, qint64 mtimeMs,
                               const QString &crc, const QString &md5, const QString &sha1) {
  if (!db.isOpen()) {
    return ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                 "FileHashCache::store");
  }
  if (path.isEmpty()) {
    return ErrorContext::warning(ErrorCode::InvalidArgument, "Empty path", "FileHashCache::store");
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral("INSERT OR REPLACE INTO file_hash_cache "
                           "(path, file_size, mtime_unix_ms, crc, md5, sha1, computed_at_unix_ms) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?)"));
  q.addBindValue(path);
  q.addBindValue(size);
  q.addBindValue(mtimeMs);
  // Empty hashes go in as NULL — the right "no value" sentinel for a read
  // failure on one digest kind, mirroring DatCache.
  q.addBindValue(crc.isEmpty() ? QVariant() : QVariant(crc));
  q.addBindValue(md5.isEmpty() ? QVariant() : QVariant(md5));
  q.addBindValue(sha1.isEmpty() ? QVariant() : QVariant(sha1));
  q.addBindValue(QDateTime::currentMSecsSinceEpoch());
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to store file hash",
                               "FileHashCache::store")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<RomHasher::Result>
hashFileCached(QSqlDatabase &db, const QString &path,
               const std::shared_ptr<std::atomic<bool>> &cancelToken) {
  const QFileInfo fi(path);
  if (!fi.isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "File does not exist",
                               "FileHashCache::hashFileCached")
        .withDetails(path);
  }
  // Canonical path keys the cache so relative / symlinked spellings of the
  // same file share one row (mirrors DatCache's keying).
  const QString key = fi.canonicalFilePath();
  const qint64 size = fi.size();
  const qint64 mtimeMs = fi.lastModified().toMSecsSinceEpoch();

  if (auto hit = lookup(db, key, size, mtimeMs)) {
    RomHasher::Result r;
    r.crc = hit->crc;
    r.md5 = hit->md5;
    r.sha1 = hit->sha1;
    r.size = hit->size;
    return r;
  }

  auto hashed = RomHasher::hashFile(path, cancelToken);
  if (hashed.isError()) {
    return hashed.error();
  }
  const RomHasher::Result r = hashed.value();
  // Best-effort persist: the hash already succeeded, so a cache write failure
  // is logged and swallowed rather than failing the operation.
  auto saved = store(db, key, r.size, mtimeMs, r.crc, r.md5, r.sha1);
  if (saved.isError()) {
    ErrorUtils::logError(saved.error());
  }
  return r;
}

void clearAll(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return;
  }
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("DELETE FROM file_hash_cache"))) {
    qWarning("FileHashCache: clearAll failed: %s", qPrintable(q.lastError().text()));
  }
}

} // namespace FileHashCache
