// DB-side playlist import / export, moved verbatim from playlistmanager.cpp.
// Free functions over an explicit QSqlDatabase& — the signal-emitting CRUD
// (playlist creation, failure cleanup, playlistsChanged) and thread affinity
// stay on PlaylistManager; format + file I/O stay in PlaylistSerializer.

#include "playlistio.h"

#include <QDateTime>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include "dbtxn.h"
#include "errorutils.h"

using ErrorUtils::ErrorCode;
using ErrorUtils::ErrorContext;

namespace {

// Upper bounds on user-/file-supplied strings before they reach the TEXT NOT
// NULL columns. A crafted or corrupt JSON envelope could otherwise carry
// megabyte-sized paths or garbage UUID strings straight into playlist_items
// (Kartend-2sg2t). These caps are generous relative to any real filesystem
// path but bound worst-case row size. (The playlist-name cap lives with the
// CRUD in playlistmanager.cpp.)
constexpr int kMaxSourcePathLength = 4096;
// source_collection_uuid is NOT a QUuid: collection identity is a 40-char SHA-1
// hex digest (CollectionUtils::computeCollectionUuid). A QUuid::fromString
// round-trip would therefore reject every real ref, so we bound the length only
// — generous over the 40-char digest while still capping worst-case row size.
constexpr int kMaxUuidLength = 128;

} // namespace

namespace PlaylistIo {

QString isoNow() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool validateItemRef(const QString &uuid, const QString &path) {
  if (path.length() > kMaxSourcePathLength) {
    return false;
  }
  if (uuid.length() > kMaxUuidLength) {
    return false;
  }
  return true;
}

const QString &insertPlaylistItemSql() {
  static const QString sql = QStringLiteral("INSERT INTO playlist_items (playlist_id, position, "
                                            "source_collection_uuid, source_path, added_at) "
                                            "VALUES (?, ?, ?, ?, ?)");
  return sql;
}

void bindPlaylistItemRow(QSqlQuery &q, const QString &playlistId, int position,
                         const QString &sourceCollectionUuid, const QString &sourcePath,
                         const QString &addedAt) {
  q.bindValue(0, playlistId);
  q.bindValue(1, position);
  q.bindValue(2, sourceCollectionUuid);
  q.bindValue(3, sourcePath);
  q.bindValue(4, addedAt);
}

PlaylistRow loadPlaylistRow(QSqlDatabase &db, const QString &id) {
  PlaylistRow row;
  QSqlQuery q(db);
  q.prepare("SELECT id, name, icon, parent_collection_uuid, reserved_kind, "
            "created_at, updated_at, is_smart, smart_filter FROM playlists WHERE id = ?");
  q.addBindValue(id);
  if (q.exec() && q.next()) {
    row.id = q.value(0).toString();
    row.name = q.value(1).toString();
    row.icon = q.value(2).toString();
    row.parentCollectionUuid = q.value(3).toString();
    row.reservedKind = q.value(4).toString();
    row.createdAt = q.value(5).toString();
    row.updatedAt = q.value(6).toString();
    row.isSmart = q.value(7).toInt() != 0;
    row.smartFilterJson = q.value(8).toString();
  }
  return row;
}

ErrorUtils::Result<int> exportToJson(QSqlDatabase &db, const QString &playlistId,
                                     const QString &outPath, const QList<PlaylistItemRef> &items) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistIo::exportToJson");
  }
  if (playlistId.isEmpty() || outPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "playlistId and outPath are required",
                               "PlaylistIo::exportToJson");
  }
  PlaylistRow row = loadPlaylistRow(db, playlistId);
  if (row.id.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "Playlist not found",
                               "PlaylistIo::exportToJson");
  }
  // Format + file I/O live in PlaylistSerializer (Kartend-k4alv); this
  // function keeps only the DB reads feeding it.
  return PlaylistSerializer::exportJson(row, items, outPath);
}

