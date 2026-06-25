#ifndef DATAUDITPROFILESTORE_H
#define DATAUDITPROFILESTORE_H

// Stage 2 of the DatAuditDialog decomposition (Kartend-ahf3d): the SQLite
// access for DAT-audit profiles, their result snapshots, and DAT-library
// provenance — extracted off the QDialog into a small store.
//
// The dialog (and DatAuditDownloadService, via its ProvenanceAccess seam) call
// these typed methods instead of opening a transient QSqlDatabase themselves, so
// the dialog no longer touches QSqlDatabase / databaseschema.h for profile
// persistence. Each call opens, uses, and removes a short-lived connection to
// the app DB (where the dat_audit_profile* / dat_library_state tables live);
// profile CRUD is light + occasional, so per-call connections avoid lifetime
// juggling and keep the store stateless and trivially testable against a real
// SQLite file (no DB mocking — tests point m_appDataDir at a sandbox dir).
//
// The typed methods return the underlying DatAuditProfile:: / DatLibraryState::
// Result unchanged, so callers keep their existing error handling (the dialog's
// QMessageBox / logError stays in the view layer). A method returns a
// connection-error Result when the app DB can't be opened.

#include <functional>
#include <optional>

#include <QList>
#include <QString>

#include "datauditprofile.h"
#include "datlibrarystate.h"
#include "errorutils.h"

class QSqlDatabase;

class DatAuditProfileStore {
public:
  /// @param appDataDir directory holding the app DB. Empty (the default) uses
  /// QStandardPaths::AppDataLocation — the production path. Tests pass a
  /// sandboxed directory so the store hits a real, isolated SQLite file.
  explicit DatAuditProfileStore(QString appDataDir = QString());

  // ── Profile CRUD ────────────────────────────────────────────────────────
  [[nodiscard]] ErrorUtils::Result<QList<DatAuditProfile::Profile>> listProfiles();
  [[nodiscard]] ErrorUtils::Result<std::optional<DatAuditProfile::Profile>> loadProfile(qint64 id);
  [[nodiscard]] ErrorUtils::Result<std::optional<DatAuditProfile::Profile>>
  loadProfileByCollectionUuid(const QString &collectionUuid);
  /// Inserts a new profile, returning its assigned id.
  [[nodiscard]] ErrorUtils::Result<qint64> insertProfile(const DatAuditProfile::Profile &p);
  [[nodiscard]] ErrorUtils::Result<bool> updateProfile(const DatAuditProfile::Profile &p);
  [[nodiscard]] ErrorUtils::Result<bool> removeProfile(qint64 id);

  // ── Result snapshot (audit-finish persist + browser/fix read) ───────────
  [[nodiscard]] ErrorUtils::Result<bool> touchLastScan(qint64 id, qint64 whenMs);
  [[nodiscard]] ErrorUtils::Result<bool>
  replaceResults(qint64 id, const QList<DatAuditProfile::ResultRow> &rows);
  [[nodiscard]] ErrorUtils::Result<QList<DatAuditProfile::ResultRow>>
  loadProfileResultRows(qint64 id);

  // ── DAT-library provenance (DatAuditDownloadService::ProvenanceAccess) ───
  /// Returns the tracked provenance list; empty (and logs) on read error, to
  /// match the ProvenanceAccess::loadAll seam's value-returning signature.
  [[nodiscard]] QList<DatLibraryState::Provenance> loadAllProvenance();
  /// Records one provenance row; logs on error (void to match
  /// ProvenanceAccess::record).
  void recordProvenance(const DatLibraryState::Provenance &p);

private:
  /// Opens a transient SQLite connection to the app DB, runs @p fn, then closes
  /// and removes it. Returns false (without calling fn) when the connection
  /// can't be opened. This is the former DatAuditDialog::withProfileDb, moved.
  bool withConnection(const std::function<void(QSqlDatabase &)> &fn);

  QString m_appDataDir;
};

#endif // DATAUDITPROFILESTORE_H
