#include "datauditbrowserrepository.h"

#include <QSqlDatabase>
#include <QStandardPaths>

#include "databaseschema.h"
#include "errorutils.h"

namespace DatAudit {

namespace {

// Short-lived UI-thread connection to the app DB (same pattern as the dialog's
// withProfileDb). The browser's reads are light + occasional. Moved here from
// DatAuditBrowserPage (Kartend-ma608) so the query origin lives behind the
// repository boundary, not in the view.
template <typename Fn> void withDb(Fn &&fn) {
  static int counter = 0; // UI thread only
  const QString conn = QStringLiteral("dataudit_browser_%1").arg(counter++);
  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (DatabaseSchema::openConnection(db, dir)) {
      DatabaseSchema::applyConnectionPragmas(db);
      fn(db);
    }
    db.close();
  }
  QSqlDatabase::removeDatabase(conn);
}

} // namespace

QList<DatAuditProfile::Profile> DatAuditBrowserRepository::profiles() const {
  QList<DatAuditProfile::Profile> out;
  withDb([&](QSqlDatabase &db) {
    if (auto p = DatAuditProfile::listAll(db); p.isOk()) {
      out = p.value();
    } else {
      ErrorUtils::logError(p.error());
    }
  });
  return out;
}

QList<DatAuditProfile::RollupRow> DatAuditBrowserRepository::rollups() const {
  QList<DatAuditProfile::RollupRow> out;
  withDb([&](QSqlDatabase &db) {
    if (auto r = DatAuditProfile::loadAllRollups(db); r.isOk()) {
      out = r.value();
    } else {
      ErrorUtils::logError(r.error());
    }
  });
  return out;
}

QList<DatAuditProfile::GameRollupRow>
DatAuditBrowserRepository::gamesFor(qint64 profileId, const QString &sourceName) const {
  QList<DatAuditProfile::GameRollupRow> out;
  withDb([&](QSqlDatabase &db) {
    if (auto g = DatAuditProfile::loadGameRollups(db, profileId, sourceName); g.isOk()) {
      out = g.value();
    } else {
      ErrorUtils::logError(g.error());
    }
  });
  return out;
}

QList<DatAuditProfile::ResultRow>
DatAuditBrowserRepository::romsFor(qint64 profileId, const QString &sourceName,
                                   const QString &gameName) const {
  QList<DatAuditProfile::ResultRow> out;
  withDb([&](QSqlDatabase &db) {
    if (auto r = DatAuditProfile::loadGameResultRows(db, profileId, sourceName, gameName);
        r.isOk()) {
      out = r.value();
    } else {
      ErrorUtils::logError(r.error());
    }
  });
  return out;
}

QList<DatAuditProfile::ResultRow>
DatAuditBrowserRepository::sourceRows(qint64 profileId, const QString &sourceName) const {
  QList<DatAuditProfile::ResultRow> out;
  withDb([&](QSqlDatabase &db) {
    if (auto r = DatAuditProfile::loadSourceResultRows(db, profileId, sourceName); r.isOk()) {
      out = r.value();
    } else {
      ErrorUtils::logError(r.error());
    }
  });
  return out;
}

QList<DatLookup::DatRecord>
DatAuditBrowserRepository::datRecordsFor(const QString &datPath, const QString &gameName) const {
  if (datPath.isEmpty()) {
    return {};
  }
  DatCache::Store cache(DatCache::defaultPath());
  if (const auto src = cache.peek(datPath)) {
    return cache.recordsForGame(*src, gameName);
  }
  return {};
}

std::optional<DatCache::CachedSource>
DatAuditBrowserRepository::datHeader(const QString &datPath) const {
  if (datPath.isEmpty()) {
    return std::nullopt;
  }
  DatCache::Store cache(DatCache::defaultPath());
  return cache.peek(datPath);
}

} // namespace DatAudit