ErrorUtils::Result<int> exportToM3U(QSqlDatabase &db, const QString &playlistId,
                                    const QString &outPath, const QList<PlaylistItemRef> &items) {
  if (!db.isOpen()) {
    return ErrorContext::error(ErrorCode::DatabaseNotOpen, "Database not open",
                               "PlaylistIo::exportToM3U");
  }
  if (playlistId.isEmpty() || outPath.isEmpty()) {
    return ErrorContext::error(ErrorCode::InvalidArgument, "playlistId and outPath are required",
                               "PlaylistIo::exportToM3U");
  }
  return PlaylistSerializer::exportM3U(items, outPath);
}

// Kartend-fd7xl: insert all items in a SINGLE transaction so a mid-loop
// failure or crash can't leave a half-populated playlist the user sees as
// "imported". (PlaylistManager::addItem opens its own per-item transaction, so
// we insert directly here instead of looping it — positions are a dense
// 0-based sequence for the fresh playlist, and an in-memory set reproduces
// addItem's idempotent (uuid,path) de-duplication.) The DbTransaction guard
// (Kartend-l94tw) is scoped to this function, so its rollback runs BEFORE the
// caller's deletePlaylist cleanup — deletePlaylist issues its own DELETE,
// which must not execute inside the still-open (about to be rolled back)
// import transaction.
//
// Kartend-2sg2t: mirror the M3U path's skip accounting — empty-path,
// duplicate, and over-long/invalid item refs are counted so the caller can
// surface "N items skipped" instead of the import silently dropping them
// while still reporting success.
std::optional<ErrorUtils::ErrorContext>
insertImportedJsonItems(QSqlDatabase &db, const QString &playlistId,
                        const QList<PlaylistSerializer::ParsedPlaylist::Item> &items,
                        int &skipped) {
  KartendDb::DbTransaction txn(db, "PlaylistIo::insertImportedJsonItems");
  if (!txn.active()) {
    return txn.beginError("Failed to begin playlist import transaction", nullptr,
                          ErrorUtils::Severity::Error);
  }
  const QString addedAt = isoNow();
  QSet<QString> seen;
  int position = 0;
  for (const PlaylistSerializer::ParsedPlaylist::Item &entry : items) {
    const QString &uuid = entry.sourceCollectionUuid;
    const QString &path = entry.sourcePath;
    if (path.isEmpty()) {
      ++skipped;
      continue;
    }
    if (!validateItemRef(uuid, path)) {
      ++skipped;
      ErrorUtils::logError(
          ErrorContext::warning(ErrorCode::InvalidArgument,
                                "Skipping over-long/invalid imported playlist item ref",
                                "PlaylistIo::insertImportedJsonItems")
              .withDetails(QStringLiteral("path length: %1, uuid length: %2")
                               .arg(path.length())
                               .arg(uuid.length())));
      continue;
    }
    const QString dedupKey = uuid + QLatin1Char('\x1f') + path;
    if (seen.contains(dedupKey)) {
      ++skipped;
      continue;
    }
    seen.insert(dedupKey);

    QSqlQuery insert(db);
    insert.prepare(insertPlaylistItemSql());
    bindPlaylistItemRow(insert, playlistId, position++, uuid, path, addedAt);
    if (!insert.exec()) {
      auto err = ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                     "Failed to insert imported playlist item",
                                     "PlaylistIo::insertImportedJsonItems")
                     .withDetails(insert.lastError().text());
      ErrorUtils::logError(err);
      return err; // guard dtor rolls back
    }
  }
  if (!txn.commit()) {
    auto err =
        txn.commitError("Failed to commit playlist import", nullptr, ErrorUtils::Severity::Error);
    ErrorUtils::logError(err);
    return err;
  }
  return std::nullopt;
}

