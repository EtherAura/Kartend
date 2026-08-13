// Smart-playlist filter -> SQL translator. One function per filter Kind so
// the routing in `evaluate()` reads as a single switch and each branch's
// SQL is in arm's reach for review.
#include "smartplaylistevaluator.h"

#include "sqllike.h"

#include <algorithm>

#include <QDateTime>
#include <QSet>
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
  // One ORed LIKE per extension. SQLite LIKE is ASCII case-insensitive by
  // default, so ".mp4" matches ".MP4" without a second uppercase pattern.
  // The bound needle is escaped (Kartend-joird): only the leading "%." is an
  // intentional wildcard — a literal % or _ in the extension must match
  // itself, not act as a LIKE metacharacter.
  QStringList globPlaceholders;
  globPlaceholders.reserve(extensions.size());
  for (const QString &ext : extensions) {
    Q_UNUSED(ext);
    globPlaceholders.append(QStringLiteral("path LIKE ?") + KartendDb::kLikeEscape);
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
    q.addBindValue(QStringLiteral("%.") + KartendDb::escapeLike(clean));
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

QList<Match> evalByCollection(QSqlDatabase &db, const QString &collectionUuid) {
  QList<Match> out;
  if (!db.isOpen()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                               "SmartPlaylistEvaluator::evalByCollection"));
    return out;
  }
  if (collectionUuid.isEmpty()) {
    // Empty uuid is the unsaved-dialog sentinel — treat as "no matches"
    // rather than returning every item across every collection.
    return out;
  }
  QSqlQuery q(db);
  if (!q.prepare("SELECT collection_uuid, path FROM items WHERE collection_uuid = ? "
                 "ORDER BY name COLLATE NOCASE ASC")) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare by-collection smart query",
                                             "SmartPlaylistEvaluator::evalByCollection")
                             .withDetails(q.lastError().text()));
    return out;
  }
  q.addBindValue(collectionUuid);
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run by-collection smart query",
                                             "SmartPlaylistEvaluator::evalByCollection")
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

QList<Match> evalByTitleSearch(QSqlDatabase &db, const QString &needle) {
  QList<Match> out;
  if (!db.isOpen()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                               "SmartPlaylistEvaluator::evalByTitleSearch"));
    return out;
  }
  const QString trimmed = needle.trimmed();
  if (trimmed.isEmpty()) {
    return out; // unsaved-dialog sentinel
  }
  QSqlQuery q(db);
  // LIKE %?% on items.name. NOCASE collation isn't applied here because
  // SQLite's default LIKE is case-insensitive for ASCII, which is the
  // common case for English-language libraries. Unicode case folding via
  // LOWER() on both sides could be a follow-up if multibyte titles need
  // it.
  if (!q.prepare(QStringLiteral("SELECT collection_uuid, path FROM items WHERE name LIKE ?") +
                 KartendDb::kLikeEscape + QStringLiteral(" ORDER BY name COLLATE NOCASE ASC"))) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to prepare title-search smart query",
                                             "SmartPlaylistEvaluator::evalByTitleSearch")
                             .withDetails(q.lastError().text()));
    return out;
  }
  q.addBindValue(QStringLiteral("%") + KartendDb::escapeLike(trimmed) + QStringLiteral("%"));
  if (!q.exec()) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run title-search smart query",
                                             "SmartPlaylistEvaluator::evalByTitleSearch")
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

QList<Match> evalMissingArtwork(QSqlDatabase &db) {
  QList<Match> out;
  if (!db.isOpen()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                               "SmartPlaylistEvaluator::evalMissingArtwork"));
    return out;
  }
  QSqlQuery q(db);
  // Symmetric to evalHasArtwork — NULL OR empty captures both shapes the
  // scanner can leave behind when an artwork file isn't found on disk.
  if (!q.exec(QStringLiteral("SELECT collection_uuid, path FROM items "
                             "WHERE artwork_path IS NULL OR artwork_path = '' "
                             "ORDER BY name COLLATE NOCASE ASC"))) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run missing-artwork smart query",
                                             "SmartPlaylistEvaluator::evalMissingArtwork")
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

QList<Match> evalFavorite(QSqlDatabase &db) {
  QList<Match> out;
  if (!db.isOpen()) {
    ErrorUtils::logError(ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open",
                                               "SmartPlaylistEvaluator::evalFavorite"));
    return out;
  }
  // Items present in the reserved Favorites playlist. INNER JOIN through
  // playlist_items + playlists; the reserved_kind literal scope avoids
  // user-named playlists colliding.
  QSqlQuery q(db);
  if (!q.exec(QStringLiteral(
          "SELECT i.collection_uuid, i.path FROM items i "
          "INNER JOIN playlist_items pi ON pi.source_collection_uuid = i.collection_uuid "
          "AND pi.source_path = i.path "
          "INNER JOIN playlists pl ON pl.id = pi.playlist_id "
          "WHERE pl.reserved_kind = 'favorites' "
          "ORDER BY i.name COLLATE NOCASE ASC"))) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run favorite smart query",
                                             "SmartPlaylistEvaluator::evalFavorite")
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

