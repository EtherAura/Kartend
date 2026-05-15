// Smart-playlist filter -> SQL translator. One function per filter Kind so
// the routing in `evaluate()` reads as a single switch and each branch's
// SQL is in arm's reach for review.
#include "smartplaylistevaluator.h"

#include <algorithm>

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

#include "errorutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace SmartPlaylistEvaluator {

namespace {

constexpr int LIMIT_MIN = 1;
constexpr int LIMIT_MAX = 1000;

int clampLimit(int n) {
  return std::clamp(n, LIMIT_MIN, LIMIT_MAX);
}

QList<Match> runRowQuery(QSqlDatabase &db, const QString &sql, int limit, const char *origin) {
  QList<Match> out;
  if (!db.isOpen()) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open", origin));
    return out;
  }
  QSqlQuery q(db);
  if (!q.prepare(sql)) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare smart-playlist query", origin)
                             .withDetails(q.lastError().text()));
    return out;
  }
  q.addBindValue(clampLimit(limit));
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run smart-playlist query", origin)
                             .withDetails(q.lastError().text()));
    return out;
  }
  while (q.next()) {
    Match m;
    m.collectionUuid = q.value(0).toString();
    m.path = q.value(1).toString();
    out.append(m);
  }
  return out;
}

QList<Match> evalRecentlyLaunched(QSqlDatabase &db, int limit) {
  return runRowQuery(db,
                     QStringLiteral("SELECT collection_uuid, path FROM items "
                                    "WHERE last_played IS NOT NULL AND last_played != '' "
                                    "ORDER BY last_played DESC LIMIT ?"),
                     limit, "SmartPlaylistEvaluator::evalRecentlyLaunched");
}

QList<Match> evalTopPlayed(QSqlDatabase &db, int limit) {
  return runRowQuery(db,
                     QStringLiteral("SELECT collection_uuid, path FROM items "
                                    "WHERE play_count > 0 "
                                    "ORDER BY play_count DESC, last_played DESC LIMIT ?"),
                     limit, "SmartPlaylistEvaluator::evalTopPlayed");
}

QList<Match> evalNeverPlayed(QSqlDatabase &db, int limit) {
  // Match the alphabetical ordering UsageStatsStore::loadNeverPlayed uses
  // so the smart playlist tile order is consistent with the Statistics
  // dialog's "Never played" tab.
  return runRowQuery(db,
                     QStringLiteral("SELECT collection_uuid, path FROM items "
                                    "WHERE play_count = 0 OR play_count IS NULL "
                                    "ORDER BY name COLLATE NOCASE ASC LIMIT ?"),
                     limit, "SmartPlaylistEvaluator::evalNeverPlayed");
}

QList<Match> evalByExtension(QSqlDatabase &db, const QStringList &extensions) {
  QList<Match> out;
  if (extensions.isEmpty() || !db.isOpen()) {
    return out;
  }
  // SQLite has no LOWER() over the column without an expression index, so
  // we build the ORed glob list dynamically and rely on path being
  // case-sensitive by convention. Lowercase comparison would require
  // either a generated column or per-row LOWER(); the cost isn't worth
  // it for what is normally a tiny extension list.
  QStringList globPlaceholders;
  globPlaceholders.reserve(extensions.size() * 2);
  for (const QString &ext : extensions) {
    Q_UNUSED(ext);
    globPlaceholders.append(QStringLiteral("path LIKE ?"));
    globPlaceholders.append(QStringLiteral("path LIKE ?"));
  }
  const QString sql = QStringLiteral("SELECT collection_uuid, path FROM items WHERE ") +
                      globPlaceholders.join(QStringLiteral(" OR ")) +
                      QStringLiteral(" ORDER BY name COLLATE NOCASE ASC");

  QSqlQuery q(db);
  if (!q.prepare(sql)) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare by-extension smart query",
                                             "SmartPlaylistEvaluator::evalByExtension")
                             .withDetails(q.lastError().text()));
    return out;
  }
  for (const QString &ext : extensions) {
    QString clean = ext.trimmed().toLower();
    if (clean.startsWith('.')) {
      clean.remove(0, 1);
    }
    // Bind both common cases so the user typing ".mp4" matches files
    // named ".MP4" too — covers the usual macOS/Windows filename quirks
    // without needing a case-insensitive collation on the path column.
    q.addBindValue(QStringLiteral("%.") + clean);
    q.addBindValue(QStringLiteral("%.") + clean.toUpper());
  }
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run by-extension smart query",
                                             "SmartPlaylistEvaluator::evalByExtension")
                             .withDetails(q.lastError().text()));
    return out;
  }
  while (q.next()) {
    Match m;
    m.collectionUuid = q.value(0).toString();
    m.path = q.value(1).toString();
    out.append(m);
  }
  return out;
}

