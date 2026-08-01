#ifndef CONNECTION_PRAGMAS_H
#define CONNECTION_PRAGMAS_H

#include <QString>

QT_BEGIN_NAMESPACE
class QSqlDatabase;
QT_END_NAMESPACE

/// Kartend-67wo: single source of truth for the PRAGMA set applied to every
/// media.db SQLite connection (main thread, query worker, scan worker, and
/// playlist sub-connection). Before this lived as ad-hoc copies in
/// databaseschema.cpp, querymanagerconnection.cpp (initial open + reconnect),
/// and playlistmanager.cpp — adding a fourth connection or changing the
/// journal mode previously meant grepping for `PRAGMA journal_mode` and
/// editing every copy. Now there's one entry point.
namespace MediaDbConnectionInit {

/// Per-connection PRAGMA knobs. The busy_timeout differs between the main
/// thread (short — main thread can't block long) and worker connections
/// (longer — workers compete with the FTS rebuild etc.). foreign_keys + WAL
/// + synchronous=NORMAL are non-negotiable for every connection.
struct PragmaConfig {
  int busyTimeoutMs = 0;
  /// Apply PRAGMA synchronous=NORMAL after WAL. Worker connections need
  /// this for write throughput; the legacy main-thread opener didn't set
  /// it, so default false to match prior behaviour and let callers opt
  /// in. Pass true for worker / scan-worker connections.
  bool setSynchronousNormal = false;
  /// Attempt PRAGMA journal_mode=WAL with a fallback to DELETE when WAL
  /// fails (typically on a network filesystem that can't honour shared
  /// memory). Default true for media.db; tiny side-databases like
  /// playlistmanager.db override.
  bool enableWalWithFallback = true;
  /// PRAGMA foreign_keys=ON. Default true; opt-out is here only so the
  /// playlist mini-connection (which doesn't define any foreign keys)
  /// can skip it cleanly.
  bool enableForeignKeys = true;
};

/// Apply @p cfg to @p db. Warnings are logged through ErrorUtils with
/// @p loggingContext as the "from" tag; no failure is fatal — the caller
/// gets back a still-usable connection even if (e.g.) WAL falls back to
/// DELETE.
void applyPragmas(QSqlDatabase &db, const PragmaConfig &cfg, const QString &loggingContext);

/// Outcome of ensureNotCorrupt().
struct CorruptionRecovery {
  /// The corruption probe failed with a file-is-bad code (SQLITE_NOTADB /
  /// SQLITE_CORRUPT). false covers both a healthy file and transient
  /// non-corruption failures (busy/locked), which are left alone.
  bool corrupt = false;
  /// The file was quarantined and the reopened connection probes healthy.
  bool recovered = false;
  /// THIS call performed the rename — the caller should surface the
  /// one-time user-visible announcement (rescan required, history lost).
  /// Concurrent connections that find the file already renamed adopt the
  /// fresh database silently, so exactly one announcement fires per
  /// corruption incident.
  bool announce = false;
  /// Where the damaged file was preserved (media.db.corrupt-<timestamp>).
  QString quarantinePath;
};

/// Kartend-kcakv: probe @p db (freshly open()ed) for the corrupt /
/// not-a-database state that a lazy sqlite3_open hides. On a healthy or
/// merely-busy connection this is one cheap sqlite_master read and a no-op.
/// On corruption: close, rename the file (plus -wal/-shm sidecars) to
/// <name>.corrupt-<timestamp> — rename, never delete, the bytes may be
/// hand-salvageable — reopen, and re-probe, so the caller's subsequent
/// schema creation runs against a fresh usable database instead of logging
/// failures forever against a dead one.
CorruptionRecovery ensureNotCorrupt(QSqlDatabase &db, const QString &loggingContext);

} // namespace MediaDbConnectionInit

#endif // CONNECTION_PRAGMAS_H
