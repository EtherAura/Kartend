#include "datauditprofile.h"

#include "dbtxn.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace DatAuditProfile {

namespace {

// A null QString binds as SQL NULL under QSQLITE, which the TEXT NOT NULL
// columns reject — and "no value" strings arrive null routinely (a cleared
// field, the "(none)" combo entry's QString() item data). Normalise to the
// empty string the schema's DEFAULT '' expects.
QString nonNull(const QString &s) {
  return s.isNull() ? QStringLiteral("") : s;
}

QString stringListToJson(const QStringList &list) {
  QJsonArray arr;
  for (const QString &s : list) {
    arr.append(s);
  }
  return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QStringList jsonToStringList(const QString &json) {
  if (json.isEmpty()) {
    return {};
  }
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
  if (!doc.isArray()) {
    return {};
  }
  const QJsonArray arr = doc.array();
  QStringList out;
  out.reserve(arr.size());
  for (const auto &v : arr) {
    if (v.isString()) {
      out.append(v.toString());
    }
  }
  return out;
}

// Build a Profile from the current row of a SELECT over dat_audit_profile's
// columns in the canonical order used by load()/listAll(). Does not populate
// `dats` — the caller fills those via loadDats().
Profile readProfileRow(const QSqlQuery &q) {
  Profile p;
  p.id = q.value(0).toLongLong();
  p.name = q.value(1).toString();
  p.collectionUuid = q.value(2).toString();
  p.scanRoots = jsonToStringList(q.value(3).toString());
  p.regionPrefs = jsonToStringList(q.value(4).toString());
  p.onePerGame = q.value(5).toInt() != 0;
  p.ignoreRules = jsonToStringList(q.value(6).toString());
  p.fixMode = fixModeFromString(q.value(7).toString());
  p.managedOutputRoot = q.value(8).toString();
  p.detectedLayout = q.value(9).toString();
  p.layoutConfirmed = q.value(10).toInt() != 0;
  p.lastScanAtMs = q.value(11).toLongLong();
  p.createdAtMs = q.value(12).toLongLong();
  p.updatedAtMs = q.value(13).toLongLong();
  p.quarantineRoot = q.value(14).toString();
  return p;
}

constexpr char kSelectColumns[] =
    "id, name, collection_uuid, scan_roots, region_prefs, one_per_game, "
    "ignore_rules, fix_mode, managed_output_root, detected_layout, layout_confirmed, "
    "last_scan_at_unix_ms, created_at_unix_ms, updated_at_unix_ms, quarantine_root";

// Load the ordered DAT child rows for one profile into p.dats.
bool loadDats(QSqlDatabase &db, Profile &p) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT dat_path, dat_mtime_unix_ms, dialect, record_count "
                           "FROM dat_audit_profile_dat WHERE profile_id = ? ORDER BY position"));
  q.addBindValue(p.id);
  if (!q.exec()) {
    return false;
  }
  while (q.next()) {
    DatRef d;
    d.path = q.value(0).toString();
    d.mtimeMs = q.value(1).toLongLong();
    d.dialect = q.value(2).toInt();
    d.recordCount = q.value(3).toInt();
    p.dats.append(d);
  }
  return true;
}

// Replace every DAT child row for `profileId` with `dats` (positions 0..N-1).
// Runs inside the caller's transaction; returns the SQL error text via errOut
// on failure.
bool writeDats(QSqlDatabase &db, qint64 profileId, const QList<DatRef> &dats, QString &errOut) {
  {
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM dat_audit_profile_dat WHERE profile_id = ?"));
    del.addBindValue(profileId);
    if (!del.exec()) {
      errOut = del.lastError().text();
      return false;
    }
  }
  QSqlQuery ins(db);
  ins.prepare(QStringLiteral("INSERT INTO dat_audit_profile_dat "
                             "(profile_id, position, dat_path, dat_mtime_unix_ms, dialect, "
                             "record_count) VALUES (?, ?, ?, ?, ?, ?)"));
  for (int i = 0; i < dats.size(); ++i) {
    const DatRef &d = dats.at(i);
    ins.bindValue(0, profileId);
    ins.bindValue(1, i);
    ins.bindValue(2, d.path);
    ins.bindValue(3, d.mtimeMs);
    ins.bindValue(4, d.dialect);
    ins.bindValue(5, d.recordCount);
    if (!ins.exec()) {
      errOut = ins.lastError().text();
      return false;
    }
  }
  return true;
}

} // namespace