QList<Match> evalStateFlag(QSqlDatabase &db, const char *column, const char *origin) {
  QList<Match> out;
  if (!db.isOpen()) {
    ErrorUtils::logError(
        ErrorContext::warning(ErrorCode::DatabaseNotOpen, "Database not open", origin));
    return out;
  }
  // Inner join keys (collection_uuid, path) which both item_metadata and
  // items share; the (collection_uuid, path) pair is unique on item_metadata
  // by the v5 constraint and on items by the v1 unique index, so the join
  // doesn't multiply rows.
  const QString sql = QStringLiteral("SELECT items.collection_uuid, items.path FROM items "
                                     "INNER JOIN item_metadata ON "
                                     "items.collection_uuid = item_metadata.collection_uuid "
                                     "AND items.path = item_metadata.path "
                                     "WHERE item_metadata.") +
                      QString::fromLatin1(column) +
                      QStringLiteral(" = 1 ORDER BY items.name COLLATE NOCASE ASC");
  QSqlQuery q(db);
  if (!q.exec(sql)) {
    ErrorUtils::logError(ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                             "Failed to run state-flag smart query", origin)
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
  case SmartFilter::Kind::Pinned:
    return evalStateFlag(db, "is_pinned", "SmartPlaylistEvaluator::evalPinned");
  case SmartFilter::Kind::Hidden:
    return evalStateFlag(db, "is_hidden", "SmartPlaylistEvaluator::evalHidden");
  case SmartFilter::Kind::ContinueLater:
    return evalStateFlag(db, "continue_later", "SmartPlaylistEvaluator::evalContinueLater");
  case SmartFilter::Kind::ByCollection:
    return evalByCollection(db, filter.collectionUuid);
  case SmartFilter::Kind::ByTitleSearch:
    return evalByTitleSearch(db, filter.titleSearch);
  case SmartFilter::Kind::MissingArtwork:
    return evalMissingArtwork(db);
  case SmartFilter::Kind::Favorite:
    return evalFavorite(db);
  }
  return {};
}

QList<Match> evaluate(QSqlDatabase &db, const SmartFilter::FilterSet &set) {
  if (set.rules.isEmpty()) {
    return {}; // rejected at parse time; belt-and-braces against a hand-built set
  }
  if (set.rules.size() == 1) {
    return evaluate(db, set.rules.first());
  }

  // Composition happens over result SETS rather than by fusing the rules into
  // one WHERE clause. Each Kind's SQL carries its own ordering and its own
  // LIMIT — "top 20 played" means the top 20, not "played, capped later" — and
  // those semantics survive intact only if each rule runs as written. The cost
  // is one query per rule on playlist open, against a table the scanner
  // already indexes.
  //
  // Ordering follows the FIRST rule: it is the one the user wrote first, and
  // an "all" set can only ever be a subset of it. Union appends later rules'
  // items in rule order, so the result is stable across rebuilds.
  const auto keyOf = [](const Match &m) { return m.collectionUuid + QChar(u'\0') + m.path; };

  QList<Match> combined = evaluate(db, set.rules.first());
  for (qsizetype i = 1; i < set.rules.size(); ++i) {
    if (combined.isEmpty() && set.match == SmartFilter::MatchMode::All) {
      break; // an intersection can only shrink — the remaining queries can't change it
    }
    const QList<Match> next = evaluate(db, set.rules.at(i));

    QSet<QString> nextKeys;
    nextKeys.reserve(next.size());
    for (const Match &m : next) {
      nextKeys.insert(keyOf(m));
    }

    if (set.match == SmartFilter::MatchMode::All) {
      QList<Match> kept;
      kept.reserve(std::min(combined.size(), next.size()));
      for (const Match &m : combined) {
        if (nextKeys.contains(keyOf(m))) {
          kept.append(m);
        }
      }
      combined = kept;
    } else {
      QSet<QString> haveKeys;
      haveKeys.reserve(combined.size());
      for (const Match &m : combined) {
        haveKeys.insert(keyOf(m));
      }
      for (const Match &m : next) {
        if (!haveKeys.contains(keyOf(m))) {
          combined.append(m);
        }
      }
    }
  }
  return combined;
}

} // namespace SmartPlaylistEvaluator
