#ifndef ITEMDETAILDATA_H
#define ITEMDETAILDATA_H

#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>

#include "itemmetadata.h"
#include "usagestatsstore.h"

/// Off-thread-loaded payload for the item detail page (Kartend-4p8o).
///
/// The query worker fills this on its own DB connection plus filesystem
/// probes, off the UI thread; DetailPageManager maps it into
/// IDetailPageOverlay::Payload on the main thread — applying tr() tile
/// labels and the display-title choice there so user-facing presentation
/// stays out of the data layer. The worker resolves on-disk paths (a data/IO
/// concern); the UI owns labelling.
///
/// `valid` is false when the request could not resolve an item (no uuid /
/// load failure); the manager then leaves the cheap overlay it already showed.
struct ItemDetailData {
  ItemMetadataStore::ItemMetadata metadata;
  UsageStatsStore::ItemUsageStats usage;

  /// Resolved manual file (empty hides the button).
  QString manualPath;

  /// Resolved typed-artwork tiles as (typeId, absolutePath), in display
  /// order — standard types first, then any custom overrides. The UI derives
  /// each tile's human label from the typeId.
  QList<QPair<QString, QString>> typedArtwork;

  /// Flat-layout fallback cover ({artworkDir}/{baseName}.{ext}); empty if none.
  /// Kept separate so the UI can prepend it with a tr("Cover") label and skip
  /// it when a typed tile already covers the same file.
  QString fallbackCoverPath;

  /// Resolved video file ({videoDir}/...); empty if none. UI prepends with a
  /// tr("Video") label.
  QString videoPath;

  qint64 fileSize = -1;
  QString fileModified;
  QString fileExtension;

  bool valid = false;
};

Q_DECLARE_METATYPE(ItemDetailData)

#endif // ITEMDETAILDATA_H
