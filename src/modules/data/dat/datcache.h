#ifndef DATCACHE_H
#define DATCACHE_H

#include <functional>
#include <optional>
#include <QSqlDatabase>
#include <QString>

#include "datlookup.h"
#include "errorutils.h"

/// On-disk sqlite cache for parsed DAT files.
///
/// The XML re-parse on cold start is the only real cost in the v1
/// DatLookup path — for No-Intro / Redump sized catalogues (~5–20k
/// entries) it's sub-second and not worth optimising. But MAME's
/// full listxml is ~100MB / ~250k entries and takes multiple seconds
/// to re-parse on every Kartend launch. This module persists the
/// parsed records to a sqlite file under the user's XDG cache dir
/// once, keyed by `(absolute path, mtime)`, and serves subsequent
/// lookups via indexed SELECTs — startup becomes effectively instant
/// regardless of DAT size.
///
/// Schema:
///   dat_sources(id, path UNIQUE, mtime_unix_ms, dialect, record_count,
///               ingested_at_unix_ms)
///   dat_records(source_id FK, game_name, rom_name, size, crc, md5, sha1)
///   indexes on (source_id, sha1|md5|crc) for hash-lookup speed.
///
/// Cache invalidation: when openOrIngest sees a DAT whose mtime no
/// longer matches the cached row, it deletes that source's records
/// and re-ingests from XML. So an edited DAT picks up automatically
/// — no manual cache flush required.
namespace DatCache {

/// Handle to a cached DAT source. Returned by `openOrIngest` and
/// consumed by `lookup`. Treat as opaque — the `id` field keys all
/// SQL queries; the other fields are exposed for UI labels
/// ("Loaded N entries from MAME listxml") and for the DAT-to-collection
/// matcher (headerName/headerDescription identify what the catalogue
/// covers; all three header fields may be empty — `<header>` is
/// optional in Logiqx and MAME listxml only carries a build version).
struct CachedSource {
  qint64 id = -1;
  qint64 mtimeUnixMs = 0;
  DatLookup::Dialect dialect = DatLookup::Dialect::Unknown;
  int recordCount = 0;
  QString headerName;
  QString headerDescription;
  QString headerVersion;

  [[nodiscard]] bool isValid() const { return id >= 0; }
};

/// Owns a sqlite connection backing the DAT cache. One instance per
/// process is the typical usage — multiple instances pointing at the
/// same file would each open their own connection, which is fine
/// (sqlite serialises writes inside the process) but redundant.
class Store {
public:
  /// Opens (creating if missing) the cache DB at `dbPath`. Schema
  /// is migrated on first use. Pass `defaultPath()` for the standard
  /// XDG cache location; tests pass a `QTemporaryDir`-based path.
  /// The connection-name registry inside QSqlDatabase is global, so
  /// every Store instance reserves a unique name to avoid collisions
  /// when multiple instances coexist (tests).
  explicit Store(const QString &dbPath);
  ~Store();

  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;

  /// True when the underlying sqlite connection opened successfully
  /// and the schema is in place. Callers should treat a non-open
  /// Store as a transient failure and degrade to filename-only
  /// scraping rather than hard-failing the user's scrape.
  [[nodiscard]] bool isOpen() const;

  /// Ensure `datPath` is ingested into the cache, then return a
  /// handle suitable for `lookup()`. Behaviour:
  ///   - Cache hit (path + mtime match): single SELECT, fast.
  ///   - Cache miss: parse DAT via DatLookup, bulk-insert into the
  ///     cache, return the new source. Slow on first ingest (the
  ///     one-time cost the cache exists to amortise).
  ///   - Stale entry (path matches, mtime differs): drop old rows,
  ///     re-ingest.
  ///   - Empty / missing / unparseable DAT: returns an error result
  ///     with the diagnosis (matches DatLookup's failure modes).
  [[nodiscard]] ErrorUtils::Result<CachedSource> openOrIngest(const QString &datPath);

  /// Read-only probe: the cached row for `datPath`, or nullopt when the
  /// cache has never ingested it (or the store isn't open). Never parses —
  /// stays cheap on a 100MB listxml — so unlike `openOrIngest` it can sit on
  /// a UI-thread attach path. The row is returned even when the file's mtime
  /// has moved on; `mtimeUnixMs` lets the caller present that as staleness.
  [[nodiscard]] std::optional<CachedSource> peek(const QString &datPath) const;

  /// Hash lookup against a cached source. Tries sha1 → md5 → crc in
  /// that order (most-to-least reliable), matching DatLookup::Store
  /// semantics. Empty hashes are skipped. Returns nullopt on miss
  /// or invalid source.
  [[nodiscard]] std::optional<DatLookup::DatRecord> lookup(const CachedSource &source,
                                                           const QString &md5, const QString &sha1,
                                                           const QString &crc) const;

  /// Stream every record belonging to `source` to `callback`, in
  /// insertion (DAT) order. Exposed for auditing: computing the
  /// "missing" set means walking the whole catalogue, which the
  /// hash-keyed `lookup()` can't do. Streams row-by-row off the
  /// sqlite forward cursor so a 250k-entry MAME source never
  /// materialises as one giant QList. Returns false on an invalid
  /// source or a query failure (callback simply isn't invoked for the
  /// missing rows); true otherwise, including the empty-source case.
  bool forEachRecord(const CachedSource &source,
                     const std::function<void(const DatLookup::DatRecord &)> &callback) const;

  /// All records of one game within `source`, in DAT order (Kartend-34lab).
  /// Backed by the (source_id, game_name) index so the audit browser's ROM
  /// detail pane can fetch just the selected game's roms — a handful of rows —
  /// without streaming the whole (250k-entry) source. Returns an empty list on
  /// an invalid source / query failure / unknown game.
  [[nodiscard]] QList<DatLookup::DatRecord> recordsForGame(const CachedSource &source,
                                                           const QString &gameName) const;

  /// Drop every cached source + record. Used by the "clear caches"
  /// UI and by tests between runs.
  void clearAll();

private:
  QString m_connectionName;
  QSqlDatabase m_db;
};

/// Default cache file path under `QStandardPaths::CacheLocation`
/// (e.g. `~/.cache/kartend/datcache.sqlite` on Linux). Creates the
/// parent directory if absent so callers can pass the result
/// straight into the `Store` constructor.
[[nodiscard]] QString defaultPath();

} // namespace DatCache

#endif // DATCACHE_H
