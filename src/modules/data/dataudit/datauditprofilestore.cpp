#include "datauditprofilestore.h"

#include <utility>

#include <QSqlDatabase>
#include <QStandardPaths>

#include "databaseschema.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;
using ErrorUtils::Result;

namespace {
// Shared connection-open-failure error for the typed methods (each seeds its
// Result with this, then overwrites it inside withConnection on success).
ErrorContext connError(const char *context) {
  return ErrorContext::error(ErrorCode::DatabaseConnectionFailed,
                             "Could not open the DAT-audit profile database", context);
}
} // namespace

DatAuditProfileStore::DatAuditProfileStore(QString appDataDir)
    : m_appDataDir(std::move(appDataDir)) {}

bool DatAuditProfileStore::withConnection(const std::function<void(QSqlDatabase &)> &fn) {
  // UI-thread-only counter, mirroring the former DatAuditDialog::withProfileDb:
  // profile CRUD is light + occasional, so a short-lived connection opened,
  // used, and removed per call avoids lifetime juggling.
  static int counter = 0;
  const QString conn = QStringLiteral("dataudit_profiles_%1").arg(counter++);
  bool opened = false;
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    const QString dir = m_appDataDir.isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            : m_appDataDir;
    if (DatabaseSchema::openConnection(db, dir)) {
      DatabaseSchema::applyConnectionPragmas(db);
      fn(db);
      opened = true;
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
  return opened;
}

Result<QList<DatAuditProfile::Profile>> DatAuditProfileStore::listProfiles() {
  Result<QList<DatAuditProfile::Profile>> out = connError("DatAuditProfileStore::listProfiles");
  withConnection([&out](QSqlDatabase &db) { out = DatAuditProfile::listAll(db); });
  return out;
}

Result<std::optional<DatAuditProfile::Profile>> DatAuditProfileStore::loadProfile(qint64 id) {
  Result<std::optional<DatAuditProfile::Profile>> out =
      connError("DatAuditProfileStore::loadProfile");
  withConnection([&out, id](QSqlDatabase &db) { out = DatAuditProfile::load(db, id); });
  return out;
}

Result<std::optional<DatAuditProfile::Profile>>
DatAuditProfileStore::loadProfileByCollectionUuid(const QString &collectionUuid) {
  Result<std::optional<DatAuditProfile::Profile>> out =
      connError("DatAuditProfileStore::loadProfileByCollectionUuid");
  withConnection([&out, &collectionUuid](QSqlDatabase &db) {
    out = DatAuditProfile::loadByCollectionUuid(db, collectionUuid);
  });
  return out;
}

Result<qint64> DatAuditProfileStore::insertProfile(const DatAuditProfile::Profile &p) {
  Result<qint64> out = connError("DatAuditProfileStore::insertProfile");
  withConnection([&out, &p](QSqlDatabase &db) { out = DatAuditProfile::insert(db, p); });
  return out;
}

Result<bool> DatAuditProfileStore::updateProfile(const DatAuditProfile::Profile &p) {
  Result<bool> out = connError("DatAuditProfileStore::updateProfile");
  withConnection([&out, &p](QSqlDatabase &db) { out = DatAuditProfile::update(db, p); });
  return out;
}

Result<bool> DatAuditProfileStore::removeProfile(qint64 id) {
  Result<bool> out = connError("DatAuditProfileStore::removeProfile");
  withConnection([&out, id](QSqlDatabase &db) { out = DatAuditProfile::remove(db, id); });
  return out;
}

Result<bool> DatAuditProfileStore::touchLastScan(qint64 id, qint64 whenMs) {
  Result<bool> out = connError("DatAuditProfileStore::touchLastScan");
  withConnection([&out, id, whenMs](QSqlDatabase &db) {
    out = DatAuditProfile::touchLastScan(db, id, whenMs);
  });
  return out;
}

Result<bool> DatAuditProfileStore::replaceResults(qint64 id,
                                                  const QList<DatAuditProfile::ResultRow> &rows) {
  Result<bool> out = connError("DatAuditProfileStore::replaceResults");
  withConnection(
      [&out, id, &rows](QSqlDatabase &db) { out = DatAuditProfile::replaceResults(db, id, rows); });
  return out;
}

Result<QList<DatAuditProfile::ResultRow>> DatAuditProfileStore::loadProfileResultRows(qint64 id) {
  Result<QList<DatAuditProfile::ResultRow>> out =
      connError("DatAuditProfileStore::loadProfileResultRows");
  withConnection(
      [&out, id](QSqlDatabase &db) { out = DatAuditProfile::loadProfileResultRows(db, id); });
  return out;
}

QList<DatLibraryState::Provenance> DatAuditProfileStore::loadAllProvenance() {
  QList<DatLibraryState::Provenance> all;
  withConnection([&all](QSqlDatabase &db) {
    if (auto r = DatLibraryState::loadAllProvenance(db); r.isOk()) {
      all = r.value();
    } else {
      ErrorUtils::logError(r.error());
    }
  });
  return all;
}

void DatAuditProfileStore::recordProvenance(const DatLibraryState::Provenance &p) {
  withConnection([&p](QSqlDatabase &db) {
    if (auto res = DatLibraryState::recordProvenance(db, p); res.isError()) {
      ErrorUtils::logError(res.error());
    }
  });
}
