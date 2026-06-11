#ifndef KARTEND_UTILS_DB_DBTXN_H
#define KARTEND_UTILS_DB_DBTXN_H

// Shared RAII transaction guard (Kartend-3ibqt). Promoted from
// QueryManagerInternal::DbTransaction (querymanagerhelpers.h, Kartend-gv7f)
// so every DB-writing file shares one guard instead of hand-rolling the
// begin/commit/rollback + DatabaseTransactionFailed error block — the same
// 5-line shape recurred across ~9 files in three flavors (log-only free
// helpers, log+emit QObject managers, and Result-returning store functions).
// The transaction mechanics are unchanged from the original; the *OrReport /
// beginError/commitError additions collapse the per-site error plumbing.

#include <functional>

#include <QSqlDatabase>
#include <QSqlError>
#include <QString>

#include "errorutils.h"

namespace KartendDb {

// Depth-tracking transaction guard for a single QSqlDatabase connection.
// SQLite has no nested BEGIN: a second transaction() silently fails, and a
// nested commit() would close the *outer* transaction early — so a cache build
// that calls helper methods which each BEGIN/COMMIT on their own was silently
// non-atomic, and a failed BEGIN/commit went unnoticed (Kartend-gv7f). Only the
// outermost guard issues transaction()/commit()/rollback(); nested guards defer
// to it. The depth counter is owned by the connection's manager and shared by
// reference so every transaction site on that connection coordinates. Callers
// without nesting (single-shot store functions) use the depth-less ctor, which
// binds an internal zero counter and is therefore always outermost.
//
// Usage (bool-returning, log-only or log+emit):
//   KartendDb::DbTransaction txn(m_db, m_txnDepth, "QueryManager::buildCache",
//                                [this](const auto &e) { emit errorOccurred(e); });
//   if (!txn.activeOrReport("Failed to begin transaction to build cache"))
//     return false;                    // error built + logged + sunk
//   ... do work; on any error just `return false` (no manual rollback) ...
//   return txn.commitOrReport("Failed to commit cache");
//
// Usage (Result-returning store functions — build, don't log):
//   KartendDb::DbTransaction txn(db);
//   if (!txn.active())
//     return txn.beginError("Failed to begin transaction", "DatAuditProfile::insert");
//   ...
//   if (!txn.commit())
//     return txn.commitError("Failed to commit profile", "DatAuditProfile::insert");
//
// A guard destroyed without a successful commit() rolls back (only when it owns
// the transaction), so every early-return error path is covered automatically —
// including after a FAILED commit(): m_committed stays false but the dtor's
// rollback on an already-aborted transaction is a harmless no-op at the SQLite
// level, and no second commit is ever attempted.
class DbTransaction {
public:
  using ErrorSink = std::function<void(const ErrorUtils::ErrorContext &)>;

  DbTransaction(QSqlDatabase &db, int &depth, const char *origin = nullptr, ErrorSink sink = {})
      : m_db(db), m_depth(depth), m_origin(origin), m_sink(std::move(sink)) {
    m_outermost = (m_depth == 0);
    if (m_outermost) {
      m_began = m_db.transaction();
    }
    ++m_depth;
  }

  /// Depth-less overload for single-shot callers with no nesting on their
  /// connection (store functions opening their own connection, one-off
  /// writers): binds an internal counter, so this guard is always outermost.
  explicit DbTransaction(QSqlDatabase &db, const char *origin = nullptr, ErrorSink sink = {})
      : DbTransaction(db, m_localDepth, origin, std::move(sink)) {}

  DbTransaction(const DbTransaction &) = delete;
  auto operator=(const DbTransaction &) -> DbTransaction & = delete;
  DbTransaction(DbTransaction &&) = delete;
  auto operator=(DbTransaction &&) -> DbTransaction & = delete;

  ~DbTransaction() {
    if (m_depth > 0) {
      --m_depth;
    }
    if (m_outermost && m_began && !m_committed) {
      m_db.rollback();
    }
  }

