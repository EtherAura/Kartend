#ifndef FILEHASHCACHE_H
#define FILEHASHCACHE_H

#include <atomic>
#include <memory>
#include <optional>

#include <QString>

#include "errorutils.h"
#include "romhasher.h"

class QSqlDatabase;

/// Content-hash cache (CRC32 / MD5 / SHA-1) for files on disk, keyed by
/// absolute path and validated against `(file_size, mtime)`. Schema v16
/// (`file_hash_cache`, see dbmigrations.cpp).
///
/// Hashing a multi-GB disc image is the single most expensive step in both
/// DAT auditing and the scraper's hash-based ROM identification. This cache
/// lets either path skip the re-hash when a file's size and mtime are
/// unchanged since the last pass — the standard `(path, size, mtime)`
/// invalidation key every ROM manager uses. It lives in the main DB (not a
/// throwaway cache file) so the scraper's write worker and the audit worker
/// can share one store, each through its own connection.
///
/// Free functions taking `QSqlDatabase &` first, matching the other
/// src/utils/db stores (UsageStatsStore, ItemMetadata). Not a QObject.
namespace FileHashCache {

/// Cached hashes for one file. `size` mirrors RomHasher::Result so a cache
/// hit reconstructs a full Result without another stat. Hex digests are
/// lowercase (as RomHasher / DatLookup normalise them).
struct Entry {
  QString crc;
  QString md5;
  QString sha1;
  qint64 size = -1;

  [[nodiscard]] bool isValid() const { return size >= 0; }
};

/// Return the cached hashes for `path`, but only when the stored
/// `(size, mtime)` still match `currentSize` / `currentMtimeMs`. A mismatch
/// (the file was edited/replaced) is reported as a miss so the caller
/// re-hashes and overwrites the stale row. Returns nullopt on miss, stale, an
/// empty path, or a closed DB. `path` is compared verbatim — pass the same
/// spelling you stored under (hashFileCached canonicalises for you).
[[nodiscard]] std::optional<Entry> lookup(QSqlDatabase &db, const QString &path, qint64 currentSize,
                                          qint64 currentMtimeMs);

/// Upsert the hashes for `path` keyed on the path primary key, recording the
/// `(size, mtime)` they were computed against. INSERT OR REPLACE so a re-hash
/// of a changed file replaces the stale row in one statement.
[[nodiscard]] ErrorUtils::Result<bool> store(QSqlDatabase &db, const QString &path, qint64 size,
                                             qint64 mtimeMs, const QString &crc, const QString &md5,
                                             const QString &sha1);

/// Hash `path` through the cache — the entry point callers should prefer.
/// Stats the file, returns the cached hashes on a `(size, mtime)` hit, else
/// runs RomHasher::hashFile and stores the result keyed by the file's
/// canonical path. A cache read/write failure never fails the call: the hash
/// itself still succeeds, so a broken cache degrades to "always re-hash".
/// Honors @p cancelToken via RomHasher (a mid-stream cancel returns
/// OperationCancelled and writes nothing).
[[nodiscard]] ErrorUtils::Result<RomHasher::Result>
hashFileCached(QSqlDatabase &db, const QString &path,
               const std::shared_ptr<std::atomic<bool>> &cancelToken = {});

/// Drop every cached row. Wired into the same "clear caches" UI as the DAT
/// cache; regenerable data, so this only costs a re-hash on the next audit.
void clearAll(QSqlDatabase &db);

} // namespace FileHashCache

#endif // FILEHASHCACHE_H