QString fixModeToString(FixMode m) {
  return m == FixMode::ManagedOutput ? QStringLiteral("managed_output")
                                     : QStringLiteral("in_place");
}

FixMode fixModeFromString(const QString &s) {
  return s == QLatin1String("managed_output") ? FixMode::ManagedOutput : FixMode::InPlace;
}

ErrorUtils::Result<qint64> insert(QSqlDatabase &db, const Profile &p) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::insert");
  }
  if (p.name.trimmed().isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Profile name is required",
                               "DatAuditProfile::insert");
  }
  KartendDb::DbTransaction txn(db, "DatAuditProfile::insert");
  if (!txn.active()) {
    return txn.beginError("Failed to begin transaction", nullptr, ErrorUtils::Severity::Error);
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  qint64 newId = -1;
  {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO dat_audit_profile "
        "(name, collection_uuid, scan_roots, region_prefs, one_per_game, "
        "ignore_rules, fix_mode, managed_output_root, detected_layout, layout_confirmed, "
        "last_scan_at_unix_ms, created_at_unix_ms, updated_at_unix_ms, quarantine_root) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(p.name);
    q.addBindValue(nonNull(p.collectionUuid));
    q.addBindValue(stringListToJson(p.scanRoots));
    q.addBindValue(stringListToJson(p.regionPrefs));
    q.addBindValue(p.onePerGame ? 1 : 0);
    q.addBindValue(stringListToJson(p.ignoreRules));
    q.addBindValue(fixModeToString(p.fixMode));
    q.addBindValue(nonNull(p.managedOutputRoot));
    q.addBindValue(nonNull(p.detectedLayout));
    q.addBindValue(p.layoutConfirmed ? 1 : 0);
    q.addBindValue(p.lastScanAtMs);
    q.addBindValue(now);
    q.addBindValue(now);
    q.addBindValue(nonNull(p.quarantineRoot));
    if (!q.exec()) {
      const QString err = q.lastError().text();
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to insert profile",
                                 "DatAuditProfile::insert")
          .withDetails(err);
    }
    newId = q.lastInsertId().toLongLong();
  }
  QString datErr;
  if (!writeDats(db, newId, p.dats, datErr)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to write profile DAT rows",
                               "DatAuditProfile::insert")
        .withDetails(datErr);
  }
  if (!txn.commit()) {
    return txn.commitError("Failed to commit profile", nullptr, ErrorUtils::Severity::Error);
  }
  return newId;
}

ErrorUtils::Result<bool> update(QSqlDatabase &db, const Profile &p) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::update");
  }
  if (p.id < 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Profile id is invalid",
                               "DatAuditProfile::update");
  }
  if (p.name.trimmed().isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Profile name is required",
                               "DatAuditProfile::update");
  }
  KartendDb::DbTransaction txn(db, "DatAuditProfile::update");
  if (!txn.active()) {
    return txn.beginError("Failed to begin transaction", nullptr, ErrorUtils::Severity::Error);
  }
  {
    QSqlQuery q(db);
    q.prepare(
        QStringLiteral("UPDATE dat_audit_profile SET "
                       "name = ?, collection_uuid = ?, scan_roots = ?, region_prefs = ?, "
                       "one_per_game = ?, ignore_rules = ?, fix_mode = ?, managed_output_root = ?, "
                       "detected_layout = ?, layout_confirmed = ?, "
                       "last_scan_at_unix_ms = ?, updated_at_unix_ms = ?, quarantine_root = ? "
                       "WHERE id = ?"));
    q.addBindValue(p.name);
    q.addBindValue(nonNull(p.collectionUuid));
    q.addBindValue(stringListToJson(p.scanRoots));
    q.addBindValue(stringListToJson(p.regionPrefs));
    q.addBindValue(p.onePerGame ? 1 : 0);
    q.addBindValue(stringListToJson(p.ignoreRules));
    q.addBindValue(fixModeToString(p.fixMode));
    q.addBindValue(nonNull(p.managedOutputRoot));
    q.addBindValue(nonNull(p.detectedLayout));
    q.addBindValue(p.layoutConfirmed ? 1 : 0);
    q.addBindValue(p.lastScanAtMs);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(nonNull(p.quarantineRoot));
    q.addBindValue(p.id);
    if (!q.exec()) {
      const QString err = q.lastError().text();
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to update profile",
                                 "DatAuditProfile::update")
          .withDetails(err);
    }
    if (q.numRowsAffected() == 0) {
      return ErrorContext::error(ErrorCode::CollectionNotFound, "No profile with that id",
                                 "DatAuditProfile::update");
    }
  }
  QString datErr;
  if (!writeDats(db, p.id, p.dats, datErr)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to write profile DAT rows",
                               "DatAuditProfile::update")
        .withDetails(datErr);
  }
  if (!txn.commit()) {
    return txn.commitError("Failed to commit profile", nullptr, ErrorUtils::Severity::Error);
  }
  return true;
}

