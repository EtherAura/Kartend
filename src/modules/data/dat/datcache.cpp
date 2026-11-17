// On-disk sqlite cache for parsed DAT files.
//
// The point of this module is to skip the XML re-parse on cold start
// for very large DATs (MAME listxml is ~100MB / ~250k entries). Hash
// lookups become indexed sqlite SELECTs; the cache invalidates on
// mtime change so an edited DAT reloads automatically.
#include "datcache.h"

#include "dbtxn.h"

#include <algorithm>

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
// v4: dat_records.mia + idx_dat_records_game (Kartend-34lab) — the wipe forces
//     a re-ingest that populates the MIA flag from the freshly re-parsed DATs.
// v5: dat_records.setid (Kartend-m6qsb.29) — the set identity cloneof points at
//     (MAME name= differs from game_name); the wipe re-ingests to populate it.
// v6: dat_machines (Kartend-tnt0l) — per-machine MAME listxml records; the
//     wipe re-ingests every cached MAME DAT so its machine rows get populated.
// v7: dat_machines.id + dat_machine_disks (Kartend-xu36n) — each machine row's
//     own <disk> list; the wipe re-ingests every MAME DAT to populate it.
constexpr int kSchemaVersion = 7;

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
                                    "  cloneof TEXT,"
                                    "  setid TEXT,"
                                    "  mia INTEGER NOT NULL DEFAULT 0,"
                                    "  FOREIGN KEY(source_id) REFERENCES dat_sources(id) "
                                    "    ON DELETE CASCADE)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_records_game "
                                    "ON dat_records(source_id, game_name)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_records_sha1 "
                                    "ON dat_records(source_id, sha1)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_records_md5 "
                                    "ON dat_records(source_id, md5)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_records_crc "
                                    "ON dat_records(source_id, crc)")) &&
      // One row per MAME machine, ROM-less ones included. set_name carries
      // COLLATE NOCASE so both the `=` lookup and the index below are
      // case-insensitive. No UNIQUE: a malformed listxml repeating a name must
      // not fail the whole ingest; the lookup takes the first row. `id` aliases
      // the rowid, so that order is unchanged and disks can point at one row.
      execSimple(db, QStringLiteral("CREATE TABLE IF NOT EXISTS dat_machines ("
                                    "  id INTEGER PRIMARY KEY,"
                                    "  source_id INTEGER NOT NULL,"
                                    "  set_name TEXT NOT NULL COLLATE NOCASE,"
                                    "  description TEXT NOT NULL DEFAULT '',"
                                    "  cloneof TEXT NOT NULL DEFAULT '',"
                                    "  romof TEXT NOT NULL DEFAULT '',"
                                    "  is_bios INTEGER NOT NULL DEFAULT 0,"
                                    "  is_device INTEGER NOT NULL DEFAULT 0,"
                                    "  is_mechanical INTEGER NOT NULL DEFAULT 0,"
                                    "  runnable INTEGER NOT NULL DEFAULT 1,"
                                    "  year TEXT NOT NULL DEFAULT '',"
                                    "  manufacturer TEXT NOT NULL DEFAULT '',"
                                    "  driver_status TEXT NOT NULL DEFAULT '',"
                                    "  players INTEGER NOT NULL DEFAULT -1,"
                                    "  rotate INTEGER NOT NULL DEFAULT -1,"
                                    "  FOREIGN KEY(source_id) REFERENCES dat_sources(id) "
                                    "    ON DELETE CASCADE)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_machines_set "
                                    "ON dat_machines(source_id, set_name)")) &&
      // A machine's dumped <disk> entries (Kartend-xu36n), keyed by that exact
      // machine row rather than by set name: a repeated name must not pool the
      // disks of every same-named machine. The cascade follows the machine row,
      // which itself follows its source.
      execSimple(db, QStringLiteral("CREATE TABLE IF NOT EXISTS dat_machine_disks ("
                                    "  machine_id INTEGER NOT NULL,"
                                    "  ordinal INTEGER NOT NULL,"
                                    "  name TEXT NOT NULL,"
                                    "  sha1 TEXT NOT NULL,"
                                    "  status TEXT NOT NULL DEFAULT '',"
                                    "  merge_name TEXT NOT NULL DEFAULT '',"
                                    "  FOREIGN KEY(machine_id) REFERENCES dat_machines(id) "
                                    "    ON DELETE CASCADE)")) &&
      execSimple(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_dat_machine_disks_machine "
                                    "ON dat_machine_disks(machine_id, ordinal)")) &&
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
    execSimple(db, QStringLiteral("DROP TABLE IF EXISTS dat_machine_disks"));
    execSimple(db, QStringLiteral("DROP TABLE IF EXISTS dat_machines"));
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

  // Either cache miss (no row) or stale (mtime mismatch). The stale-row
  // eviction now runs INSIDE the ingest transaction below (Kartend-4qqma) so
  // the DELETE + re-INSERT commit or roll back together — closing the crash
  // window between them and surfacing a real DELETE failure instead of masking
  // it behind a UNIQUE-constraint INSERT error.
  const bool stale = existing.id >= 0;

  // Read + parse the DAT via DatLookup. This is the slow path —
  // multi-second on a 100MB MAME XML, sub-second on a 5MB No-Intro.
  // The whole point of this cache is to do this once per (path,
  // mtime) and serve subsequent lookups from sqlite.
  // readDatFile transparently unpacks a .zip-packed DAT (Kartend-m6qsb.28);
  // a plain DAT is read directly.
  const QByteArray bytes = DatLookup::readDatFile(datPath);
  if (bytes.isEmpty()) {
    return ErrorContext::error(ErrorCode::FileNotFound,
                               "Failed to read DAT file for ingest (unreadable, empty, oversized, "
                               "or a .zip with no .dat member / no archive tool)",
                               "DatCache::Store::openOrIngest")
        .withDetails(datPath);
  }
  // Detect the dialect once and thread it through probeHeader + parseDat
  // (Kartend-xcqfc) instead of letting each helper re-detect (and, for
  // clrmamepro, re-decode the whole buffer) on the slow ingest path.
  const DatLookup::Dialect dialect = DatLookup::detectDialect(bytes);
  // Header metadata rides along with the ingest so the matcher / UI can
  // identify the catalogue without re-touching the XML (Kartend-m6qsb.3).
  const DatLookup::DatHeader header = DatLookup::probeHeader(bytes, dialect);
  // MAME listxml also yields the per-machine records (Kartend-tnt0l); its
  // `records` half is exactly what parseDat returns, so the per-rom rows below
  // are unchanged. Every other dialect keeps the plain parseDat path.
  QList<DatLookup::DatRecord> records;
  QList<DatLookup::MameMachine> machines;
  if (dialect == DatLookup::Dialect::Mame) {
    auto parsed = DatLookup::parseMameListXmlWithMachines(bytes);
    if (parsed.isError()) {
      return parsed.error();
    }
    records = parsed.value().records;
    machines = parsed.value().machines;
  } else {
    auto parsed = DatLookup::parseDat(bytes, dialect);
    if (parsed.isError()) {
      return parsed.error();
    }
    records = parsed.value();
  }

  // Fail loud on the "detected dialect, zero records" contradiction
  // (Kartend-u4sdu): a DAT positively detected as a known dialect that yields
  // no records is a truncated download / empty <datafile> / corrupt file, not a
  // legitimately-empty catalogue. Committing it would cache a bogus "complete
  // but empty" verdict (sticky on path+mtime) and the audit would report wrong
  // Have/Miss results. A genuinely-empty, NON-detected file (Unknown dialect)
  // already errors out of parseDat above, so this only fires on the
  // detected-but-empty case.
  if (records.isEmpty() && dialect != DatLookup::Dialect::Unknown) {
    return ErrorContext::warning(ErrorCode::InvalidArgument,
                                 "DAT was detected as a known dialect but parsed to zero records "
                                 "(truncated download, empty, or corrupt) — refusing to cache an "
                                 "incomplete catalogue as a success",
                                 "DatCache::Store::openOrIngest")
        .withDetails(datPath);
  }

  // Bulk insert under a single transaction. Without batching, ingesting
  // 250k MAME rows would take tens of seconds (one fsync per row);
  // wrapped in a transaction it's well under a second. The stale-row eviction
  // (when re-ingesting an updated DAT) runs first inside this same transaction
  // (Kartend-4qqma).
  KartendDb::DbTransaction txn(m_db, "DatCache::Store::openOrIngest");
  if (!txn.active()) {
    return txn.beginError("Failed to begin ingest transaction", nullptr,
                          ErrorUtils::Severity::Error);
  }

  // Evict the stale source row first (within the transaction). The ON DELETE
  // CASCADE on dat_records.source_id sweeps its records. A failed DELETE aborts
  // the whole re-ingest (the guard dtor rolls back) and propagates the real
  // failure, instead of being swallowed by a qWarning and then masquerading as
  // a UNIQUE-constraint "Failed to insert DAT source row" error (Kartend-4qqma).
  if (stale) {
    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM dat_sources WHERE id = ?"));
    del.addBindValue(existing.id);
    if (!del.exec()) {
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                 "Failed to evict stale DAT source row before re-ingest",
                                 "DatCache::Store::openOrIngest")
          .withDetails(del.lastError().text());
    }
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
                               "size, crc, md5, sha1, cloneof, mia, setid) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
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
      // NULL for non-clones (nullable TEXT, like the hash columns); readback
      // turns NULL back into an empty cloneOf.
      ins.bindValue(7, r.cloneOf.isEmpty() ? QVariant() : QVariant(r.cloneOf));
      ins.bindValue(8, r.mia ? 1 : 0);
      ins.bindValue(9, r.setId); // set-id (Kartend-m6qsb.29); plain TEXT, never NULL
      if (!ins.exec()) {
        const QString err = ins.lastError().text();
        return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to insert DAT record",
                                   "DatCache::Store::openOrIngest")
            .withDetails(err);
      }
    }
  }

  // Machine rows share the source row's transaction and its ON DELETE CASCADE,
  // so a stale re-ingest or clearAll() drops them together with the per-rom
  // records (Kartend-tnt0l).
  if (!machines.isEmpty()) {
    auto text = [](const QString &s) { return s.isNull() ? QStringLiteral("") : s; };
    QSqlQuery ins(m_db);
    QSqlQuery diskIns(m_db);
    diskIns.prepare(QStringLiteral("INSERT INTO dat_machine_disks (machine_id, ordinal, name, "
                                   "sha1, status, merge_name) VALUES (?, ?, ?, ?, ?, ?)"));
    ins.prepare(QStringLiteral("INSERT INTO dat_machines (source_id, set_name, description, "
                               "cloneof, romof, is_bios, is_device, is_mechanical, runnable, "
                               "year, manufacturer, driver_status, players, rotate) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const DatLookup::MameMachine &m : machines) {
      ins.bindValue(0, newId);
      ins.bindValue(1, m.setId);
      ins.bindValue(2, text(m.description));
      ins.bindValue(3, text(m.cloneOf));
      ins.bindValue(4, text(m.romOf));
      ins.bindValue(5, m.isBios ? 1 : 0);
      ins.bindValue(6, m.isDevice ? 1 : 0);
      ins.bindValue(7, m.isMechanical ? 1 : 0);
      ins.bindValue(8, m.runnable ? 1 : 0);
      ins.bindValue(9, text(m.year));
      ins.bindValue(10, text(m.manufacturer));
      ins.bindValue(11, text(m.driverStatus));
      ins.bindValue(12, m.players);
      ins.bindValue(13, m.rotate);
      if (!ins.exec()) {
        const QString err = ins.lastError().text();
        return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                   "Failed to insert DAT machine record",
                                   "DatCache::Store::openOrIngest")
            .withDetails(err);
      }
      // This machine's own disks, tied to the row just inserted (Kartend-xu36n).
      const qint64 machineId = ins.lastInsertId().toLongLong();
      for (qsizetype i = 0; i < m.disks.size(); ++i) {
        const DatLookup::MameDisk &d = m.disks.at(i);
        diskIns.bindValue(0, machineId);
        diskIns.bindValue(1, static_cast<qint64>(i));
        diskIns.bindValue(2, d.name);
        diskIns.bindValue(3, d.sha1);
        diskIns.bindValue(4, text(d.status));
        diskIns.bindValue(5, text(d.merge));
        if (!diskIns.exec()) {
          const QString err = diskIns.lastError().text();
          return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                     "Failed to insert DAT machine disk record",
                                     "DatCache::Store::openOrIngest")
              .withDetails(err);
        }
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
  q.prepare(QStringLiteral("SELECT game_name, rom_name, size, crc, md5, sha1, cloneof, mia, setid "
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
  r.cloneOf = q.value(6).toString();
  r.mia = q.value(7).toInt() != 0;
  r.setId = q.value(8).toString();
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
  q.prepare(QStringLiteral("SELECT game_name, rom_name, size, crc, md5, sha1, cloneof, mia, setid "
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
    r.cloneOf = q.value(6).toString();
    r.mia = q.value(7).toInt() != 0;
    r.setId = q.value(8).toString();
    callback(r);
  }
  return true;
}

QList<DatLookup::DatRecord> Store::recordsForGame(const CachedSource &source,
                                                  const QString &gameName) const {
  QList<DatLookup::DatRecord> out;
  if (!isOpen() || !source.isValid()) return out;
  QSqlDatabase &db = const_cast<Store *>(this)->m_db;
  QSqlQuery q(db);
  // Indexed by (source_id, game_name) — a handful of rows, not the whole source.
  q.prepare(QStringLiteral("SELECT game_name, rom_name, size, crc, md5, sha1, cloneof, mia, setid "
                           "FROM dat_records WHERE source_id = ? AND game_name = ?"));
  q.addBindValue(source.id);
  q.addBindValue(gameName);
  if (!q.exec()) {
    qWarning("DatCache: recordsForGame query failed for source %lld: %s", source.id,
             qPrintable(q.lastError().text()));
    return out;
  }
  while (q.next()) {
    DatLookup::DatRecord r;
    r.gameName = q.value(0).toString();
    r.romName = q.value(1).toString();
    r.size = q.value(2).toLongLong();
    r.crc = q.value(3).toString();
    r.md5 = q.value(4).toString();
    r.sha1 = q.value(5).toString();
    r.cloneOf = q.value(6).toString();
    r.mia = q.value(7).toInt() != 0;
    r.setId = q.value(8).toString();
    out.append(r);
  }
  return out;
}

QList<CachedMachine> Store::machinesForSet(const QList<CachedSource> &sources,
                                           const QString &setName) const {
  QList<CachedMachine> out;
  if (!isOpen() || setName.isEmpty()) return out;
  QSqlDatabase &db = const_cast<Store *>(this)->m_db;
  QSqlQuery q(db);
  // Indexed by (source_id, set_name); the column's NOCASE collation makes `=`
  // case-insensitive. One query per caller-supplied source keeps the scope
  // explicit — no row from a source outside `sources` can be read.
  q.prepare(QStringLiteral("SELECT set_name, description, cloneof, romof, is_bios, is_device, "
                           "is_mechanical, runnable, year, manufacturer, driver_status, players, "
                           "rotate, id FROM dat_machines WHERE source_id = ? AND set_name = ? "
                           "ORDER BY rowid LIMIT 1"));
  // Disks of the one machine row selected above, by its id (Kartend-xu36n).
  QSqlQuery dq(db);
  dq.prepare(QStringLiteral("SELECT name, sha1, status, merge_name FROM dat_machine_disks "
                            "WHERE machine_id = ? ORDER BY ordinal"));
  for (const CachedSource &source : sources) {
    if (!source.isValid()) continue;
    q.bindValue(0, source.id);
    q.bindValue(1, setName);
    if (!q.exec()) {
      qWarning("DatCache: machinesForSet query failed for source %lld: %s", source.id,
               qPrintable(q.lastError().text()));
      return {};
    }
    if (!q.next()) continue;
    CachedMachine cm;
    cm.sourceId = source.id;
    DatLookup::MameMachine &m = cm.machine;
    m.setId = q.value(0).toString();
    m.description = q.value(1).toString();
    m.cloneOf = q.value(2).toString();
    m.romOf = q.value(3).toString();
    m.isBios = q.value(4).toInt() != 0;
    m.isDevice = q.value(5).toInt() != 0;
    m.isMechanical = q.value(6).toInt() != 0;
    m.runnable = q.value(7).toInt() != 0;
    m.year = q.value(8).toString();
    m.manufacturer = q.value(9).toString();
    m.driverStatus = q.value(10).toString();
    m.players = q.value(11).toInt();
    m.rotate = q.value(12).toInt();
    dq.bindValue(0, q.value(13).toLongLong());
    if (!dq.exec()) {
      qWarning("DatCache: machinesForSet disk query failed for source %lld: %s", source.id,
               qPrintable(dq.lastError().text()));
      return {};
    }
    while (dq.next()) {
      DatLookup::MameDisk d;
      d.name = dq.value(0).toString();
      d.sha1 = dq.value(1).toString();
      d.status = dq.value(2).toString();
      d.merge = dq.value(3).toString();
      m.disks.append(d);
    }
    out.append(cm);
  }
  return out;
}

bool Store::isHideableMachineSet(const QList<CachedSource> &sources, const QString &setName) const {
  // Only the explicit listxml flags decide; any supplied source carrying one is
  // enough. No set-name or hash heuristics.
  const QList<CachedMachine> machines = machinesForSet(sources, setName);
  return std::any_of(machines.cbegin(), machines.cend(), [](const CachedMachine &cm) {
    return cm.machine.isBios || cm.machine.isDevice || !cm.machine.runnable;
  });
}

void Store::clearAll() {
  if (!isOpen()) return;
  execSimple(m_db, QStringLiteral("DELETE FROM dat_machine_disks"));
  execSimple(m_db, QStringLiteral("DELETE FROM dat_machines"));
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
