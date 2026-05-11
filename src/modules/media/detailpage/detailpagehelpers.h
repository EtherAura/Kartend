#ifndef DETAILPAGEHELPERS_H
#define DETAILPAGEHELPERS_H

#include "itemartwork.h"
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// Pure helpers extracted from DetailPageManager so the payload-shaping logic
// is unit-testable without a DatabaseManager / DetailPageOverlay / QWidget
// graph. The manager owns I/O (DB loads, filesystem resolution); the helpers
// own the transforms in between.
namespace DetailPagePayloadBuilder {

// Returns metaTitle when non-empty, else fallback. Used to prefer the
// scraped title from item_metadata over the file-stem-derived name the
// sidebar holds in its current item context.
[[nodiscard]] QString pickDisplayTitle(const QString &fallback, const QString &metaTitle);

// Builds an artwork-type -> manualPath lookup from the rows the DB returned.
// Empty rows produce an empty hash. Later rows overwrite earlier ones for
// the same type, matching the QHash::insert semantics the manager relied on.
[[nodiscard]] QHash<QString, QString>
buildArtworkOverrideMap(const QList<ItemArtworkStore::ItemArtwork> &rows);

// Returns the user-defined (non-standard) artwork type ids from `rows` in
// the order they appear. Used by the manager to render custom artwork tiles
// after the canonical standard set.
[[nodiscard]] QStringList
collectCustomArtworkTypes(const QList<ItemArtworkStore::ItemArtwork> &rows);

// File-info breakdown matching the corresponding DetailPageOverlay::Payload
// fields. When `info` doesn't exist, returns the sentinel struct unchanged
// (fileSize == -1, empty modified/extension) so the overlay's "unknown"
// path is taken.
struct FileInfoFields {
  qint64 fileSize = -1;
  QString fileModified;
  QString fileExtension;
};
[[nodiscard]] FileInfoFields buildFileInfoFields(const QFileInfo &info);

} // namespace DetailPagePayloadBuilder

#endif // DETAILPAGEHELPERS_H