ErrorUtils::Result<bool> remove(QSqlDatabase &db, qint64 id) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::remove");
  }
  KartendDb::DbTransaction txn(db, "DatAuditProfile::remove");
  if (!txn.active()) {
    return txn.beginError("Failed to begin transaction", nullptr, ErrorUtils::Severity::Error);
  }
  // Delete children explicitly so removal works regardless of the
  // foreign_keys pragma state on this connection.
  const char *const childTables[] = {"dat_audit_profile_dat", "dat_audit_result"};
  for (const char *table : childTables) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM %1 WHERE profile_id = ?").arg(QLatin1String(table)));
    q.addBindValue(id);
    if (!q.exec()) {
      const QString err = q.lastError().text();
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                 "Failed to delete profile children", "DatAuditProfile::remove")
          .withDetails(err);
    }
  }
  {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM dat_audit_profile WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
      const QString err = q.lastError().text();
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to delete profile",
                                 "DatAuditProfile::remove")
          .withDetails(err);
    }
  }
  if (!txn.commit()) {
    return txn.commitError("Failed to commit removal", nullptr, ErrorUtils::Severity::Error);
  }
  return true;
}

ErrorUtils::Result<std::optional<Profile>> load(QSqlDatabase &db, qint64 id) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::load");
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT %1 FROM dat_audit_profile WHERE id = ?")
                .arg(QLatin1String(kSelectColumns)));
  q.addBindValue(id);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query profile",
                               "DatAuditProfile::load")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return std::optional<Profile>(std::nullopt);
  }
  Profile p = readProfileRow(q);
  if (!loadDats(db, p)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load profile DAT rows",
                               "DatAuditProfile::load");
  }
  return std::optional<Profile>(std::move(p));
}

ErrorUtils::Result<QList<Profile>> listAll(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::listAll");
  }
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral("SELECT %1 FROM dat_audit_profile ORDER BY name COLLATE NOCASE")
                  .arg(QLatin1String(kSelectColumns)))) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to list profiles",
                               "DatAuditProfile::listAll")
        .withDetails(q.lastError().text());
  }
  QList<Profile> out;
  while (q.next()) {
    out.append(readProfileRow(q));
  }
  // Second pass for children: loadDats runs its own query, which would
  // invalidate the outer forward cursor if interleaved.
  for (Profile &p : out) {
    if (!loadDats(db, p)) {
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load profile DAT rows",
                                 "DatAuditProfile::listAll");
    }
  }
  return out;
}

