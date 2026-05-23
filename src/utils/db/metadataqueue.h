#ifndef KARTEND_UTILS_DB_METADATAQUEUE_H
#define KARTEND_UTILS_DB_METADATAQUEUE_H

#include <functional>
#include <QString>
#include <QStringList>

#include "itemmetadata.h"

// Builds a "items lacking metadata" queue for the review workflow.
// Pure analyzer — the caller supplies a metadataLoader closure so the
// helper stays unit-testable without a real DB.

namespace MetadataQueue {

/// One row of the missing-metadata queue.
struct Entry {
  QString filePath;
  QString collectionUuid;
  /// Display name (typically `items.name`). Used as the dialog header so
  /// the user can identify the item without parsing the path.
  QString itemName;
  /// Has artwork match on disk (i.e. items.artwork_path non-empty).
  /// Computed by the caller and supplied here so the queue doesn't take
  /// a separate dependency on the items table.
  bool hasArtworkOnDisk = false;
  /// Human-readable list of missing fields ("Title", "Description",
  /// "Genre", "Artwork"). Populated by build(); empty entries are
  /// excluded from the queue.
  QStringList missingFields;
};

/// Caller-supplied per-item probe inputs. Decoupling the analyzer from
/// IDatabaseManager keeps the build() function unit-testable.
struct InputRow {
  QString filePath;
  QString itemName;
  bool hasArtworkOnDisk = false;
};

/// Constructs the queue: for each input row, loads its metadata via
/// `metadataLoader(collectionUuid, filePath)` and records the missing
/// fields. Items with no missing fields are omitted from the returned
/// list. Order is preserved (the caller's input order — typically
/// name-sorted from loadAllItemPathsForCollection).
[[nodiscard]] QList<Entry>
build(const QString &collectionUuid, const QList<InputRow> &items,
      const std::function<ItemMetadataStore::ItemMetadata(const QString &uuid, const QString &path)>
          &metadataLoader);

/// Re-evaluates a single entry against fresh metadata. Returns true when
/// the row still has missing fields (i.e. it stays in the queue);
/// false means the user filled in everything and the entry can be
/// dropped from the visible queue.
[[nodiscard]] bool reevaluate(Entry &entry, const ItemMetadataStore::ItemMetadata &metadata);

} // namespace MetadataQueue

#endif // KARTEND_UTILS_DB_METADATAQUEUE_H
