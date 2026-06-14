// On-disk sqlite cache for parsed DAT files.
//
// The point of this module is to skip the XML re-parse on cold start
// for very large DATs (MAME listxml is ~100MB / ~250k entries). Hash
// lookups become indexed sqlite SELECTs; the cache invalidates on
// mtime change so an edited DAT reloads automatically.
#include "datcache.h"

#include "dbtxn.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatCache {

namespace {

// Bump when the schema changes. The on-disk DB is throwaway (a cache,
// not user data) so the migration story is "wipe + rebuild" — handled
// by `migrateOrReset` below.
// v2: header_name/header_description/header_version on dat_sources
//     (Kartend-m6qsb.3) — the wipe forces a re-ingest that populates them.
constexpr int kSchemaVersion = 2;

// Process-unique connection-name suffix so multiple Store instances
// (tests + production) don't collide in QSqlDatabase's global
// registry. Atomic because Store construction can race across threads.
QAtomicInteger<quint64> g_connectionCounter{0};

QString makeUniqueConnectionName() {
  return QStringLiteral("kartend-datcache-%1").arg(g_connectionCounter.fetchAndAddRelaxed(1));
}

bool execSimple(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  if (!q.exec(sql)) {
    qWarning("DatCache schema SQL failed: %s — %s", qPrintable(sql),
             qPrintable(q.lastError().text()));
    return false;
  }
  return true;
}

bool createSchema(QSqlDatabase &db) {
  // Manual transaction here even though most callers begin their own:
  // schema setup runs at open time before any caller has a handle.
  KartendDb::DbTransaction txn(db, "DatCache::createSchema");
  if (!txn.active()) {
    qWarning("DatCache: BEGIN failed: %s", qPrintable(db.lastError().text()));
    return false;
  }
  const bool ok =
      execSimple(db, QStringLiteral("CREATE TABLE IF NOT EXISTS dat_sources ("
                                    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                    "  path TEXT UNIQUE NOT NULL,"
                                    "  mtime_unix_ms INTEGER NOT NULL,"
                                    "  dialect INTEGER NOT NULL,"
                                    "  record_count INTEGER NOT NULL,"
                                    "  header_name TEXT NOT NULL DEFAULT '',"
                                    "  header_description TEXT NOT NULL DEFAULT '',"
                                    "  header_version TEXT NOT NULL DEFAULT '',"
                                    "  ingested_at_unix_ms INTEGER NOT NULL)")) &&
      execSimple(db, QStringLiteral("CREATE TABLE IF NOT EXISTS dat_records ("
                                    "  source_id INTEGER NOT NULL,"
                                    "  game_name TEXT NOT NULL,"
                                    "  rom_name TEXT NOT NULL,"
                                    "  size INTEGER NOT NULL,"
                                    "  crc TEXT,"
                                    "  md5 TEXT,"
                                    "  sha1 TEXT,"
                                    "  FOREIGN KEY(source_id) REFERENCES dat_sources(id) "
                                    "    ON DELETE CASCADE)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_records_sha1 "
                                    "ON dat_records(source_id, sha1)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_records_md5 "
                                    "ON dat_records(source_id, md5)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_records_crc "
                                    "ON dat_records(source_id, crc)")) &&
      execSimple(db, QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));
  if (!ok) {
    return false; // guard dtor rolls back
  }
  return txn.commit();
}

/// Read the on-disk schema version. Returns 0 for a fresh DB (no
/// PRAGMA set yet). Treats query failure as 0 so a corrupt cache file
/// gets wiped + rebuilt at the call site.
int readSchemaVersion(QSqlDatabase &db) {
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) return 0;
  return q.value(0).toInt();
}

bool migrateOrReset(QSqlDatabase &db) {
  const int current = readSchemaVersion(db);
  if (current == kSchemaVersion) return true;
  // The cache is throwaway: any version mismatch (older or newer)
  // gets nuked and rebuilt from scratch. This avoids per-version
  // migration logic for a file we can always recreate by re-parsing
  // the user's DAT.
  if (current != 0) {
    execSimple(db, QStringLiteral("DROP TABLE IF EXISTS dat_records"));
    execSimple(db, QStringLiteral("DROP TABLE IF EXISTS dat_sources"));
  }
  return createSchema(db);
}

} // namespace

