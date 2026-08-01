// Disk persistence for the CacheManager: path hashing, the SQLite
// timestamp store, and the async PNG-encode + write pipeline.
#include "cachediskstorage.h"

#include "dbtxn.h"
#include "errorutils.h"
#include "pathutils.h"
#include "threadpoolutils.h"

#include <algorithm>
#include <QApplication>
#include <QAtomicInteger>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QThreadPool>
#include <QVariant>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(lcCacheManager)

namespace {

// Encode `image` to PNG in memory, then write it to `cachePath` via QSaveFile so
// a crash mid-encode can't leave a truncated PNG on disk (which later loads as a
// corrupt / blank thumbnail). Deliberately NOT PathUtils::atomicWriteFile — the
// shared helper fsyncs the parent directory on every call, but here the batch
// loop in scheduleAsyncSave issues that once per distinct directory per batch
// instead: per-file it was hundreds of redundant fsyncs per second against the
// same artwork directory during silent precache (the helper also mkpaths the
// parent, which that loop likewise hoists per batch). Logs + returns false on
// any failure; the caller skips to the next image (Kartend-6n5r).
bool saveImageAtomically(const QString &cachePath, const QImage &image) {
  QByteArray pngBytes;
  {
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
      ErrorUtils::logError(ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                                             "Failed to encode artwork PNG",
                                                             "CacheDiskStorage::scheduleAsyncSave")
                               .withDetails(QString("Path: %1").arg(cachePath)));
      return false;
    }
  }

  QSaveFile cacheFile(cachePath);
  if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to open artwork cache PNG for writing",
                                          "CacheDiskStorage::scheduleAsyncSave")
            .withDetails(QString("Path: %1, Error: %2").arg(cachePath, cacheFile.errorString())));
    return false;
  }

  const qint64 written = cacheFile.write(pngBytes);
  if (written != pngBytes.size()) {
    cacheFile.cancelWriting();
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to write complete artwork PNG payload",
                                          "CacheDiskStorage::scheduleAsyncSave")
            .withDetails(QString("Path: %1, Written: %2, Expected: %3, Error: %4")
                             .arg(cachePath)
                             .arg(written)
                             .arg(pngBytes.size())
                             .arg(cacheFile.errorString())));
    return false;
  }

  if (!cacheFile.commit()) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                          "Failed to atomically commit artwork PNG",
                                          "CacheDiskStorage::scheduleAsyncSave")
            .withDetails(QString("Path: %1, Error: %2").arg(cachePath, cacheFile.errorString())));
    return false;
  }
  return true;
}

// ─── SQLite timestamp store (Kartend-0ldg2) ─────────────────────────────────
//
// Timestamps used to live in metadata/artwork_cache.json — one monolithic
// blob that writeTimestamps re-read, re-merged and rewrote IN FULL on every
// debounced save, that startup re-parsed key-by-key (ISO-8601 strings), and
// that was never pruned (documented at 50MB+). They now live in a tiny
// self-owned SQLite database: saves upsert only the dirty rows in one
// transaction, startup is a single full SELECT, and dead rows are deleted
// opportunistically via pruneDeadTimestamps(). Deliberately NOT media.db —
// the cache layer must stay independent of DatabaseManager.
//
// THREADING — why a scoped per-call connection: Qt requires a QSqlDatabase
// connection to be used only from the thread that created it, and this store
// is touched from at least three distinct threads over a process lifetime:
// the dedicated cache IO pool worker (whose underlying QThread is reaped and
// recreated by QThreadPool's expiry timeout), the QtConcurrent startup
// worker running CacheManager::initialize(), and the main thread on the
// synchronous shutdown snapshot write. A cached long-lived connection is
// therefore not an option; each entry point opens its own uniquely-named
// connection and closes it before returning. The store is only touched at
// debounce frequency (seconds apart), so the open cost is noise next to the
// PNG encodes sharing the worker. WAL + busy_timeout cover the one real
// overlap window (a shutdown-thread write racing a still-draining worker).
//
// TEARDOWN — the threadpoolutils abandon-on-timeout contract holds for free:
// if an abandoned save task dies mid-write, the per-batch transaction either
// committed atomically or is rolled back by SQLite on the next open. No torn
// store, matching the old QSaveFile guarantee.

