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
/// (longer — workers compete with FTS backfill etc.). foreign_keys + WAL
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

} // namespace MediaDbConnectionInit

#endif // CONNECTION_PRAGMAS_H
