#include "datlibrarystate.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatLibraryState {

ErrorUtils::Result<QSet<QString>> loadDismissalKeys(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatLibraryState::loadDismissalKeys");
  }
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("SELECT path, mtime_unix_ms FROM dat_library_dismissal"))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load dismissals",
                               "DatLibraryState::loadDismissalKeys")
        .withDetails(q.lastError().text());
  }
  QSet<QString> keys;
  while (q.next()) {
    keys.insert(dismissalKey(q.value(0).toString(), q.value(1).toLongLong()));
  }
  return keys;
}

ErrorUtils::Result<bool> addDismissal(QSqlDatabase &db, const QString &canonicalPath,
                                      qint64 mtimeMs) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatLibraryState::addDismissal");
  }
  if (canonicalPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty path",
                               "DatLibraryState::addDismissal");
  }
  QSqlQuery q(db);
  // OR REPLACE: re-dismissing an updated DAT moves its mtime forward, which
  // is exactly the intended "ask once per catalogue revision" semantic.
  q.prepare(QStringLiteral("INSERT OR REPLACE INTO dat_library_dismissal "
                           "(path, mtime_unix_ms, dismissed_at_unix_ms) VALUES (?, ?, ?)"));
  q.addBindValue(canonicalPath);
  q.addBindValue(mtimeMs);
  q.addBindValue(QDateTime::currentMSecsSinceEpoch());
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to record dismissal",
                               "DatLibraryState::addDismissal")
        .withDetails(q.lastError().text());
  }
  return true;
}

namespace {
Provenance readProvenanceRow(const QSqlQuery &q) {
  Provenance p;
  p.canonicalPath = q.value(0).toString();
  p.source = q.value(1).toString();
  p.slug = q.value(2).toString();
  p.systemId = q.value(3).toInt();
  p.version = q.value(4).toString();
  p.updatedAtMs = q.value(5).toLongLong();
  return p;
}
constexpr char kProvenanceCols[] = "canonical_path, source, slug, system_id, version, "
                                   "updated_at_unix_ms";
} // namespace

ErrorUtils::Result<bool> recordProvenance(QSqlDatabase &db, const Provenance &p) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatLibraryState::recordProvenance");
  }
  if (p.canonicalPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Empty path",
                               "DatLibraryState::recordProvenance");
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral("INSERT OR REPLACE INTO dat_library_provenance "
                           "(canonical_path, source, slug, system_id, version, updated_at_unix_ms) "
                           "VALUES (?, ?, ?, ?, ?, ?)"));
  q.addBindValue(p.canonicalPath);
  q.addBindValue(p.source);
  q.addBindValue(p.slug);
  q.addBindValue(p.systemId);
  q.addBindValue(p.version);
  q.addBindValue(QDateTime::currentMSecsSinceEpoch());
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to record provenance",
                               "DatLibraryState::recordProvenance")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<std::optional<Provenance>> loadProvenance(QSqlDatabase &db,
                                                             const QString &path) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatLibraryState::loadProvenance");
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT %1 FROM dat_library_provenance WHERE canonical_path = ?")
                .arg(QLatin1String(kProvenanceCols)));
  q.addBindValue(path);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load provenance",
                               "DatLibraryState::loadProvenance")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return std::optional<Provenance>(std::nullopt);
  }
  return std::optional<Provenance>(readProvenanceRow(q));
}

ErrorUtils::Result<QList<Provenance>> loadAllProvenance(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatLibraryState::loadAllProvenance");
  }
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("SELECT %1 FROM dat_library_provenance")
                  .arg(QLatin1String(kProvenanceCols)))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load provenance",
                               "DatLibraryState::loadAllProvenance")
        .withDetails(q.lastError().text());
  }
  QList<Provenance> out;
  while (q.next()) {
    out.append(readProvenanceRow(q));
  }
  return out;
}

bool isUpdateAvailable(const QString &storedVersion, const QString &latestVersion) {
  if (storedVersion.isEmpty() || latestVersion.isEmpty()) {
    return false; // can't tell without both revisions
  }
  return storedVersion.trimmed() != latestVersion.trimmed();
}

QList<Provenance> outdatedAmong(const QList<Provenance> &all,
                                const std::function<QString(const Provenance &)> &fetchLatest) {
  QList<Provenance> outdated;
  if (!fetchLatest) {
    return outdated;
  }
  for (const Provenance &p : all) {
    if (p.source.isEmpty() || p.version.isEmpty()) {
      continue; // generic import / never-versioned — can't check
    }
    const QString latest = fetchLatest(p);
    if (isUpdateAvailable(p.version, latest)) {
      Provenance up = p;
      up.version = latest; // the revision a re-download will land
      outdated.append(up);
    }
  }
  return outdated;
}

} // namespace DatLibraryState
