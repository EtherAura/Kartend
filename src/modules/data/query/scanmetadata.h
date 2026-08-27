#ifndef SCANMETADATA_H
#define SCANMETADATA_H

#include <QString>

QT_BEGIN_NAMESPACE
class QSqlDatabase;
QT_END_NAMESPACE

/**
 * @brief The scan-side pass that reads metadata sidecars back in (Kartend-zur26).
 *
 * WHAT WAS BROKEN. A scrape writes two things to the artwork directory: the
 * images, and a per-item JSON sidecar under `<artworkDirectory>/metadata/`
 * holding the whole scraped record — title, description, genre, developer,
 * publisher, release date, rating, players, runtime, tags and custom fields.
 * The images are re-discovered from disk on every scan. The sidecars were
 * never read back by anything: their only consumer stats the file's mtime to
 * decide whether a re-scrape is warranted.
 *
 * So `item_metadata` was the sole store of scraped text. Lose that database —
 * corruption, a fresh install pointed at the same library, moving the library
 * to another machine — and the user had to re-scrape everything, while the
 * answers sat in JSON files next to the artwork that came back by itself. The
 * artwork half of a scraped library was portable and the metadata half was
 * not, which is plainly not what a sidecar is for.
 *
 * WHAT THIS DOES. After a scan settles the item rows, every item that has NO
 * `item_metadata` row is looked up against the sidecar directory by file base
 * name, and a matching sidecar is parsed into a row.
 *
 * PRECEDENCE IS ABSOLUTE, AND DELIBERATELY CRUDE. Only items with no row at
 * all are considered; an item that already has one is never touched, not even
 * to fill a blank field. A sidecar is a stale snapshot of what some scrape
 * once returned, and the row may since have been edited by hand — notes, a
 * rating, corrected fields. Field-level merging would need a per-field notion
 * of "the user meant this" that nothing in the schema records, and getting it
 * wrong would silently overwrite the user's own words. "Only when there is
 * nothing" cannot do that, and it is exactly the case the issue is about: a
 * library whose database is gone.
 *
 * COST. One directory listing for the sidecar folder, then a hash lookup per
 * item — never a stat per item. Items are read in bounded batches, and the
 * pass returns immediately when the collection has no sidecar directory,
 * which is every collection that has never been scraped.
 *
 * WHY NOT AT SCRAPE TIME. The sidecar is already written at scrape time; the
 * gap is only ever on the way back in, and the way back in is a scan. This
 * also makes fixture and demo seeding work without writing SQL: drop the
 * sidecars beside the artwork and scan.
 */
namespace ScanMetadata {

/// Hydrate `item_metadata` rows from sidecars for every item in
/// @p collectionUuid that has no row yet.
///
/// @p artworkDirectory is the collection's resolved artwork directory (the
/// sidecars live in its `metadata` subdirectory). Returns the number of rows
/// written; 0 when the connection is closed, the artwork directory is unset,
/// or there is no sidecar directory to read.
int hydrateFromSidecars(QSqlDatabase &db, int &txnDepth, const QString &artworkDirectory,
                        const QString &collectionUuid);

} // namespace ScanMetadata

#endif // SCANMETADATA_H
