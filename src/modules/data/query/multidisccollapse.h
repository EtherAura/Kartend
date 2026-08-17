#ifndef MULTIDISCCOLLAPSE_H
#define MULTIDISCCOLLAPSE_H

#include <QList>
#include <QString>
#include <QStringList>

#include "multidisc.h"

QT_BEGIN_NAMESPACE
class QSqlDatabase;
QT_END_NAMESPACE

/**
 * @brief The scan-side half of multi-disc grouping — collapses the staged
 * rows of a multi-disc release into the single library item they represent.
 *
 * MultiDisc (src/utils/fs/multidisc.h) owns the pure half: which file names
 * belong to one release, what the playlist body looks like, where the managed
 * playlist directory lives, and how per-disc metadata merges. This is the half
 * that touches the database and the disk.
 *
 * WHY A POST-PASS, NOT A FILTER IN THE STREAM. ScanService never builds a file
 * list: both arms of stageFilesystemScan push QDirIterator entries straight
 * into the `scanned_items` TEMP table so peak memory stays flat on a
 * hundred-thousand-file library. Grouping needs every file in a directory in
 * hand before it can decide what becomes an item, which a streaming filter
 * cannot provide. So the collapse runs as a pass over the staged rows once the
 * walk has finished and before the apply phase copies them into `items` — the
 * apply then does the real work for free: the generated .m3u is upserted like
 * any other discovered file, and deleteItemsMissingFromScan prunes the member
 * rows precisely because they are no longer staged.
 *
 * That placement also makes the regeneration rules fall out rather than needing
 * to be coded:
 *   * A disc added or removed re-groups on the next scan and the .m3u is
 *     rewritten atomically with the new member list.
 *   * A release that drops to a single disc stops being a group, so no .m3u is
 *     written for it, the orphan sweep deletes the stale file, and the lone
 *     disc stages as an ordinary item again.
 *   * Turning the per-collection setting off calls discardPlaylists() and the
 *     members stage normally, so the library returns to per-disc items with
 *     nothing left behind in Kartend's data directory.
 *
 * Only the collection's OWN managed directory is ever written or swept
 * (MultiDisc::playlistDirFor is keyed on the collection uuid); the user's media
 * folders are never touched.
 */
namespace MultiDiscCollapse {

/// One release that was collapsed during this scan. Returned by
/// collapseStagedGroups so the caller can hand it back to applyMergedMetadata
/// once the staged rows have landed in `items`.
struct CollapsedGroup {
  /// Absolute path of the generated .m3u — the collapsed item's key.
  QString playlistPath;
  /// Shared base title, disc tag stripped. Becomes the item's name.
  QString base;
  /// Absolute paths of the member files, in disc order.
  QStringList memberPaths;
  /// The members' metadata reconciled by MultiDisc::mergeDiscs(). Read BEFORE
  /// the apply phase, because the apply's prune step deletes the member rows
  /// this was gathered from.
  MultiDisc::DiscMetadata merged;
};

/// Collapse every multi-disc release among the currently staged `scanned_items`
/// rows: writes each release's .m3u into @p playlistDir, replaces the members'
/// staged rows with one row keyed on that .m3u, and deletes playlists in
/// @p playlistDir that no longer correspond to a release.
///
/// Must be called after the filesystem walk has staged its rows and before the
/// apply phase reads them. @p txnDepth is the owning ScanService's shared
/// transaction depth, so the staging rewrite coordinates with the other guards
/// on this connection; the rewrite is one transaction, so a failure part-way
/// leaves the staged rows exactly as the walk produced them and the scan simply
/// proceeds without collapsing.
///
/// Returns the collapsed releases (empty when nothing grouped, when the
/// connection is closed, or when @p playlistDir is empty — the guard
/// MultiDisc::playlistDirFor returns for an unusable uuid).
[[nodiscard]] QList<CollapsedGroup> collapseStagedGroups(QSqlDatabase &db, int &txnDepth,
                                                         const QString &collectionUuid,
                                                         const QString &playlistDir);

/// Write each collapsed release's merged metadata onto its item. Call after the
/// apply phase has committed, since the row it updates does not exist until
/// then.
///
/// The collapsed item's OWN stored metadata stays authoritative — it is merged
/// in ahead of disc 1 — so an edit the user made on the collapsed item survives
/// every later rescan, while a disc still fills anything the collapsed item
/// leaves empty. Rows are only rewritten when the merge actually changes them,
/// so a steady-state rescan does not churn `updated_at`.
///
/// The members' own item_metadata rows are deliberately NOT deleted. They cost
/// nothing (their `items` rows are gone, so nothing reads them), and keeping
/// them is what lets the user turn grouping back off without losing the notes,
/// tags and ratings they recorded per disc.
void applyMergedMetadata(QSqlDatabase &db, const QString &collectionUuid,
                         const QList<CollapsedGroup> &groups);

/// Remove a collection's managed playlist directory entirely — the cleanup for
/// "grouping was turned off". A no-op when the directory does not exist.
/// Returns false only when a removal was attempted and failed.
bool discardPlaylists(const QString &playlistDir);

} // namespace MultiDiscCollapse

#endif // MULTIDISCCOLLAPSE_H
