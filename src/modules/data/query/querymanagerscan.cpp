// loadOrScanCollection — the load-or-scan hybrid called by the four load
// slots in querymanagerload.cpp.
//
// The filesystem scan + rescan-needed checks (needsRescan, scanMediaDirectory)
// and the scan-and-save pipelines were extracted into ScanService — see
// scanservice.{h,cpp}. loadOrScanCollection stays a QueryManager member: it
// is a unidirectional bridge into the scan subsystem (QueryManager ->
// ScanService) and otherwise loads cached rows via loadItemsFromDatabaseByUuid.
#include "querymanager.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

#include "collection/collectionconfig.h"
#include "collection/typehelpers.h"

QStringList QueryManager::loadOrScanCollection(int collectionIndex,
                                               const CollectionConfig &collection,
                                               QHash<QString, QDateTime> &timestamps,
                                               QHash<QString, qint64> *sizes,
                                               QHash<QString, QString> *storedAbsByKey) {
  QStringList filePaths;
  if (collection.mediaDirectory.trimmed().isEmpty()) {
    return filePaths;
  }

  if (m_scanService.needsRescan(collectionIndex, collection)) {
    QString dirSignature;
    bool scanCompleted = false;
    filePaths =
        m_scanService.scanMediaDirectory(collection, timestamps, &dirSignature, &scanCompleted);
    // Kartend-fys4o: persist zero-file results too, as long as the scan
    // genuinely COMPLETED (directory exists, not cancelled). Skipping the
    // save on empty conflated "emptied directory" with "missing directory /
    // cancelled": stale items rows were never pruned (ghost items vs DB
    // counts) and last_scanned/dir_signature never updated, so needsRescan
    // stayed true and the directory was re-walked on every load.
    // saveItemsToDatabase handles the empty list correctly: zero staged
    // rows -> deleteItemsMissingFromScan prunes everything -> scan metadata
    // is stamped.
    if (!filePaths.isEmpty() || scanCompleted) {
      m_scanService.saveItemsToDatabase(collectionIndex, filePaths, timestamps, collection,
                                        dirSignature);
      // Kartend-7r821: this inline scan-apply mutated THIS worker's own items
      // table via ScanService, but unlike a background rescan on m_scanWorker it
      // never routes through collectionScanCompleted -> invalidateQueryCaches()
      // (saveItemsToDatabase emits no such signal, and a single QueryManager has
      // no self-connect). The sorted-items cache validity hash keys on
      // (collection uuids, filter, sortMode) — NOT item contents — so without
      // this drop a subsequent fetchItemCount/fetchItemsRange on the same worker
      // would HIT the pre-rescan sorted order (missing added items, blank tiles
      // for removed ones). We own both sides here, so call the existing
      // invalidation directly rather than introducing a ScanService back-pointer.
      // Guarded by the scan branch above, so unchanged collections (the else
      // cached-load path) still hit the sorted cache.
      invalidateQueryCaches();
    }
  } else {
    const QString uuid =
        CollectionUtils::computeCollectionUuid(collection.name, collection.mediaDirectory);
    // Kartend-m9r1s: pull last_modified/file_size out of the same SELECT so
    // the Date/Size sort modes downstream reuse them instead of statting
    // every file again.
    filePaths = loadItemsFromDatabaseByUuid(uuid, &timestamps, sizes, storedAbsByKey,
                                            collection.mediaDirectory);
  }

  return filePaths;
}
