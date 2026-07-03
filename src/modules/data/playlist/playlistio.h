#ifndef PLAYLISTIO_H
#define PLAYLISTIO_H

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "errorutils.h"
#include "iplaylistmanager.h" // PlaylistRow / PlaylistItemRef
#include "playlistserializer.h"

class QSqlDatabase;
class QSqlQuery;

/**
 * @brief DB-side import / export for playlists.
 *
 * Extracted from PlaylistManager so the JSON/M3U import-export DB work (row
 * loads feeding the exporters, the single-transaction item inserts, and
 * path→collection resolution) no longer shares a file with row CRUD and
 * smart-filter persistence. Everything here is a free function over an
 * explicit QSqlDatabase& — no manager state, no signals: PlaylistManager
 * keeps thread affinity (assertOwnerThread), the signal-emitting CRUD
 * (createPlaylist / createSmartPlaylist / deletePlaylist) the imports
 * orchestrate, and the playlistsChanged emissions. Format + file I/O live one
 * step further out in PlaylistSerializer (Kartend-k4alv).
 */
namespace PlaylistIo {

/// UTC timestamp in Qt::ISODateWithMs — the shared created_at / updated_at /
/// added_at stamp format for the playlist tables.
[[nodiscard]] QString isoNow();

/// Validate a (uuid, path) item ref before it is persisted. Caps both string
/// lengths so a crafted/corrupt envelope cannot inject megabyte-sized values
/// into the TEXT NOT NULL columns. Returns true when the ref is acceptable.
/// Empty path/uuid handling stays with the callers (path-empty is a skip; an
/// empty uuid is a legitimate "no source collection" ref), so this only rejects
/// over-long values (Kartend-2sg2t). Note: collection uuids are SHA-1 hex
/// digests, not RFC-4122 UUIDs, so no QUuid::fromString well-formedness check
/// is applied — it would reject every production ref.
[[nodiscard]] bool validateItemRef(const QString &uuid, const QString &path);

/// Shared INSERT statement for playlist_items — one statement string + one
/// positional binder (bindPlaylistItemRow) so the four insert sites
/// (PlaylistManager::addItem, the removeItem re-densify,
/// insertImportedJsonItems, insertResolvedM3UItems) can't drift on column
/// order (Kartend audit D-09).
[[nodiscard]] const QString &insertPlaylistItemSql();

/// Positional binder for insertPlaylistItemSql(). Uses bindValue(pos) rather
/// than addBindValue so it also works when a single prepared query is reused
/// across a loop (the removeItem re-densify path).
void bindPlaylistItemRow(QSqlQuery &q, const QString &playlistId, int position,
                         const QString &sourceCollectionUuid, const QString &sourcePath,
                         const QString &addedAt);

/// Load one `playlists` row by id. Returns a default-constructed row (empty
/// id) when the playlist is unknown or the read fails.
[[nodiscard]] PlaylistRow loadPlaylistRow(QSqlDatabase &db, const QString &id);

/// DB half of PlaylistManager::exportToJson: validates, loads the playlist
/// row, and feeds PlaylistSerializer::exportJson. @p items is supplied by the
/// caller (PlaylistManager::loadItems) so this TU doesn't duplicate the item
/// loader. Returns the count of items written on success.
[[nodiscard]] ErrorUtils::Result<int> exportToJson(QSqlDatabase &db, const QString &playlistId,
                                                   const QString &outPath,
                                                   const QList<PlaylistItemRef> &items);

/// DB half of PlaylistManager::exportToM3U: validates and feeds
/// PlaylistSerializer::exportM3U. Returns the count of items written on
/// success.
[[nodiscard]] ErrorUtils::Result<int> exportToM3U(QSqlDatabase &db, const QString &playlistId,
                                                  const QString &outPath,
                                                  const QList<PlaylistItemRef> &items);

/// Insert parsed JSON-envelope items into @p playlistId in a SINGLE
/// transaction (Kartend-fd7xl) so a mid-loop failure or crash can't leave a
/// half-populated playlist the user sees as "imported". Returns std::nullopt
/// on success; on any failure the transaction has already rolled back before
/// this returns (the caller then drops the freshly created playlist).
/// @p skipped accumulates dropped items — empty-path, duplicate, and
/// over-long/invalid refs — so the caller can surface "N items skipped"
/// (Kartend-2sg2t).
[[nodiscard]] std::optional<ErrorUtils::ErrorContext>
insertImportedJsonItems(QSqlDatabase &db, const QString &playlistId,
                        const QList<PlaylistSerializer::ParsedPlaylist::Item> &items, int &skipped);

/// One resolved M3U line: the entry path plus the collection uuid the live
/// `items` table attributes it to.
struct ResolvedM3UEntry {
  QString uuid;
  QString path;
};

/// Resolve M3U entry paths against the live `items` table, in file order.
/// Unresolvable or over-long paths are counted in @p skipped; paths that
/// exist in more than one collection resolve deterministically to the lowest
/// collection uuid and are counted in @p ambiguous (Kartend-o84pt). Duplicate
/// (uuid, path) pairs are dropped, reproducing addItem's idempotent
/// de-duplication in memory.
[[nodiscard]] QList<ResolvedM3UEntry> resolveM3UEntries(QSqlDatabase &db, const QStringList &paths,
                                                        int &skipped, int &ambiguous);

/// Insert resolved M3U entries into @p playlistId in a SINGLE transaction
/// (Kartend-k695z — resolve everything first, then insert once, mirroring
/// insertImportedJsonItems). Returns std::nullopt on success; on any failure
/// the transaction has already rolled back before this returns.
[[nodiscard]] std::optional<ErrorUtils::ErrorContext>
insertResolvedM3UItems(QSqlDatabase &db, const QString &playlistId,
                       const QList<ResolvedM3UEntry> &entries);

} // namespace PlaylistIo

#endif // PLAYLISTIO_H
