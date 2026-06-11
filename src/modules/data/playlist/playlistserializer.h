#ifndef PLAYLISTSERIALIZER_H
#define PLAYLISTSERIALIZER_H

#include <QList>
#include <QString>
#include <QStringList>

#include "errorutils.h"
#include "iplaylistmanager.h" // PlaylistRow / PlaylistItemRef

/**
 * @brief JSON / M3U import-export for playlists (Kartend-k4alv).
 *
 * Extracted from PlaylistManager so the four serializers no longer share a
 * file with row CRUD and smart-filter persistence — adding a format or
 * changing the JSON envelope now touches only this TU. Everything here is
 * format + file I/O: no QSqlDatabase, no transactions, no signals. The
 * manager keeps the DB halves (row/item loads feeding the exporters; CRUD,
 * the single-transaction item insert, and path→collection resolution
 * consuming the parsers).
 */
namespace PlaylistSerializer {

/// Parsed shape of a Kartend playlist .json document (v1/v2 envelope).
/// `name` is the document's own name — the caller applies any override.
/// For v2 smart playlists `isSmart` is true and `smartFilterJson` carries the
/// raw filter string (the caller parses it via SmartFilter so this TU stays
/// format-only); `items` is the static-playlist item list otherwise.
struct ParsedPlaylist {
  QString name;
  QString parentCollectionUuid;
  bool isSmart = false;
  QString smartFilterJson;
  struct Item {
    QString sourceCollectionUuid;
    QString sourcePath;
  };
  QList<Item> items;
};

/// Writes the versioned (v2) JSON document for @p row + @p items to
/// @p outPath atomically (QSaveFile + parent-directory fsync). Returns the
/// exported item count.
[[nodiscard]] ErrorUtils::Result<int>
exportJson(const PlaylistRow &row, const QList<PlaylistItemRef> &items, const QString &outPath);

/// Writes the Extended-M3U rendering of @p items to @p outPath atomically.
/// Returns the exported item count.
[[nodiscard]] ErrorUtils::Result<int> exportM3U(const QList<PlaylistItemRef> &items,
                                                const QString &outPath);

/// Reads + parses a Kartend playlist .json file: open/parse failures and a
/// missing/unsupported kartend_playlist_version are errors; field absence
/// follows the v1-fallback rules documented on the export side.
[[nodiscard]] ErrorUtils::Result<ParsedPlaylist> parseJsonFile(const QString &inPath);

/// Reads the entry paths of an M3U file (blank lines and #-comments
/// skipped), in file order. Pure file I/O — the caller resolves each path
/// against the live items table.
[[nodiscard]] ErrorUtils::Result<QStringList> readM3UPaths(const QString &inPath);

} // namespace PlaylistSerializer

#endif // PLAYLISTSERIALIZER_H