  // False only when this guard owns the transaction and BEGIN failed; the
  // caller should bail. Nested guards always return true (the outer guard owns
  // the live transaction).
  [[nodiscard]] bool active() const { return m_outermost ? m_began : true; }

  // Commit the transaction. Only the outermost guard commits for real; nested
  // guards defer to it and report success. Returns false if the real commit
  // failed or the outermost BEGIN never succeeded.
  bool commit() {
    if (!m_outermost) {
      return true;
    }
    if (!m_began) {
      return false;
    }
    m_committed = m_db.commit();
    return m_committed;
  }

  // ── Reporting conveniences (Kartend-3ibqt) ────────────────────────────────
  // @p sev lets each converted site preserve its pre-existing severity
  // exactly (the hand-rolled blocks varied: warning in the query-cache
  // builders, error in the profile store, critical in the scan pipeline).

  /// Builds the standard DatabaseTransactionFailed ErrorContext for a failed
  /// BEGIN. Details capture m_db.lastError() — call before any other DB work
  /// disturbs it. Per-call origin override for Result-flavor callers.
  [[nodiscard]] ErrorUtils::ErrorContext
  beginError(const QString &what, const char *origin = nullptr,
             ErrorUtils::Severity sev = ErrorUtils::Severity::Critical) const {
    return makeError(what, origin, sev);
  }

  /// Same, for a failed COMMIT.
  [[nodiscard]] ErrorUtils::ErrorContext
  commitError(const QString &what, const char *origin = nullptr,
              ErrorUtils::Severity sev = ErrorUtils::Severity::Critical) const {
    return makeError(what, origin, sev);
  }

  /// active() + on-failure reporting: builds beginError(what, …, sev), logs
  /// it, and feeds the sink (when set — managers pass an
  /// emit-errorOccurred lambda).
  [[nodiscard]] bool activeOrReport(const QString &what,
                                    ErrorUtils::Severity sev = ErrorUtils::Severity::Critical) {
    if (active()) {
      return true;
    }
    report(beginError(what, nullptr, sev));
    return false;
  }

  /// commit() + on-failure reporting, mirroring activeOrReport().
  bool commitOrReport(const QString &what,
                      ErrorUtils::Severity sev = ErrorUtils::Severity::Critical) {
    if (commit()) {
      return true;
    }
    report(commitError(what, nullptr, sev));
    return false;
  }

private:
  [[nodiscard]] ErrorUtils::ErrorContext makeError(const QString &what, const char *origin,
                                                   ErrorUtils::Severity sev) const {
    auto err = ErrorUtils::ErrorContext::critical(
                   ErrorUtils::ErrorCode::DatabaseTransactionFailed, what,
                   origin ? origin : (m_origin ? m_origin : "KartendDb::DbTransaction"))
                   .withDetails(m_db.lastError().text());
    err.severity = sev;
    return err;
  }

  void report(const ErrorUtils::ErrorContext &err) {
    ErrorUtils::logError(err);
    if (m_sink) {
      m_sink(err);
    }
  }

  QSqlDatabase &m_db;
  int m_localDepth = 0; // backing store for the depth-less ctor only
  int &m_depth;
  const char *m_origin = nullptr;
  ErrorSink m_sink;
  bool m_outermost = false;
  bool m_began = false;
  bool m_committed = false;
};

/// True when @p err carries an SQLITE_BUSY ("database is locked") /
/// SQLITE_LOCKED ("database table is locked") failure — the transient
/// contention class that bounded-retry callers (QueryManager::runWrite,
/// Kartend-cbtml) re-attempt instead of dropping. Store functions fold
/// QSqlError::text() into ErrorContext::details, and SQLite's result-code
/// strings are not localized, so the text match is stable for QSQLITE
/// (same fallback rationale as the scanservice retry ladder, Kartend-5s54).
[[nodiscard]] inline bool isLockContentionError(const ErrorUtils::ErrorContext &err) {
  return err.details.contains(QLatin1String("locked"), Qt::CaseInsensitive);
}

} // namespace KartendDb

#endif // KARTEND_UTILS_DB_DBTXN_H