ErrorUtils::Result<std::optional<Profile>> loadByCollectionUuid(QSqlDatabase &db,
                                                                const QString &collectionUuid) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::loadByCollectionUuid");
  }
  if (collectionUuid.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Collection uuid is required",
                               "DatAuditProfile::loadByCollectionUuid");
  }
  QSqlQuery q(db);
  // id DESC tiebreak: two profiles saved in the same millisecond still
  // resolve the same way on every launch.
  q.prepare(QStringLiteral("SELECT %1 FROM dat_audit_profile WHERE collection_uuid = ? "
                           "ORDER BY updated_at_unix_ms DESC, id DESC LIMIT 1")
                .arg(QLatin1String(kSelectColumns)));
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query linked profile",
                               "DatAuditProfile::loadByCollectionUuid")
        .withDetails(q.lastError().text());
  }
  if (!q.next()) {
    return std::optional<Profile>(std::nullopt);
  }
  Profile p = readProfileRow(q);
  if (!loadDats(db, p)) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load profile DAT rows",
                               "DatAuditProfile::loadByCollectionUuid");
  }
  return std::optional<Profile>(std::move(p));
}

ErrorUtils::Result<bool> touchLastScan(QSqlDatabase &db, qint64 id, qint64 whenMs) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::touchLastScan");
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral("UPDATE dat_audit_profile SET last_scan_at_unix_ms = ?, "
                           "updated_at_unix_ms = ? WHERE id = ?"));
  q.addBindValue(whenMs);
  q.addBindValue(QDateTime::currentMSecsSinceEpoch());
  q.addBindValue(id);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to stamp last scan",
                               "DatAuditProfile::touchLastScan")
        .withDetails(q.lastError().text());
  }
  return true;
}

ErrorUtils::Result<bool> replaceResults(QSqlDatabase &db, qint64 id, const QList<ResultRow> &rows) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::replaceResults");
  }
  if (id < 0) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Profile id is invalid",
                               "DatAuditProfile::replaceResults");
  }
  KartendDb::DbTransaction txn(db, "DatAuditProfile::replaceResults");
  if (!txn.active()) {
    return txn.beginError("Failed to begin transaction", nullptr, ErrorUtils::Severity::Error);
  }
  {
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM dat_audit_result WHERE profile_id = ?"));
    del.addBindValue(id);
    if (!del.exec()) {
      const QString err = del.lastError().text();
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                 "Failed to clear previous result snapshot",
                                 "DatAuditProfile::replaceResults")
          .withDetails(err);
    }
  }
  QSqlQuery ins(db);
  ins.prepare(QStringLiteral("INSERT OR REPLACE INTO dat_audit_result "
                             "(profile_id, entry_key, status, file_path, detail, "
                             "source_name, game_name, mia) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
  for (const ResultRow &r : rows) {
    ins.bindValue(0, id);
    ins.bindValue(1, nonNull(r.entryKey));
    ins.bindValue(2, r.status);
    ins.bindValue(3, nonNull(r.filePath));
    ins.bindValue(4, nonNull(r.detail));
    ins.bindValue(5, nonNull(r.sourceName));
    ins.bindValue(6, nonNull(r.gameName));
    ins.bindValue(7, r.mia ? 1 : 0);
    if (!ins.exec()) {
      const QString err = ins.lastError().text();
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to insert result row",
                                 "DatAuditProfile::replaceResults")
          .withDetails(err);
    }
  }
  if (!txn.commit()) {
    return txn.commitError("Failed to commit result snapshot", nullptr,
                           ErrorUtils::Severity::Error);
  }
  return true;
}

ErrorUtils::Result<std::optional<ResultSummary>> loadResultSummary(QSqlDatabase &db, qint64 id) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::loadResultSummary");
  }
  ResultSummary out;
  {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT last_scan_at_unix_ms FROM dat_audit_profile WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
      return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to query profile",
                                 "DatAuditProfile::loadResultSummary")
          .withDetails(q.lastError().text());
    }
    if (!q.next()) {
      return std::optional<ResultSummary>(std::nullopt);
    }
    out.lastScanAtMs = q.value(0).toLongLong();
  }
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "SELECT status, COUNT(*) FROM dat_audit_result WHERE profile_id = ? GROUP BY status"));
  q.addBindValue(id);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to summarise results",
                               "DatAuditProfile::loadResultSummary")
        .withDetails(q.lastError().text());
  }
  while (q.next()) {
    out.countsByStatus.insert(q.value(0).toInt(), q.value(1).toInt());
    out.hasResults = true;
  }
  // "Audited but empty snapshot" still counts as having results when the
  // profile carries a last-scan stamp — zero rows is a legitimate outcome
  // of auditing an empty folder against an empty catalogue.
  if (out.lastScanAtMs > 0) {
    out.hasResults = true;
  }
  return std::optional<ResultSummary>(std::move(out));
}