constexpr int kTimestampSchemaVersion = 1;

// Process-unique connection-name suffix so concurrent scoped connections
// (worker + shutdown thread, or multiple storage instances under test)
// can't collide in QSqlDatabase's global registry.
QAtomicInteger<quint64> g_timestampConnectionCounter{0};

bool execStoreSql(QSqlDatabase &db, const QString &sql, const char *context) {
  QSqlQuery query(db);
  if (!query.exec(sql)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                          "Artwork timestamp store SQL failed", context)
            .withDetails(QString("%1 — %2").arg(sql, query.lastError().text())));
    return false;
  }
  return true;
}

// One-way legacy import: parse artwork_cache.json — accepting both the
// original ISO-8601 string form and the later epoch-ms integer form —
// INSERT OR IGNORE every entry (an earlier import that committed but failed
// to rename must not clobber rows the store has since updated), then rename
// the JSON to "<name>.migrated". Serialized by a static mutex so two threads
// opening the store concurrently can't double-import.
void migrateLegacyJsonIfPresent(QSqlDatabase &db) {
  static QMutex migrationMutex;
  QMutexLocker locker(&migrationMutex);

  const QString jsonPath = CacheDiskStorage::metadataPath();
  QFile jsonFile(jsonPath);
  if (!jsonFile.exists()) {
    return;
  }
  if (!jsonFile.open(QIODevice::ReadOnly)) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileReadError,
                                          "Failed to open legacy artwork-cache JSON for migration",
                                          "CacheDiskStorage::migrateLegacyJsonIfPresent")
            .withDetails(QString("Path: %1").arg(jsonPath)));
    return;
  }
  const QByteArray data = jsonFile.readAll();
  jsonFile.close();

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  int imported = 0;
  if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
    const QJsonObject timestamps = doc.object()[QLatin1String("timestamps")].toObject();
    KartendDb::DbTransaction txn(db, "CacheDiskStorage::migrateLegacyJsonIfPresent");
    if (!txn.active()) {
      // Leave the JSON in place — the next open retries the import.
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseTransactionFailed,
                                            "Failed to begin legacy timestamp import transaction",
                                            "CacheDiskStorage::migrateLegacyJsonIfPresent")
              .withDetails(db.lastError().text()));
      return;
    }
    QSqlQuery insert(db);
    if (!insert.prepare(
            QStringLiteral("INSERT OR IGNORE INTO timestamps(path, ts_ms) VALUES(?, ?)"))) {
      return; // txn dtor rolls back; JSON stays for a retry
    }
    for (auto it = timestamps.begin(); it != timestamps.end(); ++it) {
      const QJsonValue value = it.value();
      qint64 tsMs = 0;
      if (value.isDouble()) {
        // Phase-1 epoch-ms integer form (fits a double exactly: < 2^53).
        tsMs = static_cast<qint64>(value.toDouble());
      } else if (value.isString()) {
        const QString text = value.toString();
        bool numeric = false;
        const qint64 asEpoch = text.toLongLong(&numeric);
        if (numeric) {
          tsMs = asEpoch; // epoch-ms serialized as a string
        } else {
          const QDateTime dateTime = QDateTime::fromString(text, Qt::ISODate);
          tsMs = dateTime.isValid() ? dateTime.toMSecsSinceEpoch() : 0;
        }
      } else {
        continue;
      }
      insert.addBindValue(it.key());
      insert.addBindValue(tsMs);
      if (insert.exec()) {
        ++imported;
      }
    }
    if (!txn.commit()) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseTransactionFailed,
                                            "Failed to commit legacy timestamp import",
                                            "CacheDiskStorage::migrateLegacyJsonIfPresent")
              .withDetails(db.lastError().text()));
      return;
    }
  }
  // One-way rename, even for a malformed/empty JSON (nothing to retry).
  // QFile::rename refuses to overwrite, so drop any stale .migrated first.
  const QString migratedPath = jsonPath + QStringLiteral(".migrated");
  QFile::remove(migratedPath);
  if (!jsonFile.rename(migratedPath)) {
    ErrorUtils::logError(ErrorUtils::ErrorContext::warning(
                             ErrorUtils::ErrorCode::FileWriteError,
                             "Failed to rename legacy artwork-cache JSON after migration",
                             "CacheDiskStorage::migrateLegacyJsonIfPresent")
                             .withDetails(QString("Path: %1").arg(jsonPath)));
    return;
  }
  qCInfo(lcCacheManager) << "Migrated" << imported
                         << "artwork-cache timestamps from legacy JSON into"
                         << CacheDiskStorage::databasePath();
}

