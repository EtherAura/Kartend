#ifndef ITEMARTWORK_H
#define ITEMARTWORK_H

#include <QList>
#include <QString>
#include <QStringList>

#include "errorutils.h"

class QSqlDatabase;

namespace ItemArtworkStore {

/// One row from the `item_artwork` table. Keyed on
/// (collectionUuid, path, artworkType). `manualPath` is the optional per-item
/// override; when empty, callers fall back to subdirectory auto-discovery for
/// standard types via `resolveArtworkPath()`.
struct ItemArtwork {
  QString collectionUuid;
  QString path;
  QString artworkType;
  QString manualPath;
  QString updatedAt;
};

/// Canonical lowercase identifiers for the standard artwork types. Order is
/// the sidebar gallery's display priority (box first, logo last). The strings
/// are stable forever — they double as on-disk subdirectory names under a
/// collection's `artworkDirectory`.
namespace StandardTypes {
inline constexpr const char *Box = "box";
inline constexpr const char *Screenshot = "screenshot";
inline constexpr const char *Title = "title";
inline constexpr const char *Marquee = "marquee";
inline constexpr const char *Fanart = "fanart";
inline constexpr const char *Logo = "logo";
} // namespace StandardTypes

/// Ordered list of standard artwork type ids (see StandardTypes). The list
/// order is the canonical sidebar gallery display order.
[[nodiscard]] const QStringList &standardTypes();

/// True when `artworkType` is one of the standard ids returned by
/// `standardTypes()`. Comparison is case-sensitive — IDs are normalized to
/// lowercase by convention.
[[nodiscard]] bool isStandardType(const QString &artworkType);

/// Human-friendly label for a standard type ("box" -> "Box"). Returns an
/// empty string for non-standard types so callers can fall back to a custom
/// label they own (e.g. the user's chosen name for a custom type).
[[nodiscard]] QString standardTypeDisplayName(const QString &artworkType);

/// Loads a single artwork row for (collectionUuid, path, artworkType). When
/// no row exists, returns an `ItemArtwork` with the keys populated and
/// `manualPath`/`updatedAt` empty. Returns an error only on actual database
/// failures.
[[nodiscard]] ErrorUtils::Result<ItemArtwork> load(QSqlDatabase &db, const QString &collectionUuid,
                                                   const QString &path, const QString &artworkType);

/// Loads every artwork row stored for (collectionUuid, path), in insertion
/// order. Empty list when the item has no rows yet.
[[nodiscard]] ErrorUtils::Result<QList<ItemArtwork>>
loadAllForItem(QSqlDatabase &db, const QString &collectionUuid, const QString &path);

/// Inserts or updates the artwork row for (artwork.collectionUuid, path,
/// artworkType). Always refreshes `updated_at` to the current UTC ISO
/// timestamp. Rejects empty `path` or empty `artworkType` to keep malformed
/// rows out of the unique index.
[[nodiscard]] ErrorUtils::Result<bool> save(QSqlDatabase &db, const ItemArtwork &artwork);

/// Removes a single artwork row. Succeeds even when no matching row exists.
[[nodiscard]] ErrorUtils::Result<bool> remove(QSqlDatabase &db, const QString &collectionUuid,
                                              const QString &path, const QString &artworkType);

/// Removes every artwork row for (collectionUuid, path). Used when an item is
/// removed from the library so its typed artwork doesn't linger.
[[nodiscard]] ErrorUtils::Result<bool>
removeAllForItem(QSqlDatabase &db, const QString &collectionUuid, const QString &path);

/// Looks for a standard-type artwork file in the subdirectory layout
/// `{artworkDirectory}/{artworkType}/{baseName}.{ext}`. Tries the same image
/// extensions used by `ArtworkUtils::findArtworkForFile` (lower- and upper-
/// case). Returns an empty string for non-standard types, missing/empty
/// inputs, or no match. Does NOT recurse beyond the type subdirectory.
[[nodiscard]] QString findStandardArtwork(const QString &baseName, const QString &artworkDirectory,
                                          const QString &artworkType);

/// Resolves the on-disk artwork path for an item + type. Prefers a non-empty
/// `overridePath` (the per-item manual override stored in
/// `item_artwork.manual_path`); tilde expansion is applied so paths saved as
/// `~/art/box/foo.png` resolve at runtime. When the override is set but the
/// file is missing on disk, returns empty rather than silently falling back —
/// matches the manual-link behaviour in `ItemMetadataStore::resolveManualFile`
/// so a typo'd override is visibly broken instead of masked. When the
/// override is empty AND the type is standard, falls back to
/// `findStandardArtwork`. Custom (non-standard) types resolve only via the
/// override.
[[nodiscard]] QString resolveArtworkPath(const QString &overridePath, const QString &baseName,
                                         const QString &artworkDirectory,
                                         const QString &artworkType);

} // namespace ItemArtworkStore

#endif
