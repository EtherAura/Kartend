#ifndef DATAUDITPROFILECONTROLLER_H
#define DATAUDITPROFILECONTROLLER_H

// Profile CRUD / persistence logic lifted off DatAuditAuditPage (Kartend-n1hpy.3),
// mirroring DatAuditRunController (run orchestration) and DatAuditProfileStore
// (raw SQLite access). The page kept the insert-vs-update persist DECISION and
// the pre-write DatRef metadata refresh fused into the QWidget, where they could
// only be exercised by constructing the widget. They live here now — a plain,
// widget-free, headlessly-testable facade over the shared DatAuditProfileStore.
// User-facing error reporting (QMessageBox) stays on the page; these methods
// return typed ErrorUtils::Result values for the caller to surface.
//
// Plain class, NOT a QObject: no signals/threads, so it needs no moc.

#include <optional>

#include <QList>
#include <QString>

#include "datauditprofile.h" // DatAuditProfile::Profile / DatRef / ResultRow
#include "errorutils.h"      // ErrorUtils::Result

class DatAuditProfileStore;

class DatAuditProfileController {
public:
  explicit DatAuditProfileController(DatAuditProfileStore &store);

  /// Re-stat + cheap cache-peek each DatRef (mtime / dialect / record count)
  /// before a write. Cheap: never an ingest, so attaching a huge never-parsed
  /// DAT stays instant (its counts stay 0 until the first audit ingests it).
  static void refreshDatRefMetadata(QList<DatAuditProfile::DatRef> &dats);

  /// Persist @p p: inserts when p.id < 0 (assigning the new id back into p),
  /// otherwise updates. Refreshes DatRef metadata first. Returns the new /
  /// existing id on success, or the store error — NO UI. The caller reports.
  [[nodiscard]] ErrorUtils::Result<qint64> persist(DatAuditProfile::Profile &p);

  // ── Profile CRUD reads / delete (forwarded to the store) ─────────────────
  [[nodiscard]] ErrorUtils::Result<QList<DatAuditProfile::Profile>> list();
  [[nodiscard]] ErrorUtils::Result<std::optional<DatAuditProfile::Profile>> load(qint64 id);
  [[nodiscard]] ErrorUtils::Result<std::optional<DatAuditProfile::Profile>>
  loadByCollectionUuid(const QString &collectionUuid);
  [[nodiscard]] ErrorUtils::Result<bool> remove(qint64 id);

  // ── Audit-result snapshot persistence for a profile (forwarded) ──────────
  [[nodiscard]] ErrorUtils::Result<bool> touchLastScan(qint64 id, qint64 whenMs);
  [[nodiscard]] ErrorUtils::Result<bool>
  replaceResults(qint64 id, const QList<DatAuditProfile::ResultRow> &rows);
  [[nodiscard]] ErrorUtils::Result<QList<DatAuditProfile::ResultRow>> loadResultRows(qint64 id);

private:
  DatAuditProfileStore &m_store;
};

#endif // DATAUDITPROFILECONTROLLER_H
