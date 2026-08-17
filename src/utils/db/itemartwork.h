#ifndef ITEMARTWORK_H
#define ITEMARTWORK_H

#include <QHash>
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
/// the sidebar gallery's display priority (front cover first, logo last).
/// The strings are stable forever — they double as on-disk subdirectory
/// names under a collection's `artworkDirectory`.
///
/// `Front` is the cross-provider primary-cover slot — every API-backed
/// scraper (SS box-2D, MB front, TMDB poster, Open Library cover) maps
/// its top-quality cover to this id. `Box` is preserved as a separate
/// type so libraries with hand-curated `/box/` subfolders keep working
/// alongside scraped `/front/` content; the user can populate either or
/// both per item.
namespace StandardTypes {
inline constexpr const char *Front = "front";
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

/// The artwork types whose MANUAL link can supply an item's primary cover, in
/// priority order: `ArtworkUtils::coverSubdirPriority()` restricted to
/// standard types (`front`, `box`, `screenshot`, `title`, `fanart`,
/// `marquee`).
///
/// Restricted on purpose (Kartend-u67w0): the scraper records an
/// `item_artwork` row for every NON-standard image it downloads (`box-3d`,
/// `mixrbv1`, `mixrbv2`, `wheel`, …) so the sidebar gallery can find those
/// files. Those rows are bookkeeping, not user intent — letting them into the
/// cover fold made a scraped `mixrbv` COMPOSITE outrank the item's real
/// `front/` cover on every tile. Genuine hand-links (the links dialog, the
/// Artwork Wizard) target standard types, so they keep beating auto-discovery.
/// A non-standard file the user really wants as the tile face can be linked on
/// `front` — that is the supported override.
[[nodiscard]] const QStringList &manualCoverTypes();

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

/// The cover an item resolves to once its MANUAL links are taken into account
/// — the value `items.artwork_path` is meant to hold (Kartend-jkty9).
///
/// @p manualByType maps artwork_type -> `item_artwork.manual_path` for ONE
/// item (types with no row, or a NULL/empty path, simply aren't in it), and
/// @p autoDiscovered is the cover the name-based cascade found for that item
/// (`ArtworkUtils::findArtworkForFileCached`, or the bulk
/// `buildArtworkPathMap` form of it) — empty when nothing matched.
///
/// Precedence follows what the artwork docs promise and what the details pane
/// already renders: a manual link WINS over auto-discovery. Types are
/// consulted in `manualCoverTypes()` order, so a hand-linked `front` outranks
/// a hand-linked `box`; a link on any other type (`logo`, a custom type, and
/// the scraper-written non-standard cover variants `box-3d` / `mixrbv1` /
/// `mixrbv2` — see manualCoverTypes) is gallery-only and never answers here.
///
/// EXISTENCE IS CHECKED, exactly as `resolveArtworkPath` checks it: the link
/// is tilde-expanded and stat'd, and a link whose file has since been deleted
/// is skipped rather than returned. Without that a link to a long-gone file
/// would report "has artwork" forever, while every render path that resolves
/// it live shows nothing — the DB-vs-render disagreement Kartend-guyc5 removed.
/// Callers therefore pay one stat per manual link (not per item): the map is
/// empty for every item the user has never hand-linked.
///
/// Falls back to @p autoDiscovered when no manual cover link resolves, so
/// clearing a link restores the auto-discovered answer instead of blanking it.
[[nodiscard]] QString resolveCoverPath(const QHash<QString, QString> &manualByType,
                                       const QString &autoDiscovered);

/// `resolveCoverPath` for EVERY hand-linked item at once: absolute item path ->
/// the cover its manual links resolve to (Kartend-1js9j).
///
/// This is what the render surfaces need. A tile is built per widget while the
/// grid recycles widgets during a scroll, so it cannot ask the database whether
/// *this* item has a link — the map answers that with a hash lookup, and the
/// callers hold one map for the whole session rather than one query per tile.
///
/// Deliberately NOT scoped to a collection. `item_artwork` holds only rows
/// somebody created — the user through the links dialog, the Artwork Wizard —
/// plus the scraper's NON-standard types, and those are excluded because the
/// query asks only for `manualCoverTypes()` (the same list `resolveCoverPath`
/// consults — restricted to STANDARD cover types precisely so the scraper's
/// `box-3d` / `mixrbv*` bookkeeping rows stay out, Kartend-u67w0). Keys are
/// absolute item paths and
/// so are unique library-wide: scoping by collection uuid could only make the
/// map smaller, never change an answer, and it would make the map a
/// per-collection-switch rebuild instead of the once-per-edit rebuild it is.
///
/// One `QFile::exists` per LINK, exactly as `resolveCoverPath` documents — not
/// per item. A link whose file has been deleted contributes no entry, so a
/// stale link never pins a cover the item cannot actually paint.
///
/// Rows are streamed in path order and folded per item, so peak memory is one
/// item's links rather than the whole table.
[[nodiscard]] ErrorUtils::Result<QHash<QString, QString>> loadManualCoverPaths(QSqlDatabase &db);

} // namespace ItemArtworkStore

#endif
