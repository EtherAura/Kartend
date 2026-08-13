#ifndef SCANARTWORK_H
#define SCANARTWORK_H

#include <QString>

QT_BEGIN_NAMESPACE
class QSqlDatabase;
QT_END_NAMESPACE

/**
 * @brief The scan-side pass that fills `items.artwork_path` — the column every
 * DB-side artwork predicate reads (Kartend-guyc5).
 *
 * WHAT WAS BROKEN. Nothing in either scan pipeline ever wrote the column, so on
 * a freshly scanned library it was empty for every item, while the cover the
 * user actually sees was resolved at RENDER time by name against the
 * collection's artwork directory. Everything that asked the database instead —
 * the `HasArtwork` / `MissingArtwork` smart-playlist rules, the
 * `has:artwork` / `missing:artwork` search tokens, Collection Health's missing-
 * artwork count, the artwork review queue — therefore reported items as artless
 * while their tiles painted a cover.
 *
 * WHY POPULATE RATHER THAN TEACH THE PREDICATES TO LOOK. The alternative was to
 * make each consumer resolve artwork the way the grid does. That would push
 * filesystem work into `SmartPlaylistEvaluator` (documented pure SQL, no widget
 * coupling) and into the search-token SQL builder, where a post-filter in C++
 * would also break LIMIT/OFFSET paging and the count queries. Filling the
 * column instead fixes every consumer at once and leaves their SQL untouched.
 *
 * WHERE IT RUNS. A pass over the `scanned_items` staging table, after the
 * multi-disc collapse and before the apply — the same seam MultiDiscCollapse
 * uses, and for the same reason: it is the one point both persist pipelines
 * share, and by then the staged rows are exactly the rows that will become
 * items (collapsed releases included, which is what lets a multi-disc item pick
 * up art filed per disc). The apply carries the column into `items` and
 * refreshes it on conflict, so a rescan re-derives rather than accumulates.
 *
 * COST ON A LARGE LIBRARY. Resolution is a name lookup, never a stat per item:
 * the collection's artwork cascade (flat root + the typed cover subdirs) is
 * warmed ONCE through ArtworkUtils::DirectoryCache and folded into a single
 * key -> path map, after which each staged row costs two hash lookups. The pass
 * reads the staged rows in bounded batches, so peak memory stays flat in the
 * spirit of the streaming walk, and only rows that actually resolve are
 * written.
 *
 * STALENESS. The column is a cache of a filesystem answer, valid as of the last
 * scan — the same contract as `file_size` and `last_modified` beside it.
 * Artwork dropped in (or deleted) without the media directory changing does not
 * trigger a rescan, so the DB-side predicates catch up on the next scan of that
 * collection. The render path is unaffected: it still resolves live, and is
 * still the authority for what the user sees.
 */
namespace ScanArtwork {

/// Resolve a cover for every row staged in `scanned_items` and store it in the
/// staging table's `artwork_path` column, ready for the apply to carry into
/// `items`.
///
/// Resolution is ArtworkUtils' own — the artwork cascade is warmed through
/// DirectoryCache and folded via ArtworkUtils::buildArtworkPathMap, so the
/// stored path is the one findArtworkForFileCached would return for the same
/// item, disc-marked fallback included. There is deliberately no second
/// resolver here to drift from it.
///
/// A no-op returning 0 when the connection is closed or the collection has no
/// artwork directory. Rows that resolve nothing are left NULL, which is what
/// the apply then writes — so art deleted since the previous scan clears the
/// column rather than lingering.
///
/// @param db the scan worker's connection (the staging table is TEMP, so it
///        must be the same connection that staged the rows).
/// @param txnDepth QueryManager's shared transaction-depth counter — the whole
///        pass runs in one guarded transaction on it.
/// @param artworkDirectory the collection's artwork directory; empty skips.
/// @return the number of staged rows a cover was stored for (0 on failure,
///         which leaves the staged rows unmodified and the scan otherwise
///         unaffected — an unresolved column is a degraded predicate, never a
///         broken library).
int resolveStagedArtwork(QSqlDatabase &db, int &txnDepth, const QString &artworkDirectory);

} // namespace ScanArtwork

#endif // SCANARTWORK_H