QList<ResolvedM3UEntry> resolveM3UEntries(QSqlDatabase &db, const QStringList &paths, int &skipped,
                                          int &ambiguous) {
  // Resolve each path via the live items table — playlist_items is keyed by
  // (source_collection_uuid, source_path), so we have to know which source
  // collection owns the path to round-trip correctly. Paths the items table
  // doesn't recognise are skipped (counted in @p skipped) rather than stored
  // as broken refs; the user can re-import once collections have been
  // rescanned and the paths become resolvable.
  //
  // Kartend-o84pt: M3U carries no collection identity (unlike the lossless JSON
  // format, which stores source_collection_uuid), so a path that exists in more
  // than one collection is genuinely ambiguous on import. ORDER BY makes the
  // pick DETERMINISTIC (lowest uuid) instead of letting SQLite's row order
  // decide, and we log the ambiguity so the round-trip's lossiness is visible
  // rather than silent.
  QSqlQuery resolve(db);
  resolve.prepare("SELECT collection_uuid FROM items WHERE path = ? ORDER BY collection_uuid");

  QList<ResolvedM3UEntry> resolved;
  resolved.reserve(paths.size());
  QSet<QString> seen;
  for (const QString &line : paths) {
    // Kartend-2sg2t: bound the file-supplied path before it reaches the DB so a
    // crafted M3U cannot inject a megabyte-sized source_path. (The resolved
    // uuid comes from the trusted items table, so the path is the only
    // attacker-controlled half here.)
    if (!validateItemRef(QString(), line)) {
      ++skipped;
      ErrorUtils::logError(ErrorContext::warning(ErrorCode::InvalidArgument,
                                                 "M3U import: skipping over-long path",
                                                 "PlaylistIo::resolveM3UEntries")
                               .withDetails(QStringLiteral("path length: %1").arg(line.length())));
      continue;
    }
    resolve.bindValue(0, line);
    if (!resolve.exec() || !resolve.next()) {
      ++skipped;
      continue;
    }
    const QString uuid = resolve.value(0).toString();
    if (resolve.next()) {
      ++ambiguous;
      ErrorUtils::logError(
          ErrorContext::warning(ErrorCode::InvalidArgument,
                                "M3U import: path exists in multiple collections; M3U can't "
                                "disambiguate, using the lowest collection uuid",
                                "PlaylistIo::resolveM3UEntries")
              .withDetails(line));
    }
    // Reproduce addItem's idempotent (uuid, path) de-duplication in memory.
    const QString dedupKey = uuid + QLatin1Char('\x1f') + line;
    if (seen.contains(dedupKey)) {
      continue;
    }
    seen.insert(dedupKey);
    resolved.append(ResolvedM3UEntry{uuid, line});
  }
  return resolved;
}

// Kartend-k695z: everything was resolved FIRST (resolveM3UEntries), so this is
// one transaction over ready-made rows — mirroring insertImportedJsonItems
// (Kartend-fd7xl). The old per-line addItem() loop opened one transaction +
// one MAX(position) probe + one playlistsChanged emission per line, and a
// mid-loop DB failure left a half-imported playlist presented as success. The
// guard is scoped to this function for the same reason as
// insertImportedJsonItems: its rollback must run before the caller's
// deletePlaylist.
std::optional<ErrorUtils::ErrorContext>
insertResolvedM3UItems(QSqlDatabase &db, const QString &playlistId,
                       const QList<ResolvedM3UEntry> &entries) {
  KartendDb::DbTransaction txn(db, "PlaylistIo::insertResolvedM3UItems");
  if (!txn.active()) {
    return txn.beginError("Failed to begin M3U import transaction", nullptr,
                          ErrorUtils::Severity::Error);
  }
  const QString addedAt = isoNow();
  int position = 0;
  for (const ResolvedM3UEntry &entry : entries) {
    QSqlQuery insert(db);
    insert.prepare(insertPlaylistItemSql());
    bindPlaylistItemRow(insert, playlistId, position++, entry.uuid, entry.path, addedAt);
    if (!insert.exec()) {
      auto err = ErrorContext::error(ErrorCode::DatabaseQueryFailed,
                                     "Failed to insert imported M3U playlist item",
                                     "PlaylistIo::insertResolvedM3UItems")
                     .withDetails(insert.lastError().text());
      ErrorUtils::logError(err);
      return err; // guard dtor rolls back
    }
  }
  if (!txn.commit()) {
    auto err = txn.commitError("Failed to commit M3U playlist import", nullptr,
                               ErrorUtils::Severity::Error);
    ErrorUtils::logError(err);
    return err;
  }
  return std::nullopt;
}

} // namespace PlaylistIo