/// RAII scoped connection to the timestamp store: open + pragmas + schema +
/// legacy-JSON migration in the ctor, close + registry removal in the dtor.
/// Created and destroyed on the calling thread (see THREADING above).
class ScopedTimestampDb {
public:
  ScopedTimestampDb()
      : m_connectionName(QStringLiteral("kartend-artwork-ts-%1")
                             .arg(g_timestampConnectionCounter.fetchAndAddRelaxed(1))) {
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    const QString dbPath = CacheDiskStorage::databasePath();
    // Ensure the metadata/ parent exists independently of cacheDirectory()'s
    // thread_local ensured-root memo: that memo skips mkpath after the first
    // success, so a cache dir wiped mid-session would otherwise leave metadata/
    // gone and every timestamp-store open failing for the rest of the session.
    // The per-batch mkpath in scheduleAsyncSave only recreates artwork/, not
    // metadata/, so heal it here at the open boundary.
    const QString parentDir = QFileInfo(dbPath).absolutePath();
    if (!parentDir.isEmpty()) QDir().mkpath(parentDir);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseNotOpen,
                                            "Failed to open artwork timestamp store",
                                            "CacheDiskStorage::ScopedTimestampDb")
              .withDetails(QString("Path: %1, Error: %2")
                               .arg(CacheDiskStorage::databasePath(), m_db.lastError().text())));
      return;
    }
    constexpr const char *ctx = "CacheDiskStorage::ScopedTimestampDb";
    // WAL so a shutdown-thread write can overlap a still-draining worker
    // write without corruption; busy_timeout covers that window's lock
    // contention; synchronous=NORMAL is safe under WAL and keeps the
    // debounced upserts cheap.
    execStoreSql(m_db, QStringLiteral("PRAGMA journal_mode = WAL"), ctx);
    execStoreSql(m_db, QStringLiteral("PRAGMA busy_timeout = 5000"), ctx);
    execStoreSql(m_db, QStringLiteral("PRAGMA synchronous = NORMAL"), ctx);
    if (!ensureSchema()) {
      m_db.close();
      return;
    }
    migrateLegacyJsonIfPresent(m_db);
  }

  ~ScopedTimestampDb() {
    if (m_db.isValid()) {
      m_db.close();
      m_db = QSqlDatabase(); // release the handle before removeDatabase
    }
    QSqlDatabase::removeDatabase(m_connectionName);
  }

  ScopedTimestampDb(const ScopedTimestampDb &) = delete;
  ScopedTimestampDb &operator=(const ScopedTimestampDb &) = delete;
  ScopedTimestampDb(ScopedTimestampDb &&) = delete;
  ScopedTimestampDb &operator=(ScopedTimestampDb &&) = delete;

  [[nodiscard]] bool isOpen() const { return m_db.isOpen(); }
  [[nodiscard]] QSqlDatabase &db() { return m_db; }

