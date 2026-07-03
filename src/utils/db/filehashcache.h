#ifndef FILEHASHCACHE_H
#define FILEHASHCACHE_H

#include <atomic>
#include <memory>
#include <optional>

#include <QList>
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
///
/// `ignoreCache` forces a miss even on a valid `(size, mtime)` row, so the
/// caller re-hashes and refreshes the stale entry. This is the escape hatch for
/// the same-size/same-mtime staleness blind spot documented at the validity
/// check in filehashcache.cpp (an in-place replacement that preserves both — as
/// `rsync --times` / `cp -p` do — reads as a hit and returns the OLD file's
/// hashes). Default false leaves the fast path byte-for-byte unchanged.
[[nodiscard]] std::optional<Entry> lookup(QSqlDatabase &db, const QString &path, qint64 currentSize,
                                          qint64 currentMtimeMs, bool ignoreCache = false);

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
/// OperationCancelled and writes nothing). `forceRehash` bypasses a valid
/// `(size, mtime)` cache hit (see lookup()'s `ignoreCache`): the file is always
/// re-hashed and the cached entry refreshed. Default false — unchanged fast path.
[[nodiscard]] ErrorUtils::Result<RomHasher::Result>
hashFileCached(QSqlDatabase &db, const QString &path,
               const std::shared_ptr<std::atomic<bool>> &cancelToken = {},
               bool forceRehash = false);

/// One cached archive member (archive-per-item auditing, Kartend-m6qsb.7).
/// `memberPath` is the member's '/'-separated path inside its container.
struct MemberEntry {
  QString memberPath;
  QString crc;
  QString md5;
  QString sha1;
  qint64 size = -1;
  /// The member's index among the container's regular-file members, in
  /// central-directory order (Kartend-7iqhl.4), cached so a hit preserves that
  /// order — lookupMembers returns rows ORDER BY member_path, not archive order,
  /// so position alone can't recover it. -1 for entries cached before the
  /// column existed.
  int zipIndex = -1;
};

/// All cached members for `containerPath` (schema v19 table
/// `archive_member_hash_cache`), but only when the stored container
/// `(size, mtime)` still match — an edited/replaced archive invalidates
/// every member row at once, mirroring the outer cache's invalidation key.
/// nullopt on miss/stale/closed DB; the caller re-extracts and calls
/// storeMembers. An empty (but valid) member list is a legal hit.
///
/// `ignoreCache` forces a miss even on a valid `(size, mtime)` match, so the
/// caller re-extracts and refreshes the rows — the container-level counterpart
/// to lookup()'s bypass, for the same same-size/same-mtime staleness blind spot.
/// Default false leaves the fast path unchanged.
[[nodiscard]] std::optional<QList<MemberEntry>>
lookupMembers(QSqlDatabase &db, const QString &containerPath, qint64 currentSize,
              qint64 currentMtimeMs, bool ignoreCache = false);

/// Replace `containerPath`'s member rows with `members` in one transaction,
/// stamped against the container's `(size, mtime)`.
[[nodiscard]] ErrorUtils::Result<bool> storeMembers(QSqlDatabase &db, const QString &containerPath,
                                                    qint64 size, qint64 mtimeMs,
                                                    const QList<MemberEntry> &members);

} // namespace FileHashCache

#endif // FILEHASHCACHE_H