QList<Match> evalByDateAdded(QSqlDatabase &db, int days) {
  QList<Match> out;
  if (!db.isOpen()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                               "SmartPlaylistEvaluator::evalByDateAdded"));
    return out;
  }
  // Clamp to a sensible window so a hand-edited smart_filter row with a
  // negative or absurd value can't pull back the whole library or
  // nothing at all.
  const int clampedDays = std::clamp(days, 1, 3650);
  const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - (qint64{clampedDays} * 86400);

  QSqlQuery q(db);
  // date_added > 0 excludes rows the v12 backfill couldn't parse — those
  // are treated as "unknown date" and intentionally don't appear in
  // recency-window queries.
  if (!q.prepare("SELECT collection_uuid, path FROM items "
                 "WHERE date_added > 0 AND date_added >= ? "
                 "ORDER BY date_added DESC")) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare by-date-added smart query",
                                             "SmartPlaylistEvaluator::evalByDateAdded")
                             .withDetails(q.lastError().text()));
    return out;
  }
  q.addBindValue(cutoff);
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run by-date-added smart query",
                                             "SmartPlaylistEvaluator::evalByDateAdded")
                             .withDetails(q.lastError().text()));
    return out;
  }
  while (q.next()) {
    Match m;
    m.collectionUuid = q.value(0).toString();
    m.path = q.value(1).toString();
    out.append(m);
  }
  return out;
}

QList<Match> evalHasArtwork(QSqlDatabase &db) {
  QList<Match> out;
  if (!db.isOpen()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                               "SmartPlaylistEvaluator::evalHasArtwork"));
    return out;
  }
  QSqlQuery q(db);
  // items.artwork_path is populated by the scanner (v1 column) when an
  // artwork file matched on disk; an empty / NULL value means "fall back
  // to the procedural placeholder". Filtering on non-empty artwork_path
  // is the cheap way to ask "does this item have a real cover".
  if (!q.exec(QStringLiteral("SELECT collection_uuid, path FROM items "
                             "WHERE artwork_path IS NOT NULL AND artwork_path != '' "
                             "ORDER BY name COLLATE NOCASE ASC"))) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run has-artwork smart query",
                                             "SmartPlaylistEvaluator::evalHasArtwork")
                             .withDetails(q.lastError().text()));
    return out;
  }
  while (q.next()) {
    Match m;
    m.collectionUuid = q.value(0).toString();
    m.path = q.value(1).toString();
    out.append(m);
  }
  return out;
}

} // namespace

QList<Match> evaluate(QSqlDatabase &db, const SmartFilter::Filter &filter) {
  switch (filter.kind) {
  case SmartFilter::Kind::RecentlyLaunched:
    return evalRecentlyLaunched(db, filter.limit);
  case SmartFilter::Kind::TopPlayed:
    return evalTopPlayed(db, filter.limit);
  case SmartFilter::Kind::NeverPlayed:
    return evalNeverPlayed(db, filter.limit);
  case SmartFilter::Kind::ByExtension:
    return evalByExtension(db, filter.extensions);
  case SmartFilter::Kind::HasArtwork:
    return evalHasArtwork(db);
  case SmartFilter::Kind::ByDateAdded:
    return evalByDateAdded(db, filter.days);
  }
  return {};
}

} // namespace SmartPlaylistEvaluator