Store::Store(const QString &dbPath) : m_connectionName(makeUniqueConnectionName()) {
  m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_db.setDatabaseName(dbPath);
  if (!m_db.open()) {
    qWarning("DatCache: failed to open %s — %s", qPrintable(dbPath),
             qPrintable(m_db.lastError().text()));
    return;
  }
  // SQLite-specific pragmas matching DatabaseManager: WAL for write
  // concurrency, foreign_keys for the ON DELETE CASCADE on
  // dat_records → dat_sources.
  execSimple(m_db, QStringLiteral("PRAGMA journal_mode = WAL"));
  execSimple(m_db, QStringLiteral("PRAGMA foreign_keys = ON"));
  if (!migrateOrReset(m_db)) {
    qWarning("DatCache: schema migration failed on %s", qPrintable(dbPath));
    m_db.close();
  }
}

Store::~Store() {
  if (m_db.isValid()) {
    m_db.close();
    m_db = QSqlDatabase();
  }
  QSqlDatabase::removeDatabase(m_connectionName);
}

bool Store::isOpen() const {
  return m_db.isOpen();
}

ErrorUtils::Result<CachedSource> Store::openOrIngest(const QString &datPath) {
  if (!isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "DAT cache DB not open",
                               "DatCache::Store::openOrIngest");
  }
  if (datPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty DAT path",
                               "DatCache::Store::openOrIngest");
  }
  const QFileInfo fi(datPath);
  if (!fi.isFile()) {
    return ErrorContext::error(ErrorCode::FileNotFound, "DAT file does not exist",
                               "DatCache::Store::openOrIngest")
        .withDetails(datPath);
  }
  // Canonical-path the key so the cache hits regardless of relative
  // / symlinked variants the user types in the path picker.
  const QString canonicalPath = fi.canonicalFilePath();
  const qint64 mtimeMs = fi.lastModified().toMSecsSinceEpoch();

  // Look up the existing source row, if any. Note: we read the whole
  // handle's worth of columns together so a cache-hit path doesn't need
  // a second SELECT.
  CachedSource existing;
  {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, mtime_unix_ms, dialect, record_count, "
                             "header_name, header_description, header_version "
                             "FROM dat_sources WHERE path = ?"));
    q.addBindValue(canonicalPath);
    if (q.exec() && q.next()) {
      existing.id = q.value(0).toLongLong();
      existing.mtimeUnixMs = q.value(1).toLongLong();
      existing.dialect = static_cast<DatLookup::Dialect>(q.value(2).toInt());
      existing.recordCount = q.value(3).toInt();
      existing.headerName = q.value(4).toString();
      existing.headerDescription = q.value(5).toString();
      existing.headerVersion = q.value(6).toString();
    }
  }

  if (existing.id >= 0 && existing.mtimeUnixMs == mtimeMs) {
    // Cache hit — the file hasn't changed since we ingested it.
    return existing;
  }

  // Either cache miss (no row) or stale (mtime mismatch). For the
  // stale case, drop the old records first — the ON DELETE CASCADE
  // on dat_records.source_id handles the row sweep when we delete
  // the dat_sources row.
  if (existing.id >= 0) {
    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM dat_sources WHERE id = ?"));
    del.addBindValue(existing.id);
    if (!del.exec()) {
      qWarning("DatCache: failed to evict stale source %lld: %s", existing.id,
               qPrintable(del.lastError().text()));
    }
  }

  // Read + parse the DAT via DatLookup. This is the slow path —
  // multi-second on a 100MB MAME XML, sub-second on a 5MB No-Intro.
  // The whole point of this cache is to do this once per (path,
  // mtime) and serve subsequent lookups from sqlite.
  QFile f(datPath);
  if (!f.open(QIODevice::ReadOnly)) {
    return ErrorContext::error(ErrorCode::FileNotFound, "Failed to open DAT file for ingest",
                               "DatCache::Store::openOrIngest")
        .withDetails(f.errorString());
  }
  const QByteArray bytes = f.readAll();
  f.close();
  const DatLookup::Dialect dialect = DatLookup::detectDialect(bytes);
  // Header metadata rides along with the ingest so the matcher / UI can
  // identify the catalogue without re-touching the XML (Kartend-m6qsb.3).
  const DatLookup::DatHeader header = DatLookup::probeHeader(bytes);
  auto parsed = DatLookup::parseDat(bytes);
  if (parsed.isError()) {
    return parsed.error();
  }
  const auto records = parsed.value();

  // Bulk insert under a single transaction. Without batching, ingesting
  // 250k MAME rows would take tens of seconds (one fsync per row);
  // wrapped in a transaction it's well under a second.
  KartendDb::DbTransaction txn(m_db, "DatCache::Store::openOrIngest");
  if (!txn.active()) {
    return txn.beginError("Failed to begin ingest transaction", nullptr,
                          ErrorUtils::Severity::Error);
  }

  qint64 newId = -1;
  {
    QSqlQuery ins(m_db);
    ins.prepare(QStringLiteral("INSERT INTO dat_sources (path, mtime_unix_ms, dialect, "
                               "record_count, header_name, header_description, header_version, "
                               "ingested_at_unix_ms) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    ins.addBindValue(canonicalPath);
    ins.addBindValue(mtimeMs);
    ins.addBindValue(static_cast<int>(dialect));
    ins.addBindValue(static_cast<int>(records.size()));
    // Null QStrings bind as SQL NULL which the NOT NULL columns reject —
    // absent header fields are routinely null (headerless Logiqx DATs).
    ins.addBindValue(header.name.isNull() ? QStringLiteral("") : header.name);
    ins.addBindValue(header.description.isNull() ? QStringLiteral("") : header.description);
    ins.addBindValue(header.version.isNull() ? QStringLiteral("") : header.version);
    ins.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!ins.exec()) {
      const QString err = ins.lastError().text();
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to insert DAT source row",
                                 "DatCache::Store::openOrIngest")
          .withDetails(err);
    }
    newId = ins.lastInsertId().toLongLong();
  }

  {
    QSqlQuery ins(m_db);
    ins.prepare(QStringLiteral("INSERT INTO dat_records (source_id, game_name, rom_name, "
                               "size, crc, md5, sha1) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    for (const DatLookup::DatRecord &r : records) {
      ins.bindValue(0, newId);
      ins.bindValue(1, r.gameName);
      ins.bindValue(2, r.romName);
      ins.bindValue(3, r.size);
      // Empty hashes go in as NULL so the partial index doesn't
      // bloat with empty-string keys (NULL is the right "no value"
      // sentinel in sqlite).
      ins.bindValue(4, r.crc.isEmpty() ? QVariant() : QVariant(r.crc));
      ins.bindValue(5, r.md5.isEmpty() ? QVariant() : QVariant(r.md5));
      ins.bindValue(6, r.sha1.isEmpty() ? QVariant() : QVariant(r.sha1));
      if (!ins.exec()) {
        const QString err = ins.lastError().text();
        return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to insert DAT record",
                                   "DatCache::Store::openOrIngest")
            .withDetails(err);
      }
    }
  }

  if (!txn.commit()) {
    return txn.commitError("Failed to commit ingest transaction", nullptr,
                           ErrorUtils::Severity::Error);
  }

  return CachedSource{newId,         mtimeMs,
                      dialect,       static_cast<int>(records.size()),
                      header.name,   header.description,
                      header.version};
}