private:
  bool ensureSchema() {
    constexpr const char *ctx = "CacheDiskStorage::ScopedTimestampDb::ensureSchema";
    // Like DatCache, the store is a throwaway cache, not user data: any
    // future schema-version mismatch wipes and rebuilds instead of carrying
    // per-version migrations.
    int version = 0;
    {
      QSqlQuery query(m_db);
      if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
        version = query.value(0).toInt();
      }
    }
    if (version != 0 && version != kTimestampSchemaVersion) {
      execStoreSql(m_db, QStringLiteral("DROP TABLE IF EXISTS timestamps"), ctx);
    }
    if (!execStoreSql(m_db,
                      QStringLiteral("CREATE TABLE IF NOT EXISTS timestamps ("
                                     "path TEXT PRIMARY KEY, ts_ms INTEGER NOT NULL)"),
                      ctx)) {
      return false;
    }
    return execStoreSql(
        m_db, QStringLiteral("PRAGMA user_version = %1").arg(kTimestampSchemaVersion), ctx);
  }

  QString m_connectionName;
  QSqlDatabase m_db;
};

} // namespace

CacheDiskStorage::CacheDiskStorage()
    : m_cancelToken(std::make_shared<std::atomic_bool>(false)),
      m_pendingRemovals(std::make_shared<PendingRemovals>()), m_pool(new QThreadPool()) {
  // Single-thread pool: keep flushes sequential and reduce contention
  // with other QtConcurrent users.
  m_pool->setMaxThreadCount(1);
}

CacheDiskStorage::~CacheDiskStorage() {
  // Cooperative shutdown: m_cancelToken is captured by the dispatched
  // lambda so tasks bail quickly. Owners normally call drainWithBudget()
  // first (CacheManager does, for visibility into the failure mode), which
  // deletes-or-leaks the pool and nulls the pointer; if the pool is still
  // live here, fall back to the same bounded drain so no user of this class
  // silently leaks a running QThreadPool.
  if (m_pool) {
    constexpr int kDtorDrainMs = 500;
    cancel();
    if (!ThreadPoolUtils::shutdownWithBudget(m_pool, kDtorDrainMs)) {
      qCWarning(lcCacheManager) << "CacheDiskStorage dtor drain expired after" << kDtorDrainMs
                                << "ms — abandoning the IO pool (leak-on-timeout fallback)";
    }
  }
}

QString CacheDiskStorage::cacheDirectory() {
  const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                           QStringLiteral("/kartend");
  // Memoize the ensured-ok state per thread: artworkCachePath() lands here
  // 1-3x per item on the worker decode path (hundreds of calls/s during
  // silent precache), and every call re-paid two directory-exists stats for
  // a layout that only needs verifying once. thread_local keeps the fast
  // path lock-free; a reaped-and-recreated pool thread merely re-verifies
  // once. Keyed on the resolved root so a mid-process QStandardPaths change
  // (tests flip test mode / HOME) re-ensures instead of lying. Nothing is
  // memoized on failure, so every call retries mkpath until it succeeds.
  // NOTE: once memoized, a cache dir deleted externally is NOT re-created by
  // this function — write paths that need a subdir to exist mkpath it
  // themselves at their write boundary (scheduleAsyncSave for artwork/,
  // ScopedTimestampDb's ctor for metadata/) rather than relying on the memo.
  thread_local QString ensuredRoot;
  if (ensuredRoot == cacheDir) {
    return cacheDir;
  }

  QDir dir(cacheDir);
  const QString artworkDirPath = cacheDir + QStringLiteral("/artwork");
  const QString metadataDirPath = cacheDir + QStringLiteral("/metadata");
  // Atomic: reached concurrently from the cache-IO pool, the QtConcurrent
  // init worker, and the GUI thread; exchange keeps the log single-shot
  // without a data race.
  static std::atomic<bool> loggedMkpathFailure{false};

  bool ensured = true;
  if (!dir.exists("artwork") && !dir.mkpath(artworkDirPath)) {
    ensured = false;
    if (!loggedMkpathFailure.exchange(true)) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to create artwork cache directory",
                                            "CacheDiskStorage::cacheDirectory")
              .withDetails(QString("Path: %1").arg(artworkDirPath)));
    }
  }
  if (!dir.exists("metadata") && !dir.mkpath(metadataDirPath)) {
    ensured = false;
    if (!loggedMkpathFailure.exchange(true)) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                            "Failed to create cache metadata directory",
                                            "CacheDiskStorage::cacheDirectory")
              .withDetails(QString("Path: %1").arg(metadataDirPath)));
    }
  }
  if (ensured) {
    ensuredRoot = cacheDir;
  }
  return cacheDir;
}

