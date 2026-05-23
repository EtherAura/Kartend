#include "metadataqueue.h"

#include <QCoreApplication>

namespace MetadataQueue {

namespace {

QString tr(const char *s) {
  return QCoreApplication::translate("MetadataQueue", s);
}

/// Examines `md` and `hasArtworkOnDisk` to produce a list of missing
/// field labels. Empty list means the item is fully populated and
/// shouldn't appear in the review queue.
QStringList computeMissingFields(const ItemMetadataStore::ItemMetadata &md, bool hasArtworkOnDisk) {
  QStringList missing;
  if (md.title.trimmed().isEmpty()) {
    missing << tr("Title");
  }
  if (md.description.trimmed().isEmpty()) {
    missing << tr("Description");
  }
  if (md.genre.trimmed().isEmpty()) {
    missing << tr("Genre");
  }
  if (!hasArtworkOnDisk) {
    missing << tr("Artwork");
  }
  return missing;
}

} // namespace

QList<Entry>
build(const QString &collectionUuid, const QList<InputRow> &items,
      const std::function<ItemMetadataStore::ItemMetadata(const QString &, const QString &)>
          &metadataLoader) {
  QList<Entry> out;
  out.reserve(items.size());
  for (const InputRow &row : items) {
    ItemMetadataStore::ItemMetadata md;
    if (metadataLoader) {
      md = metadataLoader(collectionUuid, row.filePath);
    }
    const QStringList missing = computeMissingFields(md, row.hasArtworkOnDisk);
    if (missing.isEmpty()) {
      continue;
    }
    Entry e;
    e.filePath = row.filePath;
    e.collectionUuid = collectionUuid;
    e.itemName = row.itemName.isEmpty() ? row.filePath : row.itemName;
    e.hasArtworkOnDisk = row.hasArtworkOnDisk;
    e.missingFields = missing;
    out.append(e);
  }
  return out;
}

bool reevaluate(Entry &entry, const ItemMetadataStore::ItemMetadata &metadata) {
  entry.missingFields = computeMissingFields(metadata, entry.hasArtworkOnDisk);
  return !entry.missingFields.isEmpty();
}

} // namespace MetadataQueue