std::optional<CachedSource> Store::peek(const QString &datPath) const {
  if (!isOpen() || datPath.isEmpty()) {
    return std::nullopt;
  }
  // Same canonical-path key as openOrIngest so relative / symlinked
  // variants of an ingested path still hit. canonicalFilePath() is empty
  // for a missing file; fall back to the raw path so a row ingested before
  // the file vanished can still be peeked.
  const QFileInfo fi(datPath);
  const QString canonical = fi.canonicalFilePath();
  // Read-only despite the non-const QSqlDatabase QSqlQuery demands —
  // same const_cast shape as lookup().
  QSqlDatabase &db = const_cast<Store *>(this)->m_db;
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT id, mtime_unix_ms, dialect, record_count, "
                           "header_name, header_description, header_version "
                           "FROM dat_sources WHERE path = ?"));
  q.addBindValue(canonical.isEmpty() ? datPath : canonical);
  if (!q.exec() || !q.next()) {
    return std::nullopt;
  }
  return CachedSource{q.value(0).toLongLong(),
                      q.value(1).toLongLong(),
                      static_cast<DatLookup::Dialect>(q.value(2).toInt()),
                      q.value(3).toInt(),
                      q.value(4).toString(),
                      q.value(5).toString(),
                      q.value(6).toString()};
}