QString CacheDiskStorage::artworkCachePath(const QString &artworkPath) {
  const QByteArray hash = QCryptographicHash::hash(artworkPath.toUtf8(), QCryptographicHash::Md5);
  return cacheDirectory() + QStringLiteral("/artwork/") + hash.toHex() + QStringLiteral(".png");
}

QString CacheDiskStorage::metadataPath() {
  return cacheDirectory() + QStringLiteral("/metadata/artwork_cache.json");
}

QString CacheDiskStorage::databasePath() {
  return cacheDirectory() + QStringLiteral("/metadata/artwork_cache.sqlite");
}

void CacheDiskStorage::readTimestampsInto(QHash<QString, qint64> &outTimestamps) const {
  // Bail before any disk I/O if the caller already cancelled. The check
  // repeats inside the row loop so a large store on a slow disk can
  // short-circuit when shutdown signals via cancel() (Kartend-bu5n).
  if (isCancelled()) {
    return;
  }
  // First run: nothing on disk in either form — don't create an empty store
  // just to read zero rows out of it.
  if (!QFileInfo::exists(databasePath()) && !QFileInfo::exists(metadataPath())) {
    return;
  }

  ScopedTimestampDb store; // imports + renames a legacy JSON on first open
  if (!store.isOpen() || isCancelled()) {
    return;
  }

  QSqlQuery query(store.db());
  query.setForwardOnly(true);
  if (!query.exec(QStringLiteral("SELECT path, ts_ms FROM timestamps"))) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                          "Failed to read artwork timestamp store",
                                          "CacheDiskStorage::readTimestampsInto")
            .withDetails(query.lastError().text()));
    return;
  }
  int sinceCheck = 0;
  while (query.next()) {
    // Polling cancellation every row would be expensive; sample every 1024
    // entries — fine-grained enough that even a million-row store aborts in
    // ~1ms once cancel is signalled.
    if (++sinceCheck >= 1024) {
      sinceCheck = 0;
      if (isCancelled()) {
        return;
      }
    }
    outTimestamps.insert(query.value(0).toString(), query.value(1).toLongLong());
  }
}