ErrorUtils::Result<QList<RollupRow>> loadAllRollups(QSqlDatabase &db) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::loadAllRollups");
  }
  QList<RollupRow> out;
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT profile_id, source_name, status, mia, COUNT(*) "
                           "FROM dat_audit_result "
                           "GROUP BY profile_id, source_name, status, mia"));
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load audit rollups",
                               "DatAuditProfile::loadAllRollups")
        .withDetails(q.lastError().text());
  }
  while (q.next()) {
    out.append(RollupRow{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt(),
                         q.value(3).toInt() != 0, q.value(4).toInt()});
  }
  return out;
}

ErrorUtils::Result<QList<GameRollupRow>> loadGameRollups(QSqlDatabase &db, qint64 profileId,
                                                         const QString &sourceName) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::loadGameRollups");
  }
  QList<GameRollupRow> out;
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT game_name, status, mia, COUNT(*) FROM dat_audit_result "
                           "WHERE profile_id = ? AND source_name = ? "
                           "GROUP BY game_name, status, mia"));
  q.addBindValue(profileId);
  q.addBindValue(sourceName);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load game rollups",
                               "DatAuditProfile::loadGameRollups")
        .withDetails(q.lastError().text());
  }
  while (q.next()) {
    out.append(GameRollupRow{q.value(0).toString(), q.value(1).toInt(), q.value(2).toInt() != 0,
                             q.value(3).toInt()});
  }
  return out;
}

ErrorUtils::Result<QList<ResultRow>> loadGameResultRows(QSqlDatabase &db, qint64 profileId,
                                                        const QString &sourceName,
                                                        const QString &gameName) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::loadGameResultRows");
  }
  QList<ResultRow> out;
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT entry_key, status, file_path, detail, source_name, game_name, "
                           "mia FROM dat_audit_result "
                           "WHERE profile_id = ? AND source_name = ? AND game_name = ?"));
  q.addBindValue(profileId);
  q.addBindValue(sourceName);
  q.addBindValue(gameName);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load game result rows",
                               "DatAuditProfile::loadGameResultRows")
        .withDetails(q.lastError().text());
  }
  while (q.next()) {
    ResultRow r;
    r.entryKey = q.value(0).toString();
    r.status = q.value(1).toInt();
    r.filePath = q.value(2).toString();
    r.detail = q.value(3).toString();
    r.sourceName = q.value(4).toString();
    r.gameName = q.value(5).toString();
    r.mia = q.value(6).toInt() != 0;
    out.append(r);
  }
  return out;
}

ErrorUtils::Result<QList<ResultRow>> loadSourceResultRows(QSqlDatabase &db, qint64 profileId,
                                                          const QString &sourceName) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "DatAuditProfile::loadSourceResultRows");
  }
  QList<ResultRow> out;
  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT entry_key, status, file_path, detail, source_name, game_name, "
                           "mia FROM dat_audit_result "
                           "WHERE profile_id = ? AND source_name = ?"));
  q.addBindValue(profileId);
  q.addBindValue(sourceName);
  if (!q.exec()) {
    return ErrorContext::error(ErrorCode::DatabaseQueryFailed, "Failed to load source result rows",
                               "DatAuditProfile::loadSourceResultRows")
        .withDetails(q.lastError().text());
  }
  while (q.next()) {
    ResultRow r;
    r.entryKey = q.value(0).toString();
    r.status = q.value(1).toInt();
    r.filePath = q.value(2).toString();
    r.detail = q.value(3).toString();
    r.sourceName = q.value(4).toString();
    r.gameName = q.value(5).toString();
    r.mia = q.value(6).toInt() != 0;
    out.append(r);
  }
  return out;
}

} // namespace DatAuditProfile
