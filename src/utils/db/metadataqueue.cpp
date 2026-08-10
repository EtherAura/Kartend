#include "metadataqueue.h"

#include <QCoreApplication>

namespace MetadataQueue {

namespace {

/// lupdate attributes a bare Tr::tr() in a namespace to a class it cannot find a
/// Q_OBJECT on and warns; those warnings then mask a real context mismatch
/// elsewhere (Kartend-r4tno — that is how MediaFolderPage stayed latent).
/// Declaring the context on a struct keeps extraction and runtime agreeing on
/// "MetadataQueue" — exactly what the hand-written wrapper did — while giving lupdate
/// something it recognises. Call sites read Tr::tr("…").
struct Tr {
  Q_DECLARE_TR_FUNCTIONS(MetadataQueue)
};

/// Examines `md` and `hasArtworkOnDisk` to produce a list of missing
/// field labels. Empty list means the item is fully populated and
/// shouldn't appear in the review queue.
QStringList computeMissingFields(const ItemMetadataStore::ItemMetadata &md, bool hasArtworkOnDisk) {
  QStringList missing;
  if (md.title.trimmed().isEmpty()) {
    missing << Tr::tr("Title");
  }
  if (md.description.trimmed().isEmpty()) {
    missing << Tr::tr("Description");
  }
  if (md.genre.trimmed().isEmpty()) {
    missing << Tr::tr("Genre");
  }
  if (!hasArtworkOnDisk) {
    missing << Tr::tr("Artwork");
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
