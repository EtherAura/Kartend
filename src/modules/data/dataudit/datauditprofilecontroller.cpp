#include "datauditprofilecontroller.h"

#include <QDateTime>
#include <QFileInfo>

#include "datauditprofilestore.h"
#include "datcache.h"

using ErrorUtils::Result;

DatAuditProfileController::DatAuditProfileController(DatAuditProfileStore &store)
    : m_store(store) {}

void DatAuditProfileController::refreshDatRefMetadata(QList<DatAuditProfile::DatRef> &dats) {
  if (dats.isEmpty()) {
    return;
  }
  // The mtime comes from a stat; dialect/record count come from a cheap DatCache
  // peek — never an ingest, so attaching a huge never-parsed DAT stays instant
  // and its counts simply stay 0 until the first audit ingests it.
  DatCache::Store cache(DatCache::defaultPath());
  for (DatAuditProfile::DatRef &d : dats) {
    const QFileInfo fi(d.path);
    if (fi.isFile()) {
      d.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    }
    if (const auto src = cache.peek(d.path)) {
      d.dialect = static_cast<int>(src->dialect);
      d.recordCount = src->recordCount;
    }
  }
}

Result<qint64> DatAuditProfileController::persist(DatAuditProfile::Profile &p) {
  refreshDatRefMetadata(p.dats);
  if (p.id < 0) {
    auto res = m_store.insertProfile(p);
    if (res.isError()) {
      return res.error();
    }
    p.id = res.value();
    return p.id;
  }
  auto res = m_store.updateProfile(p);
  if (res.isError()) {
    return res.error();
  }
  return p.id;
}

Result<QList<DatAuditProfile::Profile>> DatAuditProfileController::list() {
  return m_store.listProfiles();
}

Result<std::optional<DatAuditProfile::Profile>> DatAuditProfileController::load(qint64 id) {
  return m_store.loadProfile(id);
}

Result<std::optional<DatAuditProfile::Profile>>
DatAuditProfileController::loadByCollectionUuid(const QString &collectionUuid) {
  return m_store.loadProfileByCollectionUuid(collectionUuid);
}

Result<bool> DatAuditProfileController::remove(qint64 id) {
  return m_store.removeProfile(id);
}

Result<bool> DatAuditProfileController::touchLastScan(qint64 id, qint64 whenMs) {
  return m_store.touchLastScan(id, whenMs);
}

Result<bool>
DatAuditProfileController::replaceResults(qint64 id,
                                          const QList<DatAuditProfile::ResultRow> &rows) {
  return m_store.replaceResults(id, rows);
}

Result<QList<DatAuditProfile::ResultRow>> DatAuditProfileController::loadResultRows(qint64 id) {
  return m_store.loadProfileResultRows(id);
}
