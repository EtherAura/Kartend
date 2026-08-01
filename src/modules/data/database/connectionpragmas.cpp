#include "connectionpragmas.h"

#include "errorutils.h"

#include <QDateTime>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

using namespace ErrorUtils;

namespace {
// SQLite primary result codes that mean the FILE is bad, as opposed to
// transient contention: 11 = SQLITE_CORRUPT, 26 = SQLITE_NOTADB. QSQLITE
// surfaces the numeric code as QSqlError::nativeErrorCode().
bool isCorruptionCode(const QString &nativeCode) {
  return nativeCode == QLatin1String("11") || nativeCode == QLatin1String("26");
}
} // namespace

namespace MediaDbConnectionInit {

void applyPragmas(QSqlDatabase &db, const PragmaConfig &cfg, const QString &loggingContext) {
  QSqlQuery query(db);

  if (cfg.enableForeignKeys) {
    if (!query.exec("PRAGMA foreign_keys = ON")) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to enable foreign keys", loggingContext)
                     .withDetails(query.lastError().text());
      ErrorUtils::logError(err);
    }
  }

  if (cfg.enableWalWithFallback) {
    if (!query.exec("PRAGMA journal_mode = WAL")) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to enable WAL mode, falling back to DELETE mode",
                                       loggingContext)
                     .withDetails(query.lastError().text());
      ErrorUtils::logError(err);
      if (!query.exec("PRAGMA journal_mode = DELETE")) {
        auto fallbackErr =
            ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                  "Failed to set DELETE journal mode", loggingContext)
                .withDetails(query.lastError().text());
        ErrorUtils::logError(fallbackErr);
      }
    }
  }

  if (cfg.busyTimeoutMs > 0) {
    const QString busyTimeoutPragma =
        QStringLiteral("PRAGMA busy_timeout = %1").arg(cfg.busyTimeoutMs);
    if (!query.exec(busyTimeoutPragma)) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed, "Failed to set busy timeout",
                                       loggingContext)
                     .withDetails(query.lastError().text());
      ErrorUtils::logError(err);
    }
  }

  if (cfg.setSynchronousNormal) {
    if (!query.exec("PRAGMA synchronous = NORMAL")) {
      auto err = ErrorContext::warning(ErrorCode::DatabaseQueryFailed,
                                       "Failed to set synchronous mode", loggingContext)
                     .withDetails(query.lastError().text());
      ErrorUtils::logError(err);
    }
  }
}

CorruptionRecovery ensureNotCorrupt(QSqlDatabase &db, const QString &loggingContext) {
  CorruptionRecovery out;

  QSqlQuery probe(db);
  if (probe.exec(QStringLiteral("SELECT count(*) FROM sqlite_master"))) {
    return out; // healthy — the overwhelmingly common path
  }
  if (!isCorruptionCode(probe.lastError().nativeErrorCode())) {
    // Busy / locked / anything transient: not our problem to fix here, and
    // definitely not a reason to rename the user's database.
    return out;
  }
  out.corrupt = true;

  const QString dbFile = db.databaseName();
  db.close();

  const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  out.quarantinePath = dbFile + QStringLiteral(".corrupt-") + stamp;

  bool renamedByUs = QFile::rename(dbFile, out.quarantinePath);
  if (!renamedByUs && !QFile::exists(dbFile)) {
    // A concurrent connection quarantined it between our probe and rename;
    // adopt the fresh file it is about to create (or created). No announce —
    // the winner announces.
    renamedByUs = false;
  } else if (!renamedByUs) {
    auto err =
        ErrorContext::critical(ErrorCode::DatabaseCorruptQuarantined,
                               "media.db is corrupt and could not be quarantined", loggingContext)
            .withDetails(QStringLiteral("Rename failed: %1 -> %2").arg(dbFile, out.quarantinePath));
    ErrorUtils::logError(err);
    (void)db.open(); // reopen the corrupt file so callers keep today's degraded state
    return out;
  }
  // WAL sidecars from the corrupt incarnation must not sit next to the
  // fresh file; move them alongside the quarantined main file.
  const QStringList sidecars{QStringLiteral("-wal"), QStringLiteral("-shm")};
  for (const QString &suffix : sidecars) {
    if (QFile::exists(dbFile + suffix)) {
      QFile::rename(dbFile + suffix, out.quarantinePath + suffix);
    }
  }

  if (!db.open()) {
    auto err = ErrorContext::critical(ErrorCode::DatabaseConnectionFailed,
                                      "Failed to reopen database after quarantine", loggingContext)
                   .withDetails(db.lastError().text());
    ErrorUtils::logError(err);
    out.announce = renamedByUs;
    return out;
  }

  QSqlQuery reprobe(db);
  out.recovered = reprobe.exec(QStringLiteral("SELECT count(*) FROM sqlite_master"));
  out.announce = renamedByUs;

  auto notice =
      ErrorContext::warning(ErrorCode::DatabaseCorruptQuarantined,
                            "media.db was corrupt; quarantined and recreated", loggingContext)
          .withDetails(QStringLiteral("Quarantined to: %1").arg(out.quarantinePath));
  ErrorUtils::logError(notice);
  return out;
}

} // namespace MediaDbConnectionInit