bool CacheDiskStorage::writeTimestamps(const QHash<QString, qint64> &dirtyTimestamps,
                                       const QStringList &removedPaths) {
  if (dirtyTimestamps.isEmpty() && removedPaths.isEmpty()) {
    return true;
  }
  // A removals-only batch against a store that doesn't exist yet has nothing
  // to delete — don't create an empty store just to run the DELETEs
  // (mirrors pruneDeadTimestamps). Vacuous success: the rows are gone.
  if (dirtyTimestamps.isEmpty() && !QFileInfo::exists(databasePath())) {
    return true;
  }

  // databasePath() routes through cacheDirectory(), which creates metadata/
  // on demand; if that failed, the open below fails and logs.
  ScopedTimestampDb store;
  if (!store.isOpen()) {
    return false;
  }

  // One transaction for the whole dirty batch (removals + upserts): atomic
  // (an abandoned task can't leave a half-written batch) and orders of
  // magnitude cheaper than per-row autocommit fsyncs.
  KartendDb::DbTransaction txn(store.db(), "CacheDiskStorage::writeTimestamps");
  if (!txn.active()) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseTransactionFailed,
                                          "Failed to begin timestamp upsert transaction",
                                          "CacheDiskStorage::writeTimestamps")
            .withDetails(store.db().lastError().text()));
    return false;
  }
  // Invalidation deletions FIRST, so a key that was invalidated and then
  // re-cached within the same debounce window (present in both batches)
  // ends with the fresh upserted row (Kartend-9lm54).
  if (!removedPaths.isEmpty()) {
    QSqlQuery del(store.db());
    if (!del.prepare(QStringLiteral("DELETE FROM timestamps WHERE path = ?"))) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                            "Failed to prepare invalidated timestamp delete",
                                            "CacheDiskStorage::writeTimestamps")
              .withDetails(del.lastError().text()));
      return false; // txn dtor rolls back
    }
    for (const QString &path : removedPaths) {
      del.addBindValue(path);
      if (!del.exec()) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                              "Failed to delete invalidated timestamp row",
                                              "CacheDiskStorage::writeTimestamps")
                .withDetails(del.lastError().text()));
        return false; // txn dtor rolls back the batch
      }
    }
  }
  QSqlQuery upsert(store.db());
  if (!dirtyTimestamps.isEmpty() &&
      !upsert.prepare(
          QStringLiteral("INSERT OR REPLACE INTO timestamps(path, ts_ms) VALUES(?, ?)"))) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                          "Failed to prepare timestamp upsert",
                                          "CacheDiskStorage::writeTimestamps")
            .withDetails(upsert.lastError().text()));
    return false; // txn dtor rolls back
  }
  for (auto it = dirtyTimestamps.begin(); it != dirtyTimestamps.end(); ++it) {
    upsert.addBindValue(it.key());
    upsert.addBindValue(it.value());
    if (!upsert.exec()) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                            "Failed to upsert artwork timestamp row",
                                            "CacheDiskStorage::writeTimestamps")
              .withDetails(upsert.lastError().text()));
      return false; // txn dtor rolls back the batch
    }
  }
  if (!txn.commit()) {
    ErrorUtils::logError(
        ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseTransactionFailed,
                                          "Failed to commit timestamp upsert batch",
                                          "CacheDiskStorage::writeTimestamps")
            .withDetails(store.db().lastError().text()));
    return false;
  }
  return true;
}

QStringList CacheDiskStorage::takePendingRemovals() {
  QMutexLocker locker(&m_pendingRemovals->mutex);
  const QStringList out(m_pendingRemovals->paths.cbegin(), m_pendingRemovals->paths.cend());
  m_pendingRemovals->paths.clear();
  return out;
}

QStringList CacheDiskStorage::pendingRemovals() const {
  QMutexLocker locker(&m_pendingRemovals->mutex);
  return {m_pendingRemovals->paths.cbegin(), m_pendingRemovals->paths.cend()};
}