namespace {

/// Run one hash-keyed SELECT and return the first matching row as a
/// DatRecord. Empty hash short-circuits to nullopt so the caller can
/// chain the three lookups cleanly.
std::optional<DatLookup::DatRecord> lookupByColumn(QSqlDatabase &db, qint64 sourceId,
                                                   const char *column, const QString &hash) {
  if (hash.isEmpty()) return std::nullopt;
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT game_name, rom_name, size, crc, md5, sha1 "
                           "FROM dat_records WHERE source_id = ? AND %1 = ? "
                           "LIMIT 1")
                .arg(QLatin1String(column)));
  q.addBindValue(sourceId);
  q.addBindValue(hash.toLower());
  if (!q.exec() || !q.next()) return std::nullopt;
  DatLookup::DatRecord r;
  r.gameName = q.value(0).toString();
  r.romName = q.value(1).toString();
  r.size = q.value(2).toLongLong();
  r.crc = q.value(3).toString();
  r.md5 = q.value(4).toString();
  r.sha1 = q.value(5).toString();
  return r;
}

} // namespace

std::optional<DatLookup::DatRecord> Store::lookup(const CachedSource &source, const QString &md5,
                                                  const QString &sha1, const QString &crc) const {
  if (!isOpen() || !source.isValid()) return std::nullopt;
  // QSqlQuery wants a non-const QSqlDatabase but the operation is
  // logically read-only. Cast away const here — same shape as
  // DatabaseManager's read paths.
  QSqlDatabase &db = const_cast<Store *>(this)->m_db;
  // sha1 → md5 → crc, in descending order of reliability (sha1
  // collisions are infeasible at this scale; crc32 has real
  // collision risk).
  if (auto r = lookupByColumn(db, source.id, "sha1", sha1)) return r;
  if (auto r = lookupByColumn(db, source.id, "md5", md5)) return r;
  if (auto r = lookupByColumn(db, source.id, "crc", crc)) return r;
  return std::nullopt;
}

bool Store::forEachRecord(const CachedSource &source,
                          const std::function<void(const DatLookup::DatRecord &)> &callback) const {
  if (!isOpen() || !source.isValid() || !callback) return false;
  // Read-only despite the non-const QSqlDatabase QSqlQuery demands —
  // same const_cast shape as lookup().
  QSqlDatabase &db = const_cast<Store *>(this)->m_db;
  QSqlQuery q(db);
  // forwardOnly keeps QSQLITE from buffering the whole 250k-row result
  // set in memory — we stream one row at a time into the callback.
  q.setForwardOnly(true);
  q.prepare(QStringLiteral("SELECT game_name, rom_name, size, crc, md5, sha1 "
                           "FROM dat_records WHERE source_id = ?"));
  q.addBindValue(source.id);
  if (!q.exec()) {
    qWarning("DatCache: forEachRecord query failed for source %lld: %s", source.id,
             qPrintable(q.lastError().text()));
    return false;
  }
  while (q.next()) {
    DatLookup::DatRecord r;
    r.gameName = q.value(0).toString();
    r.romName = q.value(1).toString();
    r.size = q.value(2).toLongLong();
    r.crc = q.value(3).toString();
    r.md5 = q.value(4).toString();
    r.sha1 = q.value(5).toString();
    callback(r);
  }
  return true;
}

void Store::clearAll() {
  if (!isOpen()) return;
  execSimple(m_db, QStringLiteral("DELETE FROM dat_records"));
  execSimple(m_db, QStringLiteral("DELETE FROM dat_sources"));
}

QString defaultPath() {
  const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cacheDir.isEmpty()) {
    // Fallback for headless / sandbox setups where CacheLocation is empty. Use
    // an absolute temp path: with WAL the -wal/-shm sidecars are written next to
    // the db, and a bare relative name would resolve against a CWD that changes
    // when Kartend launches players via QProcess (Kartend-i7qd). The cache won't
    // persist, but the rest of Kartend keeps working.
    return QDir::temp().filePath(QStringLiteral("datcache.sqlite"));
  }
  QDir().mkpath(cacheDir);
  return QDir(cacheDir).filePath(QStringLiteral("datcache.sqlite"));
}

} // namespace DatCache