QStringList CacheDiskStorage::pruneDeadTimestamps(int maxRowsToExamine, QString *cursorPath) {
  QStringList removed;
  if (maxRowsToExamine <= 0 || cursorPath == nullptr) {
    return removed;
  }
  // Don't create an empty store just to prune nothing out of it.
  if (!QFileInfo::exists(databasePath())) {
    cursorPath->clear();
    return removed;
  }

  ScopedTimestampDb store;
  if (!store.isOpen()) {
    return removed;
  }

  QStringList batch;
  {
    QSqlQuery select(store.db());
    select.setForwardOnly(true);
    if (!select.prepare(
            QStringLiteral("SELECT path FROM timestamps WHERE path > ? ORDER BY path LIMIT ?"))) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                            "Failed to prepare timestamp prune select",
                                            "CacheDiskStorage::pruneDeadTimestamps")
              .withDetails(select.lastError().text()));
      return removed;
    }
    // A null QString binds as SQL NULL and `path > NULL` matches nothing —
    // normalize the fresh/wrapped cursor to an empty (non-null) string.
    select.addBindValue(cursorPath->isNull() ? QStringLiteral("") : *cursorPath);
    select.addBindValue(maxRowsToExamine);
    if (!select.exec()) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                            "Failed to select timestamp prune batch",
                                            "CacheDiskStorage::pruneDeadTimestamps")
              .withDetails(select.lastError().text()));
      return removed;
    }
    while (select.next()) {
      batch.append(select.value(0).toString());
    }
  }

  for (const QString &path : std::as_const(batch)) {
    if (!QFileInfo::exists(path)) {
      removed.append(path);
    }
  }

  if (!removed.isEmpty()) {
    // Delete each dead row's cached PNG FIRST, while the source path (and thus
    // the hashed cache filename) is still known — once the row is gone the
    // hash can never be re-derived and the PNG becomes a permanently
    // unreferenceable orphan. If the row delete below then fails or rolls
    // back, the surviving rows merely point at a missing cache file (the
    // ordinary not-yet-cached state) and a later pass retries.
    for (const QString &path : std::as_const(removed)) {
      const QString cachePath = artworkCachePath(path);
      if (QFile::exists(cachePath) && !QFile::remove(cachePath)) {
        qCWarning(lcCacheManager) << "Failed to remove orphaned artwork cache file" << cachePath;
      }
    }
    // Any failure below reports "nothing removed" so the caller's in-memory
    // sweep can't outrun the store (the txn dtor rolls back partial work).
    constexpr const char *ctx = "CacheDiskStorage::pruneDeadTimestamps";
    KartendDb::DbTransaction txn(store.db(), ctx);
    if (!txn.activeOrReport(QStringLiteral("Failed to begin timestamp prune transaction"),
                            ErrorUtils::Severity::Warning)) {
      return {};
    }
    QSqlQuery del(store.db());
    if (!del.prepare(QStringLiteral("DELETE FROM timestamps WHERE path = ?"))) {
      ErrorUtils::logError(
          ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                            "Failed to prepare timestamp prune delete", ctx)
              .withDetails(del.lastError().text()));
      return {};
    }
    for (const QString &path : std::as_const(removed)) {
      del.addBindValue(path);
      if (!del.exec()) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::DatabaseQueryFailed,
                                              "Failed to delete dead timestamp row", ctx)
                .withDetails(del.lastError().text()));
        return {}; // txn dtor rolls back
      }
    }
    if (!txn.commitOrReport(QStringLiteral("Failed to commit timestamp prune batch"),
                            ErrorUtils::Severity::Warning)) {
      return {};
    }
    qCDebug(lcCacheManager) << "Pruned" << removed.size() << "dead artwork timestamp rows";
  }

  // Advance the rotating cursor; wrap to the start once the batch comes up
  // short (this revolution exhausted the keyspace).
  if (batch.size() < maxRowsToExamine) {
    cursorPath->clear();
  } else {
    *cursorPath = batch.last();
  }
  return removed;
}

void CacheDiskStorage::scheduleAsyncSave(bool shouldWriteMetadata,
                                         const QHash<QString, qint64> &dirtyTimestamps,
                                         const QStringList &removedTimestampPaths,
                                         const QList<QPair<QString, QImage>> &dirtyImages) {
  // Stage the removals BEFORE any early-out or dispatch: CacheManager's
  // flush clears its dirty sets at snapshot time, so a batch this call
  // drops (pool already drained), one clearQueue()/cancel() discards, or
  // one whose store write fails would otherwise lose its deletions for
  // good — leaving invalidated keys' stale rows in the store into the next
  // session. The staged set survives all three: the task below takes it,
  // re-stages it on write failure, and the shutdown flush drains whatever
  // is left into its own synchronous write.
  if (!removedTimestampPaths.isEmpty()) {
    QMutexLocker locker(&m_pendingRemovals->mutex);
    for (const QString &path : removedTimestampPaths) {
      m_pendingRemovals->paths.insert(path);
    }
  }
  if (!m_pool) {
    return;
  }
  if (!shouldWriteMetadata && dirtyImages.isEmpty()) {
    return;
  }
  // Capture shared_ptrs to the cancellation flag and the staged-removals
  // set so the lambda can safely use both even after this storage instance
  // is destroyed (the QThreadPool destructor blocks; the bounded-drain
  // helper either cleans up cleanly or abandons the pool and lets the OS
  // reclaim).
  auto cancelToken = m_cancelToken;
  auto pendingRemovals = m_pendingRemovals;
  m_pool->start([cancelToken, pendingRemovals, shouldWriteMetadata, dirtyTimestamps,
                 dirtyImages]() {
    if (cancelToken->load(std::memory_order_acquire) || QApplication::closingDown()) {
      return;
    }

    QElapsedTimer timer;
    timer.start();

    if (shouldWriteMetadata) {
      // Take the staged removals — this batch's plus any earlier failed
      // ones (the pool is single-threaded, so takes never race) — and
      // re-stage them if the write fails so no deletion is ever dropped.
      QStringList removals;
      {
        QMutexLocker locker(&pendingRemovals->mutex);
        removals = QStringList(pendingRemovals->paths.cbegin(), pendingRemovals->paths.cend());
        pendingRemovals->paths.clear();
      }
      if (!writeTimestamps(dirtyTimestamps, removals) && !removals.isEmpty()) {
        QMutexLocker locker(&pendingRemovals->mutex);
        for (const QString &path : std::as_const(removals)) {
          pendingRemovals->paths.insert(path);
        }
      }
    }

    // Parent directories of the files actually written this batch. Every
    // entry hashes into the same artwork dir today, but derive the set from
    // the writes rather than assuming — a future layout change (e.g.
    // fan-out subdirectories) keeps its durability for free.
    QSet<QString> dirsToSync;
    for (const auto &entry : dirtyImages) {
      if (cancelToken->load(std::memory_order_acquire) || QApplication::closingDown()) {
        break;
      }

      const QString &artworkPath = entry.first;
      const QImage &image = entry.second;
      if (image.isNull()) {
        continue;
      }

      const QString cachePath = artworkCachePath(artworkPath);
      const QString parentDir = QFileInfo(cachePath).absolutePath();
      if (!parentDir.isEmpty() && !QDir().mkpath(parentDir)) {
        ErrorUtils::logError(
            ErrorUtils::ErrorContext::warning(ErrorUtils::ErrorCode::FileWriteError,
                                              "Failed to create artwork cache directory",
                                              "CacheDiskStorage::scheduleAsyncSave")
                .withDetails(QString("Path: %1").arg(parentDir)));
        continue;
      }
      if (saveImageAtomically(cachePath, image)) {
        dirsToSync.insert(parentDir);
      }
    }
    // fsync each distinct parent directory ONCE per batch so the renames are
    // durable across crash / power loss. Hoisted out of saveImageAtomically:
    // at ~100 images/s the per-file sync was hundreds of directory fsyncs per
    // second on one directory. A crash inside the batch can now lose the
    // rename durability of this batch's earlier files — acceptable for a
    // rebuildable cache (worst case the PNG is re-encoded next session).
    // Runs even after a cancellation break so already-committed files still
    // get their durability.
    for (const QString &dir : std::as_const(dirsToSync)) {
      PathUtils::syncDirectory(dir);
    }

    if (lcCacheManager().isDebugEnabled()) {
      qCDebug(lcCacheManager) << "CacheDiskStorage flushed"
                              << "metadata=" << (shouldWriteMetadata ? "yes" : "no")
                              << "images=" << dirtyImages.size() << "elapsedMs=" << timer.elapsed();
    }
  });
}

void CacheDiskStorage::cancel() {
  m_cancelToken->store(true, std::memory_order_release);
}

bool CacheDiskStorage::isCancelled() const {
  return m_cancelToken->load(std::memory_order_acquire);
}

void CacheDiskStorage::clearQueue() {
  if (m_pool) {
    m_pool->clear();
  }
}

bool CacheDiskStorage::drainWithBudget(int budgetMs) {
  return ThreadPoolUtils::shutdownWithBudget(m_pool, budgetMs);
}
